#pragma once

#include "dota/modifier/enums.hpp"
#include "dota/modifier/modifier.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

class Unit;

// Owns and drives the modifiers attached to a single Unit.
//
// Ownership model: unique_ptr<Modifier> inside a vector. A modifier handle
// returned by `attach()` is stable for the lifetime of that modifier but
// becomes dangling once removed or expired — callers should prefer looking
// up by name for long-lived references.
class ModifierManager {
public:
    explicit ModifierManager(Unit& owner);

    // Adds a modifier constructed elsewhere. Returns a non-owning pointer.
    Modifier* attach(std::unique_ptr<Modifier> mod);

    // Helper: construct + attach in one call. Calls on_created() after insert.
    template <typename M, typename... Args>
    M* attach_new(Args&&... args) {
        auto up = std::make_unique<M>(owner_, std::forward<Args>(args)...);
        M* raw = up.get();
        attach(std::move(up));
        return raw;
    }

    // Remove first modifier with this name. Returns true if removed.
    bool remove(const std::string& name);
    void remove_all();

    Modifier*       find(const std::string& name);
    const Modifier* find(const std::string& name) const;

    // View raw list (read-only). Stable during a single tick.
    const std::vector<std::unique_ptr<Modifier>>& all() const { return modifiers_; }

    // Advance all modifiers by dt; purges expired ones and fires on_destroyed.
    void advance(double dt);

    // --- Aggregate queries -------------------------------------------------
    std::uint32_t aggregated_states() const;
    bool has_state(ModifierState s) const { return (aggregated_states() & state_bit(s)) != 0; }

    // Returns sum of all contributions to `p` at its declared layer. For
    // Constant layer it's an additive sum; for Percentage it's still a
    // summation that callers combine as (1 + sum).
    double aggregated(ModifierProperty p) const;

    // Convenience: returns `(base + constant_bonuses) * (1 + pct_bonuses)`
    // for properties that have both a Constant and Percentage sibling.
    double apply_stat(ModifierProperty constant, ModifierProperty pct, double base) const;

    // Dispatches `PreTakeDamageEvent` / `PostTakeDamageEvent` to every modifier
    // on this unit. Returns the mutated event.
    void dispatch_pre_take_damage(PreTakeDamageEvent& ev);
    void dispatch_post_take_damage(PostTakeDamageEvent& ev);

    // Heal equivalents.
    void dispatch_pre_take_heal(PreTakeHealEvent& ev);
    void dispatch_post_take_heal(PostTakeHealEvent& ev);

private:
    Unit& owner_;
    std::vector<std::unique_ptr<Modifier>> modifiers_;
};

} // namespace dota
