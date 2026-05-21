#pragma once

// Ability 编辑/增删操作: 直接在 YAML::Node 上原地改, 给 HeroDoc 提供配套
// 工具. 写盘动作由调用方 (UI) 通过 HeroDoc::save_to 触发.
//
// 删除 ability 时, 如果它指向 hero 独占的 ability_lua 脚本, 会一并 trash
// 那个脚本.

#include "dota/tools/trash.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dota::tools {

// hero yaml 内的 abilities 列表索引, 越界抛. yaml-cpp 的 Node 是浅引用,
// 在返回值上写就等于在 root 上写.
YAML::Node ability_node_at(YAML::Node root, std::size_t index);

// 找 ability 在 abilities 列表中的索引, 找不到返回 -1.
int find_ability_index(const YAML::Node& root, std::string_view name);

// 在末尾追加一个 ability_datadriven 占位 (空 on_spell_start). 返回新索引.
// 名字必须没在 abilities 中重复出现; 重复抛.
std::size_t add_datadriven_ability(YAML::Node& root, const std::string& name);

// 在末尾追加一个 ability_lua 占位, 同时在 data_root/scripts/abilities/<file>
// 里写入 lua 模板 (不存在才写). 模板返回的 ability 表只含一个空的
// on_spell_start. file 一般为 "<hero_stem>_<ability_short_name>.lua".
// 返回新索引. 重名 / 文件已存在都抛.
std::size_t add_lua_ability(YAML::Node& root,
                            const std::filesystem::path& data_root,
                            const std::string& ability_name,
                            const std::string& script_filename);

// 删除 abilities[index]. 如果是 ability_lua 且 script_filename 在
// data_root/scripts/abilities/ 下存在, 把脚本一起 trash. 返回 trash 记录
// (yaml 不动, 只 trash 脚本; 0 或 1 条).
std::vector<TrashRecord> remove_ability_at(
    YAML::Node& root,
    const std::filesystem::path& data_root,
    std::size_t index);

// ability_lua 模板, 写入 data_root/scripts/abilities/<filename>. 不覆盖.
std::filesystem::path write_lua_ability_template(
    const std::filesystem::path& data_root,
    const std::string& filename);

// ability_special 编辑帮助: 设置某 key 的整级数组. ints=true 时强制
// is_int=true (yaml 出整数), 否则保留小数.
struct AbilitySpecialEntry {
    std::string         key;
    std::vector<double> values;     // 与 level 数同长
    bool                is_int = false;
};
void set_ability_special(YAML::Node& ability_node,
                         const std::vector<AbilitySpecialEntry>& entries);

} // namespace dota::tools
