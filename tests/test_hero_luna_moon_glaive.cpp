// Luna 月刃 (bouncing attack). 被动: OnAttackLanded 时从主目标位置弹一颗
// TrackingProjectile 到附近敌人, 命中后衰减伤害继续弹, 最多 bounces 次.
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

// BAT 故意拉到 5s, 让 advance(1.5s) 内只触发一次普攻; 弹跳本身用
// bounce_speed=900 / radius<=500, 三跳总飞行时间约 1.5s 内能跑完.
UnitStats melee_hero(double dmg = 50.0, double hp = 4000.0) {
    UnitStats s;
    s.max_health       = hp;
    s.max_mana         = 200.0;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 5.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.magic_resist     = 0.0;
    return s;
}

// 默认 advance 1 秒, 足够 bounce_speed 900 / radius 500 的多次弹跳完成.
constexpr double kAdvance = 1.5;

class HeroLunaMoonGlaiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/luna.yaml");

        luna_   = world_.spawn("Luna", Team::Radiant, melee_hero(), {0.0, 0.0});
        // 主目标 + 两个副目标在主目标半径 500 内呈一字排开.
        // luna -> main(+100,0) -> side1(+200,0) -> side2(+300,0).
        main_   = world_.spawn("Main",  Team::Dire, melee_hero(), {100.0, 0.0});
        side1_  = world_.spawn("Side1", Team::Dire, melee_hero(), {200.0, 0.0});
        side2_  = world_.spawn("Side2", Team::Dire, melee_hero(), {300.0, 0.0});

        Ability* g = reg_.instantiate("luna_moon_glaive", *luna_, &lua_);
        ASSERT_NE(g, nullptr);
        g->set_level(1);  // bounces=3, reduction=52%.
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* luna_{};
    Unit* main_{};
    Unit* side1_{};
    Unit* side2_{};
};

} // namespace

TEST_F(HeroLunaMoonGlaiveTest, IntrinsicAttached) {
    EXPECT_TRUE(luna_->modifiers().find("modifier_luna_moon_glaive") != nullptr);
}

TEST_F(HeroLunaMoonGlaiveTest, FirstBounceDealsReducedDamage) {
    // luna 普攻 main (50 物理). 之后弹一颗到最近的 side1: 50 * (1-0.52) = 24.
    const double main_before  = main_->health();
    const double side1_before = side1_->health();

    luna_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(kAdvance);

    EXPECT_NEAR(main_before  - main_->health(),  50.0, 1e-6);
    EXPECT_NEAR(side1_before - side1_->health(), 24.0, 1e-6);
}

TEST_F(HeroLunaMoonGlaiveTest, ChainBouncesWithGeometricDecay) {
    // 一字排开, 链应当是 main -> side1 -> side2 -> (找不到没访问过的, 停).
    // bounces=3 的 level 1 上限里, 实际只用了 2 跳.
    const double s1_before = side1_->health();
    const double s2_before = side2_->health();

    luna_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(kAdvance);

    // side1: 50 * 0.48 = 24.
    EXPECT_NEAR(s1_before - side1_->health(), 24.0, 1e-6);
    // side2: 24 * 0.48 = 11.52.
    EXPECT_NEAR(s2_before - side2_->health(), 11.52, 1e-6);
}

TEST_F(HeroLunaMoonGlaiveTest, NoBounceWhenNoOtherEnemyInRadius) {
    // 把 side1 / side2 移到 600 之外, 主目标 100,0 周围 500 内只剩主目标自己.
    side1_->set_position({100.0, 700.0});
    side2_->set_position({100.0, -700.0});

    const double s1_before = side1_->health();
    const double s2_before = side2_->health();

    luna_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(kAdvance);

    EXPECT_NEAR(s1_before - side1_->health(), 0.0, 1e-6);
    EXPECT_NEAR(s2_before - side2_->health(), 0.0, 1e-6);
}

TEST_F(HeroLunaMoonGlaiveTest, BouncesScaleWithLevel) {
    // level 4: bounces=6, reduction=34%. 我们只用 3 个副目标, 仍然只弹 2 跳
    // (visited 用尽), 但伤害比 level 1 大.
    Ability* g = luna_->abilities().find("luna_moon_glaive");
    ASSERT_NE(g, nullptr);
    g->set_level(4);

    const double s1_before = side1_->health();
    const double s2_before = side2_->health();

    luna_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(kAdvance);

    // side1: 50 * 0.66 = 33.
    EXPECT_NEAR(s1_before - side1_->health(), 33.0, 1e-6);
    // side2: 33 * 0.66 = 21.78.
    EXPECT_NEAR(s2_before - side2_->health(), 21.78, 1e-6);
}

TEST_F(HeroLunaMoonGlaiveTest, DoesNotBounceBackToMain) {
    // 单一副目标在主目标旁边. 弹到 side1 后应该停, 不会再弹回 main.
    side2_->set_position({1000.0, 1000.0});  // 移到搜索半径外.

    const double main_before = main_->health();
    const double s1_before   = side1_->health();

    luna_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(kAdvance);

    // main 只承受最初一击, 没被弹回来.
    EXPECT_NEAR(main_before - main_->health(), 50.0, 1e-6);
    // side1: 1 跳 = 50 * 0.48 = 24.
    EXPECT_NEAR(s1_before - side1_->health(), 24.0, 1e-6);
}
