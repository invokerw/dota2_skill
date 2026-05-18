#include "dota/ability/datadriven.hpp"

#include "dota/ability/behavior.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <cctype>
#include <charconv>
#include <stdexcept>

namespace dota {

double resolve_expression(const std::string& expr,
                          const AbilitySpecial& special,
                          int level) {
    if (expr.empty()) return 0.0;
    if (expr.front() == '%') {
        const std::string key(expr.begin() + 1, expr.end());
        auto it = special.find(key);
        if (it == special.end()) {
            throw std::runtime_error("unknown ability_special key: " + key);
        }
        return it->second.get_float(level);
    }
    // 纯数字字面量
    try {
        return std::stod(expr);
    } catch (...) {
        throw std::runtime_error("cannot parse expression: " + expr);
    }
}

namespace {

Unit* pick_target(ActionTargetSpec spec, const CastContext& ctx) {
    switch (spec) {
        case ActionTargetSpec::Caster: return ctx.caster;
        case ActionTargetSpec::Target: return ctx.target.unit;
    }
    return nullptr;
}

void apply_action(const ActionDamage& a, const CastContext& ctx,
                  const AbilitySpecial& special, int level) {
    Unit* victim = pick_target(a.target, ctx);
    if (!victim || !victim->alive()) return;
    const double amount = resolve_expression(a.amount, special, level);
    victim->apply_damage(a.type, amount, ctx.caster ? ctx.caster->id() : 0);
}

void apply_action(const ActionHeal& a, const CastContext& ctx,
                  const AbilitySpecial& special, int level) {
    Unit* target = pick_target(a.target, ctx);
    if (!target || !target->alive()) return;
    const double amount = resolve_expression(a.amount, special, level);
    target->heal(amount);
}

void apply_action(const ActionApplyModifier& a, const CastContext& ctx,
                  const AbilitySpecial& special, int level) {
    Unit* target = pick_target(a.target, ctx);
    if (!target || !target->alive()) return;

    const double duration = a.duration.empty()
                                ? -1.0
                                : resolve_expression(a.duration, special, level);

    // 通过名称识别的通用库修饰器. 未知名称会抛出异常 -- 加载器应该尽早捕获.
    // 第 4 阶段将通过 Lua 扩展此功能.
    const std::string& n = a.modifier_name;
    if (n == "modifier_stunned") {
        target->modifiers().attach(modifiers::make_stunned(*target, duration));
    } else if (n == "modifier_silenced") {
        target->modifiers().attach(modifiers::make_silenced(*target, duration));
    } else if (n == "modifier_rooted") {
        target->modifiers().attach(modifiers::make_rooted(*target, duration));
    } else if (n == "modifier_hexed") {
        target->modifiers().attach(modifiers::make_hexed(*target, duration));
    } else if (n == "modifier_invisible") {
        target->modifiers().attach(modifiers::make_invisible(*target, duration));
    } else if (n == "modifier_magic_immune") {
        target->modifiers().attach(modifiers::make_magic_immune(*target, duration));
    } else {
        throw std::runtime_error("unknown modifier name: " + n);
    }
}

} // namespace

DataDrivenAbility::DataDrivenAbility(Unit& caster, const AbilityDef& def)
    : Ability(def.name, def.behavior, def.target_team, caster)
    , actions_(def.on_spell_start) {
    set_cast_point(def.cast_point);
    set_backswing(def.backswing);
    set_channel_time(def.channel_time);
    set_cast_range(def.cast_range);
    set_cooldown_levels(def.cooldowns);
    set_mana_cost_levels(def.mana_costs);
    set_ability_special(def.ability_special);
}

void DataDrivenAbility::on_spell_start(CastContext& ctx) {
    const int lvl = level();
    for (const auto& action : actions_) {
        std::visit([&](const auto& a) {
            apply_action(a, ctx, ability_special(), lvl);
        }, action);
    }
}

} // namespace dota
