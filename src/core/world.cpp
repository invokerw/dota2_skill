#include "dota/core/world.hpp"

#include "dota/combat/damage.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

World::World() = default;

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
    // 细分为完整的 tick，使行为具有确定性，无论调用者的 `dt` 有多粗糙
    const int ticks = static_cast<int>(std::round(dt / kTickDt));
    for (int i = 0; i < ticks; ++i) tick_once();
}

void World::tick_once() {
    time_ += kTickDt;

    // 在处理指令前推进 modifier 持续时间/思考，使即将过期的眩晕能让单位在同一 tick 内攻击
    for (auto& u : units_) {
        if (u->alive()) u->tick_modifiers(kTickDt);
    }
    // 在 modifier 之后推进技能（施法前摇计时器、引导思考、冷却），
    // 使刚过期的眩晕不会打断本 tick 应该完成的施法
    for (auto& u : units_) {
        if (u->alive()) u->tick_abilities(kTickDt);
    }

    // 首先递减所有攻击冷却，使在同一 tick 内安排新攻击保持一致
    for (auto& u : units_) {
        if (u->alive()) u->tick_attack_cd(kTickDt);
    }

    // 处理未完成的攻击指令。快照 orders_，因为攻击可能发布移除指令的事件（例如死亡时）
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
    // 普通攻击使用物理伤害管线，使 modifier 可以介入
    // （阶段 2 的护盾吸收，阶段 5 的伤害格挡/反弹）
    const double raw     = attacker.attack_damage();
    const double applied = deal_damage({&attacker, &target,
                                         DamageType::Physical, raw, 0});

    AttackLandedEvent ev{attacker.id(), target.id(), applied};
    events_.publish(ev);

    if (!target.alive()) {
        UnitDiedEvent died{target.id(), attacker.id()};
        events_.publish(died);
        stop_attack(target); // 死亡目标失去其指令
    }

    attacker.set_attack_cd(attacker.seconds_per_attack());
}

} // namespace dota
