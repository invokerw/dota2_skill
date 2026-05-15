#include "dota/modifier/manager.hpp"

#include <algorithm>

namespace dota {

ModifierManager::ModifierManager(Unit& owner) : owner_(owner) {}

Modifier* ModifierManager::attach(std::unique_ptr<Modifier> mod) {
    Modifier* raw = mod.get();
    modifiers_.push_back(std::move(mod));
    raw->on_created();
    return raw;
}

bool ModifierManager::remove(const std::string& name) {
    auto it = std::find_if(modifiers_.begin(), modifiers_.end(),
                           [&](const auto& m) { return m->name() == name; });
    if (it == modifiers_.end()) return false;
    (*it)->on_destroyed();
    modifiers_.erase(it);
    return true;
}

void ModifierManager::remove_all() {
    for (auto& m : modifiers_) m->on_destroyed();
    modifiers_.clear();
}

Modifier* ModifierManager::find(const std::string& name) {
    auto it = std::find_if(modifiers_.begin(), modifiers_.end(),
                           [&](const auto& m) { return m->name() == name; });
    return it == modifiers_.end() ? nullptr : it->get();
}

const Modifier* ModifierManager::find(const std::string& name) const {
    auto it = std::find_if(modifiers_.begin(), modifiers_.end(),
                           [&](const auto& m) { return m->name() == name; });
    return it == modifiers_.end() ? nullptr : it->get();
}

void ModifierManager::advance(double dt) {
    if (modifiers_.empty()) return;
    // 对快照进行 tick，使 think 回调可以安全地添加/移除修饰器
    std::vector<Modifier*> snapshot;
    snapshot.reserve(modifiers_.size());
    for (auto& m : modifiers_) snapshot.push_back(m.get());
    for (Modifier* m : snapshot) m->advance(dt);

    // 清除过期的修饰器。在移除前调用 on_destroyed()，确保指针仍然有效；然后就地擦除
    for (auto& m : modifiers_) {
        if (m && m->expired()) m->on_destroyed();
    }
    modifiers_.erase(
        std::remove_if(modifiers_.begin(), modifiers_.end(),
                       [](const std::unique_ptr<Modifier>& m) {
                           return m && m->expired();
                       }),
        modifiers_.end());
}

std::uint32_t ModifierManager::aggregated_states() const {
    std::uint32_t mask = 0;
    for (auto& m : modifiers_) mask |= m->declared_states();
    return mask;
}

double ModifierManager::aggregated(ModifierProperty p) const {
    double sum = 0.0;
    for (auto& m : modifiers_) {
        for (const auto& entry : m->declared_properties()) {
            if (entry.property == p) sum += entry.value * m->stack_count();
        }
    }
    return sum;
}

double ModifierManager::apply_stat(ModifierProperty constant,
                                   ModifierProperty pct,
                                   double base) const {
    const double add = aggregated(constant);
    const double mul = 1.0 + aggregated(pct);
    return (base + add) * mul;
}

void ModifierManager::dispatch_pre_take_damage(PreTakeDamageEvent& ev) {
    for (auto& m : modifiers_) m->on_pre_take_damage(ev);
}

void ModifierManager::dispatch_post_take_damage(PostTakeDamageEvent& ev) {
    for (auto& m : modifiers_) m->on_post_take_damage(ev);
}

void ModifierManager::dispatch_pre_take_heal(PreTakeHealEvent& ev) {
    for (auto& m : modifiers_) m->on_pre_take_heal(ev);
}

void ModifierManager::dispatch_post_take_heal(PostTakeHealEvent& ev) {
    for (auto& m : modifiers_) m->on_post_take_heal(ev);
}

} // namespace dota
