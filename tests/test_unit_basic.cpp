#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <gtest/gtest.h>

using dota::OrderAttackTarget;
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
    s.base_attack_time = 1.0; // 测试中使用更简洁的数学计算
    s.base_armor       = 0.0;
    return s;
}

} // namespace

TEST(Unit, HullRadiusDefaultsTo24) {
    Unit u(1, "test", Team::Radiant, basic_stats(500.0));
    EXPECT_DOUBLE_EQ(u.hull_radius(), 24.0);
}

TEST(Unit, HullRadiusReadsStatsOverride) {
    UnitStats s = basic_stats();
    s.hull_radius = 27.0;
    Unit u(1, "fat", Team::Radiant, s);
    EXPECT_DOUBLE_EQ(u.hull_radius(), 27.0);
}

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

TEST(Unit, SetStatsClampsCurrentHealthAndMana) {
    UnitStats s = basic_stats(500.0);
    s.max_mana = 300.0;
    Unit u(1, "test", Team::Radiant, s);

    UnitStats reduced = s;
    reduced.max_health = 200.0;
    reduced.max_mana = 100.0;
    u.set_stats(reduced);

    EXPECT_DOUBLE_EQ(u.health(), 200.0);
    EXPECT_DOUBLE_EQ(u.mana(), 100.0);
}

TEST(Unit, SetManaClampsToCurrentMaxMana) {
    UnitStats s = basic_stats(500.0);
    s.max_mana = 150.0;
    Unit u(1, "test", Team::Radiant, s);

    u.set_mana(500.0);
    EXPECT_DOUBLE_EQ(u.mana(), 150.0);
    u.set_mana(-10.0);
    EXPECT_DOUBLE_EQ(u.mana(), 0.0);
}

TEST(World, AttackOrderDealsDamageAtExpectedCadence) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, basic_stats(1000.0, 50.0));
    auto* b = w.spawn("B", Team::Dire,    basic_stats(1000.0, 50.0));
    a->issue_order(OrderAttackTarget{b->id()});

    int lands = 0;
    w.events().subscribe<dota::AttackLandedEvent>(
        [&](dota::AttackLandedEvent& e) {
            EXPECT_EQ(e.attacker, a->id());
            EXPECT_EQ(e.victim,   b->id());
            EXPECT_GT(e.damage, 0.0);
            ++lands;
        });

    // base_attack_time=1, AS=100 → 每秒一次攻击. 第一次攻击在
    // 第一个 tick 时命中, 因为 attack_cd 从 0 开始; 后续攻击在
    // 冷却时间耗尽时触发.
    w.advance(3.5);

    EXPECT_EQ(lands, 4); // 在 ~0.03, 1.03, 2.03, 3.03 时攻击
    EXPECT_LT(b->health(), 1000.0);
}

TEST(World, UnitDiesAndFiresEventExactlyOnce) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, basic_stats(1000.0, 200.0));
    auto* b = w.spawn("B", Team::Dire,    basic_stats(400.0,  50.0));
    a->issue_order(OrderAttackTarget{b->id()});
    b->issue_order(OrderAttackTarget{a->id()});

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

    enemy1->apply_raw_damage(999999.0); // 尸体
    auto after_death = w.find_enemies_in_radius(hero->position(), 600.0, hero->team());
    ASSERT_EQ(after_death.size(), 1u);
    EXPECT_EQ(after_death[0]->id(), enemy2->id());

    EXPECT_TRUE(ally->alive()); // 未受影响
}
