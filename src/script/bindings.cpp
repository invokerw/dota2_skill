#include "dota/script/lua_state.hpp"

#include "dota/ability/ability.hpp"
#include "dota/ability/manager.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"

#include <sol/sol.hpp>

#include <cstdio>
#include <memory>

namespace dota {

namespace {

// Lua 可访问的辅助函数

sol::table damage_table(sol::state& lua) {
    sol::table t = lua.create_table();
    t["PHYSICAL"] = static_cast<int>(DamageType::Physical);
    t["MAGICAL"]  = static_cast<int>(DamageType::Magical);
    t["PURE"]     = static_cast<int>(DamageType::Pure);
    return t;
}

sol::table state_table(sol::state& lua) {
    sol::table t = lua.create_table();
    t["STUNNED"]         = static_cast<int>(ModifierState::Stunned);
    t["SILENCED"]        = static_cast<int>(ModifierState::Silenced);
    t["ROOTED"]          = static_cast<int>(ModifierState::Rooted);
    t["DISARMED"]        = static_cast<int>(ModifierState::Disarmed);
    t["HEXED"]           = static_cast<int>(ModifierState::Hexed);
    t["INVISIBLE"]       = static_cast<int>(ModifierState::Invisible);
    t["INVULNERABLE"]    = static_cast<int>(ModifierState::Invulnerable);
    t["MAGIC_IMMUNE"]    = static_cast<int>(ModifierState::MagicImmune);
    t["OUT_OF_GAME"]     = static_cast<int>(ModifierState::OutOfGame);
    t["UNTARGETABLE"]    = static_cast<int>(ModifierState::Untargetable);
    t["NO_UNIT_COLLISION"] = static_cast<int>(ModifierState::NoUnitCollision);
    t["NO_HEALTH_BAR"]   = static_cast<int>(ModifierState::NoHealthBar);
    t["FROZEN"]          = static_cast<int>(ModifierState::Frozen);
    return t;
}

sol::table property_table(sol::state& lua) {
    sol::table t = lua.create_table();
    t["ARMOR_BONUS"]                  = static_cast<int>(ModifierProperty::ArmorBonus);
    t["ARMOR_BONUS_PCT"]              = static_cast<int>(ModifierProperty::ArmorBonusPct);
    t["HEALTH_BONUS"]                 = static_cast<int>(ModifierProperty::HealthBonus);
    t["MANA_BONUS"]                   = static_cast<int>(ModifierProperty::ManaBonus);
    t["ATTACK_DAMAGE_BONUS"]          = static_cast<int>(ModifierProperty::AttackDamageBonus);
    t["ATTACK_DAMAGE_BONUS_PCT"]      = static_cast<int>(ModifierProperty::AttackDamageBonusPct);
    t["ATTACK_SPEED_BONUS_CONSTANT"]  = static_cast<int>(ModifierProperty::AttackSpeedBonusConstant);
    t["MAGIC_RESIST_BONUS"]           = static_cast<int>(ModifierProperty::MagicResistBonus);
    t["INCOMING_DAMAGE_PCT"]          = static_cast<int>(ModifierProperty::IncomingDamagePct);
    t["OUTGOING_DAMAGE_PCT"]          = static_cast<int>(ModifierProperty::OutgoingDamagePct);
    t["MOVE_SPEED_BONUS_CONSTANT"]    = static_cast<int>(ModifierProperty::MoveSpeedBonusConstant);
    t["MOVE_SPEED_BONUS_PCT"]         = static_cast<int>(ModifierProperty::MoveSpeedBonusPct);
    t["HEAL_AMP_PCT"]                 = static_cast<int>(ModifierProperty::HealAmpPct);
    // Phase 0 扩展
    t["EVASION"]                      = static_cast<int>(ModifierProperty::Evasion);
    t["LIFESTEAL_PCT"]                = static_cast<int>(ModifierProperty::LifestealPct);
    t["HEALTH_REGEN"]                 = static_cast<int>(ModifierProperty::HealthRegen);
    t["MANA_REGEN"]                   = static_cast<int>(ModifierProperty::ManaRegen);
    t["SPELL_AMPLIFY_PCT"]            = static_cast<int>(ModifierProperty::SpellAmplifyPct);
    t["STATUS_RESISTANCE_PCT"]        = static_cast<int>(ModifierProperty::StatusResistancePct);
    t["COOLDOWN_REDUCTION_PCT"]       = static_cast<int>(ModifierProperty::CooldownReductionPct);
    t["CAST_RANGE_BONUS"]             = static_cast<int>(ModifierProperty::CastRangeBonus);
    return t;
}

} // namespace

void register_bindings(sol::state& lua, LuaState* owner) {
    // --- Vec2 ---
    // Vec2 是聚合类型(无构造函数重载), 因此暴露工厂函数,
    // 并依赖 sol2 的默认构造函数绑定 `Vec2.new()`
    lua.new_usertype<Vec2>(
        "Vec2",
        sol::call_constructor,
        sol::factories(
            []() { return Vec2{}; },
            [](double x, double y) { return Vec2{x, y}; }),
        "x", &Vec2::x,
        "y", &Vec2::y);

    // --- Unit
    //
    // Lua 持有原始指针(非所有权). 单位由 World 拥有, 在整个 World 生命周期内有效,
    // 因此只要脚本存活, 原始指针就是稳定的. 脚本不能跨 world.reset() 缓存指针.
    lua.new_usertype<Unit>(
        "Unit",
        sol::no_constructor,
        "id",            &Unit::id,
        "name",          [](const Unit& u) { return u.name(); },
        "team",          [](const Unit& u) { return static_cast<int>(u.team()); },
        "alive",         &Unit::alive,
        "position",      &Unit::position,
        "set_position",  &Unit::set_position,
        "issue_move",      [](Unit& u, Vec2 target) { u.issue_move(target); },
        "stop_move",       [](Unit& u) { u.stop_move(); },
        "has_move_target", [](const Unit& u) { return u.move_target().has_value(); },
        "move_target",     [](const Unit& u) { return u.move_target().value_or(Vec2{}); },
        "health",        &Unit::health,
        "mana",          &Unit::mana,
        "max_health",    &Unit::max_health,
        "armor",         &Unit::armor,
        "attack_damage", &Unit::attack_damage,
        "magic_resist",  &Unit::magic_resist,
        "move_speed",    &Unit::move_speed,
        "hull_radius",   &Unit::hull_radius,
        "evasion",       &Unit::evasion,
        "lifesteal_pct", &Unit::lifesteal_pct,
        "health_regen",  &Unit::health_regen,
        "mana_regen",    &Unit::mana_regen,
        "spell_amp_pct", &Unit::spell_amp_pct,
        "status_resist", &Unit::status_resist,
        "can_attack",    &Unit::can_attack,
        "can_cast",      &Unit::can_cast,
        "can_move",      &Unit::can_move,
        "heal",          &Unit::heal,
        "spend_mana",    &Unit::spend_mana,
        "apply_damage",
        [](Unit& u, int type, double amount, sol::optional<Unit*> attacker) {
            return u.apply_damage(static_cast<DamageType>(type), amount,
                                   attacker ? (*attacker)->id() : 0);
        },
        "has_state",
        [](const Unit& u, int state) {
            return u.modifiers().has_state(static_cast<ModifierState>(state));
        },
        "add_stunned",
        [](Unit& u, double duration) {
            u.modifiers().attach(modifiers::make_stunned(u, duration));
        },
        "add_silenced",
        [](Unit& u, double duration) {
            u.modifiers().attach(modifiers::make_silenced(u, duration));
        },
        "add_hexed",
        [](Unit& u, double duration) {
            u.modifiers().attach(modifiers::make_hexed(u, duration));
        },
        "add_periodic_heal",
        [](Unit& u, double heal_per_tick, double interval, double duration) {
            u.modifiers().attach(
                modifiers::make_periodic_heal(u, heal_per_tick, interval, duration));
        },
        "apply_knockback",
        [](Unit& u, Vec2 direction, double distance, double duration,
           sol::optional<int> priority) {
            u.modifiers().attach(modifiers::make_knockback(
                u, direction, distance, duration, priority.value_or(0)));
        },
        "has_modifier",
        [](const Unit& u, const std::string& name) {
            return u.modifiers().find(name) != nullptr;
        },
        "remove_modifier",
        [](Unit& u, const std::string& name) {
            return u.modifiers().remove(name);
        },
        "purge",
        [](Unit& u, sol::optional<sol::table> opts) {
            Unit::PurgeOptions o;
            if (opts) {
                sol::table t = *opts;
                if (t["buffs"].valid())   o.buffs   = t.get_or("buffs", true);
                if (t["debuffs"].valid()) o.debuffs = t.get_or("debuffs", false);
                if (t["strong"].valid())  o.strong  = t.get_or("strong", false);
            }
            u.purge(o);
        },
        // 子技能触发: 按 ability 名查找施法者的技能, 然后 trigger_cast. 返回 bool.
        "cast_ability_no_target",
        [](Unit& caster, const std::string& ability_name) {
            Ability* a = caster.abilities().find(ability_name);
            if (!a || !caster.world()) return false;
            CastTarget t;
            return a->trigger_cast(t, *caster.world()) == CastError::None;
        },
        "cast_ability_on_unit",
        [](Unit& caster, const std::string& ability_name, Unit* target) {
            Ability* a = caster.abilities().find(ability_name);
            if (!a || !caster.world() || !target) return false;
            CastTarget t; t.unit = target;
            return a->trigger_cast(t, *caster.world()) == CastError::None;
        },
        "cast_ability_on_point",
        [](Unit& caster, const std::string& ability_name, Vec2 point) {
            Ability* a = caster.abilities().find(ability_name);
            if (!a || !caster.world()) return false;
            CastTarget t; t.point = point; t.has_point = true;
            return a->trigger_cast(t, *caster.world()) == CastError::None;
        },
        // 通过名字应用注册过的 Lua 修饰器. 第 4 个 params 表可选地携带 duration / stacks.
        "add_modifier",
        [owner](Unit& u, const std::string& mod_name,
                sol::optional<Unit*> source, sol::object /*ability*/,
                sol::optional<sol::table> params) -> Modifier* {
            if (!owner) return nullptr;
            const auto* spec = owner->modifier_registry().find(mod_name);
            if (!spec) {
                owner->report_error("add_modifier", "未注册的修饰器名: " + mod_name);
                return nullptr;
            }
            double duration = -1.0;       // 默认永久
            int    stacks   = 1;
            if (params) {
                sol::table p = *params;
                sol::object od = p["duration"];
                if (od.is<double>())       duration = od.as<double>();
                else if (od.is<int>())     duration = static_cast<double>(od.as<int>());
                sol::object os = p["stacks"];
                if (os.is<int>())          stacks = std::max(1, os.as<int>());
                else if (os.is<double>())  stacks = std::max(1, static_cast<int>(os.as<double>()));
            }
            const EntityId src_id = (source && *source) ? (*source)->id() : kInvalidEntityId;
            auto mod = std::make_unique<ScriptedModifier>(
                u, mod_name, duration, *spec, *owner, src_id, /*ability=*/nullptr);
            if (stacks > 1) mod->set_stack_count(stacks);
            return u.modifiers().attach(std::move(mod));
        });

    // --- Ability ---
    // 暴露给 modifier 钩子使用 (例如 OnAbilityExecuted 事件 ev.ability).
    // 仅暴露只读元数据 + ability_special 查询.
    lua.new_usertype<Ability>(
        "Ability",
        sol::no_constructor,
        "name",       [](const Ability& a) { return a.name(); },
        "level",      &Ability::level,
        "is_passive", &Ability::is_passive,
        "caster",     [](Ability& a) -> Unit* { return &a.caster(); },
        "get_special",
        [](const Ability& a, const std::string& key) -> double {
            const auto& sp = a.ability_special();
            auto it = sp.find(key);
            return it == sp.end() ? 0.0 : it->second.get_float(a.level());
        });

    // --- Modifier ---
    // 给 Lua 脚本拿到的 modifier 句柄 (add_modifier 的返回值, 或 self 在 ScriptedModifier
    // 钩子里用 owner:find_modifier 反查到的实例) 提供基础控制方法.
    lua.new_usertype<Modifier>(
        "Modifier",
        sol::no_constructor,
        "name",               [](const Modifier& m) { return m.name(); },
        "stack_count",        &Modifier::stack_count,
        "set_stack_count",    &Modifier::set_stack_count,
        "refresh",            &Modifier::refresh,
        "duration_remaining", &Modifier::duration_remaining,
        "permanent",          &Modifier::permanent);

    // --- World ---
    lua.new_usertype<World>(
        "World",
        sol::no_constructor,
        "time", &World::time,
        "find_enemies_in_radius",
        [](World& w, Vec2 origin, double radius, int source_team) {
            // sol2 将 std::vector<Unit*> 干净地转换为 Lua 数组表
            return w.find_enemies_in_radius(origin, radius,
                                             static_cast<Team>(source_team));
        },
        "create_thinker",
        [owner](World& w, Vec2 pos, double duration, const std::string& mod_name,
                sol::optional<Unit*> source) -> Unit* {
            Unit* src = source ? *source : nullptr;
            return w.create_thinker(pos, duration, mod_name, owner, src);
        },
        "find_enemies_in_line",
        [](World& w, Vec2 start, Vec2 end, double width, int source_team) {
            return w.find_enemies_in_line(start, end, width,
                                           static_cast<Team>(source_team));
        },
        "find_enemies_in_cone",
        [](World& w, Vec2 origin, Vec2 direction, double length,
           double half_angle_rad, int source_team) {
            return w.find_enemies_in_cone(origin, direction, length, half_angle_rad,
                                           static_cast<Team>(source_team));
        },
        // 直线投射物. params 表字段:
        //   source(Unit), origin(Vec2), direction(Vec2), speed, length, width,
        //   destroy_on_first_hit(bool), on_hit(function(victim, point)), on_finish(function())
        "create_linear_projectile",
        [](World& w, sol::table p) -> Projectile* {
            LinearProjectile::Params params;
            sol::object src = p["source"];
            if (src.is<Unit*>()) {
                Unit* s = src.as<Unit*>();
                params.source_id   = s->id();
                params.source_team = s->team();
            }
            sol::object o_origin = p["origin"];
            if (o_origin.is<Vec2>()) params.origin = o_origin.as<Vec2>();
            sol::object o_dir = p["direction"];
            if (o_dir.is<Vec2>())    params.direction = o_dir.as<Vec2>();
            sol::object o_team = p["source_team"];
            if (o_team.is<int>())    params.source_team = static_cast<Team>(o_team.as<int>());

            params.speed  = p.get_or("speed", 1000.0);
            params.length = p.get_or("length", 1000.0);
            params.width  = p.get_or("width", 100.0);
            params.destroy_on_first_hit = p.get_or("destroy_on_first_hit", false);

            auto proj = std::make_unique<LinearProjectile>(params);
            sol::object on_hit_obj = p["on_hit"];
            if (on_hit_obj.is<sol::protected_function>()) {
                sol::protected_function fn = on_hit_obj;
                proj->set_on_hit([fn](Unit& victim, Vec2 point) {
                    auto r = fn(&victim, point);
                    if (!r.valid()) {
                        sol::error err = r;
                        std::fprintf(stderr, "[lua error] linear on_hit: %s\n", err.what());
                    }
                });
            }
            sol::object on_finish_obj = p["on_finish"];
            if (on_finish_obj.is<sol::protected_function>()) {
                sol::protected_function fn = on_finish_obj;
                proj->set_on_finish([fn]() {
                    auto r = fn();
                    if (!r.valid()) {
                        sol::error err = r;
                        std::fprintf(stderr, "[lua error] linear on_finish: %s\n", err.what());
                    }
                });
            }
            return w.projectiles().spawn(std::move(proj));
        },
        "create_tracking_projectile",
        [](World& w, sol::table p) -> Projectile* {
            TrackingProjectile::Params params;
            sol::object src = p["source"];
            if (src.is<Unit*>()) {
                Unit* s = src.as<Unit*>();
                params.source_id   = s->id();
                params.source_team = s->team();
                params.origin      = s->position();
            }
            sol::object o_origin = p["origin"];
            if (o_origin.is<Vec2>()) params.origin = o_origin.as<Vec2>();
            sol::object tgt = p["target"];
            if (tgt.is<Unit*>()) params.target_id = tgt.as<Unit*>()->id();
            params.speed     = p.get_or("speed", 900.0);
            params.dodgeable = p.get_or("dodgeable", true);

            auto proj = std::make_unique<TrackingProjectile>(params);
            sol::object on_hit_obj = p["on_hit"];
            if (on_hit_obj.is<sol::protected_function>()) {
                sol::protected_function fn = on_hit_obj;
                proj->set_on_hit([fn](Unit& victim, Vec2 point) {
                    auto r = fn(&victim, point);
                    if (!r.valid()) {
                        sol::error err = r;
                        std::fprintf(stderr, "[lua error] tracking on_hit: %s\n", err.what());
                    }
                });
            }
            sol::object on_finish_obj = p["on_finish"];
            if (on_finish_obj.is<sol::protected_function>()) {
                sol::protected_function fn = on_finish_obj;
                proj->set_on_finish([fn]() {
                    auto r = fn();
                    if (!r.valid()) {
                        sol::error err = r;
                        std::fprintf(stderr, "[lua error] tracking on_finish: %s\n", err.what());
                    }
                });
            }
            return w.projectiles().spawn(std::move(proj));
        });

    // 投射物作为 opaque 用户类型暴露(便于 Lua 持有引用).
    lua.new_usertype<Projectile>("Projectile", sol::no_constructor);

    // --- 枚举表(类似 VScripts 暴露 DAMAGE_TYPE_* 的方式)---
    lua["DamageType"]       = damage_table(lua);
    lua["ModifierState"]    = state_table(lua);
    lua["ModifierProperty"] = property_table(lua);

    // 队伍常量(对应 core/types.hpp)
    sol::table team = lua.create_table();
    team["RADIANT"] = static_cast<int>(Team::Radiant);
    team["DIRE"]    = static_cast<int>(Team::Dire);
    team["NEUTRAL"] = static_cast<int>(Team::Neutral);
    lua["Team"] = team;

    // --- LinkLuaModifier 风格注册 ---
    // register_modifier(name, spec_table) -- 注册到该 LuaState 的修饰器中心.
    if (owner) {
        lua["register_modifier"] = [owner](const std::string& name, sol::table spec) {
            owner->modifier_registry().register_modifier(name, spec);
        };
    } else {
        // 没有 owner 时(test_lua_bindings 这样直接注册到 sol::state 的场景),
        // 提供一个 no-op 版本, 避免脚本里 register_modifier 找不到符号.
        lua["register_modifier"] = [](const std::string&, sol::table) {};
    }
}

} // namespace dota
