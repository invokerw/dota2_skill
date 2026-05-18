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
UnitStats stats() {
    UnitStats s;
    s.max_health = 500.0;
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
