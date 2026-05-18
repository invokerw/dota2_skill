// Phase 9: Sven 风暴之锤集成测试
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
    s.max_health = 1500.0;
    s.max_mana   = 600.0;
    s.magic_resist = 0.25;
    return s;
}
} // namespace

class HeroSvenTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/sven.yaml");
        sven_  = world_.spawn("Sven", Team::Radiant, hero_stats(), {0, 0});
        enemy_ = world_.spawn("Enemy", Team::Dire, hero_stats(), {500, 0});
    }
    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* sven_{};
    Unit* enemy_{};
};

TEST_F(HeroSvenTest, StormHammerHitsTargetAndStuns) {
    Ability* sh = reg_.instantiate("sven_storm_hammer", *sven_);
    ASSERT_NE(sh, nullptr);

    CastTarget t; t.unit = enemy_;
    EXPECT_EQ(sh->order_cast(t, world_), CastError::None);

    // 0.55 cast point + 投射物飞 500 / 1100 ≈ 0.45s → 总 1.0s 后命中
    world_.advance(1.5);

    // 1 级伤害 80, 魔抗 25% → 60
    const double dealt = 1500.0 - enemy_->health();
    EXPECT_GT(dealt, 50.0);
    EXPECT_LT(dealt, 70.0);
    EXPECT_TRUE(enemy_->modifiers().has_state(ModifierState::Stunned));
}

TEST_F(HeroSvenTest, StormHammerHitsAllInRadius) {
    auto* second = world_.spawn("Enemy2", Team::Dire, hero_stats(), {550, 100});
    Ability* sh = reg_.instantiate("sven_storm_hammer", *sven_);
    CastTarget t; t.unit = enemy_;
    sh->order_cast(t, world_);
    world_.advance(1.5);

    EXPECT_LT(enemy_->health(), 1500.0);
    EXPECT_LT(second->health(), 1500.0);   // 在 225 半径内
    EXPECT_TRUE(second->modifiers().has_state(ModifierState::Stunned));
}
