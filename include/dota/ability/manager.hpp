#pragma once

#include "dota/ability/ability.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

class Unit;

// Per-unit list of abilities. Mirrors ModifierManager in style (own by
// unique_ptr, stable pointers). Abilities never self-expire, but they do
// advance() every tick for cast / channel / cooldown bookkeeping.
class AbilityManager {
public:
    explicit AbilityManager(Unit& owner) : owner_(owner) {}

    Ability* attach(std::unique_ptr<Ability> ab);

    template <typename A, typename... Args>
    A* attach_new(Args&&... args) {
        auto up = std::make_unique<A>(owner_, std::forward<Args>(args)...);
        A* raw = up.get();
        attach(std::move(up));
        return raw;
    }

    Ability*       find(const std::string& name);
    const Ability* find(const std::string& name) const;

    const std::vector<std::unique_ptr<Ability>>& all() const { return abilities_; }

    void advance(double dt);

private:
    Unit& owner_;
    std::vector<std::unique_ptr<Ability>> abilities_;
};

} // namespace dota
