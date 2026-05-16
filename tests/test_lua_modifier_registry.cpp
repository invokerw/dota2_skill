// Phase 2：LinkLuaModifier 风格注册中心单测
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {
UnitStats stats() {
    UnitStats s;
    s.max_health = 1000.0;
    s.attack_damage = 100.0;
    s.magic_resist  = 0.0;
    return s;
}
} // namespace

TEST(LuaRegistry, RegisterAndAddModifierProvidesEvasion) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});

    // 加载 modifier_test_evasion.lua（执行 register_modifier）
    sol::protected_function_result r = lua.state().safe_script_file(
        std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_evasion.lua",
        &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());

    EXPECT_TRUE(lua.modifier_registry().contains("modifier_test_evasion"));

    // 通过编译后的 spec 直接构造（绕过 binding，便于 C++ 端验证）
    const auto* spec = lua.modifier_registry().find("modifier_test_evasion");
    ASSERT_NE(spec, nullptr);
    hero->modifiers().attach(std::make_unique<ScriptedModifier>(
        *hero, "modifier_test_evasion", -1.0, *spec, lua));

    EXPECT_NEAR(hero->evasion(), 0.25, 1e-9);
}

TEST(LuaRegistry, OnIntervalThinkFiresPeriodically) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});

    sol::protected_function_result r = lua.state().safe_script_file(
        std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_dot.lua",
        &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());

    const auto* spec = lua.modifier_registry().find("modifier_test_dot");
    ASSERT_NE(spec, nullptr);
    hero->modifiers().attach(std::make_unique<ScriptedModifier>(
        *hero, "modifier_test_dot", 5.0, *spec, lua));

    const double hp_before = hero->health();
    w.advance(3.05);   // ~3 个 tick interval
    const double dealt = hp_before - hero->health();
    // 每秒 50 magical（魔抗 0），3 次 tick → 150
    EXPECT_NEAR(dealt, 150.0, 1.0);
}

TEST(LuaRegistry, IsPurgableFlagPropagates) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});

    sol::protected_function_result r = lua.state().safe_script_file(
        std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_evasion.lua",
        &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());

    const auto* spec = lua.modifier_registry().find("modifier_test_evasion");
    ASSERT_NE(spec, nullptr);
    auto* mod = hero->modifiers().attach(std::make_unique<ScriptedModifier>(
        *hero, "modifier_test_evasion", -1.0, *spec, lua));

    EXPECT_FALSE(mod->is_purgable()); // spec 写明 IsPurgable=false
    EXPECT_FALSE(mod->is_debuff());
}
