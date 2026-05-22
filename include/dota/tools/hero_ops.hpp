#pragma once

// 英雄文件级 CRUD: create / duplicate / rename / delete.
//
// ability yaml 是 hero 之间共享的资源 (data/abilities/<name>.yaml), 不归任何
// hero 独占. 因此 hero_ops 只管 data/heroes/<stem>.yaml: duplicate/rename
// 时只复制 / 重命名 hero yaml, 并改写其中的 hero.name 字段. 不复制 / 不
// rename ability yaml 或 lua 脚本.
//
// 删除走 trash. create / duplicate / rename 失败时回滚已复制的文件.
// 所有错误抛 std::runtime_error.

#include "dota/tools/trash.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dota::tools {

// stem 合法性: 非空, 仅 [a-z0-9_], 不以 _ 开头.
bool is_valid_hero_stem(const std::string& s);

// 用模板创建一个 data/heroes/<stem>.yaml. 已存在则抛.
std::filesystem::path create_hero(const std::filesystem::path& data_root,
                                  const std::string& stem);

// 拷 yaml + 改 hero.name 字段 (若是 npc_dota_hero_<src_stem> 形态). abilities
// 引用列表保持不变 (共享语义). 返回新 yaml 路径.
std::filesystem::path duplicate_hero(const std::filesystem::path& data_root,
                                     const std::string& src_stem,
                                     const std::string& dst_stem);

// 重命名 yaml + 改 hero.name 字段. abilities 引用列表保持不变.
void rename_hero(const std::filesystem::path& data_root,
                 const std::string& src_stem,
                 const std::string& dst_stem);

// 把 yaml 移到 trash. 返回 1 条 trash 记录.
std::vector<TrashRecord> delete_hero(const std::filesystem::path& data_root,
                                     const std::string& stem);

} // namespace dota::tools
