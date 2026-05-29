// WallTracer Bug2 双向追踪测试
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"
#include "dota/pathfinding/wall_tracer.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace dota;
using namespace dota::pathfinding;

namespace {
NavGrid empty_grid(double size = 200.0) {
    return NavGrid(0.0, 0.0, size, size, 1.0);
}

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}
} // namespace

TEST(WallTracer, StraightLineNoObstacle) {
    auto g = empty_grid();
    WallTracer wt(0.5);
    std::vector<Vec2> path;
    auto r = wt.find_path(g, {}, kInvalidEntityId,
                          {10.0, 10.0}, {30.0, 10.0}, path);
    EXPECT_EQ(r, PathResult::Reached);
    ASSERT_EQ(path.size(), 1u);
    EXPECT_NEAR(path[0].x, 30.0, 1e-9);
    EXPECT_NEAR(path[0].y, 10.0, 1e-9);
}

TEST(WallTracer, AroundSingleCircleObstacle) {
    auto g = empty_grid();
    g.add_circle_obstacle({50.0, 50.0}, 5.0);
    WallTracer wt(0.5);
    std::vector<Vec2> path;
    // 起点 / 终点在圆心连线两侧
    auto r = wt.find_path(g, {}, kInvalidEntityId,
                          {30.0, 50.0}, {70.0, 50.0}, path);
    EXPECT_EQ(r, PathResult::Reached);
    ASSERT_FALSE(path.empty());
    // 终点
    EXPECT_NEAR(path.back().x, 70.0, 1.0);
    EXPECT_NEAR(path.back().y, 50.0, 1.0);
    // 中间点不应穿圆 (离圆心距离 >= radius - 容差)
    for (auto p : path) {
        EXPECT_GE(dist(p, {50.0, 50.0}), 5.0 - 1e-3);
    }
}

TEST(WallTracer, BidirectionalPicksShorter) {
    auto g = empty_grid();
    // 圆形障碍偏下: 上方绕路明显短
    g.add_circle_obstacle({50.0, 47.0}, 5.0);
    WallTracer wt(0.5);
    std::vector<Vec2> path;
    auto r = wt.find_path(g, {}, kInvalidEntityId,
                          {30.0, 50.0}, {70.0, 50.0}, path);
    EXPECT_EQ(r, PathResult::Reached);
    // 路径中段 y 应在 50 上方 (偏正方向), 即从上面绕
    bool went_above = false;
    for (auto p : path)
        if (p.y > 50.5) went_above = true;
    EXPECT_TRUE(went_above);
}

TEST(WallTracer, BlockedReturnsBlocked) {
    auto g = empty_grid();
    // 起点周围被一圈大圆完全包住
    g.add_circle_obstacle({10.0, 10.0}, 5.0);  // 起点恰好在圆心 -> 出不去
    WallTracer wt(0.5);
    std::vector<Vec2> path;
    auto r = wt.find_path(g, {}, kInvalidEntityId,
                          {10.0, 10.0}, {50.0, 50.0}, path);
    EXPECT_EQ(r, PathResult::Blocked);
}

TEST(WallTracer, SimplifyCollapsesStraightSegment) {
    auto g = empty_grid();
    WallTracer wt(0.5);
    std::vector<Vec2> path = {
        {0.0, 0.0}, {5.0, 0.0}, {10.0, 0.0}, {15.0, 0.0}, {20.0, 0.0}};
    wt.simplify(g, {}, kInvalidEntityId, path);
    // 全部共线无障碍, 简化为 [start, end]
    ASSERT_EQ(path.size(), 2u);
    EXPECT_DOUBLE_EQ(path.front().x, 0.0);
    EXPECT_DOUBLE_EQ(path.back().x, 20.0);
}

TEST(WallTracer, SimplifyKeepsCornerWhenBlocked) {
    auto g = empty_grid();
    g.add_circle_obstacle({10.0, 0.0}, 1.0);
    WallTracer wt(0.5);
    // 起点 -> 拐点绕过障碍 -> 终点
    std::vector<Vec2> path = {
        {0.0, 0.0}, {10.0, 5.0}, {20.0, 0.0}};
    wt.simplify(g, {}, kInvalidEntityId, path);
    // 直线 (0,0) -> (20,0) 被障碍挡住, 拐点不能省
    EXPECT_EQ(path.size(), 3u);
}

TEST(WallTracer, AvoidsDynamicUnitWhenInGroup) {
    auto g = empty_grid();
    DynamicCircle u{{50.0, 50.0}, 3.0, CollisionGroups::All, 99};
    WallTracer wt(0.5);
    std::vector<Vec2> path;
    // query=All -> 视为障碍, 应绕开
    auto r = wt.find_path(g, {u}, kInvalidEntityId,
                          {30.0, 50.0}, {70.0, 50.0}, path,
                          CollisionGroups::All);
    EXPECT_EQ(r, PathResult::Reached);
    for (auto p : path) {
        EXPECT_GE(dist(p, {50.0, 50.0}), 3.0 - 1e-3);
    }
}
