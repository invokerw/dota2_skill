#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"  // for DamageType used in apply_damage

#include <memory>
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
    ~Unit();

    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;

    EntityId  id()   const { return id_; }
    const std::string& name() const { return name_; }
    Team      team() const { return team_; }

    const UnitStats& stats() const { return stats_; }

    double health() const { return health_; }
    double mana()   const { return mana_; }
    double max_health() const;
    double max_mana()   const;
    bool   alive()  const { return health_ > 0.0; }

    Vec2   position() const { return position_; }
    void   set_position(Vec2 p) { position_ = p; }

    // --- Combat stats (aggregated through the ModifierManager) ---
    double armor()          const;
    double attack_damage()  const;
    double magic_resist()   const;
    double move_speed()     const;

    // Seconds between attacks given current attack speed.
    double seconds_per_attack() const;

    // --- Action gating (consults modifier states) ---
    bool can_attack() const;
    bool can_cast()   const;
    bool can_move()   const;

    // Apply raw health/mana deltas. Used by the damage pipeline post-resistance.
    void heal(double amount);
    void spend_mana(double amount);
    void set_health(double hp);

    // Raw damage apply that clamps to zero. Returns the actual amount applied.
    // Does not publish events; call apply_damage() for the full pipeline.
    double apply_raw_damage(double amount);

    // Stage 2 damage entry-point: dispatch pre-damage to modifiers on this
    // unit (they may mutate `amount` or record `absorbed`), apply type
    // resistance (physical via armor, magical via magic_resist, pure
    // untouched), subtract HP, dispatch post-damage. Returns the HP actually
    // removed. The Stage 5 pipeline will grow more layers (shields, reflect)
    // but keep this signature.
    double apply_damage(DamageType type, double amount, EntityId attacker = 0);

    ModifierManager&       modifiers()       { return *modifiers_; }
    const ModifierManager& modifiers() const { return *modifiers_; }

    // Attack cooldown bookkeeping (seconds remaining until next swing).
    double attack_cd() const { return attack_cd_; }
    void   set_attack_cd(double t) { attack_cd_ = t; }
    void   tick_attack_cd(double dt);

    // Called once per tick by World; advances modifiers on this unit.
    void tick_modifiers(double dt);

private:
    EntityId    id_;
    std::string name_;
    Team        team_;
    UnitStats   stats_;

    double health_{0.0};
    double mana_{0.0};
    double attack_cd_{0.0};
    Vec2   position_{};

    std::unique_ptr<ModifierManager> modifiers_;
};

} // namespace dota
