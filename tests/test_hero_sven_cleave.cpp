// Stage 5: Sven 大劈砍 (OnAttackLanded + 锥形物理 AoE). 不算法球 -- 不修改
// bonus_damage / damage_type, 命中后给目标周围锥形敌人各打 base * cleave_pct
// 物理伤害.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats melee_hero(double dmg = 50.0) {
    UnitStats s;
    s.max_health       = 1500.0;
    s.max_mana         = 300.0;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.magic_resist     = 0.0;
    s.base_armor       = 0.0;
    return s;
}

class HeroSvenCleaveTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/sven.yaml");

        sven_   = world_.spawn("Sven", Team::Radiant, melee_hero(),  {0.0, 0.0});
        main_   = world_.spawn("Main", Team::Dire,    melee_hero(),  {100.0, 0.0});

        Ability* gc = reg_.instantiate("sven_great_cleave", *sven_, &lua_);
        ASSERT_NE(gc, nullptr);
        gc->set_level(1);  // 40% cleave.
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* sven_{};
    Unit* main_{};
};

} // namespace

TEST_F(HeroSvenCleaveTest, IntrinsicAttached) {
    EXPECT_TRUE(sven_->modifiers().find("modifier_sven_great_cleave") != nullptr);
}

TEST_F(HeroSvenCleaveTest, CleaveHitsAdjacentEnemy) {
    // 主目标在 +x 100, 副目标在 +x 200 (锥内 60 度半角). 副 hp 应该掉 base*40%=20.
    auto* side = world_.spawn("Side", Team::Dire, melee_hero(), {200.0, 50.0});
    const double main_before = main_->health();
    const double side_before = side->health();

    sven_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(World::kTickDt);

    // 主目标: 完整 50 物理.
    EXPECT_NEAR(main_before - main_->health(), 50.0, 1e-6);
    // 副目标: 50 * 0.40 = 20 物理.
    EXPECT_NEAR(side_before - side->health(), 20.0, 1e-6);
}

TEST_F(HeroSvenCleaveTest, CleaveSkipsAlliesAndOutOfCone) {
    // 在 sven 后方 (锥外) 放一个 dire, 不应被劈到.
    auto* behind = world_.spawn("Behind", Team::Dire, melee_hero(), {-300.0, 0.0});
    const double behind_before = behind->health();

    sven_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(World::kTickDt);

    EXPECT_NEAR(behind_before - behind->health(), 0.0, 1e-6);
}
