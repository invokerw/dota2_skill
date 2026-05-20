#include "dota/ability/manager.hpp"

#include <algorithm>

namespace dota {

Ability* AbilityManager::attach(std::unique_ptr<Ability> ab) {
    Ability* raw = ab.get();
    abilities_.push_back(std::move(ab));
    return raw;
}

Ability* AbilityManager::find(const std::string& name) {
    auto it = std::find_if(abilities_.begin(), abilities_.end(),
                           [&](const auto& a) { return a->name() == name; });
    return it == abilities_.end() ? nullptr : it->get();
}

const Ability* AbilityManager::find(const std::string& name) const {
    auto it = std::find_if(abilities_.begin(), abilities_.end(),
                           [&](const auto& a) { return a->name() == name; });
    return it == abilities_.end() ? nullptr : it->get();
}

bool AbilityManager::remove_at(std::size_t index) {
    if (index >= abilities_.size()) return false;
    abilities_.erase(abilities_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void AbilityManager::advance(double dt) {
    for (auto& a : abilities_) a->advance(dt);
}

} // namespace dota
