#include "modifier_catalog.hpp"
#include "scene.hpp"
#include "ui_labels.hpp"

#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/script/lua_state.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <utility>

namespace dota::skill_tester {

ModifierParamSpec number_param(std::string key, std::string label,
                               double def, double min_v, double max_v,
                               float speed, const char* format) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Number;
    out.number_default = def;
    out.min = min_v;
    out.max = max_v;
    out.speed = speed;
    out.format = format;
    return out;
}

ModifierParamSpec int_param(std::string key, std::string label,
                            int def, int min_v, int max_v, float speed) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Int;
    out.int_default = def;
    out.int_min = min_v;
    out.int_max = max_v;
    out.speed = speed;
    return out;
}

ModifierParamSpec property_param(std::string key, std::string label,
                                 ModifierProperty def) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Property;
    out.int_default = static_cast<int>(def);
    return out;
}

ModifierParamSpec vec2_param(std::string key, std::string label,
                             Vec2 def, double min_v, double max_v,
                             float speed, const char* format) {
    ModifierParamSpec out;
    out.key = std::move(key);
    out.label = std::move(label);
    out.kind = ModifierParamKind::Vec2;
    out.vec_default = def;
    out.min = min_v;
    out.max = max_v;
    out.speed = speed;
    out.format = format;
    return out;
}

std::vector<ModifierParamSpec> common_modifier_params() {
    return {
        number_param("duration", "Duration", 3.0, -1.0, 600.0, 0.05f, "%.1f"),
        int_param("stacks", "Stacks", 1, 1, 999, 0.1f),
    };
}

