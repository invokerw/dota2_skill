#include "dota/tools/ability_doc.hpp"

#include "dota/tools/hero_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace dota::tools {

namespace fs = std::filesystem;

// hero_writer.cpp 内部的 emit_default / emit_ability 是 internal-linkage. 我们这里
// 不复用代码, 直接照同样的 key 顺序写一份针对单 ability 顶层的 emitter. 关键约束:
// 1. ability 顶层 key 顺序与 hero_writer 中的 ability_order 对齐
// 2. behavior / cooldown / mana_cost / channel_time 列表用 flow 风格
// 3. ability_special 内每个 value 序列也用 flow 风格
// 4. on_spell_start 每个 action body 按 action_body_order 排序
// 直接复用 hero_writer 暴露出的 emit_hero_yaml: 给它构造一个虚拟 root
// { hero: {}, abilities: [<本 ability>] } 之后再剥掉外壳? 这样会引入临时
// 拷贝, 还得切字符串, 太脏. 自己写一份 ordered emitter, 与 hero_writer 中的
// internal helpers 保持同步即可. 由于 ability yaml 字段集稳定, 这里写完后改
// 动较少.

namespace {

const std::vector<std::string>& ability_order() {
    static const std::vector<std::string> v = {
        "name", "base_class", "script",
        "behavior", "target_team",
        "cast_point", "backswing", "channel_time", "cast_range",
        "cooldown", "mana_cost",
        "ability_special",
        "on_spell_start",
    };
    return v;
}
const std::vector<std::string>& action_body_order() {
    static const std::vector<std::string> v = {
        "target", "type", "name", "amount", "duration",
    };
    return v;
}

std::vector<std::string> ordered_keys(const YAML::Node& map,
                                       const std::vector<std::string>& priority) {
    std::vector<std::string> out;
    std::vector<std::string> rest;
    for (const auto& k : priority) {
        if (map[k]) out.push_back(k);
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        const std::string k = it->first.as<std::string>();
        if (std::find(priority.begin(), priority.end(), k) == priority.end()) {
            rest.push_back(k);
        }
    }
    out.insert(out.end(), rest.begin(), rest.end());
    return out;
}

void emit_default(YAML::Emitter& out, const YAML::Node& n);

void emit_ability_special(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (auto it = n.begin(); it != n.end(); ++it) {
        const std::string k = it->first.as<std::string>();
        out << YAML::Key << k << YAML::Value;
        const auto& v = it->second;
        if (v.IsSequence()) out << YAML::Flow;
        emit_default(out, v);
    }
    out << YAML::EndMap;
}

void emit_action(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (auto it = n.begin(); it != n.end(); ++it) {
        const std::string kind = it->first.as<std::string>();
        out << YAML::Key << kind << YAML::Value;
        const auto& body = it->second;
        if (body.IsMap()) {
            out << YAML::BeginMap;
            for (const auto& bk : ordered_keys(body, action_body_order())) {
                out << YAML::Key << bk << YAML::Value;
                emit_default(out, body[bk]);
            }
            out << YAML::EndMap;
        } else {
            emit_default(out, body);
        }
    }
    out << YAML::EndMap;
}

void emit_ability_root(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (const auto& k : ordered_keys(n, ability_order())) {
        out << YAML::Key << k << YAML::Value;
        const auto& v = n[k];
        if (k == "behavior" && v.IsSequence()) {
            out << YAML::Flow;
            emit_default(out, v);
        } else if ((k == "cooldown" || k == "mana_cost" || k == "channel_time") &&
                   v.IsSequence()) {
            out << YAML::Flow;
            emit_default(out, v);
        } else if (k == "ability_special" && v.IsMap()) {
            emit_ability_special(out, v);
        } else if (k == "on_spell_start" && v.IsSequence()) {
            out << YAML::BeginSeq;
            for (const auto& act : v) emit_action(out, act);
            out << YAML::EndSeq;
        } else {
            emit_default(out, v);
        }
    }
    out << YAML::EndMap;
}

void emit_default(YAML::Emitter& out, const YAML::Node& n) {
    if (n.IsMap()) {
        out << YAML::BeginMap;
        for (auto it = n.begin(); it != n.end(); ++it) {
            out << YAML::Key << it->first.as<std::string>() << YAML::Value;
            emit_default(out, it->second);
        }
        out << YAML::EndMap;
    } else if (n.IsSequence()) {
        out << YAML::BeginSeq;
        for (const auto& e : n) emit_default(out, e);
        out << YAML::EndSeq;
    } else {
        out << n;
    }
}

AbilityMeta parse_meta(const YAML::Node& n) {
    AbilityMeta m;
    if (n["name"])       m.name       = n["name"].as<std::string>();
    if (n["cast_range"]) m.cast_range = n["cast_range"].as<double>();
    if (n["cast_point"]) m.cast_point = n["cast_point"].as<double>();
    if (n["behavior"]) {
        std::string csv;
        if (n["behavior"].IsSequence()) {
            for (auto it = n["behavior"].begin(); it != n["behavior"].end(); ++it) {
                if (!csv.empty()) csv += ",";
                csv += it->as<std::string>();
            }
        } else {
            csv = n["behavior"].as<std::string>();
        }
        m.behavior = parse_behavior_flags(csv);
    }
    if (n["target_team"]) {
        m.target_team = parse_target_team(n["target_team"].as<std::string>());
    }
    m.is_passive    = has_flag(m.behavior, BehaviorFlag::Passive);
    m.is_channelled = has_flag(m.behavior, BehaviorFlag::Channelled);
    return m;
}

} // namespace

