#pragma once

#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>
#include <string>

namespace dota {

class Ability;

// Lua 端的修饰器实例，与一个 LuaModifierRegistry::CompiledSpec 关联。
// 每次属性查询/事件钩子触发时，按需调用 spec 表上的 Lua 函数。
//
// 兼容旧测试：保留接受裸 sol::table 的构造函数，会临时编译一份 CompiledSpec。
class ScriptedModifier : public Modifier {
public:
    // 旧式（test_lua_modifier 使用）：直接传入 Lua 表。spec 中字段名遵循新约定
    // （IsHidden、States、Properties、OnPreTakeDamage 等）；为兼容更早的旧脚本，
    // 同时支持小写的 states / properties / think_interval / on_*。
    ScriptedModifier(Unit& owner,
                     std::string name,
                     double duration,
                     sol::table table,
                     LuaState& lua);

    // 新式：直接持有已编译的 spec 引用（注册中心管理生命周期）。
    ScriptedModifier(Unit& owner,
                     std::string name,
                     double duration,
                     const LuaModifierRegistry::CompiledSpec& spec,
                     LuaState& lua,
                     Unit* source     = nullptr,
                     Ability* ability = nullptr);

    std::vector<ModifierProvidedProperty> declared_properties() const override;
    std::uint32_t declared_states() const override;

    void on_created() override;
    void on_destroyed() override;
    void on_stack_changed(int old_count, int new_count) override;
    void on_interval_think() override;
    void on_pre_take_damage(PreTakeDamageEvent& ev) override;
    void on_post_take_damage(PostTakeDamageEvent& ev) override;
    void on_pre_take_heal(PreTakeHealEvent& ev) override;
    void on_post_take_heal(PostTakeHealEvent& ev) override;
    void on_motion_tick(double dt) override;

    bool is_debuff() const override { return is_debuff_; }

    // 暴露给 Lua self.GetCaster / GetAbility 类方法用。
    Unit*    source()  const { return source_; }
    Ability* ability() const { return ability_; }

private:
    void apply_compiled_flags(const LuaModifierRegistry::CompiledSpec& spec);

    LuaState*  lua_;
    sol::table table_;                               // 实例独有的可写 self 表
    sol::table spec_table_;                          // 共享的 spec 表（方法源）
    const LuaModifierRegistry::CompiledSpec* compiled_{nullptr};
    std::uint32_t state_mask_cache_ = 0;             // 静态状态位
    bool       is_debuff_ = false;
    Unit*      source_  = nullptr;
    Ability*   ability_ = nullptr;
};

} // namespace dota
