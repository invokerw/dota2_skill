#include "dota/ability/scripted.hpp"

#include "dota/ability/datadriven.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

namespace dota {

ScriptedAbility::ScriptedAbility(Unit& caster, const AbilityDef& def, LuaState& lua)
    : Ability(def.name, def.behavior, def.target_team, caster), lua_(&lua) {
    set_cast_point(def.cast_point);
    set_backswing(def.backswing);
    set_channel_time(def.channel_time);
    set_cast_range(def.cast_range);
    set_cooldown_levels(def.cooldowns);
    set_mana_cost_levels(def.mana_costs);
    set_ability_special(def.ability_special);

    script_ = lua.load_module(def.script_path);

    // 构建一个轻量的 self 表, Lua 钩子可以接收它作为 `self`. 每个条目都是
    // 捕获 `this` 的闭包. lambda 接受一个前导 sol::object, 这样脚本可以使用
    // 冒号语法(`self:get_special("radius")`)或点语法调用 -- sol2 在冒号调用时
    // 将表作为第一个参数传递, 我们只需忽略它.
    self_ = lua.state().create_table();
    self_["get_special"] = [this](sol::object, const std::string& k) {
        return get_special(k);
    };
    self_["get_caster"]  = [this](sol::object) -> Unit* { return &this->caster(); };
    self_["level"]       = [this](sol::object) { return this->level(); };
    self_["name"]        = this->name();
    self_["target_point"] = [this](sol::object) { return last_target_point_; };
    self_["target_unit"]  = [this](sol::object) -> Unit* { return last_target_unit_; };
}

double ScriptedAbility::get_special(const std::string& key) const {
    const auto& special = ability_special();
    auto it = special.find(key);
    if (it == special.end()) return 0.0;
    return it->second.get_float(level());
}

void ScriptedAbility::call_hook(const char* hook_name,
    const std::function<void(sol::protected_function&)>& args_fn) {
    if (!script_.valid()) return;
    sol::protected_function fn = script_[hook_name];
    if (!fn.valid()) return;
    args_fn(fn);
}

void ScriptedAbility::on_spell_start(CastContext& ctx) {
    last_target_point_ = ctx.target.point;
    last_target_unit_  = ctx.target.unit;
    call_hook("on_spell_start", [&](sol::protected_function& fn) {
        auto r = fn(self_, ctx.caster, ctx.target.unit, ctx.world);
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(std::string(name()) + ".on_spell_start", err.what());
        }
    });
}

void ScriptedAbility::on_channel_think(CastContext& ctx, double dt) {
    last_target_point_ = ctx.target.point;
    last_target_unit_  = ctx.target.unit;
    call_hook("on_channel_think", [&](sol::protected_function& fn) {
        auto r = fn(self_, ctx.caster, ctx.target.unit, ctx.world, dt);
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(std::string(name()) + ".on_channel_think", err.what());
        }
    });
}

void ScriptedAbility::on_channel_finish(CastContext& ctx, bool interrupted) {
    call_hook("on_channel_finish", [&](sol::protected_function& fn) {
        auto r = fn(self_, ctx.caster, ctx.target.unit, ctx.world, interrupted);
        if (!r.valid()) {
            sol::error err = r;
            lua_->report_error(std::string(name()) + ".on_channel_finish", err.what());
        }
    });
}

} // namespace dota
