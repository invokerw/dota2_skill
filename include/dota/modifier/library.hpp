#pragma once

#include "dota/combat/damage.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <initializer_list>

namespace dota::modifiers {

// --- State-only modifiers (name-driven stun/silence/root/etc.) -------------

class GenericState : public Modifier {
public:
    // Arg order matches ModifierManager::attach_new, which prepends owner.
    GenericState(Unit& owner, std::string name, double duration, std::uint32_t state_mask)
        : Modifier(std::move(name), owner, duration), mask_(state_mask) {}

    std::uint32_t declared_states() const override { return mask_; }

private:
    std::uint32_t mask_;
};

inline std::unique_ptr<GenericState>
make_stunned(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_stunned", duration, state_bit(ModifierState::Stunned));
}

inline std::unique_ptr<GenericState>
make_silenced(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_silenced", duration, state_bit(ModifierState::Silenced));
}

inline std::unique_ptr<GenericState>
make_rooted(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_rooted", duration, state_bit(ModifierState::Rooted));
}

inline std::unique_ptr<GenericState>
make_hexed(Unit& owner, double duration) {
    // Hex (Lion) disables cast/attack but not movement in Dota. The Hexed
    // state bit alone is enough — can_attack/can_cast block on it but
    // can_move does not.
    return std::make_unique<GenericState>(
        owner, "modifier_hexed", duration,
        state_bit(ModifierState::Hexed));
}

inline std::unique_ptr<GenericState>
make_invisible(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_invisible", duration, state_bit(ModifierState::Invisible));
}

inline std::unique_ptr<GenericState>
make_magic_immune(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_magic_immune", duration, state_bit(ModifierState::MagicImmune));
}

// --- Numeric-stat modifier -------------------------------------------------

class GenericStats : public Modifier {
public:
    GenericStats(Unit& owner, std::string name, double duration,
                 std::initializer_list<ModifierProvidedProperty> props)
        : Modifier(std::move(name), owner, duration), props_(props) {}

    std::vector<ModifierProvidedProperty> declared_properties() const override {
        return props_;
    }

private:
    std::vector<ModifierProvidedProperty> props_;
};

// --- Shield/Absorb modifier ------------------------------------------------
//
// Dota examples: Pipe of Insight's barrier, Medusa's Mana Shield, etc. This
// one simply absorbs up to `capacity` damage across any number of hits and
// self-destructs when empty. Pre-resistance absorption mirrors how Dota
// resolves most "shield" mechanics.
class ShieldAbsorb : public Modifier {
public:
    ShieldAbsorb(Unit& owner, double capacity, double duration)
        : Modifier("modifier_shield_absorb", owner, duration)
        , remaining_(capacity) {}

    double remaining() const { return remaining_; }

    void on_pre_take_damage(PreTakeDamageEvent& ev) override {
        if (remaining_ <= 0.0 || ev.amount <= 0.0) return;
        const double eat = std::min(remaining_, ev.amount);
        ev.amount   -= eat;
        ev.absorbed += eat;
        remaining_  -= eat;
        if (remaining_ <= 0.0) {
            // Consume the shield by collapsing its remaining duration. The
            // manager will purge on the next advance().
            refresh(0.0);
        }
    }

private:
    double remaining_;
};

// --- Periodic heal (Healing Ward style) ------------------------------------
//
// Heals `heal_per_tick` to the owner every `interval` seconds, for `duration`
// seconds total. Runs through the heal pipeline so break-the-healing works.
class PeriodicHeal : public Modifier {
public:
    PeriodicHeal(Unit& owner, double heal_per_tick, double interval, double duration)
        : Modifier("modifier_periodic_heal", owner, duration)
        , amount_(heal_per_tick) {
        set_think_interval(interval);
    }

    void on_interval_think() override {
        deal_heal({nullptr, &owner(), amount_});
    }

private:
    double amount_;
};

inline std::unique_ptr<PeriodicHeal>
make_periodic_heal(Unit& owner, double heal_per_tick, double interval, double duration) {
    return std::make_unique<PeriodicHeal>(owner, heal_per_tick, interval, duration);
}

// --- Reflect (Blade Mail style) --------------------------------------------
//
// Reflects a fraction of the pre-resistance damage back to the attacker as
// Pure damage with DamageFlag::Reflection set, which the pipeline honours by
// skipping any reflect modifiers on the attacker (preventing infinite loops).
class ReflectDamage : public Modifier {
public:
    ReflectDamage(Unit& owner, double fraction, double duration)
        : Modifier("modifier_reflect_damage", owner, duration)
        , fraction_(fraction) {}

    void on_pre_take_damage(PreTakeDamageEvent& ev) override {
        // Record the pre-resistance amount so we can reflect that — Blade Mail
        // in Dota reflects the amount the target would have taken, not the
        // pre-resist headline number. This is an approximation acceptable for
        // Stage 5 tests.
        pending_reflect_ = fraction_ * ev.amount;
        pending_attacker_ = ev.attacker;
    }

    void on_post_take_damage(PostTakeDamageEvent& ev) override {
        if (pending_reflect_ <= 0.0) return;
        if (has_flag(ev.flags, DamageFlag::Reflection)) {
            pending_reflect_ = 0.0;     // never reflect reflected damage
            return;
        }
        Unit* attacker = owner().world()
                             ? owner().world()->find(pending_attacker_)
                             : nullptr;
        if (attacker && attacker->alive()) {
            deal_damage({attacker, attacker, DamageType::Pure,
                         pending_reflect_,
                         to_mask(DamageFlag::Reflection)});
        }
        pending_reflect_ = 0.0;
    }

private:
    double   fraction_;
    double   pending_reflect_{0.0};
    EntityId pending_attacker_{kInvalidEntityId};
};

inline std::unique_ptr<ReflectDamage>
make_blade_mail(Unit& owner, double fraction, double duration) {
    return std::make_unique<ReflectDamage>(owner, fraction, duration);
}

// --- Break-the-healing -----------------------------------------------------
//
// A debuff that reduces incoming heals by a fraction (e.g. 0.4 for -40%).
inline std::unique_ptr<GenericStats>
make_break_healing(Unit& owner, double fraction, double duration) {
    return std::make_unique<GenericStats>(
        owner, "modifier_break_healing", duration,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::HealAmpPct, -fraction}});
}

} // namespace dota::modifiers
