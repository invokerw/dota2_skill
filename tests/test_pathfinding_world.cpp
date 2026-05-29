// World + NavGrid + WallTracer 端到端: 移动绕过地形, unit 间避让, 不可达 dest.
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

using namespace dota;
using namespace dota::pathfinding;

namespace {
UnitStats stats(double speed = 300.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    return s;
}

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// 200x200 网格, cell_size=8 -- 与默认 hull_radius=24 数量级匹配.
std::shared_ptr<NavGrid> make_grid(double cs = 8.0) {
    return std::make_shared<NavGrid>(-200.0, -200.0, 400.0, 400.0, cs);
}
} // namespace

TEST(PathfindingWorld, EmptyGridStraightMove) {
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {-150.0, 0.0});
    h->issue_move({150.0, 0.0});
    w.advance(1.5);  // 300 / 300 = 1.0s, 给 1.5s 余量
    EXPECT_NEAR(h->position().x, 150.0, 5.0);
    EXPECT_NEAR(h->position().y,   0.0, 1.0);
    EXPECT_FALSE(h->move_target().has_value());
}

TEST(PathfindingWorld, TerrainCircleObstacleIsAvoided) {
    World w;
    auto grid = make_grid(8.0);
    // 在原点放一个半径 32 的圆障碍, 起点 终点位于水平两侧.
    grid->add_circle_obstacle({0.0, 0.0}, 32.0);
    w.set_nav_grid(grid);

    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {-150.0, 0.0});

    double min_d = 1e9;
    w.events().subscribe<TickEndEvent>([&](TickEndEvent&) {
        min_d = std::min(min_d, dist(h->position(), {0.0, 0.0}));
    });

    h->issue_move({150.0, 0.0});
    // 绕路距离 ~ 350, 给足 2.5s
    w.advance(2.5);

    EXPECT_NEAR(h->position().x, 150.0, 10.0);
    // 全程未穿模 (容差 hull radius)
    EXPECT_GE(min_d, 32.0 - h->hull_radius() - 1.0);
}

TEST(PathfindingWorld, TwoUnitsAvoidEachOtherOnHeadOnPath) {
    World w;
    auto* a = w.spawn("a", Team::Radiant, stats(300.0), {-150.0, 0.0});
    auto* b = w.spawn("b", Team::Dire,    stats(300.0), { 150.0, 0.0});

    a->issue_move({ 150.0, 0.0});
    b->issue_move({-150.0, 0.0});

    double min_d = 1e9;
    w.events().subscribe<TickEndEvent>([&](TickEndEvent&) {
        min_d = std::min(min_d, dist(a->position(), b->position()));
    });

    // 给足时间擦肩而过到对方起点附近
    w.advance(4.0);
    const double r = a->hull_radius() + b->hull_radius();
    EXPECT_GE(min_d, r - 1.0);
    // 双方都有显著推进
    EXPECT_GT(a->position().x,  50.0);
    EXPECT_LT(b->position().x, -50.0);
}

TEST(PathfindingWorld, BlockedDestinationFallsBackToNearestOpen) {
    World w;
    auto grid = make_grid(8.0);
    // 终点处放一个大圆: A* 找到最近通行点替代后, mover 抵达圆边缘附近.
    grid->add_circle_obstacle({150.0, 0.0}, 40.0);
    w.set_nav_grid(grid);

    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {-150.0, 0.0});
    h->issue_move({150.0, 0.0});

    w.advance(3.0);

    // mover 不应留在起点 (说明 A* 至少给出了部分路径)
    EXPECT_GT(h->position().x, -100.0);
    // 不会穿入圆障碍
    EXPECT_GE(dist(h->position(), {150.0, 0.0}),
              40.0 - h->hull_radius() - 1.0);
}

TEST(PathfindingWorld, NavGridDefaultsToEmptyAndBehavesLikeOldMover) {
    // 兼容现有移动测试: World 默认 NavGrid 是空的, issue_move 应正常工作.
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(600.0), {0.0, 0.0});
    h->issue_move({600.0, 0.0});
    w.advance(1.1);
    EXPECT_NEAR(h->position().x, 600.0, 1.0);
    EXPECT_FALSE(h->move_target().has_value());
}

TEST(PathfindingWorld, MoveStateIsClearedOnArrival) {
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_move({100.0, 0.0});
    w.advance(0.5);  // 300 * 0.5 = 150 > 100, 应已到达
    EXPECT_FALSE(h->move_state().active);
    EXPECT_TRUE(h->orders().empty());
}
