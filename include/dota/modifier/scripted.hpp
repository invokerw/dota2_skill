#pragma once

#include "dota/modifier/modifier.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>
#include <string>

namespace dota {

// 声明的属性和事件钩子位于 Lua 表中的修饰器。
// Lua 模块必须 `return` 一个如下形状的表：
//
//   local M = {}
//   M.properties = { { ModifierProperty.ARMOR_BONUS, 5 }, ... }
//   M.states     = { ModifierState.STUNNED, ... }
//   M.think_interval = 0.5
//   function M:on_interval_think(owner) ... end
//   function M:on_pre_take_damage(owner, amount, type) -> absorb ... end
//   return M
//
// 任何部分都可以省略。如果 `properties` 通过函数（而非数字）引用值，
// 则在每次聚合查询时重新调用，以便脚本可以生成堆叠计数感知的值。
class ScriptedModifier : public Modifier {
public:
    ScriptedModifier(Unit& owner,
                     std::string name,
                     double duration,
                     sol::table table,
                     LuaState& lua);

    std::vector<ModifierProvidedProperty> declared_properties() const override;
    std::uint32_t declared_states() const override;

    void on_created() override;
    void on_destroyed() override;
    void on_interval_think() override;
    void on_pre_take_damage(PreTakeDamageEvent& ev) override;

private:
    LuaState*  lua_;
    sol::table table_;
    std::uint32_t state_mask_cache_ = 0;
};

} // namespace dota
