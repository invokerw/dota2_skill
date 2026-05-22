#include "dota/tools/ability_ops.hpp"

#include "dota/tools/ability_doc.hpp"
#include "dota/tools/hero_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

fs::path heroes_dir(const fs::path& data_root)    { return data_root / "heroes"; }
fs::path abilities_dir(const fs::path& data_root) { return data_root / "abilities"; }
fs::path scripts_dir(const fs::path& data_root) {
    return data_root / "scripts" / "abilities";
}

void ensure_valid_name(std::string_view s) {
    if (!is_valid_ability_name(s)) {
        throw std::runtime_error(
            std::string("ability_ops: 非法 ability name (允许 a-z0-9_, 不以 _ 开头): ") +
            std::string(s));
    }
}

void write_text_if_absent(const fs::path& p, const std::string& body) {
    if (fs::exists(p)) {
        throw std::runtime_error("ability_ops: 文件已存在 " + p.string());
    }
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("ability_ops: 无法写入 " + p.string());
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string default_datadriven_template(const std::string& name) {
    return std::string(
        "name: ") + name + "\n"
        "base_class: ability_datadriven\n"
        "behavior: [NO_TARGET]\n"
        "target_team: NONE\n"
        "cast_point: 0.0\n"
        "cooldown: [10]\n"
        "mana_cost: [0]\n"
        "ability_special: {}\n"
        "on_spell_start: []\n";
}

std::string default_lua_template(const std::string& name,
                                  const std::string& script_rel) {
    return std::string(
        "name: ") + name + "\n"
        "base_class: ability_lua\n"
        "script: " + script_rel + "\n"
        "behavior: [NO_TARGET]\n"
        "target_team: NONE\n"
        "cast_point: 0.0\n"
        "cooldown: [10]\n"
        "mana_cost: [0]\n"
        "ability_special: {}\n";
}

constexpr const char* kLuaAbilityTemplateBody =
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

// 在 hero yaml 中扫描 abilities scalar 引用 = <name> 的位置, 把它替换为 dst.
// 返回是否发生改动.
bool replace_ref_in_hero(const fs::path& hero_yaml,
                          const std::string& src,
                          const std::string& dst) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(hero_yaml.string());
    } catch (const YAML::Exception& e) {
        throw std::runtime_error("ability_ops: 解析 hero yaml 失败 " +
                                  hero_yaml.string() + ": " + e.what());
    }
    if (!root["abilities"] || !root["abilities"].IsSequence()) return false;

    bool changed = false;
    YAML::Node seq = root["abilities"];
    for (std::size_t i = 0; i < seq.size(); ++i) {
        if (seq[i].IsScalar() && seq[i].as<std::string>() == src) {
            seq[i] = dst;
            changed = true;
        }
    }
    if (!changed) return false;

    const std::string text = emit_hero_yaml(root);
    std::ofstream f(hero_yaml, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("ability_ops: 无法写入 " + hero_yaml.string());
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) throw std::runtime_error("ability_ops: 写入失败 " + hero_yaml.string());
    return true;
}

} // namespace

