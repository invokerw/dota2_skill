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

// 附加从 Lua 测试护盾模块构建的 ScriptedModifier，并验证
// 声明的属性流入单位的聚合魔法抗性
TEST(LuaModifier, DeclaredPropertyAggregates) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    sol::table tbl = lua.load_module("modifiers/modifier_test_shield.lua");
    ASSERT_TRUE(tbl.valid());

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, tbl, lua));

    // 基础 0.25 + 0.10 → 0.35
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
    // 300 魔法伤害；护盾吸收 200（抗性前）。剩余 100，然后
    // 0.35 魔法抗性 → 实际造成 65
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
    // 100 物理伤害 → 护甲为 0，所以完整 100 穿透
    hero->apply_damage(DamageType::Physical, 100.0, 0);
    const double dealt = hp_before - hero->health();
    EXPECT_NEAR(dealt, 100.0, 0.5);
}
