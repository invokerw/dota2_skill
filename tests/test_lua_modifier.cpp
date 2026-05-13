#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace dota;

namespace {

UnitStats stats() {
    UnitStats s;
    s.max_health   = 1000.0;
    s.max_mana     = 500.0;
    s.magic_resist = 0.25;
    return s;
}

} // namespace

// Attach a ScriptedModifier built from the Lua test shield module and verify
// that declared properties flow into the unit's aggregated magic resist.
TEST(LuaModifier, DeclaredPropertyAggregates) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    sol::table tbl = lua.load_module("modifiers/modifier_test_shield.lua");
    ASSERT_TRUE(tbl.valid());

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, tbl, lua));

    // Base 0.25 + 0.10 → 0.35
    EXPECT_NEAR(hero->magic_resist(), 0.35, 1e-6);
}

TEST(LuaModifier, OnPreTakeDamageAbsorbsMagical) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    sol::table tbl = lua.load_module("modifiers/modifier_test_shield.lua");
    ASSERT_TRUE(tbl.valid());

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, tbl, lua));

    const double hp_before = hero->health();
    // 300 magical; 200 absorbed by shield (pre-resist). Remaining 100, then
    // 0.35 magic resist → 65 applied.
    hero->apply_damage(DamageType::Magical, 300.0, 0);
    const double dealt = hp_before - hero->health();
    EXPECT_NEAR(dealt, 65.0, 0.5);
}

TEST(LuaModifier, OnPreTakeDamageIgnoresPhysical) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    sol::table tbl = lua.load_module("modifiers/modifier_test_shield.lua");
    ASSERT_TRUE(tbl.valid());

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, tbl, lua));

    const double hp_before = hero->health();
    // 100 physical → armor is 0, so full 100 goes through.
    hero->apply_damage(DamageType::Physical, 100.0, 0);
    const double dealt = hp_before - hero->health();
    EXPECT_NEAR(dealt, 100.0, 0.5);
}
