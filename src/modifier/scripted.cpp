#include "dota/modifier/scripted.hpp"

#include "dota/core/unit.hpp"

#include <algorithm>

namespace dota {

ScriptedModifier::ScriptedModifier(Unit& owner, std::string name, double duration,
                                   const LuaModifierRegistry::CompiledSpec& spec,
                                   LuaState& lua, Unit* source, Ability* ability)
    : Modifier(std::move(name), owner, duration)
    , lua_(&lua)
    , spec_table_(spec.table)
    , compiled_(&spec)
    , source_(source)
    , ability_(ability) {
    sol::state_view sv(lua_->state());
    table_ = sv.create_table();
    apply_compiled_flags(spec);
    if (spec.think_interval > 0.0) set_think_interval(spec.think_interval);
}

void ScriptedModifier::apply_compiled_flags(const LuaModifierRegistry::CompiledSpec& spec) {
    state_mask_cache_ = spec.static_state_mask;
    is_debuff_        = spec.is_debuff;
    set_purgable(spec.is_purgable);
    set_dispellable(spec.is_dispellable);
    set_motion_controller(spec.is_motion_ctrl);
    set_motion_priority(spec.motion_priority);
}

std::vector<ModifierProvidedProperty> ScriptedModifier::declared_properties() const {
    std::vector<ModifierProvidedProperty> out;
    if (!compiled_) return out;
    out.reserve(compiled_->properties.size());
    for (const auto& pe : compiled_->properties) {
        if (!pe.dynamic) {
            out.push_back({pe.prop, pe.constant});
            continue;
        }
        sol::protected_function fn = spec_table_[pe.fn_name];
        if (!fn.valid()) continue;
        auto r = fn(table_, &owner());
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(name() + "." + pe.fn_name, err.what());
            continue;
        }
        sol::object val = r;
        if (val.is<double>())      out.push_back({pe.prop, val.as<double>()});
        else if (val.is<int>())    out.push_back({pe.prop, static_cast<double>(val.as<int>())});
    }
    return out;
}

std::uint32_t ScriptedModifier::declared_states() const {
    std::uint32_t mask = state_mask_cache_;
    if (compiled_ && compiled_->has_check_state) {
        sol::protected_function fn = spec_table_["CheckState"];
        if (fn.valid()) {
            auto r = fn(table_, &owner());
            if (r.valid()) {
                sol::object out = r;
                if (out.is<sol::table>()) {
                    sol::table list = out;
                    for (auto& kv : list) {
                        if (kv.second.is<int>()) {
                            mask |= state_bit(static_cast<ModifierState>(kv.second.as<int>()));
                        }
                    }
                }
            } else {
                sol::error err = r;
                lua_->report_error(name() + ".CheckState", err.what());
            }
        }
    }
    return mask;
}

namespace {

void call_simple(LuaState* lua, const std::string& mod_name, sol::table self,
                 sol::table spec, const char* fn_name, Unit* owner) {
    sol::protected_function fn = spec[fn_name];
    if (!fn.valid()) return;
    auto r = fn(self, owner);
    if (!r.valid()) {
        sol::error err = r;
        lua->report_error(mod_name + "." + fn_name, err.what());
    }
}

} // namespace

void ScriptedModifier::on_created() {
    call_simple(lua_, name(), table_, spec_table_, "OnCreated", &owner());
}

void ScriptedModifier::on_destroyed() {
    call_simple(lua_, name(), table_, spec_table_, "OnDestroyed", &owner());
}

void ScriptedModifier::on_stack_changed(int old_count, int new_count) {
    sol::protected_function fn = spec_table_["OnStackChanged"];
    if (!fn.valid()) return;
    auto r = fn(table_, &owner(), old_count, new_count);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnStackChanged", err.what());
    }
}

void ScriptedModifier::on_interval_think() {
    call_simple(lua_, name(), table_, spec_table_, "OnIntervalThink", &owner());
}

void ScriptedModifier::on_pre_take_damage(PreTakeDamageEvent& ev) {
    sol::protected_function fn = spec_table_["OnPreTakeDamage"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = sv.create_table();
    evt["amount"]      = ev.amount;
    evt["type"]        = static_cast<int>(ev.type);
    evt["flags"]       = ev.flags;
    evt["attacker_id"] = ev.attacker;
    evt["victim_id"]   = ev.victim;
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnPreTakeDamage", err.what());
        return;
    }
    sol::object out = r;
    if (out.is<double>()) {
        double absorb = std::min(out.as<double>(), ev.amount);
        if (absorb > 0.0) {
            ev.amount   -= absorb;
            ev.absorbed += absorb;
        }
    } else {
        // 钩子也可以直接修改 evt.amount 来下调伤害(无返回值即视为按修改后的 amount 处理).
        sol::object new_amount = evt["amount"];
        if (new_amount.is<double>()) {
            const double a = new_amount.as<double>();
            if (a < ev.amount) ev.absorbed += (ev.amount - a);
            ev.amount = a;
        }
    }
}

void ScriptedModifier::on_post_take_damage(PostTakeDamageEvent& ev) {
    sol::protected_function fn = spec_table_["OnPostTakeDamage"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = sv.create_table();
    evt["amount"]      = ev.amount;
    evt["type"]        = static_cast<int>(ev.type);
    evt["flags"]       = ev.flags;
    evt["attacker_id"] = ev.attacker;
    evt["victim_id"]   = ev.victim;
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnPostTakeDamage", err.what());
    }
}

void ScriptedModifier::on_pre_take_heal(PreTakeHealEvent& ev) {
    sol::protected_function fn = spec_table_["OnPreTakeHeal"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = sv.create_table();
    evt["amount"] = ev.amount;
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnPreTakeHeal", err.what());
        return;
    }
    sol::object new_amount = evt["amount"];
    if (new_amount.is<double>()) ev.amount = new_amount.as<double>();
}

void ScriptedModifier::on_post_take_heal(PostTakeHealEvent& ev) {
    sol::protected_function fn = spec_table_["OnPostTakeHeal"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = sv.create_table();
    evt["amount"] = ev.amount;
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnPostTakeHeal", err.what());
    }
}

void ScriptedModifier::on_motion_tick(double dt) {
    sol::protected_function fn = spec_table_["OnMotionTick"];
    if (!fn.valid()) return;
    auto r = fn(table_, &owner(), dt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnMotionTick", err.what());
    }
}

} // namespace dota
