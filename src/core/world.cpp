#include "dota/core/world.hpp"

#include "dota/combat/damage.hpp"
#include "dota/core/attack.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/script/lua_state.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

World::World() : projectiles_(std::make_unique<ProjectileManager>()) {
    projectiles_->set_world(this);
}
World::~World() = default;

ProjectileManager& World::projectiles() {
    return *projectiles_;
}
const ProjectileManager& World::projectiles() const {
    return *projectiles_;
}

Unit* World::spawn(std::string name, Team team, UnitStats stats, Vec2 position) {
    auto unit = std::make_unique<Unit>(next_id_++, std::move(name), team, stats);
    unit->set_position(position);
    // 与分离 pass 对齐 baseline -- spawn 当帧不算"移动".
    unit->snapshot_tick_position();
    unit->set_world(this);
    Unit* raw = unit.get();
    units_.push_back(std::move(unit));
    UnitSpawnedEvent ev{raw->id()};
    events_.publish(ev);
    return raw;
}

Unit* World::find(EntityId id) {
    auto it = std::find_if(units_.begin(), units_.end(),
                           [id](const auto& u) { return u->id() == id; });
    return it == units_.end() ? nullptr : it->get();
}

const Unit* World::find(EntityId id) const {
    auto it = std::find_if(units_.begin(), units_.end(),
                           [id](const auto& u) { return u->id() == id; });
    return it == units_.end() ? nullptr : it->get();
}

std::vector<Unit*> World::units_on_team(Team team) {
    std::vector<Unit*> out;
    for (auto& u : units_) {
        if (u->team() == team) out.push_back(u.get());
    }
    return out;
}

std::vector<Unit*> World::enemies_of(const Unit& u) {
    std::vector<Unit*> out;
    for (auto& other : units_) {
        if (other->team() != u.team() && other->team() != Team::Neutral &&
            other->alive() && other->id() != u.id()) {
            out.push_back(other.get());
        }
    }
    return out;
}

Unit* World::create_thinker(Vec2 position, double duration,
                            const std::string& modifier_name,
                            LuaState* lua, Unit* source) {
    UnitStats s;
    s.max_health = 1.0;
    s.max_mana   = 0.0;
    s.attack_damage = 0.0;
    s.move_speed = 0.0;
    Unit* th = spawn("npc_dota_thinker", Team::Neutral, s, position);
    // thinker 是临时占位单位, 到期或被自毁后立刻从 World::units_ 中 erase, 防止
    // 长期累积. 普通 spawn 不开此项 -- 由调用方按需设置.
    th->set_remove_on_death(true);
    // 始终给 thinker 一个 untargetable + invulnerable + no-collision + no-health-bar 状态.
    const std::uint32_t mask = state_bit(ModifierState::Untargetable) |
                                state_bit(ModifierState::Invulnerable) |
                                state_bit(ModifierState::NoUnitCollision) |
                                state_bit(ModifierState::NoHealthBar);
    th->modifiers().attach(std::make_unique<modifiers::ThinkerBase>(
        *th, duration, mask));

    // 挂载用户提供的 Lua 修饰器(携带 OnIntervalThink 等).
    if (lua && !modifier_name.empty()) {
        const auto* spec = lua->modifier_registry().find(modifier_name);
        if (spec) {
            const EntityId src_id = source ? source->id() : kInvalidEntityId;
            th->modifiers().attach(std::make_unique<ScriptedModifier>(
                *th, modifier_name, duration, *spec, *lua, src_id, /*ability=*/nullptr));
        } else {
            lua->report_error("create_thinker", "未注册的修饰器: " + modifier_name);
        }
    }
    return th;
}

std::vector<Unit*> World::find_enemies_in_radius(Vec2 origin, double radius, Team source_team) {
    std::vector<Unit*> out;
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const double r = radius + u->hull_radius();
        if (distance_sq(origin, u->position()) <= r * r) out.push_back(u.get());
    }
    return out;
}

