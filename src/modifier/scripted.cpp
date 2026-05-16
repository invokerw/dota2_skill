#include "dota/modifier/scripted.hpp"

#include "dota/core/unit.hpp"

#include <algorithm>

namespace dota {

namespace {

// 旧脚本（modifier_test_shield.lua）使用 lower_case_with_underscore 字段
// （states / properties / think_interval / on_*）。新 spec 使用 PascalCase
// （States / Properties / ThinkInterval / OnXxx）。这里把旧 spec 转换成新约定，
// 让 ScriptedModifier 的查询路径统一。
sol::table normalize_legacy_spec(sol::state_view lua, sol::table src) {
    if (!src.valid()) return src;

    auto promote = [&](const char* old_key, const char* new_key) {
        sol::object old_val = src[old_key];
        sol::object new_val = src[new_key];
        if (old_val.valid() && !new_val.valid()) {
            src[new_key] = old_val;
        }
    };
    promote("states", "States");
    promote("think_interval", "ThinkInterval");
    promote("on_created", "OnCreated");
    promote("on_destroyed", "OnDestroyed");
    promote("on_interval_think", "OnIntervalThink");
    promote("on_stack_changed", "OnStackChanged");
    // 注意：on_pre_take_damage / on_post_take_damage / on_pre_take_heal / on_post_take_heal
    //   旧签名是 (self, owner, amount, dtype) 返回 absorb 数值；新签名 (self, owner, ev_table)。
    //   两套签名不能直接互相 promote，dispatcher 会按需走兼容路径。
    promote("on_motion_tick", "OnMotionTick");

    // 把 properties = { {ARMOR, 5}, ... } 升级到 Properties。
    sol::object lower_props = src["properties"];
    sol::object upper_props = src["Properties"];
    if (lower_props.is<sol::table>() && !upper_props.valid()) {
        src["Properties"] = lower_props;
    }
    (void)lua;
    return src;
}

} // namespace

ScriptedModifier::ScriptedModifier(Unit& owner, std::string name, double duration,
                                   sol::table table, LuaState& lua)
    : Modifier(std::move(name), owner, duration), lua_(&lua) {
    if (!table.valid()) return;

    sol::state_view sv(lua_->state());
    sol::table normalized = normalize_legacy_spec(sv, std::move(table));

    // 临时编译为 CompiledSpec 以复用统一路径。spec 由 lua VM 持有，
    // 注册中心拷贝一份编译结果，存在自身字段里，避免悬挂。
    LuaModifierRegistry::CompiledSpec tmp;
    tmp.name  = this->name();
    tmp.table = normalized;
    // 复用 registry 的编译逻辑：把它注入注册中心（即使脚本未通过 register_modifier）
    auto& reg = lua_->modifier_registry();
    reg.register_modifier(this->name(), normalized);
    compiled_   = reg.find(this->name());
    spec_table_ = compiled_ ? compiled_->table : normalized;
    table_ = sv.create_table();
    apply_compiled_flags(compiled_ ? *compiled_ : tmp);
    if (compiled_ && compiled_->think_interval > 0.0) {
        set_think_interval(compiled_->think_interval);
    }
}

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
        // 动态：调用 spec_table[fn_name](self, owner)。失败/缺函数则跳过。
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
    // 优先新签名 OnPreTakeDamage(self, owner, ev_table)。
    sol::protected_function fn = spec_table_["OnPreTakeDamage"];
    if (fn.valid()) {
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
            sol::object new_amount = evt["amount"];
            if (new_amount.is<double>()) {
                const double a = new_amount.as<double>();
                if (a < ev.amount) ev.absorbed += (ev.amount - a);
                ev.amount = a;
            }
        }
        return;
    }
    // 旧签名 on_pre_take_damage(self, owner, amount, dtype) 返回 absorb 数值。
    sol::protected_function legacy = spec_table_["on_pre_take_damage"];
    if (!legacy.valid()) return;
    auto r = legacy(table_, &owner(), ev.amount, static_cast<int>(ev.type));
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

void ScriptedModifier::on_post_take_damage(PostTakeDamageEvent& ev) {
    sol::protected_function fn = spec_table_["OnPostTakeDamage"];
    if (!fn.valid()) {
        // 旧签名兜底
        fn = spec_table_["on_post_take_damage"];
        if (!fn.valid()) return;
        auto r = fn(table_, &owner(), ev.amount, static_cast<int>(ev.type));
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(name() + ".on_post_take_damage", err.what());
        }
        return;
    }
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
    if (!fn.valid()) {
        fn = spec_table_["on_pre_take_heal"];
        if (!fn.valid()) return;
        auto r = fn(table_, &owner(), ev.amount);
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(name() + ".on_pre_take_heal", err.what());
            return;
        }
        sol::object out = r;
        if (out.is<double>()) ev.amount = out.as<double>();
        return;
    }
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
    if (!fn.valid()) {
        fn = spec_table_["on_post_take_heal"];
        if (!fn.valid()) return;
        auto r = fn(table_, &owner(), ev.amount);
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(name() + ".on_post_take_heal", err.what());
        }
        return;
    }
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
