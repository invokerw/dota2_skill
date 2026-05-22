#include "dota/modifier/scripted.hpp"

#include "dota/ability/ability.hpp"
#include "dota/core/attack.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <algorithm>

namespace dota {

ScriptedModifier::ScriptedModifier(Unit& owner, std::string name, double duration,
                                   const LuaModifierRegistry::CompiledSpec& spec,
                                   LuaState& lua, EntityId source_id, Ability* ability)
    : Modifier(std::move(name), owner, duration)
    , lua_(&lua)
    , spec_table_(spec.table)
    , compiled_(&spec)
    , source_id_(source_id)
    , ability_(ability) {
    sol::state_view sv(lua_->state());
    table_ = sv.create_table();
    // self.handle: 让 Lua 钩子能调用 stack_count / refresh / duration_remaining
    // 等基类方法 (sol2 Modifier usertype). self.ability: 持有 intrinsic ability 句柄,
    // 方便取 ability_special.
    table_["handle"]  = static_cast<Modifier*>(this);
    table_["ability"] = ability_;
    apply_compiled_flags(spec);
    if (spec.think_interval > 0.0) set_think_interval(spec.think_interval);
}

Unit* ScriptedModifier::source() const {
    if (source_id_ == kInvalidEntityId) return nullptr;
    World* w = owner().world();
    return w ? w->find(source_id_) : nullptr;
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
    // 与 C++ 的 MotionKnockback 一致: 拖拽 / 击退 modifier 销毁时, 通知分离
    // pass 把单位推出 caster (本身 NoUnitCollision 状态刚解除).
    if (is_motion_controller()) owner().force_moved_for_collision();
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

void ScriptedModifier::on_refresh() {
    call_simple(lua_, name(), table_, spec_table_, "OnRefresh", &owner());
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

namespace {

// 把 AttackRecord 关键字段同步到 Lua 表. on_attack 钩子可写 bonus_damage /
// damage_type, 这两项写回 record. id / attacker / target / base_damage 是只读语义,
// 即便 lua 改了也忽略.
sol::table record_to_table(sol::state_view sv, const AttackRecord& r) {
    sol::table t = sv.create_table();
    t["id"]           = static_cast<int>(r.id);
    t["attacker_id"]  = static_cast<int>(r.attacker);
    t["target_id"]    = static_cast<int>(r.target);
    t["base_damage"]  = r.base_damage;
    t["bonus_damage"] = r.bonus_damage;
    t["damage_type"]  = static_cast<int>(r.damage_type);
    t["missed"]       = r.missed;
    return t;
}

void apply_writable_fields(const sol::table& t, AttackRecord& r) {
    sol::object bonus = t["bonus_damage"];
    if (bonus.is<double>())      r.bonus_damage = bonus.as<double>();
    else if (bonus.is<int>())    r.bonus_damage = static_cast<double>(bonus.as<int>());
    sol::object dtype = t["damage_type"];
    if (dtype.is<int>()) {
        const int v = dtype.as<int>();
        if (v >= 0 && v <= 2) r.damage_type = static_cast<DamageType>(v);
    }
}

} // namespace

void ScriptedModifier::on_attack(AttackRecord& record) {
    sol::protected_function fn = spec_table_["OnAttack"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = record_to_table(sv, record);
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnAttack", err.what());
        return;
    }
    apply_writable_fields(evt, record);
    // claim 字段: lua 端写 evt.claim = true 即把 self 加到 orb_listeners.
    sol::object claim = evt["claim"];
    if (claim.is<bool>() && claim.as<bool>()) {
        record.orb_listeners.push_back(this);
    }
}

void ScriptedModifier::on_attack_landed(const AttackRecord& record) {
    sol::protected_function fn = spec_table_["OnAttackLanded"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = record_to_table(sv, record);
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnAttackLanded", err.what());
    }
}

void ScriptedModifier::on_attack_fail(const AttackRecord& record) {
    sol::protected_function fn = spec_table_["OnAttackFail"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = record_to_table(sv, record);
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnAttackFail", err.what());
    }
}

void ScriptedModifier::on_attack_record_destroy(const AttackRecord& record) {
    sol::protected_function fn = spec_table_["OnAttackRecordDestroy"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = record_to_table(sv, record);
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnAttackRecordDestroy", err.what());
    }
}

std::string ScriptedModifier::projectile_name() const {
    sol::protected_function fn = spec_table_["GetAttackProjectileName"];
    if (!fn.valid()) return {};
    auto r = fn(table_, &owner());
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".GetAttackProjectileName", err.what());
        return {};
    }
    sol::object out = r;
    if (out.is<std::string>()) return out.as<std::string>();
    return {};
}

void ScriptedModifier::on_ability_executed(const AbilityExecutedInfo& info) {
    sol::protected_function fn = spec_table_["OnAbilityExecuted"];
    if (!fn.valid()) return;
    sol::state_view sv(lua_->state());
    sol::table evt = sv.create_table();
    evt["unit"]         = info.caster;
    evt["ability"]      = info.ability;
    evt["ability_name"] = info.ability_name;
    evt["is_passive"]   = info.is_passive;
    auto r = fn(table_, &owner(), evt);
    if (!r.valid()) {
        sol::error err = r;
        lua_->report_error(name() + ".OnAbilityExecuted", err.what());
    }
}

} // namespace dota
