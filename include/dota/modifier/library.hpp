#pragma once

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

} // namespace dota::modifiers
