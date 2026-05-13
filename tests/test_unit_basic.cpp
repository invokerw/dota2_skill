#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <gtest/gtest.h>

using dota::Team;
using dota::Unit;
using dota::UnitStats;
using dota::World;

namespace {

UnitStats basic_stats(double hp = 500.0, double dmg = 50.0) {
    UnitStats s;
    s.max_health       = hp;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0; // cleaner math for tests
    s.base_armor       = 0.0;
    return s;
}

} // namespace

TEST(Unit, StartsAtFullHealth) {
    Unit u(1, "test", Team::Radiant, basic_stats(750.0));
    EXPECT_EQ(u.health(), 750.0);
    EXPECT_TRUE(u.alive());
}

TEST(Unit, ApplyRawDamageClampsToZeroAndMarksDead) {
    Unit u(1, "test", Team::Radiant, basic_stats(100.0));
    const double applied = u.apply_raw_damage(150.0);
    EXPECT_DOUBLE_EQ(applied, 100.0);
    EXPECT_DOUBLE_EQ(u.health(), 0.0);
    EXPECT_FALSE(u.alive());
}

TEST(Unit, HealDoesNotExceedMaxHealth) {
    Unit u(1, "test", Team::Radiant, basic_stats(500.0));
    u.apply_raw_damage(200.0);
    u.heal(1000.0);
    EXPECT_DOUBLE_EQ(u.health(), 500.0);
}

TEST(World, AttackOrderDealsDamageAtExpectedCadence) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, basic_stats(1000.0, 50.0));
    auto* b = w.spawn("B", Team::Dire,    basic_stats(1000.0, 50.0));
    w.order_attack(*a, *b);

    int lands = 0;
    w.events().subscribe<dota::AttackLandedEvent>(
        [&](dota::AttackLandedEvent& e) {
            EXPECT_EQ(e.attacker, a->id());
            EXPECT_EQ(e.victim,   b->id());
            EXPECT_GT(e.damage, 0.0);
            ++lands;
        });

    // base_attack_time=1, AS=100 → one swing per second. First swing lands on
    // the first tick because attack_cd starts at 0; further swings fire each
    // time the cooldown drains.
    w.advance(3.5);

    EXPECT_EQ(lands, 4); // swings at ~0.03, 1.03, 2.03, 3.03
    EXPECT_LT(b->health(), 1000.0);
}

TEST(World, UnitDiesAndFiresEventExactlyOnce) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, basic_stats(1000.0, 200.0));
    auto* b = w.spawn("B", Team::Dire,    basic_stats(400.0,  50.0));
    w.order_attack(*a, *b);
    w.order_attack(*b, *a);

    int deaths = 0;
    dota::EntityId dead_id = 0;
    w.events().subscribe<dota::UnitDiedEvent>(
        [&](dota::UnitDiedEvent& e) {
            ++deaths;
            dead_id = e.victim;
        });

    w.advance(5.0);

    EXPECT_EQ(deaths, 1);
    EXPECT_EQ(dead_id, b->id());
    EXPECT_FALSE(b->alive());
    EXPECT_TRUE(a->alive());
}

TEST(World, FindEnemiesInRadiusExcludesAlliesAndCorpses) {
    World w;
    auto* hero   = w.spawn("hero",   Team::Radiant, basic_stats(), {0.0, 0.0});
    auto* ally   = w.spawn("ally",   Team::Radiant, basic_stats(), {50.0, 0.0});
    auto* enemy1 = w.spawn("enemy1", Team::Dire,    basic_stats(), {100.0, 0.0});
    auto* enemy2 = w.spawn("enemy2", Team::Dire,    basic_stats(), {500.0, 0.0});

    auto in_range = w.find_enemies_in_radius(hero->position(), 200.0, hero->team());
    ASSERT_EQ(in_range.size(), 1u);
    EXPECT_EQ(in_range[0]->id(), enemy1->id());

    enemy1->apply_raw_damage(999999.0); // corpse
    auto after_death = w.find_enemies_in_radius(hero->position(), 600.0, hero->team());
    ASSERT_EQ(after_death.size(), 1u);
    EXPECT_EQ(after_death[0]->id(), enemy2->id());

    EXPECT_TRUE(ally->alive()); // untouched
}
