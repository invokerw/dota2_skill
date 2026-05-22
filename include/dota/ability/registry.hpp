#pragma once

#include "dota/ability/datadriven.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace dota {

class Ability;
class LuaState;
class Unit;

// 存储按名称索引的已解析 AbilityDef. 加载 YAML 文件会注册
// 其中的每个技能. 使用 `instantiate(name, caster)` 构造运行时 Ability.
class AbilityRegistry {
public:
    // 加载 YAML 文件: 兼容两种格式
    //   1. 旧格式: 顶层含 `abilities: [...]` 序列 (内嵌在 hero yaml)
    //   2. 新格式: 顶层就是单个 ability 字段 (独立 ability yaml)
    // 返回注册的技能数量. 输入格式错误时抛出 std::runtime_error.
    std::size_t load_file(const std::string& path);

    // 扫描目录, 把每个 .yaml/.yml 当作单 ability 文件加载. 子目录递归.
    // 返回新注册的技能数量.
    std::size_t load_dir(const std::string& dir);

    // 读 hero yaml 的 `abilities: [name1, name2, ...]` 引用列表, 然后
    // 在 abilities_dir 里 load 这些名字对应的 <name>.yaml 文件. 找不到
    // 的引用抛 std::runtime_error.
    //
    // abilities_dir 留空时, 从 hero yaml 的兄弟目录 ../abilities 推断
    // (适配标准布局 data/heroes + data/abilities).
    //
    // 返回新注册的技能数量.
    std::size_t load_hero(const std::string& hero_yaml,
                          const std::string& abilities_dir = "");

    // 直接注册已解析的定义(绕过 YAML -- 对测试有用).
    void register_def(AbilityDef def);

    const AbilityDef* find(const std::string& name) const;

    // 构造运行时 Ability(根据 def.base_class 决定是 DataDriven 还是 Scripted)
    // 并将其附加到施法者. 成功时返回非拥有指针, 如果技能未注册或
    // 缺少所需的 Lua 支持则返回 nullptr.
    //
    // 对于基于 Lua 的技能, 调用者必须提供 LuaState. 如果注册表通过
    // `set_lua()` 设置了默认 LuaState, 则使用该 LuaState.
    Ability* instantiate(const std::string& name, Unit& caster,
                         LuaState* lua = nullptr);

    void      set_lua(LuaState* lua) { default_lua_ = lua; }
    LuaState* lua() const            { return default_lua_; }

private:
    std::unordered_map<std::string, AbilityDef> defs_;
    LuaState* default_lua_ = nullptr;
};

} // namespace dota
