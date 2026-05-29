// ShapeCast 解析解测试: 圆-圆 swept, 圆-AABB swept, group 过滤, 多目标 toi 取最小
#include "dota/pathfinding/nav_grid.hpp"
#include "dota/pathfinding/shape_cast.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace dota;
using namespace dota::pathfinding;

namespace {
NavGrid empty_grid() {
    return NavGrid(0.0, 0.0, 100.0, 100.0, 1.0);
}
}

TEST(ShapeCast, MissesEmptyGrid) {
    auto g = empty_grid();
    auto h = shape_cast_circle(g, {}, kInvalidEntityId,
                                {10.0, 10.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::All);
    EXPECT_FALSE(h.hit);
}

TEST(ShapeCast, HitsAxisAlignedCellByFace) {
    auto g = empty_grid();
    g.add_blocked(20, 5);   // cell AABB x=[20,21], y=[5,6]
    // 起点 (10, 5.5), 沿 +X, radius=0.5. 命中点 = 圆与左 face 切, x = 20 - 0.5 = 19.5
    // toi = 19.5 - 10 = 9.5
    auto h = shape_cast_circle(g, {}, kInvalidEntityId,
                                {10.0, 5.5}, {1.0, 0.0}, 50.0, 0.5,
                                CollisionGroups::Terrain);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 9.5, 1e-9);
    EXPECT_EQ(h.kind, ShapeCastHit::Kind::Terrain);
}

TEST(ShapeCast, HitsCellCornerOnDiagonal) {
    // cell AABB x=[5,6], y=[5,6]. 起点 (0,0), dir = (1,1)/sqrt(2), radius=0.5.
    // 圆-圆 swept (角点 (5,5) 半径 0.5): |o + t*d| = 0.5
    // o = (-5, -5), o·d = -5*sqrt(2), |o|^2 = 50, R^2 = 0.25
    // disc = 50 - (50 - 0.25) = 0.25, t = 5*sqrt(2) - 0.5 ≈ 6.5710678
    auto g = empty_grid();
    g.add_blocked(5, 5);
    const double inv2 = 1.0 / std::sqrt(2.0);
    auto h = shape_cast_circle(g, {}, kInvalidEntityId,
                                {0.0, 0.0}, {inv2, inv2}, 20.0, 0.5,
                                CollisionGroups::Terrain);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 5.0 * std::sqrt(2.0) - 0.5, 1e-9);
}

TEST(ShapeCast, HitsCircleObstacleAnalytical) {
    auto g = empty_grid();
    g.add_circle_obstacle({30.0, 0.0}, 3.0);
    // 起点 (0,0), +X, radius=1, 圆心 (30,0) 半径 3. R=4. toi = 30 - 4 = 26.
    auto h = shape_cast_circle(g, {}, kInvalidEntityId,
                                {0.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::Terrain);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 26.0, 1e-9);
}

TEST(ShapeCast, HitsDynamicUnit) {
    auto g = empty_grid();
    DynamicCircle d{{20.0, 0.0}, 2.0, CollisionGroups::All, 42};
    auto h = shape_cast_circle(g, {d}, kInvalidEntityId,
                                {0.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::All);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 17.0, 1e-9);  // 20 - (1+2)
    EXPECT_EQ(h.kind, ShapeCastHit::Kind::Unit);
    EXPECT_EQ(h.unit, 42u);
}

TEST(ShapeCast, IgnoreIdSkipsSelf) {
    auto g = empty_grid();
    DynamicCircle me{{20.0, 0.0}, 2.0, CollisionGroups::All, 42};
    auto h = shape_cast_circle(g, {me}, /*ignore=*/42,
                                {0.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::All);
    EXPECT_FALSE(h.hit);
}

TEST(ShapeCast, GroupFilterTerrainSkipsUnitOnlyBody) {
    auto g = empty_grid();
    DynamicCircle u{{20.0, 0.0}, 2.0, CollisionGroups::Unit, 1};
    auto h = shape_cast_circle(g, {u}, kInvalidEntityId,
                                {0.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::Terrain);
    EXPECT_FALSE(h.hit);
}

TEST(ShapeCast, GroupFilterTerrainHitsTerrainGroupBody) {
    // 静止 unit 同时挂 Terrain | Unit, query=Terrain 时应能命中
    auto g = empty_grid();
    DynamicCircle u{{20.0, 0.0}, 2.0, CollisionGroups::All, 1};
    auto h = shape_cast_circle(g, {u}, kInvalidEntityId,
                                {0.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::Terrain);
    ASSERT_TRUE(h.hit);
    EXPECT_EQ(h.unit, 1u);
}

TEST(ShapeCast, MultipleTargetsTakeNearest) {
    auto g = empty_grid();
    g.add_blocked(40, 5);  // cell, far
    DynamicCircle near_unit{{15.0, 5.5}, 1.0, CollisionGroups::All, 7};
    auto h = shape_cast_circle(g, {near_unit}, kInvalidEntityId,
                                {0.0, 5.5}, {1.0, 0.0}, 60.0, 0.5,
                                CollisionGroups::All);
    ASSERT_TRUE(h.hit);
    EXPECT_NEAR(h.toi, 13.5, 1e-9);  // 15 - (0.5 + 1.0) = 13.5
    EXPECT_EQ(h.kind, ShapeCastHit::Kind::Unit);
    EXPECT_EQ(h.unit, 7u);
}

TEST(ShapeCast, OverlappingAtStartReturnsZero) {
    auto g = empty_grid();
    DynamicCircle d{{0.0, 0.0}, 1.0, CollisionGroups::All, 1};
    auto h = shape_cast_circle(g, {d}, kInvalidEntityId,
                                {0.5, 0.0}, {1.0, 0.0}, 10.0, 1.0,
                                CollisionGroups::All);
    ASSERT_TRUE(h.hit);
    EXPECT_DOUBLE_EQ(h.toi, 0.0);
}

TEST(ShapeCast, MovingAwayMisses) {
    auto g = empty_grid();
    DynamicCircle d{{0.0, 0.0}, 1.0, CollisionGroups::All, 1};
    // 圆心在 (0,0), 起点 (5,0) 沿 +X 离开 -> 不命中
    auto h = shape_cast_circle(g, {d}, kInvalidEntityId,
                                {5.0, 0.0}, {1.0, 0.0}, 50.0, 1.0,
                                CollisionGroups::All);
    EXPECT_FALSE(h.hit);
}
