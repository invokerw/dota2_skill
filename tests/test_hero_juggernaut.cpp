#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health    = 1000.0;
    s.max_mana      = 500.0;
    s.magic_resist  = 0.25;
    s.attack_damage = 50.0;
    s.base_armor    = 0.0;
    return s;
}

class HeroJuggernautTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/juggernaut.yaml");
        caster_ = world_.spawn("Juggernaut", Team::Radiant, hero_stats(), {0.0, 0.0});
        enemy_  = world_.spawn("Enemy", Team::Dire, hero_stats(), {100.0, 0.0});
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* caster_{};
    Unit* enemy_{};
};

} // namespace

TEST_F(HeroJuggernautTest, BladeFuryChannelsDamage) {
    Ability* fury = reg_.instantiate("juggernaut_blade_fury", *caster_);
    ASSERT_NE(fury, nullptr);

    CastTarget t;
    EXPECT_EQ(fury->order_cast(t, world_), CastError::None);
    EXPECT_EQ(fury->phase(), CastPhase::Channelling);

    const double hp_before = enemy_->health();
    world_.advance(1.0);

    // 80 DPS，0.2 秒间隔 = 5 次脉冲 × 16 = 80 魔法伤害 → 经过抗性后 60
    const double dealt = hp_before - enemy_->health();
    EXPECT_GT(dealt, 40.0);
    EXPECT_LT(dealt, 100.0);
}

TEST_F(HeroJuggernautTest, HealingWardHealsOverTime) {
    Ability* ward = reg_.instantiate("juggernaut_healing_ward", *caster_);
    ASSERT_NE(ward, nullptr);

    // 先降低施法者生命值
    caster_->set_health(500.0);

    CastTarget t;
    EXPECT_EQ(ward->order_cast(t, world_), CastError::None);

    // 施法前摇 0.3 秒
    world_.advance(0.35);

    // 施法后，施法者应该有周期性治疗修饰器
    EXPECT_TRUE(caster_->modifiers().find("modifier_periodic_heal") != nullptr);

    const double hp_after_cast = caster_->health();
    // 让 3 次跳动触发（跳动间隔 = 1 秒，治疗 = 1000 的 2% = 每次 20）
    world_.advance(3.0);

    const double healed = caster_->health() - hp_after_cast;
    // 3 次跳动 × 20 生命值 = 60
    EXPECT_NEAR(healed, 60.0, 5.0);
}

TEST_F(HeroJuggernautTest, OmnislashDealsPureDamage) {
    Ability* omni = reg_.instantiate("juggernaut_omnislash", *caster_);
    ASSERT_NE(omni, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(omni->order_cast(t, world_), CastError::None);

    // 施法前摇 0.3 秒
    world_.advance(0.35);

    // 1 级：3 次斩击 × 200 纯粹伤害 = 600 纯粹伤害（无抗性）
    const double dealt = 1000.0 - enemy_->health();
    EXPECT_NEAR(dealt, 600.0, 1.0);
}

TEST_F(HeroJuggernautTest, OmnislashStopsOnTargetDeath) {
    enemy_->set_health(300.0);
    Ability* omni = reg_.instantiate("juggernaut_omnislash", *caster_);
    ASSERT_NE(omni, nullptr);

    CastTarget t;
    t.unit = enemy_;
    omni->order_cast(t, world_);
    world_.advance(0.35);

    // 300 生命值，每次攻击 = 200 纯粹伤害 → 2 次攻击击杀，第三次斩击不应发生
    EXPECT_FALSE(enemy_->alive());
}
