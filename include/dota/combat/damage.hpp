#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/modifier.hpp"

#include <cstdint>

namespace dota {

class Unit;

// Input to the damage pipeline. Collects every knob callers can set before a
// damage event resolves — type/amount/flags plus the attacker for lifesteal
// and reflect bookkeeping. Stage 5 pulls the math out of Unit::apply_damage
// and centralises it here so abilities, basic attacks, and reflected hits all
// travel the same path.
struct DamageInstance {
    Unit*         attacker{nullptr};     // may be null for environment damage
    Unit*         victim{nullptr};
    DamageType    type{DamageType::Physical};
    double        amount{0.0};
    std::uint32_t flags{0};              // DamageFlag bitmask
};

// Input to the heal pipeline. Heal amp modifiers can amplify or break heals.
struct HealInstance {
    Unit*   healer{nullptr};
    Unit*   target{nullptr};
    double  amount{0.0};
};

// Run the full damage pipeline:
//   1. OutgoingAmp on attacker (unless NoSpellAmplification)
//   2. IncomingAmp on victim   (unless NoSpellAmplification)
//   3. Modifiers' on_pre_take_damage (shields / absorb)
//   4. Magic-immune short-circuit (unless BypassMagicImmune)
//   5. Type resistance (physical/armor curve, magical/magic-resist)
//   6. Subtract HP
//   7. Modifiers' on_post_take_damage (reflect, lifesteal, on-hit triggers)
// Returns the HP actually removed. Safe to call with null attacker.
double deal_damage(DamageInstance dmg);

// Run the heal pipeline: dispatch pre-heal to victim modifiers, apply
// HealAmpPct, clamp to max HP, dispatch post-heal. Returns HP actually
// restored.
double deal_heal(HealInstance heal);

} // namespace dota
