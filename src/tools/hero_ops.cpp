#include "dota/tools/hero_ops.hpp"

#include "dota/tools/hero_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

fs::path heroes_dir(const fs::path& data_root) { return data_root / "heroes"; }
fs::path scripts_dir(const fs::path& data_root) {
    return data_root / "scripts" / "abilities";
}

void ensure_valid_stem(const std::string& stem) {
    if (!is_valid_hero_stem(stem)) {
        throw std::runtime_error("hero_ops: 非法 stem (允许 a-z0-9_): " + stem);
    }
}

// 模板: 一个最小可解析的英雄 yaml.
std::string default_hero_template(const std::string& stem) {
    return std::string(
        "hero:\n"
        "  name: npc_dota_hero_") + stem + "\n"
        "  base_health: 600\n"
        "  base_mana: 300\n"
        "  base_armor: 1\n"
        "  base_magic_resist: 0.25\n"
        "\n"
        "abilities: []\n";
}

// 写整个文件 (覆盖). parent 必须已存在.
void write_text_file(const fs::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("hero_ops: 无法写入 " + path.string());
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

// 把 src_stem_xxx 前缀替换成 dst_stem_xxx; 不匹配前缀就原样返回.
std::string rewrite_prefix(const std::string& s,
                           const std::string& src_stem,
                           const std::string& dst_stem) {
    const std::string p = src_stem + "_";
    if (s.rfind(p, 0) == 0) return dst_stem + "_" + s.substr(p.size());
    return s;
}

std::string rewrite_script_path(const std::string& s,
                                 const std::string& src_stem,
                                 const std::string& dst_stem) {
    // "abilities/lion_earth_spike.lua" -> "abilities/lionx_earth_spike.lua"
    const std::string p = "abilities/" + src_stem + "_";
    if (s.rfind(p, 0) == 0) {
        return "abilities/" + dst_stem + "_" + s.substr(p.size());
    }
    return s;
}

// 给整份 yaml 文档替换 stem 前缀. 在原 Node 上原地改.
void rewrite_hero_doc_stems(YAML::Node& root,
                             const std::string& src_stem,
                             const std::string& dst_stem) {
    if (root["hero"] && root["hero"]["name"]) {
        const auto cur = root["hero"]["name"].as<std::string>();
        // 只在 name 形如 npc_dota_hero_<src_stem> 时重写, 否则保持用户自定义.
        const std::string target = "npc_dota_hero_" + src_stem;
        if (cur == target) {
            root["hero"]["name"] = "npc_dota_hero_" + dst_stem;
        }
    }
    if (root["abilities"] && root["abilities"].IsSequence()) {
        for (std::size_t i = 0; i < root["abilities"].size(); ++i) {
            YAML::Node a = root["abilities"][i];
            if (a["name"]) {
                a["name"] = rewrite_prefix(a["name"].as<std::string>(),
                                           src_stem, dst_stem);
            }
            if (a["script"]) {
                a["script"] = rewrite_script_path(
                    a["script"].as<std::string>(), src_stem, dst_stem);
            }
        }
    }
}

} // namespace

bool is_valid_hero_stem(const std::string& s) {
    if (s.empty()) return false;
    if (s.front() == '_') return false;
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

HeroFiles collect_hero_files(const fs::path& data_root, const std::string& stem) {
    HeroFiles out;
    out.yaml_path = heroes_dir(data_root) / (stem + ".yaml");

    const fs::path scripts = scripts_dir(data_root);
    if (fs::exists(scripts) && fs::is_directory(scripts)) {
        const std::string prefix = stem + "_";
        for (const auto& e : fs::directory_iterator(scripts)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".lua") continue;
            const std::string fname = e.path().filename().string();
            if (fname.rfind(prefix, 0) == 0) {
                out.ability_scripts.push_back(e.path());
            }
        }
        std::sort(out.ability_scripts.begin(), out.ability_scripts.end());
    }
    return out;
}

fs::path create_hero(const fs::path& data_root, const std::string& stem) {
    ensure_valid_stem(stem);
    const fs::path yaml = heroes_dir(data_root) / (stem + ".yaml");
    if (fs::exists(yaml)) {
        throw std::runtime_error("hero_ops: 已存在 " + yaml.string());
    }
    fs::create_directories(yaml.parent_path());
    write_text_file(yaml, default_hero_template(stem));
    return yaml;
}

