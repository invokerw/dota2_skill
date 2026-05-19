// 移动指令与 motion controller 交互: KB 期间冻结指令式移动, KB 结束后续走.
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace dota;
using namespace dota::modifiers;

namespace {
UnitStats stats(double speed = 300.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    return s;
}
} // namespace

TEST(MovementMC, KnockbackPausesMoveAndResumes) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});

    // 先走 0.5s -- 应推进 ~150 单位
    w.advance(0.5);
    const Vec2 pos_a = hero->position();
    EXPECT_GT(pos_a.x, 100.0);
    EXPECT_LT(pos_a.x, 200.0);

    // 挂 KB 朝 +y dist=120 dur=0.4s
    hero->modifiers().attach(make_knockback(*hero, {0.0, 1.0}, 120.0, 0.4));
    w.advance(0.4);
    // KB 期间 x 不前进 (允许末帧 KB 已 destroy 后单 tick 续走 -- 给较松的 30 单位余量)
    EXPECT_NEAR(hero->position().x, pos_a.x, 30.0);
    EXPECT_NEAR(hero->position().y, pos_a.y + 120.0, 5.0);

    // KB 结束后续走到 (600, +120) -- 注意 y 不会被改回 0, 路径是单航点 (600, 0),
    // 所以会从被推后的位置直线走向 (600, 0).
    w.advance(3.0);
    EXPECT_NEAR(hero->position().x, 600.0, 1.0);
    EXPECT_NEAR(hero->position().y, 0.0, 1.0);
    EXPECT_FALSE(hero->move_target().has_value());
}

TEST(MovementMC, MoveTargetSurvivesKnockback) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    hero->modifiers().attach(make_knockback(*hero, {0.0, 1.0}, 120.0, 0.4));

    w.advance(0.4);
    ASSERT_TRUE(hero->move_target().has_value());
    EXPECT_NEAR(hero->move_target()->x, 600.0, 1e-6);
    EXPECT_NEAR(hero->move_target()->y, 0.0, 1e-6);
}

TEST(MovementMC, RootedDuringMovePreservesTarget) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    w.advance(0.3);
    const Vec2 pos_b = hero->position();

    hero->modifiers().attach(make_rooted(*hero, 1.0));
    w.advance(0.8);
    EXPECT_NEAR(hero->position().x, pos_b.x, 1e-6);
    EXPECT_TRUE(hero->move_target().has_value());

    // root 解除后续走
    w.advance(3.0);
    EXPECT_NEAR(hero->position().x, 600.0, 1.0);
    EXPECT_FALSE(hero->move_target().has_value());
}
