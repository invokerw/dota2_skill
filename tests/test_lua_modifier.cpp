#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/registry.hpp"
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

// 加载并注册 modifier_test_shield.lua（脚本顶层调用 register_modifier），
// 然后取出已编译的 CompiledSpec 并实例化挂到 hero 上。
const LuaModifierRegistry::CompiledSpec*
load_shield_spec(LuaState& lua) {
    sol::protected_function_result r = lua.state().safe_script_file(
        std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_shield.lua",
        &sol::script_pass_on_error);
    if (!r.valid()) return nullptr;
    return lua.modifier_registry().find("modifier_test_shield");
}

} // namespace

// 验证 Lua modifier 声明的属性流入单位的聚合魔法抗性。
TEST(LuaModifier, DeclaredPropertyAggregates) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    const auto* spec = load_shield_spec(lua);
    ASSERT_NE(spec, nullptr);

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, *spec, lua));

    // 基础 0.25 + 0.10 → 0.35
    EXPECT_NEAR(hero->magic_resist(), 0.35, 1e-6);
}

TEST(LuaModifier, OnPreTakeDamageAbsorbsMagical) {
    LuaState lua;
    World w;
    auto* hero = w.spawn("Hero", Team::Radiant, stats(), {0.0, 0.0});

    const auto* spec = load_shield_spec(lua);
    ASSERT_NE(spec, nullptr);

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, *spec, lua));

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

    const auto* spec = load_shield_spec(lua);
    ASSERT_NE(spec, nullptr);

    hero->modifiers().attach(
        std::make_unique<ScriptedModifier>(
            *hero, "modifier_test_shield", 10.0, *spec, lua));

    const double hp_before = hero->health();
    // 100 物理伤害 → 护甲为 0，所以完整 100 穿透
    hero->apply_damage(DamageType::Physical, 100.0, 0);
    const double dealt = hp_before - hero->health();
    EXPECT_NEAR(dealt, 100.0, 0.5);
}
