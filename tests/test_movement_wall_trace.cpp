// Wall tracing 切线滑动测试: 路径上有英雄阻挡时绕开, 不穿模.
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace dota;
using namespace dota::modifiers;

namespace {
UnitStats stats(double speed = 300.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    return s;
}

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace

TEST(WallTrace, SlidesAroundSingleObstacle) {
    World w;
    auto* mover = w.spawn("mover",  Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* block = w.spawn("block",  Team::Dire,    stats(0.0),   {   0.0, 0.0});

    // 订阅 TickEnd 抓每 tick 的 mover-block 距离, 验证全程未穿模.
    double min_dist = 1e9;
    w.events().subscribe<TickEndEvent>(
        [&](TickEndEvent&) { min_dist = std::min(min_dist, dist(mover->position(), block->position())); });

    mover->issue_move({200.0, 0.0});

    // 直线 400 / 300 ≈ 1.33s. 绕路给足 4s.
    w.advance(4.0);

    EXPECT_NEAR(mover->position().x, 200.0, 5.0);
    const double r = mover->hull_radius() + block->hull_radius();
    EXPECT_GE(min_dist, r - 1.0);    // 全程未实质穿模(分离 pass 容差 1px)
}

TEST(WallTrace, ObstacleOnTargetPreventsArrivalWithoutPenetration) {
    World w;
    auto* mover = w.spawn("mover", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* block = w.spawn("block", Team::Dire,    stats(0.0),   { 200.0, 0.0});

    mover->issue_move({200.0, 0.0});
    w.advance(3.0);

    // 应该停在 block 的 hull 表面附近, 没有穿模, target 仍保留(从未到达).
    const double r = mover->hull_radius() + block->hull_radius();
    EXPECT_GE(dist(mover->position(), block->position()), r - 1.0);
    EXPECT_TRUE(mover->move_target().has_value());
}

TEST(WallTrace, PassesThroughGapBetweenTwoObstacles) {
    // 两障碍 y=±60, 间隙 = 60 - 24 = 36(单边到中线), 通过时 mover hull 24,
    // 离每个障碍中心 ≥ 48 即可. 走中线 y≈0 时距离两障碍 60, 充足.
    World w;
    auto* mover = w.spawn("mover", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* a = w.spawn("a", Team::Dire, stats(0.0), {0.0,  60.0});
    auto* b = w.spawn("b", Team::Dire, stats(0.0), {0.0, -60.0});
    (void)a; (void)b;

    mover->issue_move({200.0, 0.0});
    w.advance(2.5);

    EXPECT_NEAR(mover->position().x, 200.0, 5.0);
    const double r = mover->hull_radius() + a->hull_radius();
    EXPECT_GE(dist(mover->position(), a->position()), r - 1.0);
    EXPECT_GE(dist(mover->position(), b->position()), r - 1.0);
}

TEST(WallTrace, GivesUpInTightWedge) {
    // 两障碍 y=±20, 中心间距 40. mover 与障碍 hull 都 24 -- 无法穿过.
    // 期望: 不到达, 但绝不穿模.
    World w;
    auto* mover = w.spawn("mover", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* a = w.spawn("a", Team::Dire, stats(0.0), {0.0,  20.0});
    auto* b = w.spawn("b", Team::Dire, stats(0.0), {0.0, -20.0});

    double min_a = 1e9, min_b = 1e9;
    w.events().subscribe<TickEndEvent>([&](TickEndEvent&) {
        min_a = std::min(min_a, dist(mover->position(), a->position()));
        min_b = std::min(min_b, dist(mover->position(), b->position()));
    });

    mover->issue_move({200.0, 0.0});
    w.advance(2.0);

    const double r = mover->hull_radius() + a->hull_radius();
    EXPECT_GE(min_a, r - 1.0);
    EXPECT_GE(min_b, r - 1.0);
    // 没穿过去 -- x 应该被卡在 wedge 之前
    EXPECT_LT(mover->position().x, 100.0);
}

TEST(WallTrace, NoUnitCollisionTargetIsNotABlocker) {
    // 障碍带 NoUnitCollision (用 invisible-style modifier 简化 -- 用 KB,
    // 它声明 NoUnitCollision; 但我们要的是静止障碍, 直接构造 GenericState).
    // 这里复用 thinker 路径不太合适, 改用 modifiers 直接 attach 一个
    // 自定义 modifier 略复杂; 更简单: 用 Neutral 队伍触发同样的过滤(下个测试覆盖).
    // 这个测试用 modifier_invisible 不会附带 NoUnitCollision -- 改用一个声明
    // NoUnitCollision 的现成 modifier: GenericState 在 library 里需要直接构造.
    // 简化做法: 把 block 设成 NoCollisionStatic 通过显式构造 GenericState.
    World w;
    auto* mover = w.spawn("mover", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* block = w.spawn("block", Team::Dire,    stats(0.0),   {   0.0, 0.0});
    block->modifiers().attach(std::make_unique<GenericState>(
        *block, "modifier_no_collision", -1.0,
        state_bit(ModifierState::NoUnitCollision)));

    mover->issue_move({200.0, 0.0});
    // 没有 wall trace 阻挡, 直线 400/300 ≈ 1.33s, 给余量
    w.advance(1.6);
    EXPECT_NEAR(mover->position().x, 200.0, 1.0);
}

TEST(WallTrace, NeutralIsNotABlocker) {
    World w;
    auto* mover = w.spawn("mover", Team::Radiant, stats(300.0), {-200.0, 0.0});
    auto* neutral = w.spawn("blocker", Team::Neutral, stats(0.0), {0.0, 0.0});
    (void)neutral;

    mover->issue_move({200.0, 0.0});
    w.advance(1.6);
    EXPECT_NEAR(mover->position().x, 200.0, 1.0);
}