std::vector<ModifierParamSpec>
with_common_params(std::initializer_list<ModifierParamSpec> extra) {
    auto out = common_modifier_params();
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

void reset_modifier_param_values(const ModifierAddSpec& spec, ModifierParamBag& values) {
    values.clear();
    for (const auto& param : spec.params) {
        ModifierParamValue value;
        value.number = param.number_default;
        value.integer = param.int_default;
        value.property_index = param.int_default;
        value.vec = param.vec_default;
        values.emplace(param.key, value);
    }
}

namespace {

double param_number(const ModifierParamBag& params, const std::string& key, double fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.number;
}

int param_int(const ModifierParamBag& params, const std::string& key, int fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.integer;
}

ModifierProperty param_property(const ModifierParamBag& params, const std::string& key,
                                ModifierProperty fallback) {
    const auto it = params.find(key);
    const int idx = it == params.end()
        ? static_cast<int>(fallback)
        : it->second.property_index;
    return static_cast<ModifierProperty>(
        std::clamp(idx, 0, static_cast<int>(ModifierProperty::Count_) - 1));
}

Vec2 param_vec2(const ModifierParamBag& params, const std::string& key, Vec2 fallback) {
    const auto it = params.find(key);
    return it == params.end() ? fallback : it->second.vec;
}

std::unique_ptr<Modifier>
finish_modifier(std::unique_ptr<Modifier> mod, const ModifierParamBag& params) {
    if (!mod) return nullptr;
    const int stacks = std::max(1, param_int(params, "stacks", 1));
    if (stacks > 1) mod->set_stack_count(stacks);
    return mod;
}

std::vector<ModifierAddSpec> build_builtin_modifier_specs() {
    std::vector<ModifierAddSpec> out;
    auto push = [&](std::string name,
                    std::vector<ModifierParamSpec> params,
                    ModifierAddFactory create) {
        out.push_back({name, "c++ / " + name, std::move(params), std::move(create)});
    };

    push("modifier_stunned", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_stunned(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_silenced", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_silenced(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_rooted", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_rooted(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_hexed", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_hexed(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_invisible", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_invisible(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_magic_immune", common_modifier_params(),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_magic_immune(unit, param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_debug_stats",
         with_common_params({
             property_param("property", "Property", ModifierProperty::AttackDamageBonus),
             number_param("value", "Value", 25.0, -10000.0, 10000.0, 0.1f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             const auto prop = param_property(
                 params, "property", ModifierProperty::AttackDamageBonus);
             return finish_modifier(
                 std::make_unique<modifiers::GenericStats>(
                     unit, "modifier_debug_stats",
                     param_number(params, "duration", 3.0),
                     std::initializer_list<ModifierProvidedProperty>{
                         {prop, param_number(params, "value", 25.0)}}),
                 params);
         });
    push("modifier_shield_absorb",
         with_common_params({
             number_param("capacity", "Capacity", 300.0, 1.0, 100000.0, 5.0f, "%.0f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 std::make_unique<modifiers::ShieldAbsorb>(
                     unit, param_number(params, "capacity", 300.0),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_periodic_heal",
         with_common_params({
             number_param("heal_per_tick", "Heal/Tick", 50.0,
                          -10000.0, 10000.0, 1.0f, "%.0f"),
             number_param("interval", "Interval", 1.0, 0.01, 60.0, 0.05f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_periodic_heal(
                     unit, param_number(params, "heal_per_tick", 50.0),
                     std::max(0.01, param_number(params, "interval", 1.0)),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_reflect_damage",
         with_common_params({
             number_param("fraction", "Reflect", 0.5, 0.0, 10.0, 0.01f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_blade_mail(
                     unit, param_number(params, "fraction", 0.5),
                     param_number(params, "duration", 3.0)),
                 params);
         });
    push("modifier_motion_knockback",
         with_common_params({
             vec2_param("direction", "Direction", {1.0, 0.0}, -1.0, 1.0, 0.05f, "%.2f"),
             number_param("distance", "Distance", 300.0, 0.0, 5000.0, 5.0f, "%.0f"),
             int_param("priority", "Priority", 1, -100, 100, 0.1f),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_knockback(
                     unit, param_vec2(params, "direction", {1.0, 0.0}),
                     param_number(params, "distance", 300.0),
                     param_number(params, "duration", 3.0),
                     param_int(params, "priority", 1)),
                 params);
         });
    push("modifier_break_healing",
         with_common_params({
             number_param("fraction", "Heal Break", 0.4, 0.0, 1.0, 0.01f, "%.2f"),
         }),
         [](Unit& unit, const ModifierParamBag& params) {
             return finish_modifier(
                 modifiers::make_break_healing(
                     unit, param_number(params, "fraction", 0.4),
                     param_number(params, "duration", 3.0)),
                 params);
         });

    return out;
}

} // namespace

void draw_modifier_param_controls(const ModifierAddSpec& spec, ModifierParamBag& values) {
    for (const auto& param : spec.params) {
        ModifierParamValue& value = values[param.key];
        switch (param.kind) {
            case ModifierParamKind::Number:
                drag_double(param.label.c_str(), value.number,
                            param.speed, param.min, param.max, param.format);
                break;
            case ModifierParamKind::Int:
                ImGui::DragInt(param.label.c_str(), &value.integer,
                               param.speed, param.int_min, param.int_max);
                value.integer = std::clamp(value.integer, param.int_min, param.int_max);
                break;
            case ModifierParamKind::Property: {
                value.property_index = std::clamp(
                    value.property_index, 0,
                    static_cast<int>(ModifierProperty::Count_) - 1);
                const auto current = static_cast<ModifierProperty>(value.property_index);
                if (ImGui::BeginCombo(param.label.c_str(), property_label(current))) {
                    for (int i = 0; i < static_cast<int>(ModifierProperty::Count_); ++i) {
                        const bool selected_prop = value.property_index == i;
                        const auto prop = static_cast<ModifierProperty>(i);
                        if (ImGui::Selectable(property_label(prop), selected_prop)) {
                            value.property_index = i;
                        }
                        if (selected_prop) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case ModifierParamKind::Vec2:
                ImGui::PushID(param.key.c_str());
                ImGui::TextUnformatted(param.label.c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 4.0f);
                drag_double("X", value.vec.x, param.speed, param.min, param.max, param.format);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                drag_double("Y", value.vec.y, param.speed, param.min, param.max, param.format);
                ImGui::PopID();
                break;
        }
    }
}

std::vector<ModifierAddSpec> build_modifier_catalog(Scene& scene) {
    std::vector<ModifierAddSpec> out = build_builtin_modifier_specs();
    LuaState* lua = scene.lua_state();
    if (!lua) return out;

    for (const std::string& name : lua->modifier_registry().names()) {
        out.push_back({
            name,
            "lua / " + name,
            common_modifier_params(),
            [lua, name](Unit& unit, const ModifierParamBag& params) {
                const auto* spec = lua->modifier_registry().find(name);
                if (!spec) return std::unique_ptr<Modifier>{};
                return finish_modifier(
                    std::make_unique<ScriptedModifier>(
                        unit, name, param_number(params, "duration", 3.0),
                        *spec, *lua),
                    params);
            },
        });
    }
    return out;
}

} // namespace dota::skill_tester