std::vector<Unit*> World::find_enemies_in_line(Vec2 start, Vec2 end, double width, Team source_team) {
    std::vector<Unit*> out;
    const Vec2   seg = end - start;
    const double seg_len2 = seg.x * seg.x + seg.y * seg.y;
    if (seg_len2 <= 0.0) return out;

    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const Vec2 d = u->position() - start;
        // 投影参数 t∈[0,1] (clamp 后端点形成胶囊)
        double t = (d.x * seg.x + d.y * seg.y) / seg_len2;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
        const Vec2 proj{start.x + seg.x * t, start.y + seg.y * t};
        const double thresh = width * 0.5 + u->hull_radius();
        if (distance_sq(proj, u->position()) <= thresh * thresh) out.push_back(u.get());
    }
    return out;
}

namespace {
// 点 p 到线段 [a,b] 的距离平方
double point_to_segment_dist_sq(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab{b.x - a.x, b.y - a.y};
    const double len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 <= 0.0) return distance_sq(p, a);
    double t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2;
    if (t < 0.0) t = 0.0;
    else if (t > 1.0) t = 1.0;
    const Vec2 proj{a.x + ab.x * t, a.y + ab.y * t};
    return distance_sq(p, proj);
}

// wall tracing 用: 找出 desired 位置上与 self 重叠(圆心距 < hull 之和)的
// 第一个其他单位. 过滤规则与 resolve_unit_collisions 保持一致 -- 否则会出现
// "wall trace 挡了, 分离 pass 又不挡"的不一致.
Unit* first_blocker(const std::vector<std::unique_ptr<Unit>>& units,
                     const Unit& self, Vec2 desired, double self_hull) {
    for (const auto& up : units) {
        Unit* u = up.get();
        if (!u || u == &self) continue;
        if (!u->alive()) continue;
        if (u->team() == Team::Neutral) continue;
        if (u->modifiers().has_state(ModifierState::NoUnitCollision)) continue;
        const double r = self_hull + u->hull_radius();
        if (distance_sq(desired, u->position()) < r * r) return u;
    }
    return nullptr;
}
} // namespace

std::vector<Unit*> World::find_enemies_in_cone(Vec2 origin, Vec2 direction, double length,
                                                double half_angle_rad, Team source_team) {
    std::vector<Unit*> out;
    const Vec2 dir = normalized(direction);
    if (dir.x == 0.0 && dir.y == 0.0) return out;
    const double cos_half = std::cos(half_angle_rad);
    const double sin_half = std::sin(half_angle_rad);
    // 锥的两条边界射线终点 (将 dir 旋转 ±half_angle 后乘 length)
    const Vec2 left_end{
        origin.x + ( dir.x * cos_half - dir.y * sin_half) * length,
        origin.y + ( dir.x * sin_half + dir.y * cos_half) * length
    };
    const Vec2 right_end{
        origin.x + ( dir.x * cos_half + dir.y * sin_half) * length,
        origin.y + (-dir.x * sin_half + dir.y * cos_half) * length
    };
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const double r  = u->hull_radius();
        const double r2 = r * r;
        const Vec2 P = u->position();
        const Vec2 d{P.x - origin.x, P.y - origin.y};
        const double dist2 = d.x * d.x + d.y * d.y;
        // (a) origin 在单位圆内 -> 命中
        if (dist2 <= r2) { out.push_back(u.get()); continue; }
        // (b) 圆心距 origin 超过 length + r -> 不可能命中
        const double max_reach = length + r;
        if (dist2 > max_reach * max_reach) continue;
        const double dist = std::sqrt(dist2);
        // (c) 圆心在角度楔形内 -> 命中
        const double cos_to = (dir.x * d.x + dir.y * d.y) / dist;
        if (cos_to >= cos_half) { out.push_back(u.get()); continue; }
        // (d) 圆心在锥外, 但圆与边界射线段相交 -> 命中
        if (point_to_segment_dist_sq(P, origin, left_end)  <= r2) { out.push_back(u.get()); continue; }
        if (point_to_segment_dist_sq(P, origin, right_end) <= r2) { out.push_back(u.get()); continue; }
    }
    return out;
}

