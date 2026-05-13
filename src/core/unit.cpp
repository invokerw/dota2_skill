#include "dota/core/unit.hpp"

#include <algorithm>

namespace dota {

Unit::Unit(EntityId id, std::string name, Team team, UnitStats stats)
    : id_(id)
    , name_(std::move(name))
    , team_(team)
    , stats_(stats)
    , health_(stats.max_health)
    , mana_(stats.max_mana) {}

double Unit::seconds_per_attack() const {
    // Dota: effective BAT = base_attack_time / (attack_speed / 100)
    const double as = std::max(20.0, stats_.attack_speed); // clamp like Dota's floor
    return stats_.base_attack_time / (as / 100.0);
}

void Unit::heal(double amount) {
    if (amount <= 0.0 || !alive()) return;
    health_ = std::min(stats_.max_health, health_ + amount);
}

void Unit::spend_mana(double amount) {
    if (amount <= 0.0) return;
    mana_ = std::max(0.0, mana_ - amount);
}

void Unit::set_health(double hp) {
    health_ = std::clamp(hp, 0.0, stats_.max_health);
}

double Unit::apply_raw_damage(double amount) {
    if (amount <= 0.0 || !alive()) return 0.0;
    const double before = health_;
    health_ = std::max(0.0, health_ - amount);
    return before - health_;
}

void Unit::tick_attack_cd(double dt) {
    attack_cd_ = std::max(0.0, attack_cd_ - dt);
}

} // namespace dota
