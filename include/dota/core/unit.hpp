#pragma once

#include "dota/core/types.hpp"

#include <string>

namespace dota {

class World;

// Base stats and combat state for a controllable entity (hero or creep).
//
// Stage 1 keeps this intentionally concrete — no modifiers, no abilities yet.
// Later stages will route most getters through an aggregator so modifiers can
// inject bonuses (armor/attack-speed/etc.) without touching base fields.
struct UnitStats {
    double max_health        = 100.0;
    double max_mana          = 0.0;
    double base_armor        = 0.0;
    double magic_resist      = 0.25;   // 0..1, fraction of magical damage blocked
    double attack_damage     = 10.0;
    double attack_speed      = 100.0;  // base 100 means 1 BAT between attacks
    double base_attack_time  = 1.7;    // seconds between attacks at 100 AS
    double move_speed        = 300.0;
    double attack_range      = 150.0;
};

class Unit {
public:
    Unit(EntityId id, std::string name, Team team, UnitStats stats);

    EntityId  id()   const { return id_; }
    const std::string& name() const { return name_; }
    Team      team() const { return team_; }

    const UnitStats& stats() const { return stats_; }

    double health() const { return health_; }
    double mana()   const { return mana_; }
    bool   alive()  const { return health_ > 0.0; }

    Vec2   position() const { return position_; }
    void   set_position(Vec2 p) { position_ = p; }

    // --- Combat helpers (pre-modifier; later stages will aggregate) ---
    double armor()          const { return stats_.base_armor; }
    double attack_damage()  const { return stats_.attack_damage; }
    double magic_resist()   const { return stats_.magic_resist; }

    // Seconds between attacks given current attack speed.
    double seconds_per_attack() const;

    // Apply raw health/mana deltas. Stage 1 direct apply; later stages route
    // through a damage pipeline.
    void heal(double amount);
    void spend_mana(double amount);
    void set_health(double hp);

    // Raw damage apply that clamps to zero. Returns the actual amount applied.
    double apply_raw_damage(double amount);

    // Attack cooldown bookkeeping (seconds remaining until next swing).
    double attack_cd() const { return attack_cd_; }
    void   set_attack_cd(double t) { attack_cd_ = t; }
    void   tick_attack_cd(double dt);

private:
    EntityId    id_;
    std::string name_;
    Team        team_;
    UnitStats   stats_;

    double health_{0.0};
    double mana_{0.0};
    double attack_cd_{0.0};
    Vec2   position_{};
};

} // namespace dota
