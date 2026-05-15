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
    s.base_armor    = 0.0;
    return s;
}

class HeroLinaTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/lina.yaml");
        caster_ = world_.spawn("Lina", Team::Radiant, hero_stats(), {0.0, 0.0});
        enemy_  = world_.spawn("Enemy", Team::Dire, hero_stats(), {200.0, 0.0});
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* caster_{};
    Unit* enemy_{};
};

} // namespace

TEST_F(HeroLinaTest, DragonSlaveHitsEnemyInLine) {
    Ability* ds = reg_.instantiate("lina_dragon_slave", *caster_);
    ASSERT_NE(ds, nullptr);

    CastTarget t;
    t.point = {200.0, 0.0};
    t.has_point = true;
    EXPECT_EQ(ds->order_cast(t, world_), CastError::None);

    // 施法前摇 0.45 秒
    world_.advance(0.5);

    // 1 级 85 魔法伤害，经过 25% 抗性后 → 63.75
    const double dealt = 1000.0 - enemy_->health();
    EXPECT_NEAR(dealt, 63.75, 1.0);
}

TEST_F(HeroLinaTest, DragonSlaveMissesDistantEnemy) {
    auto* far = world_.spawn("FarEnemy", Team::Dire, hero_stats(), {5000.0, 0.0});

    Ability* ds = reg_.instantiate("lina_dragon_slave", *caster_);
    ASSERT_NE(ds, nullptr);

    CastTarget t;
    t.point = {200.0, 0.0};
    t.has_point = true;
    ds->order_cast(t, world_);
    world_.advance(0.5);

    EXPECT_DOUBLE_EQ(far->health(), 1000.0);
}

TEST_F(HeroLinaTest, LightStrikeArrayStunsDealsDamage) {
    Ability* lsa = reg_.instantiate("lina_light_strike_array", *caster_);
    ASSERT_NE(lsa, nullptr);

    CastTarget t;
    t.point = {200.0, 0.0};
    t.has_point = true;
    EXPECT_EQ(lsa->order_cast(t, world_), CastError::None);

    // 施法前摇 0.45 秒
    world_.advance(0.5);

    // 1 级 120 魔法伤害，经过 25% 抗性后 → 90
    const double dealt = 1000.0 - enemy_->health();
    EXPECT_NEAR(dealt, 90.0, 1.0);

    // 敌人应该被眩晕（1 级持续 1.6 秒）
    EXPECT_TRUE(enemy_->modifiers().has_state(ModifierState::Stunned));
}

TEST_F(HeroLinaTest, LagunaBladeDealsMassiveMagical) {
    Ability* laguna = reg_.instantiate("lina_laguna_blade", *caster_);
    ASSERT_NE(laguna, nullptr);

    CastTarget t;
    t.unit = enemy_;
    EXPECT_EQ(laguna->order_cast(t, world_), CastError::None);

    // 施法前摇 0.45 秒
    world_.advance(0.5);

    // 1 级 450 魔法伤害，经过 25% 抗性后 → 337.5
    const double dealt = 1000.0 - enemy_->health();
    EXPECT_NEAR(dealt, 337.5, 1.0);
}

TEST_F(HeroLinaTest, LagunaBladeKillsLowHpTarget) {
    enemy_->set_health(200.0);
    Ability* laguna = reg_.instantiate("lina_laguna_blade", *caster_);
    ASSERT_NE(laguna, nullptr);

    CastTarget t;
    t.unit = enemy_;
    laguna->order_cast(t, world_);
    world_.advance(0.5);

    EXPECT_FALSE(enemy_->alive());
}
