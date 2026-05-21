#pragma once

// 扫描 data/scripts/modifiers/*.lua, 用文本匹配抽出 register_modifier
// 调用的第 1 个字符串实参 (modifier name). 不实际执行 Lua, 因此对语法错误
// 的文件也能给出 "暴露了哪些 name" 的近似答案.

#include <filesystem>
#include <string>
#include <vector>

namespace dota::tools {

struct ModifierSourceInfo {
    std::filesystem::path file_path;     // 绝对路径
    std::string           file_stem;     // 不含扩展名
    std::vector<std::string> register_names;  // 文件内所有 register_modifier 的 name
};

// 扫描 dir 下所有 *.lua. 不递归.
std::vector<ModifierSourceInfo>
scan_modifier_dir(const std::filesystem::path& dir);

// 从单个文件文本中抽出全部 register_modifier("<name>", ...) 的 name. 跳过
// 行级 -- 注释; 不做完整 Lua parser, 不识别多行注释 / 变量名当作 name 的
// 极端写法 -- 这里只服务约定俗成的注册形式.
std::vector<std::string>
extract_register_names(const std::string& source);

} // namespace dota::tools
