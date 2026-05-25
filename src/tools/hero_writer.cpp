#include "dota/tools/hero_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace dota::tools {

namespace {

// 各层级的 "首选 key 顺序". 列表中没出现的 key 按源文件顺序追加在后面.
const std::vector<std::string>& top_order() {
    static const std::vector<std::string> v = {"hero", "abilities"};
    return v;
}
const std::vector<std::string>& hero_order() {
    static const std::vector<std::string> v = {
        "name",
        "base_health",
        "base_mana",
        "base_armor",
        "base_magic_resist",
        "hull_radius",
        "attack_type",
        "attack_range",
        "projectile_speed",
    };
    return v;
}
const std::vector<std::string>& ability_order() {
    static const std::vector<std::string> v = {
        "name",
        "base_class",
        "script",
        "behavior",
        "target_team",
        "cast_point",
        "backswing",
        "channel_time",
        "cast_range",
        "cooldown",
        "mana_cost",
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

// 按 priority 排好的 key 列表, 未列出的按源 Node 迭代顺序追加.
std::vector<std::string> ordered_keys(const YAML::Node& map,
                                       const std::vector<std::string>& priority) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& k : priority) {
        if (map[k]) {
            out.push_back(k);
            seen.insert(k);
        }
    }
    for (auto it = map.begin(); it != map.end(); ++it) {
        const std::string k = it->first.as<std::string>();
        if (seen.insert(k).second) out.push_back(k);
    }
    return out;
}

void emit_default(YAML::Emitter& out, const YAML::Node& n);

// ability_special: 顶层 block style, 每个 value 强制 flow style (除非是 scalar).
void emit_ability_special(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (auto it = n.begin(); it != n.end(); ++it) {
        const std::string k = it->first.as<std::string>();
        out << YAML::Key << k << YAML::Value;
        const auto& v = it->second;
        if (v.IsSequence()) {
            out << YAML::Flow;
        }
        emit_default(out, v);
    }
    out << YAML::EndMap;
}

// 单条 action: { kind: { body } }. 外 / 内都 block style, body 内部按
// action_body_order 排.
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

// 单个 ability map.
void emit_ability(YAML::Emitter& out, const YAML::Node& n) {
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

void emit_hero_block(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (const auto& k : ordered_keys(n, hero_order())) {
        out << YAML::Key << k << YAML::Value;
        emit_default(out, n[k]);
    }
    out << YAML::EndMap;
}

void emit_root(YAML::Emitter& out, const YAML::Node& n) {
    out << YAML::BeginMap;
    for (const auto& k : ordered_keys(n, top_order())) {
        out << YAML::Key << k << YAML::Value;
        if (k == "hero" && n[k].IsMap()) {
            emit_hero_block(out, n[k]);
        } else if (k == "abilities" && n[k].IsSequence()) {
            out << YAML::BeginSeq;
            for (const auto& a : n[k]) {
                if (a.IsScalar()) {
                    out << a.as<std::string>();   // 引用名 (新格式)
                } else {
                    emit_ability(out, a);          // 内嵌定义 (老格式, 兼容)
                }
            }
            out << YAML::EndSeq;
        } else {
            emit_default(out, n[k]);
        }
    }
    out << YAML::EndMap;
}

// 通用兜底: 任意 Node -> Emitter, 保持源结构 + 源 key 顺序. yaml-cpp 自带的
// `out << node` 对未知字段已经够用; 仅在 map/sequence 时递归显式写, 这样能
// 在调用点用 `out << YAML::Flow;` 修饰 (修饰只对下一个 collection 起效, 必须
// 由我们 BeginSeq/BeginMap 显式触发).
void emit_default(YAML::Emitter& out, const YAML::Node& n) {
    if (n.IsMap()) {
        out << YAML::BeginMap;
        for (auto it = n.begin(); it != n.end(); ++it) {
            out << YAML::Key << it->first.as<std::string>()
                << YAML::Value;
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

} // namespace

std::string emit_hero_yaml(const YAML::Node& root) {
    YAML::Emitter out;
    out.SetIndent(2);
    out.SetMapFormat(YAML::Block);
    out.SetSeqFormat(YAML::Block);
    if (!root || !root.IsMap()) {
        emit_default(out, root);
    } else {
        emit_root(out, root);
    }
    if (!out.good()) {
        throw std::runtime_error(std::string("yaml emit failed: ") + out.GetLastError());
    }
    std::string s = out.c_str();
    if (s.empty() || s.back() != '\n') s.push_back('\n');
    return s;
}

HeroDoc HeroDoc::load(const std::string& path) {
    HeroDoc d;
    try {
        d.root_ = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("hero_doc: 解析 yaml 失败 " + path + ": " + e.what());
    }
    return d;
}

std::string HeroDoc::emit() const {
    return emit_hero_yaml(root_);
}

void HeroDoc::save_to(const std::string& path) const {
    const std::string text = emit();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("hero_doc: 无法写入 " + path);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) throw std::runtime_error("hero_doc: 写入失败 " + path);
}

} // namespace dota::tools
