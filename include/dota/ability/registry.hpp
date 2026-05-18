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
    // 加载包含 `abilities: ` 列表和/或 `hero: ` 块的 YAML 文件.
    // 返回注册的技能数量. 输入格式错误时抛出 std::runtime_error.
    std::size_t load_file(const std::string& path);

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
