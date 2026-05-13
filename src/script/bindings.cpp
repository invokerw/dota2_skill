#include "dota/script/lua_state.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <sol/sol.hpp>

namespace dota {

namespace {

// Lua-accessible helpers ----------------------------------------------------

sol::table damage_table(sol::state& lua) {
    sol::table t = lua.create_table();
    t["PHYSICAL"] = static_cast<int>(DamageType::Physical);
    t["MAGICAL"]  = static_cast<int>(DamageType::Magical);
    t["PURE"]     = static_cast<int>(DamageType::Pure);
    return t;
}

sol::table state_table(sol::state& lua) {
    sol::table t = lua.create_table();
    t["STUNNED"]      = static_cast<int>(ModifierState::Stunned);
    t["SILENCED"]     = static_cast<int>(ModifierState::Silenced);
    t["ROOTED"]       = static_cast<int>(ModifierState::Rooted);
    t["DISARMED"]     = static_cast<int>(ModifierState::Disarmed);
    t["HEXED"]        = static_cast<int>(ModifierState::Hexed);
    t["INVISIBLE"]    = static_cast<int>(ModifierState::Invisible);
    t["INVULNERABLE"] = static_cast<int>(ModifierState::Invulnerable);
    t["MAGIC_IMMUNE"] = static_cast<int>(ModifierState::MagicImmune);
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
    return t;
}

} // namespace

void register_bindings(sol::state& lua) {
    // --- Vec2 ---
    // Vec2 is an aggregate (no ctor overloads), so we expose a factory fn and
    // rely on sol2's default-ctor binding for `Vec2.new()`.
    lua.new_usertype<Vec2>(
        "Vec2",
        sol::call_constructor,
        sol::factories(
            []() { return Vec2{}; },
            [](double x, double y) { return Vec2{x, y}; }),
        "x", &Vec2::x,
        "y", &Vec2::y);

    // --- Unit -----------------------------------------------------------
    //
    // Lua keeps raw pointers (non-owning). Units are owned by World for the
    // full World lifetime, so a raw pointer is stable as long as the script
    // is alive. Scripts must not cache pointers across world.reset().
    lua.new_usertype<Unit>(
        "Unit",
        sol::no_constructor,
        "id",            &Unit::id,
        "name",          [](const Unit& u) { return u.name(); },
        "team",          [](const Unit& u) { return static_cast<int>(u.team()); },
        "alive",         &Unit::alive,
        "position",      &Unit::position,
        "set_position",  &Unit::set_position,
        "health",        &Unit::health,
        "mana",          &Unit::mana,
        "max_health",    &Unit::max_health,
        "armor",         &Unit::armor,
        "attack_damage", &Unit::attack_damage,
        "magic_resist",  &Unit::magic_resist,
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
        "remove_modifier",
        [](Unit& u, const std::string& name) {
            return u.modifiers().remove(name);
        });

    // --- World ---
    lua.new_usertype<World>(
        "World",
        sol::no_constructor,
        "time", &World::time,
        "find_enemies_in_radius",
        [](World& w, Vec2 origin, double radius, int source_team) {
            // sol2 converts std::vector<Unit*> to a Lua array table cleanly.
            return w.find_enemies_in_radius(origin, radius,
                                             static_cast<Team>(source_team));
        });

    // --- Enums as tables (closer to how VScripts exposes DAMAGE_TYPE_*) ---
    lua["DamageType"]       = damage_table(lua);
    lua["ModifierState"]    = state_table(lua);
    lua["ModifierProperty"] = property_table(lua);

    // Team constants (match core/types.hpp).
    sol::table team = lua.create_table();
    team["RADIANT"] = static_cast<int>(Team::Radiant);
    team["DIRE"]    = static_cast<int>(Team::Dire);
    team["NEUTRAL"] = static_cast<int>(Team::Neutral);
    lua["Team"] = team;
}

} // namespace dota
