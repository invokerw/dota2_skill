#pragma once

// 英雄文件级 CRUD: create / duplicate / rename / delete.
//
// 约定: 一个英雄"独占" `data/scripts/abilities/<stem>_*.lua`. duplicate /
// rename / delete 操作会同时处理这些 lua 脚本以及 yaml 内的 ability name /
// script path / hero.name 字段.
//
// 删除走 trash; create / duplicate / rename 失败会尽量回滚已复制的 lua 文件.
// 所有抛 std::runtime_error.

#include "dota/tools/trash.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dota::tools {

struct HeroFiles {
    std::filesystem::path              yaml_path;        // data/heroes/<stem>.yaml
    std::vector<std::filesystem::path> ability_scripts;  // 绝对路径
};

// stem 合法性: 非空, 仅 [a-z0-9_], 不以 _ 开头.
bool is_valid_hero_stem(const std::string& s);

// 找出 stem 对应的 yaml + 它独占的所有 lua 脚本.
HeroFiles collect_hero_files(const std::filesystem::path& data_root,
                             const std::string& stem);

// 用模板创建一个 data/heroes/<stem>.yaml, 含一个空 ability_datadriven 占位.
// 如果文件已存在则抛.
std::filesystem::path create_hero(const std::filesystem::path& data_root,
                                  const std::string& stem);

// 拷 yaml + 独占脚本, 重写 hero.name / 每个 ability.name / script: 字段以
// 替换 stem 前缀. 返回新 yaml 路径.
std::filesystem::path duplicate_hero(const std::filesystem::path& data_root,
                                     const std::string& src_stem,
                                     const std::string& dst_stem);

// 重命名 yaml + 独占脚本, 重写 hero.name / 每个 ability.name / script: 字段.
void rename_hero(const std::filesystem::path& data_root,
                 const std::string& src_stem,
                 const std::string& dst_stem);

// 把 yaml + 独占脚本批量移到 trash. 返回 trash 记录, 顺序与 collect 相同.
std::vector<TrashRecord> delete_hero(const std::filesystem::path& data_root,
                                     const std::string& stem);

} // namespace dota::tools
