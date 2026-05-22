#include "dota/tools/hero_ops.hpp"

#include "dota/tools/hero_writer.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

fs::path heroes_dir(const fs::path& data_root) { return data_root / "heroes"; }

void ensure_valid_stem(const std::string& stem) {
    if (!is_valid_hero_stem(stem)) {
        throw std::runtime_error("hero_ops: 非法 stem (允许 a-z0-9_): " + stem);
    }
}

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

void write_text_file(const fs::path& path, const std::string& body) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("hero_ops: 无法写入 " + path.string());
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

// 仅把 hero.name 形如 npc_dota_hero_<src_stem> 替换为 npc_dota_hero_<dst_stem>;
// 自定义 name 保持不变.
void rewrite_hero_name(YAML::Node& root,
                        const std::string& src_stem,
                        const std::string& dst_stem) {
    if (!root["hero"] || !root["hero"]["name"]) return;
    const auto cur = root["hero"]["name"].as<std::string>();
    const std::string target = "npc_dota_hero_" + src_stem;
    if (cur == target) {
        root["hero"]["name"] = "npc_dota_hero_" + dst_stem;
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

    HeroDoc doc = HeroDoc::load(src_yaml.string());
    YAML::Node root = doc.root();
    rewrite_hero_name(root, src_stem, dst_stem);

    fs::create_directories(dst_yaml.parent_path());
    write_text_file(dst_yaml, emit_hero_yaml(root));
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

    HeroDoc doc = HeroDoc::load(src_yaml.string());
    YAML::Node root = doc.root();
    rewrite_hero_name(root, src_stem, dst_stem);

    fs::create_directories(dst_yaml.parent_path());
    write_text_file(dst_yaml, emit_hero_yaml(root));

    std::error_code ec;
    if (!fs::remove(src_yaml, ec)) {
        // 回滚.
        fs::remove(dst_yaml, ec);
        throw std::runtime_error("hero_ops: 删除源失败 " + src_yaml.string());
    }
}

std::vector<TrashRecord> delete_hero(const fs::path& data_root,
                                       const std::string& stem) {
    const fs::path yaml = heroes_dir(data_root) / (stem + ".yaml");
    std::vector<TrashRecord> out;
    if (fs::exists(yaml)) out.push_back(move_to_trash(data_root, yaml));
    return out;
}

} // namespace dota::tools
