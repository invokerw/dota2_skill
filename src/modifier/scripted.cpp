#include "dota/modifier/scripted.hpp"

#include "dota/core/unit.hpp"

namespace dota {

namespace {

std::uint32_t compute_state_mask(const sol::table& t) {
    std::uint32_t mask = 0;
    sol::object states = t["states"];
    if (!states.valid() || !states.is<sol::table>()) return 0;
    sol::table st = states;
    for (auto& kv : st) {
        if (kv.second.is<int>()) {
            mask |= state_bit(static_cast<ModifierState>(kv.second.as<int>()));
        }
    }
    return mask;
}

} // namespace

ScriptedModifier::ScriptedModifier(Unit& owner, std::string name, double duration,
                                   sol::table table, LuaState& lua)
    : Modifier(std::move(name), owner, duration), lua_(&lua), table_(std::move(table)) {
    if (table_.valid()) {
        state_mask_cache_ = compute_state_mask(table_);
        sol::object ti = table_["think_interval"];
        if (ti.is<double>()) set_think_interval(ti.as<double>());
    }
}

std::vector<ModifierProvidedProperty> ScriptedModifier::declared_properties() const {
    if (!table_.valid()) return {};
    sol::object props = table_["properties"];
    if (!props.is<sol::table>()) return {};
    std::vector<ModifierProvidedProperty> out;
    sol::table list = props;
    for (auto& kv : list) {
        if (!kv.second.is<sol::table>()) continue;
        sol::table entry = kv.second;
        sol::object key = entry[1];
        sol::object val = entry[2];
        if (!key.is<int>()) continue;
        double v = 0.0;
        if (val.is<double>())         v = val.as<double>();
        else if (val.is<int>())       v = static_cast<double>(val.as<int>());
        out.push_back({static_cast<ModifierProperty>(key.as<int>()), v});
    }
    return out;
}

std::uint32_t ScriptedModifier::declared_states() const {
    return state_mask_cache_;
}

void ScriptedModifier::on_created() {
    if (!table_.valid()) return;
    sol::protected_function fn = table_["on_created"];
    if (!fn.valid()) return;
    auto r = fn(table_, &owner());
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".on_created", err.what());
    }
}

void ScriptedModifier::on_destroyed() {
    if (!table_.valid()) return;
    sol::protected_function fn = table_["on_destroyed"];
    if (!fn.valid()) return;
    auto r = fn(table_, &owner());
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".on_destroyed", err.what());
    }
}

void ScriptedModifier::on_interval_think() {
    if (!table_.valid()) return;
    sol::protected_function fn = table_["on_interval_think"];
    if (!fn.valid()) return;
    auto r = fn(table_, &owner());
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".on_interval_think", err.what());
    }
}

void ScriptedModifier::on_pre_take_damage(PreTakeDamageEvent& ev) {
    if (!table_.valid()) return;
    sol::protected_function fn = table_["on_pre_take_damage"];
    if (!fn.valid()) return;
    // 脚本接收 (self, owner, amount, type) 并返回吸收的伤害量
    auto r = fn(table_, &owner(), ev.amount, static_cast<int>(ev.type));
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".on_pre_take_damage", err.what());
        return;
    }
    sol::object out = r;
    if (out.is<double>()) {
        double absorb = std::min(out.as<double>(), ev.amount);
        if (absorb > 0.0) {
            ev.amount   -= absorb;
            ev.absorbed += absorb;
        }
    }
}

} // namespace dota
