// Phase 3：投射物系统单测
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace dota;

namespace {
UnitStats stats() {
    UnitStats s;
    s.max_health = 1000.0;
    return s;
}
} // namespace

TEST(Projectile, LinearHitsEnemyAndFires) {
    World w;
    auto* hero  = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    auto* enemy = w.spawn("e1", Team::Dire, stats(), {300, 10});

    int hits = 0;
    LinearProjectile::Params p;
    p.source_id   = hero->id();
    p.source_team = Team::Radiant;
    p.origin      = {0, 0};
    p.direction   = {1.0, 0.0};
    p.speed       = 1500.0;
    p.length      = 1500.0;
    p.width       = 100.0;
    p.destroy_on_first_hit = false;
    auto proj = std::make_unique<LinearProjectile>(p);
    proj->set_on_hit([&](Unit& victim, Vec2 /*point*/) {
        ++hits;
        EXPECT_EQ(victim.id(), enemy->id());
    });
    w.projectiles().spawn(std::move(proj));

    w.advance(1.5);   // 应已飞完
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(w.projectiles().live_count(), 0u);
}

TEST(Projectile, LinearPiercesMultipleEnemies) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    w.spawn("e1", Team::Dire, stats(), {200, 0});
    w.spawn("e2", Team::Dire, stats(), {600, 0});
    w.spawn("e3", Team::Dire, stats(), {900, 0});
    (void)hero;

    int hits = 0;
    LinearProjectile::Params p;
    p.source_team = Team::Radiant;
    p.origin = {0,0}; p.direction = {1,0};
    p.speed = 2000; p.length = 2000; p.width = 80;
    p.destroy_on_first_hit = false;
    auto proj = std::make_unique<LinearProjectile>(p);
    proj->set_on_hit([&](Unit& /*v*/, Vec2 /*pt*/) { ++hits; });
    w.projectiles().spawn(std::move(proj));

    w.advance(1.0);
    EXPECT_EQ(hits, 3);
}

TEST(Projectile, LinearStopsOnFirstHit) {
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0, 0});
    w.spawn("e1", Team::Dire, stats(), {200, 0});
    w.spawn("e2", Team::Dire, stats(), {600, 0});

    int hits = 0;
    LinearProjectile::Params p;
    p.source_team = Team::Radiant;
    p.origin = {0,0}; p.direction = {1,0};
    p.speed = 2000; p.length = 2000; p.width = 80;
    p.destroy_on_first_hit = true;
    auto proj = std::make_unique<LinearProjectile>(p);
    proj->set_on_hit([&](Unit& /*v*/, Vec2 /*pt*/) { ++hits; });
    w.projectiles().spawn(std::move(proj));

    w.advance(1.0);
    EXPECT_EQ(hits, 1);
}

TEST(Projectile, TrackingHitsTarget) {
    World w;
    auto* hero  = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    auto* enemy = w.spawn("e1", Team::Dire, stats(), {500, 0});

    bool hit = false;
    TrackingProjectile::Params p;
    p.source_id = hero->id(); p.source_team = Team::Radiant;
    p.origin = hero->position();
    p.target_id = enemy->id();
    p.speed = 1000.0;
    auto proj = std::make_unique<TrackingProjectile>(p);
    proj->set_on_hit([&](Unit& v, Vec2 /*pt*/) {
        hit = true;
        EXPECT_EQ(v.id(), enemy->id());
    });
    w.projectiles().spawn(std::move(proj));

    w.advance(1.0);
    EXPECT_TRUE(hit);
}

TEST(Projectile, TrackingFizzlesOnTargetDeath) {
    World w;
    auto* hero  = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    auto* enemy = w.spawn("e1", Team::Dire, stats(), {500, 0});

    bool hit = false, finished = false;
    TrackingProjectile::Params p;
    p.source_id = hero->id(); p.source_team = Team::Radiant;
    p.origin = hero->position();
    p.target_id = enemy->id();
    p.speed = 200.0;          // 慢，给我们时间杀掉目标
    auto proj = std::make_unique<TrackingProjectile>(p);
    proj->set_on_hit([&](Unit&, Vec2){ hit = true; });
    proj->set_on_finish([&](){ finished = true; });
    w.projectiles().spawn(std::move(proj));

    enemy->apply_raw_damage(99999.0);
    w.advance(2.0);
    EXPECT_FALSE(hit);
    EXPECT_TRUE(finished);
}
