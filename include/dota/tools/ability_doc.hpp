#pragma once

// AbilityDoc / AbilityCatalog: 独立 ability yaml 文件级管理. 与 HeroDoc 平级.
//
// 一个 ability yaml 顶层就是 ability 字段 (name / base_class / behavior / ...).
// 多个 hero 可以引用同一个 ability, 因此 ability 文件是共享资源.

#include "dota/ability/behavior.hpp"
#include "dota/tools/hero_catalog.hpp"  // 复用 AbilityMeta

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <string>
#include <vector>

namespace dota::tools {

// 一个独立 ability yaml 文件的 in-memory 表示. 通过修改 root() 编辑, 然后 save_to
// 落盘. emit 走 hero_writer 的 ordered emitter (复用同一 key 顺序).
class AbilityDoc {
public:
    static AbilityDoc load(const std::filesystem::path& path);

    YAML::Node&       root()       { return root_; }
    const YAML::Node& root() const { return root_; }

    std::string emit() const;
    void        save_to(const std::filesystem::path& path) const;

private:
    YAML::Node root_;
};

// emit 一个独立 ability yaml. 与 hero_writer 共享 key 顺序, 但顶层就是 ability
// 的字段 (没有 hero / abilities 包裹).
std::string emit_ability_yaml(const YAML::Node& root);

struct AbilityFileEntry {
    std::string yaml_path;   // 绝对路径
    std::string yaml_stem;   // 文件名 stem (= ability name 约定)
    AbilityMeta meta;        // 解析出的元数据
};

// 扫描 abilities/ 目录, 收集所有独立 ability yaml.
class AbilityCatalog {
public:
    std::size_t scan(const std::filesystem::path& abilities_dir);

    const std::vector<AbilityFileEntry>& entries() const { return entries_; }

    // 按 yaml_stem (= ability name) 查找. 找不到返回 nullptr.
    const AbilityFileEntry* find(std::string_view name) const;

private:
    std::vector<AbilityFileEntry> entries_;
};

} // namespace dota::tools
