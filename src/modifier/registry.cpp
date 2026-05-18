#include "dota/modifier/registry.hpp"

namespace dota {

namespace {

// ModifierProperty -> 默认 GetModifier 函数名, 便于 Lua 端只填属性枚举即可.
// 对应 Dota2 LinkLuaModifier 的命名习惯.
const char* default_property_fn(ModifierProperty p) {
    switch (p) {
        case ModifierProperty::ArmorBonus:               return "GetModifierPhysicalArmor";
        case ModifierProperty::ArmorBonusPct:            return "GetModifierPhysicalArmorPct";
        case ModifierProperty::HealthBonus:              return "GetModifierHealthBonus";
        case ModifierProperty::ManaBonus:                return "GetModifierManaBonus";
        case ModifierProperty::AttackDamageBonus:        return "GetModifierAttackDamageBonus";
        case ModifierProperty::AttackDamageBonusPct:     return "GetModifierAttackDamageBonusPct";
        case ModifierProperty::AttackSpeedBonusConstant: return "GetModifierAttackSpeed";
        case ModifierProperty::MagicResistBonus:         return "GetModifierMagicResistance";
        case ModifierProperty::IncomingDamagePct:        return "GetModifierIncomingDamagePct";
        case ModifierProperty::OutgoingDamagePct:        return "GetModifierOutgoingDamagePct";
        case ModifierProperty::MoveSpeedBonusConstant:   return "GetModifierMoveSpeedBonus";
        case ModifierProperty::MoveSpeedBonusPct:        return "GetModifierMoveSpeedBonusPct";
        case ModifierProperty::HealAmpPct:               return "GetModifierHealAmpPct";
        case ModifierProperty::Evasion:                  return "GetModifierEvasion";
        case ModifierProperty::LifestealPct:             return "GetModifierLifesteal";
        case ModifierProperty::HealthRegen:              return "GetModifierHealthRegen";
        case ModifierProperty::ManaRegen:                return "GetModifierManaRegen";
        case ModifierProperty::SpellAmplifyPct:          return "GetModifierSpellAmplify";
        case ModifierProperty::StatusResistancePct:      return "GetModifierStatusResistance";
        case ModifierProperty::CooldownReductionPct:     return "GetModifierCooldownReduction";
        case ModifierProperty::CastRangeBonus:           return "GetModifierCastRange";
        default:                                          return nullptr;
    }
}

} // namespace

void LuaModifierRegistry::register_modifier(std::string name, sol::table spec) {
    if (name.empty() || !spec.valid()) return;
    CompiledSpec compiled;
    compile(compiled, name, std::move(spec));
    compiled_[std::move(name)] = std::move(compiled);
}

bool LuaModifierRegistry::contains(const std::string& name) const {
    return compiled_.find(name) != compiled_.end();
}

const LuaModifierRegistry::CompiledSpec*
LuaModifierRegistry::find(const std::string& name) const {
    auto it = compiled_.find(name);
    if (it == compiled_.end()) return nullptr;
    return &it->second;
}

void LuaModifierRegistry::compile(CompiledSpec& out, const std::string& name, sol::table spec) {
    out.name  = name;
    out.table = std::move(spec);

    auto bool_or = [&](const char* key, bool def) {
        sol::object o = out.table[key];
        if (o.is<bool>()) return o.as<bool>();
        if (o.is<int>())  return o.as<int>() != 0;
        return def;
    };
    auto int_or = [&](const char* key, int def) {
        sol::object o = out.table[key];
        if (o.is<int>())    return o.as<int>();
        if (o.is<double>()) return static_cast<int>(o.as<double>());
        return def;
    };
    auto double_or = [&](const char* key, double def) {
        sol::object o = out.table[key];
        if (o.is<double>()) return o.as<double>();
        if (o.is<int>())    return static_cast<double>(o.as<int>());
        return def;
    };

    out.is_purgable     = bool_or("IsPurgable", true);
    out.is_dispellable  = bool_or("IsDispellable", true);
    out.is_debuff       = bool_or("IsDebuff", false);
    out.is_motion_ctrl  = bool_or("IsMotionController", false);
    out.motion_priority = int_or("MotionPriority", 0);
    out.remove_on_death = bool_or("RemoveOnDeath", true);
    out.think_interval  = double_or("ThinkInterval", 0.0);

    // States: 可以是 ModifierState 数组(静态), 也可以同时定义 CheckState 回调(动态).
    sol::object states = out.table["States"];
    if (states.is<sol::table>()) {
        sol::table st = states;
        for (auto& kv : st) {
            if (kv.second.is<int>()) {
                out.static_state_mask |=
                    state_bit(static_cast<ModifierState>(kv.second.as<int>()));
            }
        }
    }
    sol::object cs = out.table["CheckState"];
    if (cs.is<sol::protected_function>()) out.has_check_state = true;

    // Properties: 列表, 每项为 { ModifierProperty.X, value | "FnName" }
    sol::object props = out.table["Properties"];
    if (props.is<sol::table>()) {
        sol::table list = props;
        for (auto& kv : list) {
            if (!kv.second.is<sol::table>()) continue;
            sol::table entry = kv.second;
            sol::object key = entry[1];
            sol::object val = entry[2];
            if (!key.is<int>()) continue;
            PropEntry pe;
            pe.prop = static_cast<ModifierProperty>(key.as<int>());

            if (val.is<double>()) {
                pe.constant = val.as<double>();
            } else if (val.is<int>()) {
                pe.constant = static_cast<double>(val.as<int>());
            } else if (val.is<std::string>()) {
                pe.dynamic = true;
                pe.fn_name = val.as<std::string>();
            } else if (!val.valid()) {
                // 未提供 value, 默认走 dynamic 并使用约定函数名
                if (const char* fn = default_property_fn(pe.prop)) {
                    pe.dynamic = true;
                    pe.fn_name = fn;
                } else {
                    continue;
                }
            } else {
                continue;
            }
            out.properties.push_back(std::move(pe));
        }
    }
}

} // namespace dota
