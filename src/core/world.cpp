#include "dota/core/world.hpp"

#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

World::World() = default;

Unit* World::spawn(std::string name, Team team, UnitStats stats, Vec2 position) {
    auto unit = std::make_unique<Unit>(next_id_++, std::move(name), team, stats);
    unit->set_position(position);
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
    // Subdivide into whole ticks so behavior is deterministic regardless of
    // how coarse the caller's `dt` is.
    const int ticks = static_cast<int>(std::round(dt / kTickDt));
    for (int i = 0; i < ticks; ++i) tick_once();
}

void World::tick_once() {
    time_ += kTickDt;

    // Advance modifier durations/thinks before resolving orders so an expiring
    // stun lets a unit swing on the same tick it expires.
    for (auto& u : units_) {
        if (u->alive()) u->tick_modifiers(kTickDt);
    }

    // Decrement all attack cooldowns first so scheduling a new attack in the
    // same tick is consistent.
    for (auto& u : units_) {
        if (u->alive()) u->tick_attack_cd(kTickDt);
    }

    // Resolve outstanding attack orders. Snapshot orders_ because a swing may
    // publish events that remove orders (e.g., on death).
    auto snapshot = orders_;
    for (const auto& order : snapshot) {
        Unit* attacker = find(order.attacker);
        Unit* target   = find(order.target);
        if (!attacker || !target) continue;
        if (!attacker->alive() || !target->alive()) continue;
        if (attacker->attack_cd() > 0.0) continue;
        if (!attacker->can_attack()) continue;   // stunned/disarmed/etc.
        resolve_attack(*attacker, *target);
    }

    // Purge orders whose participants are gone or dead.
    orders_.erase(
        std::remove_if(orders_.begin(), orders_.end(), [&](const AttackOrder& o) {
            Unit* a = find(o.attacker);
            Unit* t = find(o.target);
            return !a || !t || !a->alive() || !t->alive();
        }),
        orders_.end());
}

void World::resolve_attack(Unit& attacker, Unit& target) {
    // Basic attacks use the physical damage pipeline so modifiers can hook in
    // (shield absorb in Stage 2, damage block / reflect in Stage 5).
    const double raw     = attacker.attack_damage();
    const double applied = target.apply_damage(DamageType::Physical, raw, attacker.id());

    AttackLandedEvent ev{attacker.id(), target.id(), applied};
    events_.publish(ev);

    if (!target.alive()) {
        UnitDiedEvent died{target.id(), attacker.id()};
        events_.publish(died);
        stop_attack(target); // dead targets lose their orders
    }

    attacker.set_attack_cd(attacker.seconds_per_attack());
}

} // namespace dota
