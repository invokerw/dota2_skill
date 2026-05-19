// 移动系统基础测试: issue_move / stop_move / 到达 / 阻断 / 与碰撞分离 pass 兼容
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

TEST(MovementBasic, ArrivesAtExpectedTime) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    EXPECT_TRUE(hero->move_target().has_value());

    // 600 单位距离 / 300 速度 = 2.0s. 加点余量保证最后一帧切到 final.
    w.advance(2.1);
    EXPECT_NEAR(hero->position().x, 600.0, 1.0);
    EXPECT_NEAR(hero->position().y, 0.0, 1e-6);
    EXPECT_FALSE(hero->move_target().has_value());
}

TEST(MovementBasic, MoveSpeedAffectsArrivalTime) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(600.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    w.advance(1.05);   // 600 / 600 = 1.0s
    EXPECT_NEAR(hero->position().x, 600.0, 1.0);
    EXPECT_FALSE(hero->move_target().has_value());
}

TEST(MovementBasic, RootBlocksMovement) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->modifiers().attach(make_rooted(*hero, 5.0));
    hero->issue_move({600.0, 0.0});

    w.advance(1.0);
    EXPECT_NEAR(hero->position().x, 0.0, 1e-6);
    // 指令保留 -- root 解除后会自动续走
    EXPECT_TRUE(hero->move_target().has_value());
}

TEST(MovementBasic, StunBlocksMovement) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->modifiers().attach(make_stunned(*hero, 5.0));
    hero->issue_move({600.0, 0.0});

    w.advance(1.0);
    EXPECT_NEAR(hero->position().x, 0.0, 1e-6);
    EXPECT_TRUE(hero->move_target().has_value());
}

TEST(MovementBasic, StopMoveClearsTarget) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    w.advance(0.5);
    const double x_before = hero->position().x;
    EXPECT_GT(x_before, 100.0);

    hero->stop_move();
    EXPECT_FALSE(hero->move_target().has_value());

    w.advance(1.0);
    EXPECT_NEAR(hero->position().x, x_before, 1e-6);
}

TEST(MovementBasic, DeadUnitDoesNotMove) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    hero->apply_raw_damage(hero->max_health() + 1.0);
    ASSERT_FALSE(hero->alive());

    w.advance(1.0);
    EXPECT_NEAR(hero->position().x, 0.0, 1e-6);
}

TEST(MovementBasic, IssueMoveOverwritesPrevious) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(300.0), {0.0, 0.0});
    hero->issue_move({600.0, 0.0});
    w.advance(0.5);
    // 改朝 -y 方向
    hero->issue_move({0.0, -600.0});
    w.advance(2.5);
    EXPECT_NEAR(hero->position().y, -600.0, 1.0);
    EXPECT_FALSE(hero->move_target().has_value());
}

TEST(MovementBasic, MovedFlagInteractsWithCollision) {
    // a 走向静止 b. 验证 set_position + 末尾 resolve_unit_collisions 仍然
    // 把 a 推出 b 的 hull, 没有穿模.
    World w;
    auto* a = w.spawn("a", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* b = w.spawn("b", Team::Dire,    stats(0.0),   {   0.0, 0.0});
    (void)b;
    a->issue_move({200.0, 0.0});

    w.advance(2.0);
    const double r = a->hull_radius() + b->hull_radius();
    const double dx = a->position().x - b->position().x;
    const double dy = a->position().y - b->position().y;
    const double d  = std::sqrt(dx * dx + dy * dy);
    // 没有 wall trace 时, a 会顶在 b 上 (分离 pass 把 a 推出 b 一个 hull 之外).
    // 关键检查: 没有穿模 (距离 >= r - eps).
    EXPECT_GE(d, r - 1e-3);
}
