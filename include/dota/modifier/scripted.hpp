#pragma once

#include "dota/modifier/modifier.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>
#include <string>

namespace dota {

// Modifier whose declared properties and event hooks live in a Lua table.
// The Lua module must `return` a table of shape:
//
//   local M = {}
//   M.properties = { { ModifierProperty.ARMOR_BONUS, 5 }, ... }
//   M.states     = { ModifierState.STUNNED, ... }
//   M.think_interval = 0.5
//   function M:on_interval_think(owner) ... end
//   function M:on_pre_take_damage(owner, amount, type) -> absorb ... end
//   return M
//
// Any section may be omitted. If `properties` references a value by function
// (rather than number), it is re-invoked on every aggregate query so scripts
// can produce stack-count-aware values.
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
