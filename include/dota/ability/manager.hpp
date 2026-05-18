#pragma once

#include "dota/ability/ability.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

class Unit;

// 每个单位的技能列表. 风格上对应 ModifierManager(通过
// unique_ptr 拥有, 指针稳定). 技能永不自动过期, 但它们会
// 在每个 tick 执行 advance() 以进行施法/引导/冷却记账.
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
