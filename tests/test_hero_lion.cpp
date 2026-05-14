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
    s.max_mana      = 600.0;
    s.magic_resist  = 0.25;
    s.attack_damage = 50.0;
    return s;
}

class HeroLionTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/lion.yaml");
        caster_ = world_.spawn("Lion", Team::Radiant, hero_stats(), {0.0, 0.0});
        enemy_  = world_.spawn("Enemy", Team::Dire, hero_stats(), {200.0, 0.0});
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* caster_{};
    Unit* enemy_{};
};

} // namespace

TEST_F(HeroLionTest, EarthSpikeStunsDealsDamage) {
    Ability* spike = reg_.instantiate("lion_earth_spike", *caster_);
    ASSERT_NE(spike, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(spike->order_cast(t, world_), CastError::None);

    // Advance past cast point (0.3s)
    world_.advance(0.35);

    const double hp_after = enemy_->health();
    // 80 magical at level 1, after 25% resist → 60 damage
    EXPECT_NEAR(1000.0 - hp_after, 60.0, 1.0);

    // Enemy should be stunned (1.7s at level 1)
    EXPECT_TRUE(enemy_->modifiers().has_state(ModifierState::Stunned));
    EXPECT_FALSE(enemy_->can_attack());
    EXPECT_FALSE(enemy_->can_cast());
}

TEST_F(HeroLionTest, HexDisablesTarget) {
    Ability* hex = reg_.instantiate("lion_hex", *caster_);
    ASSERT_NE(hex, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(hex->order_cast(t, world_), CastError::None);

    // Cast point is 0.1s
    world_.advance(0.15);

    EXPECT_TRUE(enemy_->modifiers().has_state(ModifierState::Hexed));
    EXPECT_FALSE(enemy_->can_attack());
    EXPECT_FALSE(enemy_->can_cast());
    // Hex allows movement in Dota
    EXPECT_TRUE(enemy_->can_move());
}

TEST_F(HeroLionTest, FingerOfDeathDealsMassiveMagical) {
    Ability* finger = reg_.instantiate("lion_finger_of_death", *caster_);
    ASSERT_NE(finger, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(finger->order_cast(t, world_), CastError::None);

    // Cast point is 0.5s
    world_.advance(0.55);

    // 600 magical at level 1, after 25% resist → 450
    const double dealt = 1000.0 - enemy_->health();
    EXPECT_NEAR(dealt, 450.0, 1.0);
}

TEST_F(HeroLionTest, FingerKillsLowHpTarget) {
    enemy_->set_health(100.0);
    Ability* finger = reg_.instantiate("lion_finger_of_death", *caster_);
    ASSERT_NE(finger, nullptr);

    CastTarget t;
    t.unit = enemy_;
    finger->order_cast(t, world_);
    world_.advance(0.55);

    EXPECT_FALSE(enemy_->alive());
}
