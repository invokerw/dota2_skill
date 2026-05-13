#include "dota/ability/registry.hpp"

#include "dota/ability/manager.hpp"
#include "dota/core/unit.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace dota {

namespace {

// --- YAML helpers ---------------------------------------------------------

std::vector<double> parse_number_list(const YAML::Node& n) {
    std::vector<double> out;
    if (!n) return out;
    if (n.IsScalar()) {
        out.push_back(n.as<double>());
        return out;
    }
    if (n.IsSequence()) {
        for (const auto& e : n) out.push_back(e.as<double>());
    }
    return out;
}

std::string join_flags(const YAML::Node& n) {
    if (!n) return {};
    if (n.IsScalar()) return n.as<std::string>();
    if (n.IsSequence()) {
        std::string joined;
        for (const auto& e : n) {
            if (!joined.empty()) joined.push_back(',');
            joined += e.as<std::string>();
        }
        return joined;
    }
    return {};
}

DamageType parse_damage_type(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(),
                   [](char c) { return static_cast<char>(std::toupper(c)); });
    if (u == "PHYSICAL" || u == "DAMAGE_TYPE_PHYSICAL") return DamageType::Physical;
    if (u == "MAGICAL"  || u == "DAMAGE_TYPE_MAGICAL")  return DamageType::Magical;
    if (u == "PURE"     || u == "DAMAGE_TYPE_PURE")     return DamageType::Pure;
    throw std::runtime_error("unknown damage type: " + s);
}

ActionTargetSpec parse_target_spec(const std::string& s) {
    std::string u = s;
    std::transform(u.begin(), u.end(), u.begin(),
                   [](char c) { return static_cast<char>(std::toupper(c)); });
    if (u == "CASTER") return ActionTargetSpec::Caster;
    if (u == "TARGET") return ActionTargetSpec::Target;
    throw std::runtime_error("unknown action target: " + s);
}

AbilitySpecial parse_ability_special(const YAML::Node& n) {
    AbilitySpecial out;
    if (!n || !n.IsMap()) return out;
    for (const auto& kv : n) {
        const auto key = kv.first.as<std::string>();
        AbilitySpecialValue v;
        const auto& val = kv.second;
        if (!val) continue;
        if (val.IsSequence()) {
            bool all_int = true;
            std::vector<double> floats;
            std::vector<long>   ints;
            for (const auto& item : val) {
                const auto s = item.as<std::string>();
                if (s.find('.') != std::string::npos) all_int = false;
                floats.push_back(std::stod(s));
                if (all_int) ints.push_back(std::stol(s));
            }
            v.is_int = all_int;
            v.floats = std::move(floats);
            if (all_int) v.ints = std::move(ints);
        } else {
            const auto s = val.as<std::string>();
            const bool has_dot = s.find('.') != std::string::npos;
            v.is_int = !has_dot;
            if (has_dot) v.floats.push_back(std::stod(s));
            else {
                v.ints.push_back(std::stol(s));
                v.floats.push_back(static_cast<double>(v.ints.front()));
            }
        }
        out.emplace(key, std::move(v));
    }
    return out;
}

SpellAction parse_action(const YAML::Node& n) {
    if (!n || !n.IsMap() || n.size() != 1) {
        throw std::runtime_error("action must be a single-key map");
    }
    const auto kv = *n.begin();
    const auto kind = kv.first.as<std::string>();
    const auto body = kv.second;

    auto target_spec = [&] { return parse_target_spec(body["target"].as<std::string>()); };

    if (kind == "damage") {
        ActionDamage a;
        a.target = target_spec();
        a.type   = parse_damage_type(body["type"].as<std::string>());
        a.amount = body["amount"].as<std::string>();
        return a;
    }
    if (kind == "heal") {
        ActionHeal a;
        a.target = target_spec();
        a.amount = body["amount"].as<std::string>();
        return a;
    }
    if (kind == "apply_modifier") {
        ActionApplyModifier a;
        a.target         = target_spec();
        a.modifier_name  = body["name"].as<std::string>();
        a.duration       = body["duration"] ? body["duration"].as<std::string>() : "";
        return a;
    }
    throw std::runtime_error("unknown action kind: " + kind);
}

AbilityDef parse_ability_node(const YAML::Node& n) {
    AbilityDef def;
    def.name        = n["name"].as<std::string>();
    def.base_class  = n["base_class"] ? n["base_class"].as<std::string>()
                                       : "ability_datadriven";
    def.behavior    = parse_behavior_flags(join_flags(n["behavior"]));
    if (n["target_team"]) {
        def.target_team = parse_target_team(n["target_team"].as<std::string>());
    }
    if (n["cast_point"])   def.cast_point   = n["cast_point"].as<double>();
    if (n["backswing"])    def.backswing    = n["backswing"].as<double>();
    if (n["channel_time"]) def.channel_time = n["channel_time"].IsScalar()
                                                 ? n["channel_time"].as<double>()
                                                 : parse_number_list(n["channel_time"]).front();
    if (n["cast_range"])   def.cast_range   = n["cast_range"].as<double>();

    def.cooldowns  = parse_number_list(n["cooldown"]);
    def.mana_costs = parse_number_list(n["mana_cost"]);
    def.ability_special = parse_ability_special(n["ability_special"]);

    if (def.base_class == "ability_lua") {
        def.script_path = n["script"] ? n["script"].as<std::string>() : "";
    } else {
        if (n["on_spell_start"] && n["on_spell_start"].IsSequence()) {
            for (const auto& entry : n["on_spell_start"]) {
                def.on_spell_start.push_back(parse_action(entry));
            }
        }
    }
    return def;
}

} // namespace

std::size_t AbilityRegistry::load_file(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("failed to parse YAML " + path + ": " + e.what());
    }

    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        return 0;
    }
    std::size_t count = 0;
    for (const auto& entry : root["abilities"]) {
        auto def = parse_ability_node(entry);
        defs_[def.name] = std::move(def);
        ++count;
    }
    return count;
}

void AbilityRegistry::register_def(AbilityDef def) {
    defs_[def.name] = std::move(def);
}

const AbilityDef* AbilityRegistry::find(const std::string& name) const {
    auto it = defs_.find(name);
    return it == defs_.end() ? nullptr : &it->second;
}

DataDrivenAbility* AbilityRegistry::instantiate(const std::string& name, Unit& caster) {
    const AbilityDef* def = find(name);
    if (!def) return nullptr;
    if (def->base_class != "ability_datadriven") return nullptr;
    auto ability = std::make_unique<DataDrivenAbility>(caster, *def);
    DataDrivenAbility* raw = ability.get();
    caster.abilities().attach(std::move(ability));
    return raw;
}

} // namespace dota
