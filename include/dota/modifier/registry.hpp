#pragma once

#include "dota/modifier/enums.hpp"

#include <sol/sol.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dota {

// LinkLuaModifier 风格的全局修饰器注册中心.
//
// Lua 端通过 `register_modifier(name, spec_table)` 注册一个规范表, 运行时
// 可通过名字 `unit:add_modifier(name, source, ability, params)` 实例化为
// `ScriptedModifier`. 注册中心生命周期与 LuaState 绑定.
//
// 编译期把 Lua 表里的纯静态字段抽取成 `CompiledSpec`, 使每次实例化无需重复扫描表.
// 动态属性(值由 Lua 函数返回)按需在属性查询时调用.
class LuaModifierRegistry {
public:
    struct PropEntry {
        ModifierProperty prop;
        double           constant = 0.0;
        bool             dynamic  = false;        // 调用 spec.table[fn_name](self, owner)
        std::string      fn_name;                 // 仅在 dynamic=true 时有效
    };

    struct CompiledSpec {
        std::string   name;
        sol::table    table;
        std::uint32_t static_state_mask = 0;
        bool          has_check_state   = false;  // 优先调用 CheckState 回调
        std::vector<PropEntry> properties;
        double        think_interval   = 0.0;
        bool          is_purgable      = true;
        bool          is_dispellable   = true;
        bool          is_debuff        = false;
        bool          is_motion_ctrl   = false;
        int           motion_priority  = 0;
        bool          remove_on_death  = true;
    };

    // 注册(或覆盖). Lua 端调用.
    void register_modifier(std::string name, sol::table spec);

    bool contains(const std::string& name) const;

    // 返回 nullptr 当未注册或编译失败.
    const CompiledSpec* find(const std::string& name) const;

private:
    void compile(CompiledSpec& out, const std::string& name, sol::table spec);

    std::unordered_map<std::string, CompiledSpec> compiled_;
};

} // namespace dota
