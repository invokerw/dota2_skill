#include "dota/ability/manager.hpp"

#include "dota/core/unit.hpp"
#include "dota/modifier/manager.hpp"

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
    // 先清掉这个 ability 在 instantiate 时挂上的 intrinsic modifier, 否则它内部
    // 持有的 ability_ 指针会随 ability 析构而悬空, 后续 OnRefresh / 属性聚合
    // 调用 self.ability:get_special() 会崩.
    const std::string& intrinsic = abilities_[index]->intrinsic_modifier_name();
    if (!intrinsic.empty()) {
        owner_.modifiers().remove(intrinsic);
    }
    abilities_.erase(abilities_.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void AbilityManager::advance(double dt) {
    for (auto& a : abilities_) a->advance(dt);
}

} // namespace dota