fs::path duplicate_hero(const fs::path& data_root,
                        const std::string& src_stem,
                        const std::string& dst_stem) {
    ensure_valid_stem(src_stem);
    ensure_valid_stem(dst_stem);
    if (src_stem == dst_stem) {
        throw std::runtime_error("hero_ops: src 和 dst 相同");
    }

    const fs::path src_yaml = heroes_dir(data_root) / (src_stem + ".yaml");
    const fs::path dst_yaml = heroes_dir(data_root) / (dst_stem + ".yaml");
    if (!fs::exists(src_yaml)) {
        throw std::runtime_error("hero_ops: 源不存在 " + src_yaml.string());
    }
    if (fs::exists(dst_yaml)) {
        throw std::runtime_error("hero_ops: 目标已存在 " + dst_yaml.string());
    }

    const HeroFiles src = collect_hero_files(data_root, src_stem);

    // 先把 yaml 加载并重写, 再写入 dst.
    HeroDoc doc = HeroDoc::load(src_yaml.string());
    YAML::Node root = doc.root();
    rewrite_hero_doc_stems(root, src_stem, dst_stem);

    // 拷脚本; 失败时回滚.
    std::vector<fs::path> copied;
    try {
        const fs::path scripts = scripts_dir(data_root);
        for (const auto& s : src.ability_scripts) {
            const std::string fname = s.filename().string();
            const std::string new_name =
                rewrite_prefix(fname, src_stem, dst_stem);
            const fs::path dst_path = scripts / new_name;
            if (fs::exists(dst_path)) {
                throw std::runtime_error("hero_ops: 脚本已存在 " +
                                         dst_path.string());
            }
            fs::copy_file(s, dst_path);
            copied.push_back(dst_path);
        }

        // 写新 yaml. HeroDoc 在 root 引用上写, 直接 emit 即可.
        fs::create_directories(dst_yaml.parent_path());
        write_text_file(dst_yaml, emit_hero_yaml(root));
    } catch (...) {
        std::error_code ec;
        for (const auto& p : copied) fs::remove(p, ec);
        throw;
    }
    return dst_yaml;
}

void rename_hero(const fs::path& data_root,
                 const std::string& src_stem,
                 const std::string& dst_stem) {
    ensure_valid_stem(src_stem);
    ensure_valid_stem(dst_stem);
    if (src_stem == dst_stem) return;

    const fs::path src_yaml = heroes_dir(data_root) / (src_stem + ".yaml");
    const fs::path dst_yaml = heroes_dir(data_root) / (dst_stem + ".yaml");
    if (!fs::exists(src_yaml)) {
        throw std::runtime_error("hero_ops: 源不存在 " + src_yaml.string());
    }
    if (fs::exists(dst_yaml)) {
        throw std::runtime_error("hero_ops: 目标已存在 " + dst_yaml.string());
    }

    const HeroFiles src = collect_hero_files(data_root, src_stem);
    HeroDoc doc = HeroDoc::load(src_yaml.string());
    YAML::Node root = doc.root();
    rewrite_hero_doc_stems(root, src_stem, dst_stem);

    // 计划好的脚本重命名 (src -> dst). 提前校验冲突.
    const fs::path scripts = scripts_dir(data_root);
    std::vector<std::pair<fs::path, fs::path>> moves;
    moves.reserve(src.ability_scripts.size());
    for (const auto& s : src.ability_scripts) {
        const std::string fname = s.filename().string();
        const fs::path dst_path = scripts /
            rewrite_prefix(fname, src_stem, dst_stem);
        if (fs::exists(dst_path)) {
            throw std::runtime_error("hero_ops: 脚本目标已存在 " +
                                     dst_path.string());
        }
        moves.emplace_back(s, dst_path);
    }

    // 先写 dst yaml, 再 rename 脚本, 最后删 src yaml. 任一步失败都回滚.
    fs::create_directories(dst_yaml.parent_path());
    write_text_file(dst_yaml, emit_hero_yaml(root));

    std::vector<std::pair<fs::path, fs::path>> renamed;
    try {
        for (const auto& [from, to] : moves) {
            fs::rename(from, to);
            renamed.emplace_back(from, to);
        }
        fs::remove(src_yaml);
    } catch (...) {
        std::error_code ec;
        for (const auto& [from, to] : renamed) {
            fs::rename(to, from, ec);
        }
        fs::remove(dst_yaml, ec);
        throw;
    }
}

std::vector<TrashRecord> delete_hero(const fs::path& data_root,
                                     const std::string& stem) {
    const HeroFiles files = collect_hero_files(data_root, stem);
    std::vector<TrashRecord> out;
    out.reserve(1 + files.ability_scripts.size());

    if (fs::exists(files.yaml_path)) {
        out.push_back(move_to_trash(data_root, files.yaml_path));
    }
    for (const auto& s : files.ability_scripts) {
        out.push_back(move_to_trash(data_root, s));
    }
    return out;
}

} // namespace dota::tools
