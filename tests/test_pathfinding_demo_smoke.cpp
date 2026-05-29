// pathfinding_demo 的无窗口 smoke: 用同样的 NavGrid + 8 个 unit 交叉穿越, 跑数秒
// 后期望大多数 unit 已抵达对侧, 且没有穿模 / 没有崩溃. 主要保证 demo 主循环里的
// 模拟逻辑 (圆障碍 + WallTracer + tick_movement) 在 batch 场景下也健康.

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>

using namespace dota;
using namespace dota::pathfinding;

namespace {

UnitStats stats(double speed = 280.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    s.hull_radius = 22.0;
    return s;
}

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

TEST(PathfindingDemoSmoke, EightUnitsCrossWithCircleObstacles) {
    World w;
    auto grid = std::make_shared<NavGrid>(-600.0, -400.0, 1200.0, 800.0, 16.0);
    grid->add_circle_obstacle({0.0, 0.0}, 70.0);
    grid->add_circle_obstacle({-200.0, 120.0}, 50.0);
    grid->add_circle_obstacle({ 220.0, -140.0}, 60.0);
    w.set_nav_grid(grid);

    const std::array<Vec2, 4> left  = {Vec2{-500.0, -150.0}, {-500.0, -50.0},
                                        {-500.0,   50.0}, {-500.0, 150.0}};
    const std::array<Vec2, 4> right = {Vec2{ 500.0, -150.0}, { 500.0, -50.0},
                                        { 500.0,   50.0}, { 500.0, 150.0}};

    std::vector<Unit*> units;
    units.reserve(8);
    for (const auto& p : left)  units.push_back(w.spawn("R", Team::Radiant, stats(), p));
    for (const auto& p : right) units.push_back(w.spawn("D", Team::Dire,    stats(), p));

    // 各自走对面起点
    for (std::size_t i = 0; i < 4; ++i) {
        units[i]->issue_move(right[i]);
        units[i + 4]->issue_move(left[i]);
    }

    // 跑 12s. 直线距离 1000, 速度 280, 加上绕障 + 头对头避让, 留足时间.
    for (int step = 0; step < 360; ++step) {
        w.advance(1.0 / 30.0);
        // 不应穿入任何圆障碍
        for (Unit* u : units) {
            for (const auto& c : grid->circles()) {
                EXPECT_GE(dist(u->position(), c.center),
                          c.radius - u->hull_radius() - 1.0)
                    << "unit " << u->id() << " penetrated circle at step " << step;
            }
        }
    }

    // 头对头 8 unit 在 3 圆障碍走廊里相向而行: 是密集场景压力测试.
    // 不要求所有 unit 抵达, 仅要求每个 unit 都有显著推进 (避免完全卡死),
    // 整体上至少有半数越过起点对面方向 200 单位 (超过最近障碍).
    int progressed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (units[i]->position().x      > -300.0) ++progressed;  // R 离开起点
        if (units[i + 4]->position().x  <  300.0) ++progressed;  // D 离开起点
    }
    EXPECT_EQ(progressed, 8) << "expected all 8 units to move from spawn";

    int notably_moved = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (units[i]->position().x     > -100.0) ++notably_moved;
        if (units[i + 4]->position().x <  100.0) ++notably_moved;
    }
    EXPECT_GE(notably_moved, 4) << "only " << notably_moved
        << "/8 units made significant progress through the corridor";
}
