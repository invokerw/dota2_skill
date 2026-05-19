// Phase 1: 空间查询 line / cone 单测
#include "dota/core/world.hpp"

#include <gtest/gtest.h>

#include <cmath>

using dota::Team;
using dota::Unit;
using dota::UnitStats;
using dota::Vec2;
using dota::World;

namespace {
// 默认 hull_radius=0, 让原有几何精确边界测试不受 24 默认值干扰.
// hull_radius>0 的覆盖测试在文件末尾另外用 fat_stats() 显式构造.
UnitStats stats() {
    UnitStats s;
    s.max_health  = 500.0;
    s.hull_radius = 0.0;
    return s;
}
UnitStats fat_stats(double hull) {
    UnitStats s = stats();
    s.hull_radius = hull;
    return s;
}
} // namespace

TEST(SpatialQuery, LineHitsEnemyOnPath) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* enemy = w.spawn("e1", Team::Dire, stats(), {500.0, 30.0});
    auto* far_off = w.spawn("e2", Team::Dire, stats(), {500.0, 200.0});
    (void)hero; (void)far_off;

    auto hit = w.find_enemies_in_line({0.0, 0.0}, {1000.0, 0.0}, /*width=*/100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), enemy->id());
}

TEST(SpatialQuery, LineExcludesAllies) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    w.spawn("ally", Team::Radiant, stats(), {300.0, 0.0});
    auto* enemy = w.spawn("e1", Team::Dire, stats(), {500.0, 0.0});

    auto hit = w.find_enemies_in_line({0.0, 0.0}, {1000.0, 0.0}, 100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), enemy->id());
}

TEST(SpatialQuery, LineWidthBoundary) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* on_edge = w.spawn("e_edge", Team::Dire, stats(), {500.0, 49.9});
    auto* over   = w.spawn("e_over", Team::Dire, stats(), {500.0, 60.0});
    (void)over;

    auto hit = w.find_enemies_in_line({0.0, 0.0}, {1000.0, 0.0}, 100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), on_edge->id());
}

TEST(SpatialQuery, LineEndpointClampsT) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    // 在线段终点之外, 但与终点本身距离 < width/2 -- 投影 t 被 clamp 到 1.0, 依然命中.
    auto* past_end_close = w.spawn("close", Team::Dire, stats(), {1010.0, 0.0});
    auto* past_end_far   = w.spawn("far", Team::Dire, stats(), {1100.0, 0.0});
    (void)past_end_far;

    auto hit = w.find_enemies_in_line({0.0, 0.0}, {1000.0, 0.0}, 100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), past_end_close->id());
}

TEST(SpatialQuery, ConeIncludesFront) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* front = w.spawn("front", Team::Dire, stats(), {300.0, 0.0});
    auto* side  = w.spawn("side", Team::Dire, stats(), {0.0, 300.0});
    (void)side;

    // 90 度圆锥(half=45°), 向 +x 朝向
    const double half = M_PI / 4.0;
    auto hit = w.find_enemies_in_cone({0.0, 0.0}, {1.0, 0.0}, 500.0, half, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), front->id());
}

TEST(SpatialQuery, ConeBoundedByLength) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* close = w.spawn("close", Team::Dire, stats(), {200.0, 0.0});
    auto* far   = w.spawn("far", Team::Dire, stats(), {800.0, 0.0});
    (void)far;

    auto hit = w.find_enemies_in_cone({0.0, 0.0}, {1.0, 0.0}, 400.0, M_PI / 2.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), close->id());
}

TEST(SpatialQuery, ConeIncludesPointAtOrigin) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* on_top = w.spawn("on", Team::Dire, stats(), {0.0, 0.0});
    (void)hero;

    auto hit = w.find_enemies_in_cone({0.0, 0.0}, {1.0, 0.0}, 100.0, M_PI / 4.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), on_top->id());
}

// --- hull_radius 接入测试 ---

TEST(SpatialQuery, RadiusUsesTargetHullRadius) {
    // 圆心在 520, 但 hull=30, 边缘伸到 490, 落在 500 半径内 -> 命中
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* in_edge  = w.spawn("e_in",  Team::Dire, fat_stats(30.0), {520.0, 0.0});
    auto* far_off  = w.spawn("e_far", Team::Dire, fat_stats(10.0), {520.0, 0.0});
    (void)far_off; // hull=10, 边缘到 510, 在 500 外 -> 不命中

    auto hit = w.find_enemies_in_radius({0.0, 0.0}, 500.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), in_edge->id());
}

TEST(SpatialQuery, LineUsesTargetHullRadius) {
    // line width=100 (半宽 50), 单位圆心 y=70 但 hull=30 -> 距线 70-30=40 ≤ 50, 命中
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* fat_e   = w.spawn("fat",   Team::Dire, fat_stats(30.0), {500.0, 70.0});
    auto* slim_e  = w.spawn("slim",  Team::Dire, fat_stats(10.0), {500.0, 70.0});
    (void)slim_e; // hull=10, 边缘 60 > 50 半宽 -> 不命中

    auto hit = w.find_enemies_in_line({0.0, 0.0}, {1000.0, 0.0}, 100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), fat_e->id());
}

TEST(SpatialQuery, ConeIncludesUnitOutsideAngleButTangentToBoundaryRay) {
    // 圆心位于 cone 边界射线之外, 但圆与边界射线相切
    // 锥: 朝 +x, half=45°, length=500, 边界射线 +y 方向斜 45°
    // 边界射线方向: (cos45, sin45) ≈ (0.7071, 0.7071)
    // 取圆心在射线"上方"(逆时针方向之外), 沿 normal (-sin45, cos45) 偏离 40 单位.
    // 射线上 t=300 处的点 (212.13, 212.13), normal 方向偏 40 -> (212.13-28.28, 212.13+28.28)
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    const double t = 300.0;
    const double s = std::sqrt(0.5);
    const Vec2 ray_pt{ s * t, s * t };
    const double off = 40.0;          // 离边界射线垂直距离 40
    const Vec2 P{ ray_pt.x + (-s) * off, ray_pt.y + s * off };
    auto* fat_e = w.spawn("fat", Team::Dire, fat_stats(50.0), P);  // hull=50 > 40 -> 切入
    auto* slim_e = w.spawn("slim", Team::Dire, fat_stats(20.0),
                           Vec2{P.x + 200.0, P.y + 200.0});         // 远离, 不命中
    (void)slim_e;

    auto hit = w.find_enemies_in_cone({0.0, 0.0}, {1.0, 0.0}, 500.0, M_PI / 4.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), fat_e->id());
}

TEST(SpatialQuery, ConeRejectsBehindEvenWithLargeHull) {
    // 单位整个在锥后方, 即使 hull 大, 也不应命中
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    w.spawn("behind", Team::Dire, fat_stats(80.0), {-200.0, 0.0});

    auto hit = w.find_enemies_in_cone({0.0, 0.0}, {1.0, 0.0}, 500.0, M_PI / 4.0, Team::Radiant);
    EXPECT_TRUE(hit.empty());
}
