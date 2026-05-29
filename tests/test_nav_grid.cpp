// NavGrid 基础测试: 坐标转换, blocked 计数, 圆形障碍, A* 8 方向 + 防穿角, simplify
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

#include <gtest/gtest.h>

using namespace dota;
using namespace dota::pathfinding;

namespace {
NavGrid make_grid(int w, int h, double cell = 1.0) {
    return NavGrid(0.0, 0.0, w * cell, h * cell, cell);
}
} // namespace

TEST(NavGrid, WorldGridRoundTrip) {
    NavGrid g = make_grid(10, 10, 2.0);
    int gx, gy;
    g.world_to_grid(5.5, 7.5, gx, gy);
    EXPECT_EQ(gx, 2);  // floor((5.5-0)/2) = 2
    EXPECT_EQ(gy, 3);
    double wx, wy;
    g.grid_to_world(gx, gy, wx, wy);
    EXPECT_DOUBLE_EQ(wx, 5.0);  // origin + (gx + 0.5) * cell
    EXPECT_DOUBLE_EQ(wy, 7.0);
}

TEST(NavGrid, AddRemoveBlockedSymmetric) {
    NavGrid g = make_grid(5, 5);
    EXPECT_FALSE(g.is_blocked(2, 2));
    g.add_blocked(2, 2);
    EXPECT_TRUE(g.is_blocked(2, 2));
    g.add_blocked(2, 2);
    g.remove_blocked(2, 2);
    EXPECT_TRUE(g.is_blocked(2, 2));  // 仍有一层引用
    g.remove_blocked(2, 2);
    EXPECT_FALSE(g.is_blocked(2, 2));
    // 越界静默忽略
    g.remove_blocked(2, 2);
    EXPECT_FALSE(g.is_blocked(2, 2));
}

TEST(NavGrid, OutOfBoundsIsBlocked) {
    NavGrid g = make_grid(3, 3);
    EXPECT_TRUE(g.is_blocked(-1, 0));
    EXPECT_TRUE(g.is_blocked(0, -1));
    EXPECT_TRUE(g.is_blocked(3, 0));
    EXPECT_TRUE(g.is_blocked(0, 3));
}

TEST(NavGrid, AddCircleObstacleBlocksOnlyFullyEnclosedCells) {
    NavGrid g = make_grid(10, 10, 1.0);
    // 圆心在格子 (5,5) 中心, 半径 1.5: cell (5,5) 的四角距圆心为 sqrt(0.5) ≈ 0.71, 全在内, blocked.
    // 邻近 cell (4,5) 等四角部分在内部分在外, 不 blocked.
    auto cells = g.add_circle_obstacle({5.5, 5.5}, 1.5);
    EXPECT_FALSE(cells.empty());
    EXPECT_TRUE(g.is_blocked(5, 5));
    // 角落 cell 不应全部 blocked
    EXPECT_FALSE(g.is_blocked(0, 0));
    EXPECT_FALSE(g.is_blocked(9, 9));

    // 圆障碍记录
    EXPECT_EQ(g.circles().size(), 1u);
    EXPECT_DOUBLE_EQ(g.circles()[0].radius, 1.5);

    // 反向: remove_blocked_cells 应该让 cell 通行
    g.remove_blocked_cells(cells);
    EXPECT_FALSE(g.is_blocked(5, 5));
}

TEST(NavGrid, FindPathStraightLine) {
    NavGrid g = make_grid(20, 20, 1.0);
    auto p = g.find_path({0.5, 0.5}, {19.5, 0.5});
    ASSERT_FALSE(p.empty());
    // 起终点替换
    EXPECT_DOUBLE_EQ(p.front().x, 0.5);
    EXPECT_DOUBLE_EQ(p.back().x, 19.5);
    // LOS simplify 后仅 2 个点
    EXPECT_EQ(p.size(), 2u);
}

TEST(NavGrid, FindPathAroundWall) {
    NavGrid g = make_grid(10, 10, 1.0);
    // 中央竖墙: x=5, y=0..7 blocked, 顶部 y=8,9 留缝
    for (int y = 0; y < 8; ++y) g.add_blocked(5, y);
    auto p = g.find_path({1.5, 1.5}, {8.5, 1.5});
    ASSERT_FALSE(p.empty());
    // 路径应绕过墙顶部, 中间至少有一个拐点
    EXPECT_GE(p.size(), 3u);
    // 终点正确
    EXPECT_DOUBLE_EQ(p.back().x, 8.5);
    EXPECT_DOUBLE_EQ(p.back().y, 1.5);
}

TEST(NavGrid, FindPathBlockedStartUsesNearestOpen) {
    NavGrid g = make_grid(10, 10, 1.0);
    g.add_blocked(0, 0);
    auto p = g.find_path({0.5, 0.5}, {5.5, 5.5});
    ASSERT_FALSE(p.empty());
    EXPECT_DOUBLE_EQ(p.back().x, 5.5);
    EXPECT_DOUBLE_EQ(p.back().y, 5.5);
}

TEST(NavGrid, FindPathReturnsEmptyWhenIsolated) {
    NavGrid g = make_grid(5, 5, 1.0);
    // 围 (2,2) 一圈
    for (int y = 1; y <= 3; ++y)
        for (int x = 1; x <= 3; ++x)
            if (!(x == 2 && y == 2)) g.add_blocked(x, y);
    auto p = g.find_path({2.5, 2.5}, {0.5, 0.5});
    EXPECT_TRUE(p.empty());
}

TEST(NavGrid, NoCornerCutting) {
    // (0,0) 通行, (1,0) 与 (0,1) blocked, (1,1) 通行: 不应允许从 (0,0) 对角走到 (1,1)
    NavGrid g = make_grid(3, 3, 1.0);
    g.add_blocked(1, 0);
    g.add_blocked(0, 1);
    auto p = g.find_path({0.5, 0.5}, {1.5, 1.5});
    EXPECT_TRUE(p.empty());
}

TEST(NavGrid, LineOfSightDetectsBlocker) {
    NavGrid g = make_grid(10, 10, 1.0);
    g.add_blocked(5, 5);
    EXPECT_FALSE(g.has_line_of_sight({0.5, 0.5}, {9.5, 9.5}));
    EXPECT_TRUE(g.has_line_of_sight({0.5, 0.5}, {0.5, 9.5}));
}
