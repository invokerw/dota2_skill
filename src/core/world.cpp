#include "dota/core/world.hpp"

#include "dota/combat/damage.hpp"
#include "dota/core/attack.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"
#include "dota/pathfinding/shape_cast.hpp"
#include "dota/pathfinding/wall_tracer.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/script/lua_state.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

World::World()
    : projectiles_(std::make_unique<ProjectileManager>())
    , nav_grid_(std::make_shared<pathfinding::NavGrid>(
          0.0, 0.0, 1.0, 1.0, 1.0)) {
    projectiles_->set_world(this);
}
World::~World() = default;

void World::set_nav_grid(std::shared_ptr<pathfinding::NavGrid> g) {
    if (g) nav_grid_ = std::move(g);
}

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

namespace {

using namespace dota::pathfinding;

// 收集所有"参与碰撞的"动态圆 body. 过滤 Neutral / NoUnitCollision / 死亡, 与
// resolve_unit_collisions 保持一致.
//
// 与 Unity MoveDemo SetMoving 对齐: 正在移动且非阻挡等待的单位 group=Unit (其它
// 单位的 WallTracer 查 Terrain 时绕不到它), 静止 / 阻挡等待中的单位 group=All
// (会被绕开). 这样头碰头时一方进入 block_wait -> 升级为 Terrain group, 对方
// 重算 smooth 时 WallTracer 看到它并自然绕路.
std::vector<DynamicCircle> collect_dynamics(
    const std::vector<std::unique_ptr<Unit>>& units) {
    std::vector<DynamicCircle> out;
    out.reserve(units.size());
    for (const auto& up : units) {
        Unit* u = up.get();
        if (!u || !u->alive()) continue;
        if (u->team() == Team::Neutral) continue;
        if (u->modifiers().has_state(ModifierState::NoUnitCollision)) continue;
        const auto& ms = u->move_state();
        const bool moving = ms.active && ms.block_wait == 0;
        const std::uint32_t g = moving ? CollisionGroups::Unit
                                        : CollisionGroups::All;
        out.push_back(DynamicCircle{
            u->position(), u->hull_radius(), g, u->id()});
    }
    return out;
}

} // namespace

