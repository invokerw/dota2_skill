#include "dota/modifier/manager.hpp"

#include "dota/ability/ability.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <algorithm>
#include <limits>

namespace dota {

namespace {

void publish_modifier_added(Unit& owner, Modifier& m) {
    World* w = owner.world();
    if (!w) return;
    ModifierAddedEvent ev{owner.id(), m.name(),
                          m.permanent() ? -1.0 : m.duration_remaining(),
                          m.stack_count()};
    w->events().publish(ev);
}

void publish_modifier_removed(Unit& owner, const std::string& name) {
    World* w = owner.world();
    if (!w) return;
    ModifierRemovedEvent ev{owner.id(), name};
    w->events().publish(ev);
}

} // namespace

ModifierManager::ModifierManager(Unit& owner) : owner_(owner) {}

Modifier* ModifierManager::attach(std::unique_ptr<Modifier> mod) {
    if (mod && mod->is_motion_controller()) {
        return attach_motion(std::move(mod));
    }
    Modifier* raw = mod.get();
    modifiers_.push_back(std::move(mod));
    raw->on_created();
    publish_modifier_added(owner_, *raw);
    return raw;
}

Modifier* ModifierManager::attach_motion(std::unique_ptr<Modifier> mod) {
    if (!mod || !mod->is_motion_controller()) return attach(std::move(mod));
    // 检查现有 MC: 按 priority 抢占. 同优先级"后来者赢".
    int existing_max = std::numeric_limits<int>::min();
    for (auto& m : modifiers_) {
        if (m && m->is_motion_controller()) {
            existing_max = std::max(existing_max, m->motion_priority());
        }
    }
    if (existing_max != std::numeric_limits<int>::min() &&
        mod->motion_priority() < existing_max) {
        return nullptr;
    }
    // 移除现有 MC(任何 priority 不高于新 MC 的, 被替换).
    for (auto& m : modifiers_) {
        if (m && m->is_motion_controller()) {
            const std::string removed_name = m->name();
            m->on_destroyed();
            m.reset();
            publish_modifier_removed(owner_, removed_name);
        }
    }
    modifiers_.erase(
        std::remove_if(modifiers_.begin(), modifiers_.end(),
                       [](const std::unique_ptr<Modifier>& m){ return !m; }),
        modifiers_.end());

    Modifier* raw = mod.get();
    modifiers_.push_back(std::move(mod));
    raw->on_created();
    publish_modifier_added(owner_, *raw);
    return raw;
}

void ModifierManager::advance_motion(double dt) {
    if (modifiers_.empty()) return;
    std::vector<Modifier*> snapshot;
    snapshot.reserve(modifiers_.size());
    for (auto& m : modifiers_) {
        if (m && m->is_motion_controller()) snapshot.push_back(m.get());
    }
    for (Modifier* m : snapshot) m->on_motion_tick(dt);
}

bool ModifierManager::remove(const std::string& name) {
    auto it = std::find_if(modifiers_.begin(), modifiers_.end(),
                           [&](const auto& m) { return m->name() == name; });
    if (it == modifiers_.end()) return false;
    (*it)->on_destroyed();
    modifiers_.erase(it);
    publish_modifier_removed(owner_, name);
    return true;
}

bool ModifierManager::remove_at(std::size_t index) {
    if (index >= modifiers_.size()) return false;
    const std::string name = modifiers_[index]->name();
    modifiers_[index]->on_destroyed();
    modifiers_.erase(modifiers_.begin() + static_cast<std::ptrdiff_t>(index));
    publish_modifier_removed(owner_, name);
    return true;
}

void ModifierManager::remove_all() {
    std::vector<std::string> removed;
    removed.reserve(modifiers_.size());
    for (auto& m : modifiers_) {
        removed.push_back(m->name());
        m->on_destroyed();
    }
    modifiers_.clear();
    for (const auto& n : removed) publish_modifier_removed(owner_, n);
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
    // 对快照进行 tick, 使 think 回调可以安全地添加/移除修饰器
    std::vector<Modifier*> snapshot;
    snapshot.reserve(modifiers_.size());
    for (auto& m : modifiers_) snapshot.push_back(m.get());
    for (Modifier* m : snapshot) m->advance(dt);

    // 收集 expired 的名字, 调用 on_destroyed, 擦除, 然后发事件
    std::vector<std::string> expired_names;
    for (auto& m : modifiers_) {
        if (m && m->expired()) {
            expired_names.push_back(m->name());
            m->on_destroyed();
        }
    }
    modifiers_.erase(
        std::remove_if(modifiers_.begin(), modifiers_.end(),
                       [](const std::unique_ptr<Modifier>& m) {
                           return m && m->expired();
                       }),
        modifiers_.end());
    for (const auto& n : expired_names) publish_modifier_removed(owner_, n);
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

// 注: dispatch_* 在 Lua 钩子可能 mutate modifiers_ 的前提下, 先快照指针再迭代.
namespace {
template <typename Fn>
void for_each_snapshot(const std::vector<std::unique_ptr<Modifier>>& mods, Fn&& fn) {
    std::vector<Modifier*> snapshot;
    snapshot.reserve(mods.size());
    for (const auto& m : mods) {
        if (m) snapshot.push_back(m.get());
    }
    for (Modifier* m : snapshot) fn(*m);
}
} // namespace

void ModifierManager::dispatch_pre_take_damage(PreTakeDamageEvent& ev) {
    for_each_snapshot(modifiers_, [&](Modifier& m) { m.on_pre_take_damage(ev); });
}

void ModifierManager::dispatch_post_take_damage(PostTakeDamageEvent& ev) {
    for_each_snapshot(modifiers_, [&](Modifier& m) { m.on_post_take_damage(ev); });
}

void ModifierManager::dispatch_pre_take_heal(PreTakeHealEvent& ev) {
    for_each_snapshot(modifiers_, [&](Modifier& m) { m.on_pre_take_heal(ev); });
}

void ModifierManager::dispatch_post_take_heal(PostTakeHealEvent& ev) {
    for_each_snapshot(modifiers_, [&](Modifier& m) { m.on_post_take_heal(ev); });
}

void ModifierManager::dispatch_ability_executed(const AbilityExecutedInfo& info) {
    for_each_snapshot(modifiers_, [&](Modifier& m) { m.on_ability_executed(info); });
}

} // namespace dota
