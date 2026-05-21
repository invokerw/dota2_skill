#pragma once

// 修饰器源文件 (Lua) 的文件级 CRUD. 不解析 Lua 表 -- 复杂的 spec 编辑由
// 用户在外部编辑器里写, IDE 端只负责: 用模板新建 / 拷贝 / 改名 / 走 trash.
//
// "modifier name" 指 register_modifier("<name>", ...) 调用里的字符串. 文件
// 默认按 "name == file_stem" 命名约定来管理.

#include "dota/tools/trash.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dota::tools {

enum class ModifierTemplate {
    Empty,           // 仅 IsHidden / IsDebuff
    DoT,             // ThinkInterval + OnIntervalThink
    Shield,          // OnPreTakeDamage 吸收
    AoEThinker,      // ThinkInterval + 注释占位 (供 Thinker 调用)
    MotionController // IsMotionController + OnMotionTick
};

// 校验 "modifier_*" 风格的 name. 与英雄 stem 不同, modifier name 通常以
// modifier_ 起头, 但本工具不强制 -- 仅要求 [a-z0-9_], 不空, 不以 _ 起头.
bool is_valid_modifier_name(std::string_view name);

// data/scripts/modifiers/<name>.lua. 不存在则返回空 path.
std::filesystem::path modifier_file_path(
    const std::filesystem::path& data_root, std::string_view name);

// 用模板新建. 若文件已存在抛 std::runtime_error.
std::filesystem::path create_modifier(
    const std::filesystem::path& data_root,
    const std::string& name,
    ModifierTemplate kind);

// 拷贝 src.lua 到 dst.lua, 并把文件内全部 register_modifier("src", ...) 的
// name 改写为 dst (同时维持文件中其他出现 src 字符串的位置不动 -- 仅替换
// register_modifier 第 1 参). dst 文件已存在抛.
std::filesystem::path duplicate_modifier(
    const std::filesystem::path& data_root,
    const std::string& src_name,
    const std::string& dst_name);

// 重命名: 改文件名 + 改 register_modifier 第 1 参. dst 已存在抛, src 不存在抛.
std::filesystem::path rename_modifier(
    const std::filesystem::path& data_root,
    const std::string& src_name,
    const std::string& dst_name);

// 把 modifier 文件移到 .trash/. 文件不存在抛.
TrashRecord delete_modifier(
    const std::filesystem::path& data_root,
    const std::string& name);

// 测试 / UI 共享: 模板的 lua 源代码 (内嵌 name).
std::string render_modifier_template(const std::string& name,
                                      ModifierTemplate kind);

// 改写 src 中 register_modifier("src_name", ...) 为 ("dst_name", ...).
// 只替换字符串字面量中 src_name 完整匹配的位置. 返回新文本.
std::string rewrite_register_name(const std::string& source,
                                   const std::string& src_name,
                                   const std::string& dst_name);

} // namespace dota::tools