void World::fill_move_path(Unit& u, Vec2 target, MovePath& out) {
    out.waypoints.clear();
    out.index = 0;
    if (pathfinder_) {
        auto pts = pathfinder_->find_path(u.position(), target);
        if (!pts.empty()) {
            out.waypoints = std::move(pts);
            return;
        }
    }
    out.waypoints.push_back(target);
}

void World::tick_movement(double dt) {
    if (dt <= 0.0) return;
    constexpr double kEps = 1e-6;
    // 同 tick_once 中的 tick_units 模式: 用索引迭代避免 set_position 触发的 Lua
    // 钩子在 vector 扩容时让 reference 悬挂.
    const std::size_t n = units_.size();
    for (std::size_t i = 0; i < n; ++i) {
        Unit* u = units_[i].get();
        if (!u || !u->alive()) continue;
        if (u->move_path().empty()) continue;
        if (!u->can_move()) continue;
        // 任何 motion controller 正在驱动 -> 暂停指令式移动. KB / Hook 仍按其
        // 优先级生效, 结束后玩家原指令自然续走.
        bool has_mc = false;
        for (const auto& m : u->modifiers().all()) {
            if (m && m->is_motion_controller()) { has_mc = true; break; }
        }
        if (has_mc) continue;

        const double speed = u->move_speed();
        if (speed <= 0.0) continue;
        const double step = speed * dt;
        if (step <= 0.0) continue;

        const Vec2 pos    = u->position();
        const Vec2 wp     = u->move_path().current();
        const Vec2 to_wp  = wp - pos;
        const double remain = length(to_wp);

        // 到达当前航点 -> 切到下一个(或清空 path).
        if (remain <= step + kEps) {
            u->set_position(wp);
            u->advance_move_waypoint();
            if (u->move_path().empty()) u->clear_move_path();
            continue;
        }

        const Vec2 dir = to_wp * (1.0 / remain);
        Vec2 desired = pos + dir * step;

        // Wall tracing 切线滑动: 路径上有其他单位阻挡时, 沿连心线垂直方向
        // 滑动 step 长度(不缩短步长, 视觉速度连续), 朝目标方向那一侧.
        // 楔形夹角内卡住 -> 本 tick 放弃, 下 tick 再尝试.
        constexpr int kMaxSlideIters = 3;
        Vec2 try_pos = desired;
        for (int iter = 0; iter < kMaxSlideIters; ++iter) {
            Unit* b = first_blocker(units_, *u, try_pos, u->hull_radius());
            if (!b) break;
            const Vec2 to_b = b->position() - pos;
            Vec2 tangent{-to_b.y, to_b.x};
            // 选朝目标推进的一侧
            if (dot(tangent, to_wp) < 0.0) {
                tangent.x = -tangent.x;
                tangent.y = -tangent.y;
            }
            const double tlen = length(tangent);
            if (tlen <= kEps) { try_pos = pos; break; }
            const Vec2 tn{tangent.x / tlen, tangent.y / tlen};
            try_pos = pos + tn * step;
        }
        // 迭代用尽仍重叠 -> 不动
        if (first_blocker(units_, *u, try_pos, u->hull_radius()) != nullptr) {
            try_pos = pos;
        }
        desired = try_pos;
        u->set_position(desired);
    }
}

void World::advance(double dt) {
    if (dt <= 0.0) return;
    // 细分为完整的 tick, 使行为具有确定性, 无论调用者的 `dt` 有多粗糙
    const int ticks = static_cast<int>(std::round(dt / kTickDt));
    for (int i = 0; i < ticks; ++i) tick_once();
}

