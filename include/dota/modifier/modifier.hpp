#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/enums.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dota {

class Unit;

// Damage type. Duplicated here (in addition to the Stage 5 combat header
// that will own it) so Stage 2 can already reason about magical vs physical
// in the pre-damage event.
enum class DamageType : std::uint8_t {
    Physical = 0,
    Magical,
    Pure,
};

// --- Modifier-observable events --------------------------------------------
//
// Events carry references where a modifier is expected to mutate a field.
// Damage amount in PreTakeDamageEvent is non-const on purpose — it lets a
// shield modifier absorb part of the damage *before* resistance is applied.
// The Stage 5 damage pipeline will publish these events itself; for Stage 2
// the tests invoke Unit::apply_damage() which is a thin wrapper.

// Flag bitmask applied to a DamageInstance. Mirrors Valve's DOTA_DAMAGE_FLAG_*
// — only the flags Stage 5 actually branches on are enumerated.
enum class DamageFlag : std::uint32_t {
    None                  = 0,
    BypassMagicImmune     = 1u << 0,   // Magical goes through MagicImmune
    HPLoss                = 1u << 1,   // Skips shields + type resist
    NoSpellAmplification  = 1u << 2,   // Skips incoming/outgoing damage pct
    Reflection            = 1u << 3,   // Tagged reflected damage — never reflects again
    NoLifesteal           = 1u << 4,   // Used by Stage 5 lifesteal modifier
};

constexpr std::uint32_t to_mask(DamageFlag f) {
    return static_cast<std::uint32_t>(f);
}
constexpr std::uint32_t operator|(DamageFlag a, DamageFlag b) {
    return to_mask(a) | to_mask(b);
}
constexpr bool has_flag(std::uint32_t mask, DamageFlag f) {
    return (mask & to_mask(f)) != 0;
}

struct PreTakeDamageEvent {
    EntityId      attacker{kInvalidEntityId};
    EntityId      victim{kInvalidEntityId};
    DamageType    type{DamageType::Physical};
    std::uint32_t flags{0};
    double        amount{0.0};      // mutable
    double        absorbed{0.0};    // modifiers record absorption here
};

struct PostTakeDamageEvent {
    EntityId      attacker{kInvalidEntityId};
    EntityId      victim{kInvalidEntityId};
    DamageType    type{DamageType::Physical};
    std::uint32_t flags{0};
    double        amount{0.0};      // final amount applied
};

// Heal events. Modifiers may reduce (or amplify) healing via on_pre_take_heal.
struct PreTakeHealEvent {
    EntityId      healer{kInvalidEntityId};
    EntityId      target{kInvalidEntityId};
    double        amount{0.0};      // mutable
};

struct PostTakeHealEvent {
    EntityId      healer{kInvalidEntityId};
    EntityId      target{kInvalidEntityId};
    double        amount{0.0};      // final amount applied
};

struct ModifierProvidedProperty {
    ModifierProperty property;
    double           value;
};

// Base class for all modifiers. Subclass and override the `declared_*` hooks
// to participate in property aggregation, state bits, and event reactions.
//
// Lifetime: owned by ModifierManager (the target unit's manager). Duration
// and think ticks are driven from the manager. A modifier with
// duration_ < 0 lives forever; think_interval_ <= 0 disables thinking.
class Modifier {
public:
    Modifier(std::string name, Unit& owner, double duration);
    virtual ~Modifier() = default;

    Modifier(const Modifier&) = delete;
    Modifier& operator=(const Modifier&) = delete;

    const std::string& name() const { return name_; }
    Unit&              owner()       { return owner_; }
    const Unit&        owner() const { return owner_; }

    double duration_remaining() const { return duration_; }
    bool   permanent()          const { return permanent_; }
    bool   expired()            const { return !permanent_ && duration_ <= 0.0; }

    int    stack_count() const { return stack_count_; }
    void   set_stack_count(int n);

    // Duration refresh: caller resets remaining duration (Dota reapply). A
    // negative duration makes the modifier permanent again.
    void refresh(double new_duration) {
        duration_  = new_duration;
        permanent_ = new_duration < 0.0;
    }

    // Called by ModifierManager every world tick with dt (seconds).
    void advance(double dt);

    // Subclasses override to contribute numeric bonuses. Called every time the
    // manager recomputes the aggregate — cheap, pure function style.
    virtual std::vector<ModifierProvidedProperty> declared_properties() const { return {}; }

    // Bitmask of declared states. Use state_bit(ModifierState::X) | ...
    virtual std::uint32_t declared_states() const { return 0; }

    // Event hooks. Default no-ops.
    virtual void on_created()                      {}
    virtual void on_destroyed()                    {}
    virtual void on_stack_changed(int /*old*/,
                                  int /*new_*/)    {}
    virtual void on_interval_think()               {}  // fires every think_interval_
    virtual void on_pre_take_damage(PreTakeDamageEvent&)  {}
    virtual void on_post_take_damage(PostTakeDamageEvent&){}
    virtual void on_pre_take_heal(PreTakeHealEvent&)      {}
    virtual void on_post_take_heal(PostTakeHealEvent&)    {}

protected:
    void set_think_interval(double s) { think_interval_ = s; think_accum_ = 0.0; }

private:
    std::string name_;
    Unit&       owner_;
    double      duration_;          // seconds when finite; unused when permanent_
    bool        permanent_;
    double      think_interval_{0.0};
    double      think_accum_{0.0};
    int         stack_count_{1};
};

} // namespace dota