bool is_valid_ability_name(std::string_view s) {
    if (s.empty()) return false;
    if (s.front() == '_') return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

fs::path ability_file_path(const fs::path& data_root, std::string_view name) {
    return abilities_dir(data_root) / (std::string(name) + ".yaml");
}

std::vector<std::string> heroes_referencing(const fs::path& data_root,
                                              std::string_view ability_name) {
    std::vector<std::string> out;
    const fs::path dir = heroes_dir(data_root);
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;

    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".yaml") continue;
        YAML::Node root;
        try {
            root = YAML::LoadFile(e.path().string());
        } catch (const YAML::Exception&) { continue; }
        if (!root["abilities"] || !root["abilities"].IsSequence()) continue;
        for (const auto& a : root["abilities"]) {
            if (a.IsScalar() && a.as<std::string>() == ability_name) {
                out.push_back(e.path().stem().string());
                break;
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

fs::path create_datadriven_ability_file(const fs::path& data_root,
                                          const std::string& name) {
    ensure_valid_name(name);
    const fs::path target = ability_file_path(data_root, name);
    write_text_if_absent(target, default_datadriven_template(name));
    return target;
}

fs::path create_lua_ability_file(const fs::path& data_root,
                                  const std::string& name,
                                  const std::string& script_filename) {
    ensure_valid_name(name);
    const std::string fname =
        script_filename.empty() ? (name + ".lua") : script_filename;
    const fs::path yaml_path = ability_file_path(data_root, name);
    const fs::path lua_path  = scripts_dir(data_root) / fname;

    if (fs::exists(yaml_path)) {
        throw std::runtime_error("ability_ops: 文件已存在 " + yaml_path.string());
    }
    if (fs::exists(lua_path)) {
        throw std::runtime_error("ability_ops: 脚本已存在 " + lua_path.string());
    }

    // 先写脚本, 失败时不会留 yaml 副产物.
    write_text_if_absent(lua_path, kLuaAbilityTemplateBody);

    try {
        const std::string rel = "abilities/" + fname;
        write_text_if_absent(yaml_path, default_lua_template(name, rel));
    } catch (...) {
        std::error_code ec;
        fs::remove(lua_path, ec);
        throw;
    }
    return yaml_path;
}

fs::path duplicate_ability_file(const fs::path& data_root,
                                  const std::string& src_name,
                                  const std::string& dst_name) {
    ensure_valid_name(src_name);
    ensure_valid_name(dst_name);
    if (src_name == dst_name) {
        throw std::runtime_error("ability_ops: src 和 dst 相同");
    }
    const fs::path src = ability_file_path(data_root, src_name);
    const fs::path dst = ability_file_path(data_root, dst_name);
    if (!fs::exists(src)) {
        throw std::runtime_error("ability_ops: 源不存在 " + src.string());
    }
    if (fs::exists(dst)) {
        throw std::runtime_error("ability_ops: 目标已存在 " + dst.string());
    }

    AbilityDoc doc = AbilityDoc::load(src);
    doc.root()["name"] = dst_name;

    // 如果是 ability_lua, 需要把 lua 脚本一并复制并改 script 引用; 复制规则
    // 沿用 modifier_ops 的 "1 ability = 1 lua 文件" 约定: dst 文件命名为
    // <dst_name>.lua. 若该文件已存在, 抛.
    std::optional<fs::path> copied_lua;
    if (doc.root()["base_class"] &&
        doc.root()["base_class"].as<std::string>() == "ability_lua" &&
        doc.root()["script"]) {
        const std::string old_rel = doc.root()["script"].as<std::string>();
        const std::string new_rel = "abilities/" + dst_name + ".lua";
        const fs::path old_lua    = data_root / "scripts" / old_rel;
        const fs::path new_lua    = data_root / "scripts" / new_rel;
        if (!fs::exists(old_lua)) {
            throw std::runtime_error("ability_ops: 源脚本不存在 " +
                                      old_lua.string());
        }
        if (fs::exists(new_lua)) {
            throw std::runtime_error("ability_ops: 目标脚本已存在 " +
                                      new_lua.string());
        }
        fs::create_directories(new_lua.parent_path());
        fs::copy_file(old_lua, new_lua);
        copied_lua = new_lua;
        doc.root()["script"] = new_rel;
    }

    try {
        doc.save_to(dst);
    } catch (...) {
        std::error_code ec;
        if (copied_lua) fs::remove(*copied_lua, ec);
        throw;
    }
    return dst;
}

RenameAbilityResult rename_ability_file(const fs::path& data_root,
                                          const std::string& src_name,
                                          const std::string& dst_name) {
    ensure_valid_name(src_name);
    ensure_valid_name(dst_name);
    if (src_name == dst_name) {
        RenameAbilityResult r;
        r.new_yaml_path = ability_file_path(data_root, src_name);
        return r;
    }
    const fs::path src = ability_file_path(data_root, src_name);
    const fs::path dst = ability_file_path(data_root, dst_name);
    if (!fs::exists(src)) {
        throw std::runtime_error("ability_ops: 源不存在 " + src.string());
    }
    if (fs::exists(dst)) {
        throw std::runtime_error("ability_ops: 目标已存在 " + dst.string());
    }

    // 加载源 + 改 name 字段; 若 lua 还要改 script 路径.
    AbilityDoc doc = AbilityDoc::load(src);
    doc.root()["name"] = dst_name;

    std::optional<std::pair<fs::path, fs::path>> lua_move;
    if (doc.root()["base_class"] &&
        doc.root()["base_class"].as<std::string>() == "ability_lua" &&
        doc.root()["script"]) {
        const std::string old_rel = doc.root()["script"].as<std::string>();
        const fs::path old_lua    = data_root / "scripts" / old_rel;
        // 仅在脚本名匹配 "<src>.lua" 约定时改名; 自定义文件名保持原状.
        const std::string expected_old = "abilities/" + src_name + ".lua";
        if (old_rel == expected_old) {
            const std::string new_rel = "abilities/" + dst_name + ".lua";
            const fs::path new_lua    = data_root / "scripts" / new_rel;
            if (fs::exists(new_lua)) {
                throw std::runtime_error("ability_ops: 目标脚本已存在 " +
                                          new_lua.string());
            }
            if (fs::exists(old_lua)) {
                lua_move = std::make_pair(old_lua, new_lua);
            }
            doc.root()["script"] = new_rel;
        }
    }

    // 写 dst yaml -> 删 src yaml -> 移脚本 -> 同步 hero 引用. 任一步失败回滚.
    doc.save_to(dst);
    std::error_code ec;
    fs::remove(src, ec);
    if (ec) {
        fs::remove(dst, ec);
        throw std::runtime_error("ability_ops: 删除源失败 " + src.string());
    }
    if (lua_move) {
        std::error_code mec;
        fs::rename(lua_move->first, lua_move->second, mec);
        if (mec) {
            // 回滚: dst yaml 移回 src.
            fs::rename(dst, src, ec);
            throw std::runtime_error("ability_ops: 重命名脚本失败 " +
                                      lua_move->first.string());
        }
    }

    RenameAbilityResult result;
    result.new_yaml_path = dst;
    // 同步所有 hero yaml.
    const fs::path hdir = heroes_dir(data_root);
    if (fs::exists(hdir) && fs::is_directory(hdir)) {
        std::vector<fs::path> hero_files;
        for (const auto& e : fs::directory_iterator(hdir)) {
            if (e.is_regular_file() && e.path().extension() == ".yaml") {
                hero_files.push_back(e.path());
            }
        }
        std::sort(hero_files.begin(), hero_files.end());
        for (const auto& hp : hero_files) {
            if (replace_ref_in_hero(hp, src_name, dst_name)) {
                result.updated_hero_stems.push_back(hp.stem().string());
            }
        }
    }
    return result;
}

DeleteAbilityResult delete_ability_file(const fs::path& data_root,
                                          const std::string& name) {
    ensure_valid_name(name);
    const fs::path yaml_path = ability_file_path(data_root, name);
    if (!fs::exists(yaml_path)) {
        throw std::runtime_error("ability_ops: 文件不存在 " + yaml_path.string());
    }
    const auto refs = heroes_referencing(data_root, name);
    if (!refs.empty()) {
        std::string msg = "ability_ops: 仍被引用, 拒绝删除 (";
        for (std::size_t i = 0; i < refs.size(); ++i) {
            if (i) msg += ", ";
            msg += refs[i];
        }
        msg += ")";
        throw std::runtime_error(msg);
    }

    // 解析 -- 看是否需要带走 lua 脚本.
    std::optional<fs::path> lua_path;
    {
        YAML::Node root = YAML::LoadFile(yaml_path.string());
        if (root["base_class"] &&
            root["base_class"].as<std::string>() == "ability_lua" &&
            root["script"]) {
            const fs::path candidate =
                data_root / "scripts" / root["script"].as<std::string>();
            if (fs::exists(candidate)) lua_path = candidate;
        }
    }

    DeleteAbilityResult out;
    out.yaml_record = move_to_trash(data_root, yaml_path);
    if (lua_path) {
        out.script_record = move_to_trash(data_root, *lua_path);
    }
    return out;
}

void hero_add_ability_ref(YAML::Node& hero_root, const std::string& name) {
    if (name.empty()) throw std::runtime_error("ability_ops: 引用名为空");
    if (!hero_root["abilities"] || !hero_root["abilities"].IsSequence()) {
        hero_root["abilities"] = YAML::Node(YAML::NodeType::Sequence);
    }
    YAML::Node seq = hero_root["abilities"];
    for (const auto& a : seq) {
        if (a.IsScalar() && a.as<std::string>() == name) {
            throw std::runtime_error("ability_ops: 引用已存在 " + name);
        }
    }
    seq.push_back(name);
}

void hero_remove_ability_ref_at(YAML::Node& hero_root, std::size_t index) {
    if (!hero_root["abilities"] || !hero_root["abilities"].IsSequence()) {
        throw std::runtime_error("ability_ops: 没有 abilities 列表");
    }
    if (index >= hero_root["abilities"].size()) {
        throw std::runtime_error("ability_ops: 索引越界");
    }
    hero_root["abilities"].remove(index);
}

void hero_move_ability_ref(YAML::Node& hero_root,
                            std::size_t from, std::size_t to) {
    if (!hero_root["abilities"] || !hero_root["abilities"].IsSequence()) {
        throw std::runtime_error("ability_ops: 没有 abilities 列表");
    }
    YAML::Node seq = hero_root["abilities"];
    if (from >= seq.size() || to >= seq.size()) {
        throw std::runtime_error("ability_ops: 索引越界");
    }
    if (from == to) return;
    // yaml-cpp 没有原地 move; 拷贝后重建序列.
    std::vector<std::string> names;
    names.reserve(seq.size());
    for (std::size_t i = 0; i < seq.size(); ++i) {
        names.push_back(seq[i].IsScalar() ? seq[i].as<std::string>() : "");
    }
    std::string moved = names[from];
    names.erase(names.begin() + from);
    names.insert(names.begin() + to, moved);

    YAML::Node fresh(YAML::NodeType::Sequence);
    for (const auto& s : names) fresh.push_back(s);
    hero_root["abilities"] = fresh;
}

int hero_find_ability_ref(const YAML::Node& hero_root, std::string_view name) {
    if (!hero_root["abilities"] || !hero_root["abilities"].IsSequence()) return -1;
    const auto& seq = hero_root["abilities"];
    for (std::size_t i = 0; i < seq.size(); ++i) {
        if (seq[i].IsScalar() && seq[i].as<std::string>() == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void set_ability_special(YAML::Node& ability_root,
                          const std::vector<AbilitySpecialEntry>& entries) {
    YAML::Node spec(YAML::NodeType::Map);
    for (const auto& e : entries) {
        YAML::Node arr(YAML::NodeType::Sequence);
        for (double v : e.values) {
            if (e.is_int) arr.push_back(static_cast<long>(std::llround(v)));
            else          arr.push_back(v);
        }
        spec[e.key] = arr;
    }
    ability_root["ability_special"] = spec;
}

fs::path write_lua_ability_template(const fs::path& data_root,
                                      const std::string& filename) {
    const fs::path target = scripts_dir(data_root) / filename;
    write_text_if_absent(target, kLuaAbilityTemplateBody);
    return target;
}

} // namespace dota::tools