void World::tick_once() {
    time_ += kTickDt;

    // 注意: 下面的循环中, modifier / ability 钩子可能通过 Lua 回调
    // World::create_thinker / spawn, 进而向 units_ push_back. 当 vector 扩容时
    // 旧 buffer 会被释放, 任何范围 for 拿到的 reference / iterator 都会悬挂
    // (heap-use-after-free). 因此在每个循环开始前 freeze 一次 size, 用索引
    // 迭代 -- 每次 units_[i] 都重新解引用当前 buffer, 既安全又保留"本 tick
    // 不处理新 spawn 单位"的原语义 (与投射物延一帧的设计一致).
    const auto tick_units = [&](auto&& fn) {
        const std::size_t n = units_.size();
        for (std::size_t i = 0; i < n; ++i) {
            auto& u = units_[i];
            if (u->alive()) fn(*u);
        }
    };

    // 在处理指令前推进 modifier 持续时间/思考, 使即将过期的眩晕能让单位在同一 tick 内攻击
    tick_units([&](Unit& u) { u.tick_modifiers(kTickDt); });
    // motion controller 在普通 modifier tick 之后, ability tick 之前生效,
    // 使被击退的单位在同 tick 内可被技能命中其新位置(与 Dota2 行为一致).
    tick_units([&](Unit& u) { u.modifiers().advance_motion(kTickDt); });
    // 玩家指令式移动: 在 motion controller 之后, ability 之前. 通过 set_position
    // 改坐标 -- moved_this_tick_for_collision 自动置位, 末尾的 resolve_unit_collisions
    // 兜底分离重叠.
    tick_movement(kTickDt);
    // 在 modifier 之后推进技能(施法前摇计时器, 引导思考, 冷却),
    // 使刚过期的眩晕不会打断本 tick 应该完成的施法
    tick_units([&](Unit& u) { u.tick_abilities(kTickDt); });

    // 投射物在 ability tick 之后; 这样 ability 在本 tick 中 spawn 的投射物
    // 会有一帧的延迟(与 Dota 一致), 给 hit 回调留出在下个 tick 引发
    // 链式反应的余地.
    projectiles_->advance(kTickDt, *this);

    // 软碰撞分离: 在 motion / ability / projectile 全部移动完之后, 攻击之前.
    // 这样攻击距离判定看到的是分离后的位置.
    resolve_unit_collisions();

    // 分离结束后刷新 baseline -- 下一 tick 的"是否移动过"判定基于此 snapshot,
    // 即外部代码 (Lua / 测试) 在 tick 间隔中通过 set_position 改坐标也算"动过".
    tick_units([&](Unit& u) { u.snapshot_tick_position(); });

    // 首先递减所有攻击冷却, 使在同一 tick 内安排新攻击保持一致
    tick_units([&](Unit& u) { u.tick_attack_cd(kTickDt); });

    // 指令队列尾部 sweep: tick_movement / tick_attack_cd 都走完之后, 派发各单位
    // 队首. 三个职责:
    //   - MoveToPoint: 走完 (move_path 空) -> pop 并衔接下一条.
    //   - Cast*: 派发 / 完成检测 (Stage 3 落地).
    //   - AttackTarget: 距离不够派生跟随 move; 在范围且 attack_cd<=0 -> 调
    //     World::resolve_attack. 持续行为, 不 pop, 直到 target 死亡或被新指令覆盖.
    tick_units([&](Unit& u) { u.pump_orders(); });

    // 死亡清理: 把 alive=false 且 remove_on_death=true 的单位从 units_ 中移除.
    // 必须在所有 tick 阶段(motion / movement / ability / projectile / pump_orders)
    // 之后, 才能保证不会有热路径还持有指针. 跨 tick 的引用统一走 EntityId + find,
    // 所以这里 erase 不会让任何长寿命指针悬挂.
    units_.erase(
        std::remove_if(units_.begin(), units_.end(),
                       [](const std::unique_ptr<Unit>& u) {
                           return u && !u->alive() && u->remove_on_death();
                       }),
        units_.end());

    ++tick_count_;
    TickEndEvent ev{time_, tick_count_};
    events_.publish(ev);
}

