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

World::World() : projectiles_(std::make_unique<ProjectileManager>()) {}
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
    unit->set_world(this);
    Unit* raw = unit.get();
    units_.push_back(std::move(unit));
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
    const double r2 = radius * radius;
    std::vector<Unit*> out;
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        if (distance_sq(origin, u->position()) <= r2) out.push_back(u.get());
    }
    return out;
}

std::vector<Unit*> World::find_enemies_in_line(Vec2 start, Vec2 end, double width, Team source_team) {
    std::vector<Unit*> out;
    const Vec2   seg = end - start;
    const double seg_len2 = seg.x * seg.x + seg.y * seg.y;
    if (seg_len2 <= 0.0) return out;
    const double half_w2 = (width * 0.5) * (width * 0.5);

    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const Vec2 d = u->position() - start;
        // 投影参数 t∈[0,1]
        double t = (d.x * seg.x + d.y * seg.y) / seg_len2;
        if (t < 0.0) t = 0.0;
        else if (t > 1.0) t = 1.0;
        const Vec2 proj{start.x + seg.x * t, start.y + seg.y * t};
        if (distance_sq(proj, u->position()) <= half_w2) out.push_back(u.get());
    }
    return out;
}

std::vector<Unit*> World::find_enemies_in_cone(Vec2 origin, Vec2 direction, double length,
                                                double half_angle_rad, Team source_team) {
    std::vector<Unit*> out;
    const Vec2 dir = normalized(direction);
    if (dir.x == 0.0 && dir.y == 0.0) return out;
    const double cos_half = std::cos(half_angle_rad);
    const double r2 = length * length;
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const Vec2 d = u->position() - origin;
        const double dist2 = d.x * d.x + d.y * d.y;
        if (dist2 > r2) continue;
        if (dist2 <= 1e-9) { out.push_back(u.get()); continue; }
        const double dist = std::sqrt(dist2);
        // dot(dir, d/|d|) ≥ cos(half_angle)
        const double cos_to = (dir.x * d.x + dir.y * d.y) / dist;
        if (cos_to >= cos_half) out.push_back(u.get());
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

    // 在处理指令前推进 modifier 持续时间/思考, 使即将过期的眩晕能让单位在同一 tick 内攻击
    for (auto& u : units_) {
        if (u->alive()) u->tick_modifiers(kTickDt);
    }
    // motion controller 在普通 modifier tick 之后, ability tick 之前生效,
    // 使被击退的单位在同 tick 内可被技能命中其新位置(与 Dota2 行为一致).
    for (auto& u : units_) {
        if (u->alive()) u->modifiers().advance_motion(kTickDt);
    }
    // 在 modifier 之后推进技能(施法前摇计时器, 引导思考, 冷却),
    // 使刚过期的眩晕不会打断本 tick 应该完成的施法
    for (auto& u : units_) {
        if (u->alive()) u->tick_abilities(kTickDt);
    }

    // 投射物在 ability tick 之后; 这样 ability 在本 tick 中 spawn 的投射物
    // 会有一帧的延迟(与 Dota 一致), 给 hit 回调留出在下个 tick 引发
    // 链式反应的余地.
    projectiles_->advance(kTickDt, *this);

    // 首先递减所有攻击冷却, 使在同一 tick 内安排新攻击保持一致
    for (auto& u : units_) {
        if (u->alive()) u->tick_attack_cd(kTickDt);
    }

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