std::string emit_ability_yaml(const YAML::Node& root) {
    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    if (!root || !root.IsMap()) {
        emit_default(out, root);
    } else {
        emit_ability_root(out, root);
    }
    if (!out.good()) {
        throw std::runtime_error(std::string("ability emit failed: ") +
                                  out.GetLastError());
    }
    std::string s = out.c_str();
    if (s.empty() || s.back() != '\n') s.push_back('\n');
    return s;
}

AbilityDoc AbilityDoc::load(const fs::path& path) {
    AbilityDoc d;
    try {
        d.root_ = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("ability_doc: 解析 yaml 失败 " + path.string() +
                                  ": " + e.what());
    }
    return d;
}

std::string AbilityDoc::emit() const { return emit_ability_yaml(root_); }

void AbilityDoc::save_to(const fs::path& path) const {
    const std::string text = emit();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("ability_doc: 无法写入 " + path.string());
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) throw std::runtime_error("ability_doc: 写入失败 " + path.string());
}

std::size_t AbilityCatalog::scan(const fs::path& abilities_dir) {
    entries_.clear();
    if (!fs::exists(abilities_dir) || !fs::is_directory(abilities_dir)) {
        throw std::runtime_error("ability_catalog: 目录不存在 " +
                                  abilities_dir.string());
    }
    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(abilities_dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".yaml") continue;
        files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& p : files) {
        YAML::Node n;
        try {
            n = YAML::LoadFile(p.string());
        } catch (const YAML::Exception& e) {
            throw std::runtime_error("ability_catalog: 解析失败 " + p.string() +
                                      ": " + e.what());
        }
        if (!n || !n.IsMap() || !n["name"]) continue;
        AbilityFileEntry entry;
        entry.yaml_path = p.string();
        entry.yaml_stem = p.stem().string();
        entry.meta      = parse_meta(n);
        entries_.push_back(std::move(entry));
    }
    return entries_.size();
}

const AbilityFileEntry* AbilityCatalog::find(std::string_view name) const {
    for (const auto& e : entries_) {
        if (e.yaml_stem == name) return &e;
    }
    return nullptr;
}

} // namespace dota::tools