void World::resolve_unit_collisions() {
    // 收集本 tick 参与碰撞的单位 (alive / 非 Neutral / 无 NoUnitCollision).
    // Neutral 用于 thinker, 已经常态带 NoUnitCollision, 但保留 team 过滤兜底.
    struct Body {
        Unit*  u;
        double r;
        bool   moved;
    };
    std::vector<Body> bodies;
    bodies.reserve(units_.size());
    for (auto& up : units_) {
        Unit* u = up.get();
        if (!u || !u->alive()) continue;
        if (u->team() == Team::Neutral) continue;
        if (u->modifiers().has_state(ModifierState::NoUnitCollision)) continue;
        bodies.push_back({u, u->hull_radius(), u->moved_this_tick_for_collision()});
    }
    if (bodies.size() < 2) return;

    // 多轮迭代以处理链式挤压. 每轮所有重叠对各推一次.
    constexpr int kMaxPasses = 4;
    constexpr double kEps = 1e-6;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        bool any_overlap = false;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            for (std::size_t j = i + 1; j < bodies.size(); ++j) {
                Body& a = bodies[i];
                Body& b = bodies[j];
                const Vec2 pa = a.u->position();
                const Vec2 pb = b.u->position();
                double dx = pa.x - pb.x;
                double dy = pa.y - pb.y;
                const double min_d = a.r + b.r;
                const double dist2 = dx * dx + dy * dy;
                if (dist2 >= min_d * min_d) continue;

                any_overlap = true;
                double dist = std::sqrt(dist2);
                // 重叠量先锁住, fallback 选方向时不会被新的 |sa-sb| 污染.
                const double overlap = min_d - dist;
                // 圆心几乎重合时, 用 "a 的 tick 起点 -> b 当前位置" 作为方向,
                // 让 a 沿它走来的方向退回去 (而非随机方向). 起点也重合则退回 b
                // 的起点反方向; 都不行就给个默认 (1, 0).
                if (dist < kEps) {
                    const Vec2 sa = a.u->tick_start_position();
                    const Vec2 sb = b.u->tick_start_position();
                    dx = sa.x - sb.x;
                    dy = sa.y - sb.y;
                    double sd = std::sqrt(dx * dx + dy * dy);
                    if (sd < kEps) { dx = 1.0; dy = 0.0; sd = 1.0; }
                    dist = sd;
                }
                const double inv = 1.0 / dist;
                const double nx = dx * inv;
                const double ny = dy * inv;

                double share_a = 0.0, share_b = 0.0;
                if (a.moved && b.moved)      { share_a = 0.5; share_b = 0.5; }
                else if (a.moved)            { share_a = 1.0; }
                else if (b.moved)            { share_b = 1.0; }
                else                         { /* 双方都没动: 保留初始重叠 */ continue; }

                if (share_a > 0.0) {
                    a.u->set_position({pa.x + nx * overlap * share_a,
                                       pa.y + ny * overlap * share_a});
                }
                if (share_b > 0.0) {
                    b.u->set_position({pb.x - nx * overlap * share_b,
                                       pb.y - ny * overlap * share_b});
                }
            }
        }
        if (!any_overlap) break;
    }
}

