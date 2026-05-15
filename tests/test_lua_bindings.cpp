#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats stats() {
    UnitStats s;
    s.max_health    = 1000.0;
    s.max_mana      = 500.0;
    s.magic_resist  = 0.25;
    s.attack_damage = 50.0;
    return s;
}

} // namespace

// 健全性检查：LuaState 打开并暴露我们的枚举表
TEST(LuaBindings, EnumTablesArePresent) {
    LuaState lua;
    sol::state& s = lua.state();

    EXPECT_TRUE(s["DamageType"].valid());
    EXPECT_TRUE(s["ModifierState"].valid());
    EXPECT_TRUE(s["ModifierProperty"].valid());
    EXPECT_TRUE(s["Team"].valid());

    sol::table dmg = s["DamageType"];
    EXPECT_EQ(dmg["PHYSICAL"].get<int>(), static_cast<int>(DamageType::Physical));
    EXPECT_EQ(dmg["MAGICAL"].get<int>(),  static_cast<int>(DamageType::Magical));
    EXPECT_EQ(dmg["PURE"].get<int>(),     static_cast<int>(DamageType::Pure));
}

TEST(LuaBindings, UnitMethodsReachable) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {10.0, 20.0});

    lua.state()["hero"] = hero;

    const char* src = R"(
        result_name   = hero:name()
        result_team   = hero:team()
        result_health = hero:health()
        result_x      = hero:position().x
    )";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid()) << sol::error(r).what();

    EXPECT_EQ(lua.state()["result_name"].get<std::string>(),  "Hero");
    EXPECT_EQ(lua.state()["result_team"].get<int>(),          static_cast<int>(Team::Radiant));
    EXPECT_DOUBLE_EQ(lua.state()["result_health"].get<double>(), hero->max_health());
    EXPECT_DOUBLE_EQ(lua.state()["result_x"].get<double>(),   10.0);
}

TEST(LuaBindings, ApplyDamageFromLuaUsesFullPipeline) {
    LuaState lua;
    World w;
    auto* hero  = w.spawn("Hero",  Team::Radiant, stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    stats(), {100.0, 0.0});

    lua.state()["attacker"] = hero;
    lua.state()["victim"]   = enemy;

    // 200 魔法伤害 → 经过 25% 魔法抗性后 150
    const char* src = R"(
        victim:apply_damage(DamageType.MAGICAL, 200.0, attacker)
    )";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid()) << sol::error(r).what();

    EXPECT_NEAR(stats().max_health - enemy->health(), 150.0, 0.5);
}

TEST(LuaBindings, StateHelpersFlipBitmask) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});
    lua.state()["hero"] = hero;

    const char* src = R"(
        hero:add_stunned(1.5)
        hero:add_silenced(2.5)
        stunned  = hero:has_state(ModifierState.STUNNED)
        silenced = hero:has_state(ModifierState.SILENCED)
    )";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid()) << sol::error(r).what();

    EXPECT_TRUE(lua.state()["stunned"].get<bool>());
    EXPECT_TRUE(lua.state()["silenced"].get<bool>());
    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Stunned));
    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Silenced));
}

TEST(LuaBindings, FindEnemiesInRadius) {
    LuaState lua;
    World w;
    auto* hero  = w.spawn("Hero",  Team::Radiant, stats(), {0.0, 0.0});
    auto* e1    = w.spawn("E1",    Team::Dire,    stats(), {100.0, 0.0});
    auto* e2    = w.spawn("E2",    Team::Dire,    stats(), {500.0, 0.0});
    auto* ally  = w.spawn("Ally",  Team::Radiant, stats(), {50.0, 0.0});

    lua.state()["world"] = &w;
    lua.state()["hero"]  = hero;

    const char* src = R"(
        enemies = world:find_enemies_in_radius(hero:position(), 200.0, hero:team())
        count = #enemies
    )";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid()) << sol::error(r).what();

    EXPECT_EQ(lua.state()["count"].get<int>(), 1);
    (void)e1; (void)e2; (void)ally;
}
