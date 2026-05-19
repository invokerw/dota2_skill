#include "dota/core/world.hpp"

#include "dota/combat/damage.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/projectile/manager.hpp"
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
            th->modifiers().attach(std::make_unique<ScriptedModifier>(
                *th, modifier_name, duration, *spec, *lua, source, /*ability=*/nullptr));
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

void World::order_attack(Unit& attacker, Unit& target) {
    stop_attack(attacker);
    orders_.push_back({attacker.id(), target.id()});
}

void World::stop_attack(Unit& attacker) {
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(),
                       [&](const AttackOrder& o) { return o.attacker == attacker.id(); }),
        orders_.end());
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

    // 处理未完成的攻击指令. 快照 orders_, 因为攻击可能发布移除指令的事件(例如死亡时)
    auto snapshot = orders_;
    for (const auto& order : snapshot) {
        Unit* attacker = find(order.attacker);
        Unit* target   = find(order.target);
        if (!attacker || !target) continue;
        if (!attacker->alive() || !target->alive()) continue;
        if (attacker->attack_cd() > 0.0) continue;
        if (!attacker->can_attack()) continue;   // 眩晕/缴械等
        resolve_attack(*attacker, *target);
    }

    // 清除参与者消失或死亡的指令
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(), [&](const AttackOrder& o) {
            Unit* a = find(o.attacker);
            Unit* t = find(o.target);
            return !a || !t || !a->alive() || !t->alive();
        }),
        orders_.end());

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

void World::resolve_attack(Unit& attacker, Unit& target) {
    // 闪避检定: 在伤害结算之前掷骰. 命中则走伤害管线.
    const double evasion = target.evasion();
    if (evasion > 0.0 && rng_.chance(evasion)) {
        AttackLandedEvent miss{attacker.id(), target.id(), 0.0, /*missed=*/true};
        events_.publish(miss);
        attacker.set_attack_cd(attacker.seconds_per_attack());
        return;
    }

    // 普通攻击使用物理伤害管线, 使 modifier 可以介入
    // (阶段 2 的护盾吸收, 阶段 5 的伤害格挡/反弹)
    const double raw     = attacker.attack_damage();
    const double applied = deal_damage({&attacker, &target,
                                         DamageType::Physical, raw, 0});

    AttackLandedEvent ev{attacker.id(), target.id(), applied, /*missed=*/false};
    events_.publish(ev);

    if (!target.alive()) {
        UnitDiedEvent died{target.id(), attacker.id()};
        events_.publish(died);
        stop_attack(target); // 死亡目标失去其指令
    }

    attacker.set_attack_cd(attacker.seconds_per_attack());
}

} // namespace dota
