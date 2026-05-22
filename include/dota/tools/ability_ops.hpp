#pragma once

// Ability 文件级 CRUD: 直接对 data/abilities/<name>.yaml 操作.
//
// 一个 ability 是 hero 共享资源 -- 多个 hero 可引用同一个 ability. 因此:
// - rename_ability_file: 改文件 stem + 内部 name 字段, 同时扫所有 hero yaml
//   把引用 <src> 替换为 <dst>
// - delete_ability_file: 任何 hero 还在引用就拒绝 (调用方应先调
//   `heroes_referencing` 检查)
//
// 还有为 hero ability 引用列表 (abilities: [name1, name2, ...]) 提供的小工具:
// hero_add_ability_ref / hero_remove_ability_ref_at / hero_move_ability_ref.
//
// 删除走 trash. 创建/重命名/复制失败时尽量回滚.

#include "dota/tools/trash.hpp"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dota::tools {

// 校验 ability 名: 非空, 仅 [a-z0-9_], 不以 _ 开头.
bool is_valid_ability_name(std::string_view s);

// data/abilities/<name>.yaml 路径 (不要求存在).
std::filesystem::path ability_file_path(
    const std::filesystem::path& data_root, std::string_view name);

// 扫描 data/heroes/, 返回所有引用 <ability_name> 的 hero yaml 的 stem.
// 用来在 delete / rename 前给用户提示.
std::vector<std::string> heroes_referencing(
    const std::filesystem::path& data_root, std::string_view ability_name);

// 用模板创建一个 ability_datadriven yaml. 文件已存在抛.
std::filesystem::path create_datadriven_ability_file(
    const std::filesystem::path& data_root, const std::string& name);

// 用模板创建一个 ability_lua yaml + lua 脚本占位. ability yaml 引用
// "abilities/<script_filename>". script_filename 默认为 "<name>.lua".
// 任一文件已存在抛.
std::filesystem::path create_lua_ability_file(
    const std::filesystem::path& data_root,
    const std::string& name,
    const std::string& script_filename = {});

// 拷贝 ability yaml: 改 stem + 内部 name. 不修改 hero 引用 (复制本身不影响
// 引用方). dst 已存在抛.
std::filesystem::path duplicate_ability_file(
    const std::filesystem::path& data_root,
    const std::string& src_name,
    const std::string& dst_name);

// 重命名 ability yaml: 改 stem + 内部 name. 同步把所有 hero yaml 中
// 引用 <src_name> 的 scalar 改成 <dst_name>. 返回被同步的 hero stem 列表.
// dst 已存在抛, src 不存在抛.
struct RenameAbilityResult {
    std::filesystem::path        new_yaml_path;
    std::vector<std::string>     updated_hero_stems;
};
RenameAbilityResult rename_ability_file(
    const std::filesystem::path& data_root,
    const std::string& src_name,
    const std::string& dst_name);

// 删除 ability yaml + (若 ability_lua) 对应脚本. 若有 hero 引用则抛
// std::runtime_error (异常消息列出引用 hero stem).
struct DeleteAbilityResult {
    TrashRecord                 yaml_record;
    std::optional<TrashRecord>  script_record;
};
DeleteAbilityResult delete_ability_file(
    const std::filesystem::path& data_root, const std::string& name);

// --- hero ability 引用列表编辑 (operate on hero yaml root: abilities: [...]) ---

// 在 abilities 末尾追加 <name>; 重复抛.
void hero_add_ability_ref(YAML::Node& hero_root, const std::string& name);

// 删除 abilities[idx]; 越界抛.
void hero_remove_ability_ref_at(YAML::Node& hero_root, std::size_t index);

// 把 abilities[from] 移到 abilities[to] 位置. 越界抛.
void hero_move_ability_ref(YAML::Node& hero_root,
                            std::size_t from, std::size_t to);

// 在 hero yaml 的 abilities 列表中找 <name> 引用的索引, 找不到返回 -1.
int hero_find_ability_ref(const YAML::Node& hero_root, std::string_view name);

// --- ability_special 编辑帮助 (作用在单 ability yaml 的 root) ---

struct AbilitySpecialEntry {
    std::string         key;
    std::vector<double> values;
    bool                is_int = false;
};
void set_ability_special(YAML::Node& ability_root,
                          const std::vector<AbilitySpecialEntry>& entries);

// --- 工具子函数 (主要给 UI 拼路径用) ---

// 写一份 ability_lua 模板 lua 文件. 文件已存在抛.
std::filesystem::path write_lua_ability_template(
    const std::filesystem::path& data_root, const std::string& filename);

} // namespace dota::tools
