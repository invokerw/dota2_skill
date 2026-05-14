#pragma once

#include "dota/ability/ability.hpp"
#include "dota/ability/datadriven.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>

namespace dota {

// Ability subclass whose lifecycle hooks live in a Lua table. The Lua module
// at `def.script_path` must `return` a table with any subset of:
//
//   function M:on_spell_start(caster, target, world) ... end
//   function M:on_channel_think(caster, target, world, dt) ... end
//   function M:on_channel_finish(caster, target, world, interrupted) ... end
//
// The colon syntax (`self:`) is optional — the C++ side always passes a
// self-like ability handle as the first arg so either style works, but for
// clarity scripts are recommended to use `:` and access `self:get_special`.
class ScriptedAbility : public Ability {
public:
    ScriptedAbility(Unit& caster, const AbilityDef& def, LuaState& lua);

    void on_spell_start(CastContext& ctx) override;
    void on_channel_think(CastContext& ctx, double dt) override;
    void on_channel_finish(CastContext& ctx, bool interrupted) override;

    // Lua-facing helpers. Exposed via the "self" table.
    double get_special(const std::string& key) const;

private:
    void call_hook(const char* name,
                   const std::function<void(sol::protected_function&)>& args_fn);

    LuaState*  lua_;
    sol::table script_;
    sol::table self_;     // wraps `this` for Lua calls

    // Snapshot of the last CastContext target so Lua closures installed at
    // construction can retrieve the point/unit for the current cast.
    Vec2   last_target_point_{};
    Unit*  last_target_unit_{nullptr};
};

} // namespace dota
