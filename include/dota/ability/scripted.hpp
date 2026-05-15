#pragma once

#include "dota/ability/ability.hpp"
#include "dota/ability/datadriven.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>

namespace dota {

// 生命周期钩子存在于 Lua 表中的技能子类。位于 `def.script_path` 的 Lua 模块
// 必须 `return` 一个包含以下任意子集的表：
//
//   function M:on_spell_start(caster, target, world) ... end
//   function M:on_channel_think(caster, target, world, dt) ... end
//   function M:on_channel_finish(caster, target, world, interrupted) ... end
//
// 冒号语法（`self:`）是可选的 — C++ 端总是将类似 self 的技能句柄作为第一个参数传递，
// 因此两种风格都可以工作，但为了清晰起见，建议脚本使用 `:` 并访问 `self:get_special`。
class ScriptedAbility : public Ability {
public:
    ScriptedAbility(Unit& caster, const AbilityDef& def, LuaState& lua);

    void on_spell_start(CastContext& ctx) override;
    void on_channel_think(CastContext& ctx, double dt) override;
    void on_channel_finish(CastContext& ctx, bool interrupted) override;

    // 面向 Lua 的辅助函数。通过 "self" 表暴露。
    double get_special(const std::string& key) const;

private:
    void call_hook(const char* name,
                   const std::function<void(sol::protected_function&)>& args_fn);

    LuaState*  lua_;
    sol::table script_;
    sol::table self_;     // 为 Lua 调用包装 `this`

    // 最后一次 CastContext 目标的快照，以便在构造时安装的 Lua 闭包
    // 可以检索当前施法的点/单位。
    Vec2   last_target_point_{};
    Unit*  last_target_unit_{nullptr};
};

} // namespace dota
