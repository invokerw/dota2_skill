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

    // 80 dps at 0.2s interval = 5 pulses × 16 = 80 magical → 60 after resist
    const double dealt = hp_before - enemy_->health();
    EXPECT_GT(dealt, 40.0);
    EXPECT_LT(dealt, 100.0);
}

TEST_F(HeroJuggernautTest, HealingWardHealsOverTime) {
    Ability* ward = reg_.instantiate("juggernaut_healing_ward", *caster_);
    ASSERT_NE(ward, nullptr);

    // Reduce caster HP first
    caster_->set_health(500.0);

    CastTarget t;
    EXPECT_EQ(ward->order_cast(t, world_), CastError::None);

    // Cast point is 0.3s
    world_.advance(0.35);

    // After cast, caster should have the periodic heal modifier
    EXPECT_TRUE(caster_->modifiers().find("modifier_periodic_heal") != nullptr);

    const double hp_after_cast = caster_->health();
    // Let 3 ticks fire (tick_interval = 1s, heal = 2% of 1000 = 20 per tick)
    world_.advance(3.0);

    const double healed = caster_->health() - hp_after_cast;
    // 3 ticks × 20 HP = 60
    EXPECT_NEAR(healed, 60.0, 5.0);
}

TEST_F(HeroJuggernautTest, OmnislashDealsPureDamage) {
    Ability* omni = reg_.instantiate("juggernaut_omnislash", *caster_);
    ASSERT_NE(omni, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(omni->order_cast(t, world_), CastError::None);

    // Cast point is 0.3s
    world_.advance(0.35);

    // Level 1: 3 slashes × 200 pure = 600 pure (no resist)
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

    // 300 HP, each hit = 200 pure → 2 hits kill, third slash should not happen
    EXPECT_FALSE(enemy_->alive());
}
