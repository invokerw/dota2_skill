#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 500.0;
    s.magic_resist     = 0.25;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    return s;
}

Ability* attach_blade_fury(AbilityRegistry& reg, Unit& caster) {
    reg.load_file(std::string(kDataDir) + "/heroes/juggernaut.yaml");
    return reg.instantiate("juggernaut_blade_fury", caster);
}

} // namespace

TEST(LuaAbility, YamlLoadsScriptPath) {
    AbilityRegistry reg;
    reg.load_file(std::string(kDataDir) + "/heroes/juggernaut.yaml");
    const AbilityDef* def = reg.find("juggernaut_blade_fury");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->base_class, "ability_lua");
    EXPECT_EQ(def->script_path, "abilities/juggernaut_blade_fury.lua");
    EXPECT_TRUE(has_flag(def->behavior, BehaviorFlag::NoTarget));
    EXPECT_TRUE(has_flag(def->behavior, BehaviorFlag::Channelled));
    EXPECT_DOUBLE_EQ(def->channel_time, 5.0);
}

TEST(LuaAbility, InstantiateFailsWithoutLuaState) {
    AbilityRegistry reg;
    World w;
    auto* jugg = w.spawn("Jugg", Team::Radiant, hero_stats(), {0.0, 0.0});
    reg.load_file(std::string(kDataDir) + "/heroes/juggernaut.yaml");

    Ability* a = reg.instantiate("juggernaut_blade_fury", *jugg);
    EXPECT_EQ(a, nullptr);
}

TEST(LuaAbility, ChannelPulsesDamageOverTime) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    World w;
    auto* jugg  = w.spawn("Jugg",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});

    Ability* blade = attach_blade_fury(reg, *jugg);
    ASSERT_NE(blade, nullptr);

    CastTarget t; // NO_TARGET
    EXPECT_EQ(blade->order_cast(t, w), CastError::None);
    // 施法前摇为 0，所以立即进入引导状态
    EXPECT_EQ(blade->phase(), CastPhase::Channelling);

    const double hp_before = enemy->health();
    // 引导 1 秒 ≈ 0.2 秒间隔 5 次跳动 → 5 * 80 * 0.2 = 80 魔法伤害
    // → 经过 25% 抗性后 60
    w.advance(1.0);

    const double diff = hp_before - enemy->health();
    EXPECT_GT(diff, 40.0);   // 至少四次脉冲
    EXPECT_LT(diff, 100.0);  // 不是完整引导的所有伤害
}

TEST(LuaAbility, OutOfRangeEnemyUnaffected) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    World w;
    auto* jugg  = w.spawn("Jugg",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* far   = w.spawn("Far",   Team::Dire,    hero_stats(), {10000.0, 0.0});
    Ability* blade = attach_blade_fury(reg, *jugg);

    CastTarget t;
    blade->order_cast(t, w);
    const double hp_before = far->health();
    w.advance(2.0);
    EXPECT_DOUBLE_EQ(far->health(), hp_before);
}

TEST(LuaAbility, ChannelEndsAfterDuration) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    World w;
    auto* jugg  = w.spawn("Jugg",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    Ability* blade = attach_blade_fury(reg, *jugg);

    CastTarget t;
    blade->order_cast(t, w);
    w.advance(5.5);

    // 5 秒，0.2 秒间隔 → 25 次脉冲 × 80 × 0.2 = 400 魔法伤害
    // → 经过 25% 抗性后 300（某些脉冲可能重叠跳动边界）
    const double dealt = hero_stats().max_health - enemy->health();
    EXPECT_GT(dealt, 250.0);
    EXPECT_LT(dealt, 360.0);
    EXPECT_EQ(blade->phase(), CastPhase::OnCooldown);
    (void)jugg;
}

TEST(LuaAbility, StunDuringChannelInterrupts) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    World w;
    auto* jugg  = w.spawn("Jugg",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    Ability* blade = attach_blade_fury(reg, *jugg);

    CastTarget t;
    blade->order_cast(t, w);
    w.advance(0.5);  // 一些脉冲命中
    const double after_half = enemy->health();

    jugg->modifiers().attach(modifiers::make_stunned(*jugg, 2.0));
    w.advance(3.0);

    // 眩晕生效后，不应再造成伤害
    EXPECT_NEAR(enemy->health(), after_half, 1e-3);
    EXPECT_EQ(blade->phase(), CastPhase::OnCooldown);
}