void World::tick_movement(double dt) {
    if (dt <= 0.0) return;
    using namespace dota::pathfinding;

    NavGrid& grid = *nav_grid_;
    // dynamics 每个单位前重建一次. unit 数量量级不大, 本身就是 O(N), 重建外
    // 加包了一层 O(N) -> O(N^2), 仍然便宜; 换来可以读到本 tick 中先动单位的
    // 新位置, 头碰头场景里两边能看到彼此真实距离而不是 stale snapshot.
    const std::size_t n = units_.size();
    for (std::size_t i = 0; i < n; ++i) {
        const auto dynamics_snapshot = collect_dynamics(units_);
        Unit* u = units_[i].get();
        if (!u || !u->alive()) continue;
        MoveState& ms = u->move_state_mut();
        if (!ms.active) continue;
        if (!u->can_move()) continue;

        // 任何 motion controller 正在驱动 -> 暂停指令式移动.
        bool has_mc = false;
        for (const auto& m : u->modifiers().all()) {
            if (m && m->is_motion_controller()) { has_mc = true; break; }
        }
        if (has_mc) continue;

        const double speed  = u->move_speed();
        if (speed <= 0.0) continue;
        const double step   = speed * dt;
        if (step <= 0.0) continue;

        const double radius = u->hull_radius();
        const EntityId self_id = u->id();
        const int rid          = static_cast<int>(self_id);
        const double waypoint_eps =
            MovementConfig::arrival_epsilon * grid.cell_size();
        const double waypoint_thr = std::max(waypoint_eps, 0.3);

        // 移动期间临时把自身从 dynamics 中"剥离" (传 ignore_id 即可).
        // 当前 dynamics_snapshot 是本 tick 起始的快照; 用 ignore_id 跳过.

        // --- 1. 阻挡等待倒计时 ---
        if (ms.block_wait > 0) {
            --ms.block_wait;
            if (ms.block_wait > 0) continue;
            ++ms.seg_block;
            ms.smooth.clear();
            ms.smooth_index = 0;
        }

        // --- 2. 子路径走完 ---
        if (!ms.smooth.empty() && ms.smooth_index >= ms.smooth.size()) {
            if (ms.rough_index < ms.rough.size()) {
                const Vec2 seg_end = ms.rough[ms.rough_index];
                const double dx = seg_end.x - u->position().x;
                const double dy = seg_end.y - u->position().y;
                if (dx * dx + dy * dy < waypoint_thr * waypoint_thr) {
                    ++ms.rough_index;
                    ms.seg_block = 0;
                    ms.seg_miss  = 0;
                } else {
                    ++ms.seg_miss;
                }
            }
            ms.smooth.clear();
            ms.smooth_index = 0;
        }

        // --- 3. 阻挡 / 偏移过多 -> skip 或 full replan ---
        if (ms.seg_block > MovementConfig::replan_threshold ||
            ms.seg_miss  > MovementConfig::replan_threshold) {
            ms.rough.clear();
            ms.rough_index = 0;
            ms.seg_block   = 0;
            ms.seg_miss    = 0;
        } else if ((ms.seg_block >= MovementConfig::waypoint_skip_threshold ||
                    ms.seg_miss  >= MovementConfig::waypoint_skip_threshold) &&
                   ms.smooth.empty()) {
            // SkipOrReplan: 优先跳到下一 waypoint, 没有则 full replan.
            if (ms.rough_index + 1 < ms.rough.size()) {
                ++ms.rough_index;
                ms.seg_block = 0;
                ms.seg_miss  = 0;
            } else {
                ms.rough.clear();
                ms.rough_index = 0;
                ms.seg_block   = 0;
                ms.seg_miss    = 0;
            }
        }

        // --- 4. 计算 rough path (A*) ---
        if (ms.rough.empty()) {
            ms.rough = grid.find_path(u->position(), ms.destination);
            if (ms.rough.empty()) {
                ms = MoveState{};   // 不可达 -> 视为完成, pump_orders 衔接下一条
                continue;
            }
            ms.rough.back() = ms.destination;
            ms.rough_index = ms.rough.size() > 1 ? 1u : 0u;
            ms.smooth.clear();
            ms.smooth_index = 0;
            ms.closest_stall = 0;
        }

        // --- 5. 计算 smooth (WallTracer) ---
        if (ms.smooth.empty()) {
            if (ms.rough_index >= ms.rough.size()) {
                ms = MoveState{};
                continue;
            }
            const Vec2 seg_end = ms.rough[ms.rough_index];
            WallTracer tracer(radius);
            std::vector<Vec2> path;
            const PathResult pr = tracer.find_path(
                grid, dynamics_snapshot, self_id,
                u->position(), seg_end, path,
                CollisionGroups::Terrain);

            if (pr != PathResult::Reached) {
                const bool is_last = ms.rough_index + 1 >= ms.rough.size();
                if (is_last && !path.empty()) {
                    // 最后一段: 截到距 dest 最近的点
                    const Vec2 cur = u->position();
                    double best_d2 = (cur.x - seg_end.x) * (cur.x - seg_end.x) +
                                      (cur.y - seg_end.y) * (cur.y - seg_end.y);
                    int best_idx = -1;
                    for (std::size_t k = 0; k < path.size(); ++k) {
                        const double dx = path[k].x - seg_end.x;
                        const double dy = path[k].y - seg_end.y;
                        const double d2 = dx * dx + dy * dy;
                        if (d2 < best_d2) {
                            best_d2  = d2;
                            best_idx = static_cast<int>(k);
                        }
                    }
                    if (best_idx < 0) {
                        ++ms.closest_stall;
                        if (ms.closest_stall > MovementConfig::closest_stall_threshold) {
                            ms = MoveState{};
                            continue;
                        }
                        ms.block_wait = (ms.block_seed + rid) %
                                        MovementConfig::max_block_wait_frames + 1;
                        ms.smooth.clear();
                        ms.smooth_index = 0;
                        continue;
                    }
                    path.resize(static_cast<std::size_t>(best_idx) + 1);
                    tracer.simplify(grid, dynamics_snapshot, self_id, path,
                                    CollisionGroups::Terrain);
                    ms.smooth      = std::move(path);
                    ms.smooth_index = 0;
                } else {
                    ++ms.seg_block;
                    ms.block_wait = (ms.block_seed + rid) %
                                    MovementConfig::max_block_wait_frames + 1;
                    ms.smooth.clear();
                    ms.smooth_index = 0;
                    continue;
                }
            } else {
                if (path.empty()) path.push_back(seg_end);
                if (ms.rough_index + 1 >= ms.rough.size()) {
                    tracer.simplify(grid, dynamics_snapshot, self_id, path,
                                    CollisionGroups::Terrain);
                    ms.smooth      = std::move(path);
                    ms.smooth_index = 0;
                } else {
                    // 跨段 simplify: 把后续 rough waypoint 拼接做整体优化
                    std::vector<Vec2> combined;
                    combined.reserve(path.size() + 4);
                    combined.push_back(u->position());
                    for (auto& p : path) combined.push_back(p);
                    const std::size_t lookahead = std::min(
                        ms.rough_index + 1 + 3, ms.rough.size());
                    for (std::size_t r = ms.rough_index + 1; r < lookahead; ++r) {
                        combined.push_back(ms.rough[r]);
                    }
                    tracer.simplify(grid, dynamics_snapshot, self_id, combined,
                                    CollisionGroups::Terrain);

                    // 找第一个仍存在于 simplified 中的 rough waypoint
                    auto approx_eq = [](Vec2 a, Vec2 b) {
                        const double dx = a.x - b.x;
                        const double dy = a.y - b.y;
                        return dx * dx + dy * dy < 1e-12;
                    };
                    std::size_t new_rough_idx = ms.rough.size() - 1;
                    std::size_t cut_idx       = combined.size() - 1;
                    std::size_t search_from   = 1;
                    for (std::size_t ri = ms.rough_index + 1;
                         ri < ms.rough.size(); ++ri) {
                        bool found = false;
                        for (std::size_t si = search_from;
                             si < combined.size(); ++si) {
                            if (approx_eq(combined[si], ms.rough[ri])) {
                                new_rough_idx = ri;
                                cut_idx       = si;
                                found         = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                    ms.rough_index = new_rough_idx;

                    ms.smooth.clear();
                    ms.smooth.reserve(cut_idx);
                    for (std::size_t k = 1; k <= cut_idx; ++k) {
                        ms.smooth.push_back(combined[k]);
                    }
                    ms.smooth_index = 0;
                }
            }
        }

        // --- 6. 沿 smooth 推进 ---
        if (ms.smooth.empty() || ms.smooth_index >= ms.smooth.size()) continue;

        ++ms.block_seed;

        const Vec2 wp     = ms.smooth[ms.smooth_index];
        const Vec2 pos    = u->position();
        const double dx   = wp.x - pos.x;
        const double dy   = wp.y - pos.y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < waypoint_eps) {
            ++ms.smooth_index;
            continue;
        }
        const Vec2 dir{dx / dist, dy / dist};
        const bool arriving = step >= dist;
        const double mv     = arriving ? dist : step;

        // 检查 mv 距离上是否撞到 unit / terrain. 自身从 dynamics 中跳过.
        const auto hit = shape_cast_circle(
            grid, dynamics_snapshot, self_id, pos, dir, mv, radius,
            CollisionGroups::All);

        if (hit.hit) {
            if (hit.kind == ShapeCastHit::Kind::Unit) {
                // 最后一段且距 dest 已经够近 -> 视为到达停下.
                const bool is_last = ms.rough_index + 1 >= ms.rough.size();
                if (is_last) {
                    const double ddx = ms.destination.x - pos.x;
                    const double ddy = ms.destination.y - pos.y;
                    const double dd  = std::sqrt(ddx * ddx + ddy * ddy);
                    if (dd < radius * MovementConfig::arrival_tolerance_multiplier) {
                        ms = MoveState{};
                        continue;
                    }
                }
            } else {
                // 地形阻挡 -> rough 失效, full replan
                ms.rough.clear();
                ms.smooth.clear();
                ms.rough_index  = 0;
                ms.smooth_index = 0;
                ms.seg_block    = 0;
                ms.seg_miss     = 0;
            }
            ms.block_wait = (ms.block_seed + rid) %
                            MovementConfig::max_block_wait_frames + 1;
            continue;
        }

        // 推进
        if (arriving) {
            u->set_position(wp);
            ++ms.smooth_index;
        } else {
            u->set_position({pos.x + dir.x * step, pos.y + dir.y * step});
        }
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
