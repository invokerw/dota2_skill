#pragma once

#include <cstdint>

namespace dota {

// Numeric properties a modifier can contribute to. These mirror Valve's
// MODIFIER_PROPERTY_* namespace but are trimmed to what Stage 2 needs. Adding
// a new property requires: (1) a new enum entry, (2) a lookup in the Unit's
// stat getter, and (3) optional documentation of the layer it participates in
// (constant vs percentage vs override) — see PropertyLayer.
enum class ModifierProperty : std::uint16_t {
    // Armor
    ArmorBonus = 0,                    // CONSTANT
    ArmorBonusPct,                     // PERCENTAGE (multiplicative on armor)

    // Health / mana
    HealthBonus,                       // CONSTANT on max health
    ManaBonus,                         // CONSTANT on max mana

    // Offense
    AttackDamageBonus,                 // CONSTANT on base attack damage
    AttackDamageBonusPct,              // PERCENTAGE on total attack damage
    AttackSpeedBonusConstant,          // CONSTANT added to AS (e.g. +40 AS)

    // Resist
    MagicResistBonus,                  // CONSTANT added to magic_resist (0..1)

    // Incoming/outgoing damage multipliers (applied in the damage pipeline
    // Stage 5; declared here so properties and tests can already reason about
    // them).
    IncomingDamagePct,                 // PERCENTAGE on incoming damage
    OutgoingDamagePct,                 // PERCENTAGE on outgoing damage

    // Movement
    MoveSpeedBonusConstant,            // CONSTANT (flat MS bonus, e.g. +30)
    MoveSpeedBonusPct,                 // PERCENTAGE

    Count_                             // sentinel
};

// Layer an ModifierProperty contributes into. Aggregation order is:
//   final = (base + sum(CONSTANT)) * (1 + sum(PERCENTAGE)) * product(TOTAL_PCT)
// OVERRIDE wins if any modifier declares it (last-writer semantics).
enum class PropertyLayer : std::uint8_t {
    Constant    = 0,
    Percentage  = 1,
    TotalPercentage = 2,
    Override    = 3,
};

// Boolean states. Stored as a bitmask inside ModifierManager, so any modifier
// declaring a state causes that state to be active on the unit.
enum class ModifierState : std::uint8_t {
    Stunned = 0,
    Silenced,
    Rooted,
    Disarmed,
    Hexed,                 // stun-like but allows items; used by Lion Hex later
    Invisible,
    Invulnerable,
    OutOfGame,             // e.g. Omnislash caster is out-of-game
    MagicImmune,

    Count_                 // sentinel
};

constexpr std::uint32_t state_bit(ModifierState s) {
    return std::uint32_t{1} << static_cast<std::uint32_t>(s);
}

// Layer a property contributes into. The mapping is intentionally static: the
// property name alone determines its aggregation layer.
constexpr PropertyLayer layer_of(ModifierProperty p) {
    switch (p) {
        case ModifierProperty::ArmorBonusPct:
        case ModifierProperty::AttackDamageBonusPct:
        case ModifierProperty::IncomingDamagePct:
        case ModifierProperty::OutgoingDamagePct:
        case ModifierProperty::MoveSpeedBonusPct:
            return PropertyLayer::Percentage;
        default:
            return PropertyLayer::Constant;
    }
}

} // namespace dota