namespace {

// complete: 走完一次 attack record 的伤害结算. 闪避 -> miss + on_attack_fail;
// 命中 -> deal_damage + on_attack_landed. 最后无论结果都派发 on_attack_record_destroy.
// attacker / target 任意一方在攻击飞行期间死亡时也会调用 (record.missed=true 路径).
//
// 调用时 record.processed 必须为 false; 调用后置 true 防止重复处理 (远程
// projectile 的 on_finish 兜底也走这里, 期间命中已 complete 则 no-op).
void complete_attack_impl(World& world, AttackRecord& record) {
    if (record.processed) return;
    record.processed = true;

    Unit* attacker = world.find(record.attacker);
    Unit* target   = world.find(record.target);

    // attacker 已不存在 -> 仍要派发 destroy 给已认领 modifier (但目前 listeners
    // 指针属于 attacker 上的 ModifierManager, attacker 没了等于自动失效, 直接返回).
    if (!attacker) return;

    const bool target_alive = target && target->alive() &&
                               !target->modifiers().has_state(ModifierState::Untargetable) &&
                               !target->modifiers().has_state(ModifierState::OutOfGame);

    if (!target_alive) {
        record.missed = true;
        AttackLandedEvent miss{record.attacker, record.target, 0.0,
                                /*missed=*/true, record.id};
        world.events().publish(miss);
        attacker->modifiers().dispatch_on_attack_fail(record);
        attacker->modifiers().dispatch_on_attack_record_destroy(record);
        return;
    }

    // 闪避检定 (与 Stage 1 之前位置一致, 在伤害结算前).
    const double evasion = target->evasion();
    if (evasion > 0.0 && world.rng().chance(evasion)) {
        record.missed = true;
        AttackLandedEvent miss{record.attacker, record.target, 0.0,
                                /*missed=*/true, record.id};
        world.events().publish(miss);
        attacker->modifiers().dispatch_on_attack_fail(record);
        attacker->modifiers().dispatch_on_attack_record_destroy(record);
        return;
    }

    const double total = record.base_damage + record.bonus_damage;
    const double applied = deal_damage({attacker, target,
                                         record.damage_type, total, 0});

    AttackLandedEvent ev{record.attacker, record.target, applied,
                          /*missed=*/false, record.id};
    world.events().publish(ev);
    attacker->modifiers().dispatch_on_attack_landed(record);

    if (!target->alive()) {
        UnitDiedEvent died{target->id(), attacker->id()};
        world.events().publish(died);
        target->clear_orders();
    }
    attacker->modifiers().dispatch_on_attack_record_destroy(record);
}

} // namespace

void World::resolve_attack(Unit& attacker, Unit& target) {
    // Stage 1 流程:
    //   1. begin: 锁 base_damage, 创建 record, 派发 on_attack (法球认领).
    //   2. 远程 -> spawn TrackingProjectile, 命中回调 complete_attack_impl.
    //      近战 -> 立即 complete_attack_impl.
    //   3. attack_cd 在 begin 立即设置 -- 与 Dota 的 swing 节奏一致, 不会因为
    //      远程在飞而连点.
    AttackRecord record;
    record.id           = next_attack_record_id_++;
    record.attacker     = attacker.id();
    record.target       = target.id();
    record.base_damage  = attacker.attack_damage();
    record.damage_type  = DamageType::Physical;
    record.missed       = false;
    record.processed    = false;

    attacker.modifiers().dispatch_on_attack(record);

    attacker.set_attack_cd(attacker.seconds_per_attack());

    if (!attacker.stats().ranged) {
        complete_attack_impl(*this, record);
        return;
    }

    // 远程: 用 TrackingProjectile 把 record 带到 target 命中点再结算.
    // shared_ptr 持有 record, 让 hit / finish 回调共享同一份并 mutate processed.
    auto rec = std::make_shared<AttackRecord>(std::move(record));

    TrackingProjectile::Params p;
    p.source_id   = attacker.id();
    p.source_team = attacker.team();
    p.origin      = attacker.position();
    p.target_id   = target.id();
    const double speed = attacker.stats().projectile_speed > 0.0
                             ? attacker.stats().projectile_speed
                             : 900.0;
    p.speed       = speed;

    auto proj = std::make_unique<TrackingProjectile>(p);

    // 法球 (或其它 modifier) 可提供普攻 projectile 名 (粒子). 选 attacker 上
    // 首个非空字符串. 空串表示走默认普攻投射物 (录像层用 unit 默认).
    for (const auto& mod : attacker.modifiers().all()) {
        if (!mod) continue;
        std::string n = mod->projectile_name();
        if (!n.empty()) {
            proj->set_name(std::move(n));
            break;
        }
    }

    World* self = this;
    proj->set_on_hit([self, rec](Unit& /*victim*/, Vec2 /*hit*/) {
        complete_attack_impl(*self, *rec);
    });
    proj->set_on_finish([self, rec]() {
        // target 死亡 / Untargetable 中途逃逸 -> projectile 没调 on_hit. 这里
        // 兜底走 fail 路径. complete_attack_impl 内部用 processed 防重入.
        complete_attack_impl(*self, *rec);
    });

    projectiles_->spawn(std::move(proj));
}

} // namespace dota
