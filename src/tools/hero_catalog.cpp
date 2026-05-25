#include "dota/tools/hero_catalog.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace dota::tools {

namespace {

// behavior csv 解析: 复用 src/ability/behavior_parse.cpp 中的 parse_behavior_flags.
} // namespace

namespace {

// 把单个 ability node (可能来自独立 yaml 顶层, 也可能来自老格式内嵌
// abilities[i]) 解析成 AbilityMeta.
AbilityMeta parse_ability_meta(const YAML::Node& a) {
    AbilityMeta m;
    if (a["name"])       m.name = a["name"].as<std::string>();
    if (a["cast_range"]) m.cast_range = a["cast_range"].as<double>();
    if (a["cast_point"]) m.cast_point = a["cast_point"].as<double>();

    if (a["behavior"]) {
        // behavior 在 yaml 里有可能是 list 也可能是 csv 字符串
        std::string csv;
        if (a["behavior"].IsSequence()) {
            for (auto it = a["behavior"].begin(); it != a["behavior"].end(); ++it) {
                if (!csv.empty()) csv += ",";
                csv += it->as<std::string>();
            }
        } else {
            csv = a["behavior"].as<std::string>();
        }
        m.behavior = parse_behavior_flags(csv);
    }
    if (a["target_team"]) {
        m.target_team = parse_target_team(a["target_team"].as<std::string>());
    }
    m.is_passive    = has_flag(m.behavior, BehaviorFlag::Passive);
    m.is_channelled = has_flag(m.behavior, BehaviorFlag::Channelled);
    return m;
}

} // namespace

std::size_t HeroCatalog::scan(const std::string& directory) {
    namespace fs = std::filesystem;
    heroes_.clear();
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        throw std::runtime_error("hero catalog: 目录不存在: " + directory);
    }

    // 推断 abilities/ 目录: data/heroes -> data/abilities.
    const fs::path abil_dir =
        fs::path(directory).parent_path() / "abilities";

    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(directory)) {
        if (e.is_regular_file() && e.path().extension() == ".yaml") {
            files.push_back(e.path());
        }
    }
    // 稳定顺序 (按文件名), 否则 UI 列表会随机.
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        YAML::Node root = YAML::LoadFile(path.string());
        if (!root || !root["hero"]) continue;  // 只接受顶层 hero 文件

        HeroEntry h;
        h.yaml_name = path.stem().string();
        h.yaml_path = path.string();

        const auto& hero = root["hero"];
        if (hero["name"])              h.display_name = hero["name"].as<std::string>();
        if (hero["base_health"])       h.base_health  = hero["base_health"].as<double>();
        if (hero["base_mana"])         h.base_mana    = hero["base_mana"].as<double>();
        if (hero["base_armor"])        h.base_armor   = hero["base_armor"].as<double>();
        if (hero["base_magic_resist"]) h.magic_resist = hero["base_magic_resist"].as<double>();
        if (hero["hull_radius"])       h.hull_radius  = hero["hull_radius"].as<double>();
        if (hero["attack_type"]) {
            const std::string at = hero["attack_type"].as<std::string>();
            h.ranged = (at == "ranged" || at == "Ranged" || at == "RANGED");
        }
        if (hero["attack_range"])      h.attack_range     = hero["attack_range"].as<double>();
        if (hero["projectile_speed"])  h.projectile_speed = hero["projectile_speed"].as<double>();

        if (root["abilities"] && root["abilities"].IsSequence()) {
            for (const auto& a : root["abilities"]) {
                if (a.IsScalar()) {
                    // 新格式: 引用名, 去 abilities/<name>.yaml 读详情.
                    const std::string ref = a.as<std::string>();
                    const fs::path ap = abil_dir / (ref + ".yaml");
                    if (!fs::exists(ap)) {
                        AbilityMeta stub;
                        stub.name = ref;
                        h.abilities.push_back(std::move(stub));
                        continue;
                    }
                    YAML::Node an = YAML::LoadFile(ap.string());
                    auto m = parse_ability_meta(an);
                    if (m.name.empty()) m.name = ref;
                    h.abilities.push_back(std::move(m));
                } else if (a.IsMap()) {
                    h.abilities.push_back(parse_ability_meta(a));
                }
            }
        }

        heroes_.push_back(std::move(h));
    }
    return heroes_.size();
}

const HeroEntry* HeroCatalog::find(const std::string& yaml_name) const {
    for (const auto& h : heroes_) {
        if (h.yaml_name == yaml_name) return &h;
    }
    return nullptr;
}

} // namespace dota::tools
