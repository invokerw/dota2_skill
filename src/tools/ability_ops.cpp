#include "dota/tools/ability_ops.hpp"

#include <yaml-cpp/yaml.h>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

void ensure_abilities_seq(YAML::Node& root) {
    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        root["abilities"] = YAML::Node(YAML::NodeType::Sequence);
    }
}

bool name_exists(const YAML::Node& root, const std::string& name) {
    if (!root["abilities"] || !root["abilities"].IsSequence()) return false;
    for (const auto& a : root["abilities"]) {
        if (a["name"] && a["name"].as<std::string>() == name) return true;
    }
    return false;
}

void write_text_if_absent(const fs::path& p, const std::string& body) {
    if (fs::exists(p)) {
        throw std::runtime_error("ability_ops: 脚本已存在 " + p.string());
    }
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        throw std::runtime_error("ability_ops: 写入失败 " + p.string());
    }
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

} // namespace

YAML::Node ability_node_at(YAML::Node root, std::size_t index) {
    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        throw std::runtime_error("ability_ops: 没有 abilities 列表");
    }
    if (index >= root["abilities"].size()) {
        throw std::runtime_error("ability_ops: 索引越界");
    }
    return root["abilities"][index];
}

int find_ability_index(const YAML::Node& root, std::string_view name) {
    if (!root["abilities"] || !root["abilities"].IsSequence()) return -1;
    for (std::size_t i = 0; i < root["abilities"].size(); ++i) {
        const auto& a = root["abilities"][i];
        if (a["name"] && a["name"].as<std::string>() == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::size_t add_datadriven_ability(YAML::Node& root, const std::string& name) {
    if (name.empty()) throw std::runtime_error("ability_ops: 名字为空");
    if (name_exists(root, name)) {
        throw std::runtime_error("ability_ops: 名字已存在 " + name);
    }
    ensure_abilities_seq(root);
    YAML::Node a;
    a["name"]            = name;
    a["base_class"]      = "ability_datadriven";
    a["behavior"]        = YAML::Load("[NO_TARGET]");
    a["target_team"]     = "NONE";
    a["cast_point"]      = 0.0;
    a["cooldown"]        = YAML::Load("[10]");
    a["mana_cost"]       = YAML::Load("[0]");
    a["ability_special"] = YAML::Node(YAML::NodeType::Map);
    a["on_spell_start"]  = YAML::Node(YAML::NodeType::Sequence);
    root["abilities"].push_back(a);
    return root["abilities"].size() - 1;
}

std::size_t add_lua_ability(YAML::Node& root,
                            const fs::path& data_root,
                            const std::string& ability_name,
                            const std::string& script_filename) {
    if (ability_name.empty()) throw std::runtime_error("ability_ops: 名字为空");
    if (script_filename.empty()) {
        throw std::runtime_error("ability_ops: 脚本文件名为空");
    }
    if (name_exists(root, ability_name)) {
        throw std::runtime_error("ability_ops: 名字已存在 " + ability_name);
    }

    // 先尝试写脚本; 失败则不动 yaml.
    write_lua_ability_template(data_root, script_filename);

    ensure_abilities_seq(root);
    YAML::Node a;
    a["name"]            = ability_name;
    a["base_class"]      = "ability_lua";
    a["script"]          = "abilities/" + script_filename;
    a["behavior"]        = YAML::Load("[NO_TARGET]");
    a["target_team"]     = "NONE";
    a["cast_point"]      = 0.0;
    a["cooldown"]        = YAML::Load("[10]");
    a["mana_cost"]       = YAML::Load("[0]");
    a["ability_special"] = YAML::Node(YAML::NodeType::Map);
    root["abilities"].push_back(a);
    return root["abilities"].size() - 1;
}

std::vector<TrashRecord> remove_ability_at(YAML::Node& root,
                                            const fs::path& data_root,
                                            std::size_t index) {
    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        throw std::runtime_error("ability_ops: 没有 abilities 列表");
    }
    if (index >= root["abilities"].size()) {
        throw std::runtime_error("ability_ops: 索引越界");
    }

    std::vector<TrashRecord> out;
    const YAML::Node& a = root["abilities"][index];
    if (a["base_class"] && a["base_class"].as<std::string>() == "ability_lua" &&
        a["script"]) {
        const std::string rel = a["script"].as<std::string>();
        const fs::path script_path = data_root / "scripts" / rel;
        if (fs::exists(script_path)) {
            out.push_back(move_to_trash(data_root, script_path));
        }
    }

    // yaml-cpp Node::remove(idx) 实现是 O(n), 但够用.
    root["abilities"].remove(index);
    return out;
}

fs::path write_lua_ability_template(const fs::path& data_root,
                                     const std::string& filename) {
    const fs::path target = data_root / "scripts" / "abilities" / filename;
    static const char* kTemplate =
        "-- ability_lua 模板. 入参 self 是 ScriptedAbility 注入的轻量表,\n"
        "-- 含 :get_special / :target_point / :target_unit / :get_caster / :level.\n"
        "\n"
        "local M = {}\n"
        "\n"
        "function M:on_spell_start(caster, target, world)\n"
        "    -- TODO: 在这里写技能逻辑.\n"
        "end\n"
        "\n"
        "return M\n";
    write_text_if_absent(target, kTemplate);
    return target;
}

void set_ability_special(YAML::Node& ability_node,
                          const std::vector<AbilitySpecialEntry>& entries) {
    YAML::Node spec(YAML::NodeType::Map);
    for (const auto& e : entries) {
        YAML::Node arr(YAML::NodeType::Sequence);
        for (double v : e.values) {
            if (e.is_int) {
                arr.push_back(static_cast<long>(std::llround(v)));
            } else {
                arr.push_back(v);
            }
        }
        spec[e.key] = arr;
    }
    ability_node["ability_special"] = spec;
}

} // namespace dota::tools
