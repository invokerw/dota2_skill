#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats stats_with_armor(double armor) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.attack_damage    = 50.0;
    s.base_armor       = armor;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.move_speed       = 300.0;
    return s;
}

} // namespace

TEST(ModifierProperty, ConstantArmorBonusesStack) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, stats_with_armor(5.0));

    u->modifiers().attach_new<modifiers::GenericStats>(
        "armor_small", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 3.0}});
    u->modifiers().attach_new<modifiers::GenericStats>(
        "armor_large", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 7.0}});

    EXPECT_DOUBLE_EQ(u->armor(), 15.0); // base 5 + 3 + 7
}

TEST(ModifierProperty, PercentageBonusMultipliesAfterConstant) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, stats_with_armor(10.0));

    u->modifiers().attach_new<modifiers::GenericStats>(
        "armor_flat", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 10.0}});        // 10 + 10 = 20
    u->modifiers().attach_new<modifiers::GenericStats>(
        "armor_pct", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonusPct, 0.5}});       // 20 * 1.5 = 30

    EXPECT_DOUBLE_EQ(u->armor(), 30.0);
}

TEST(ModifierProperty, AttackDamageRoutesThroughAggregator) {
    World w;
    UnitStats s = stats_with_armor(0.0);
    s.attack_damage = 40.0;
    auto* u = w.spawn("u", Team::Radiant, s);

    u->modifiers().attach_new<modifiers::GenericStats>(
        "bonus", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::AttackDamageBonus, 20.0},
            {ModifierProperty::AttackDamageBonusPct, 0.25}});

    EXPECT_DOUBLE_EQ(u->attack_damage(), (40.0 + 20.0) * 1.25); // 75.0
}

TEST(ModifierProperty, AttackSpeedBonusShrinksBaseAttackTime) {
    World w;
    UnitStats s = stats_with_armor(0.0);
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.4;
    auto* u = w.spawn("u", Team::Radiant, s);

    EXPECT_DOUBLE_EQ(u->seconds_per_attack(), 1.4); // 1.4 / (100/100)

    u->modifiers().attach_new<modifiers::GenericStats>(
        "as_buff", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::AttackSpeedBonusConstant, 100.0}});

    // 1.4 / ((100 + 100)/100) = 0.7
    EXPECT_DOUBLE_EQ(u->seconds_per_attack(), 0.7);
}

TEST(ModifierProperty, StackCountMultipliesContribution) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, stats_with_armor(0.0));

    auto* m = u->modifiers().attach_new<modifiers::GenericStats>(
        "stacking", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 2.0}});

    EXPECT_DOUBLE_EQ(u->armor(), 2.0);
    m->set_stack_count(5);
    EXPECT_DOUBLE_EQ(u->armor(), 10.0); // 2 * 5
}

TEST(ModifierProperty, HealthBonusRaisesMaxHealthCap) {
    World w;
    UnitStats s = stats_with_armor(0.0);
    s.max_health = 500.0;
    auto* u = w.spawn("u", Team::Radiant, s);

    u->modifiers().attach_new<modifiers::GenericStats>(
        "vit", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::HealthBonus, 250.0}});

    EXPECT_DOUBLE_EQ(u->max_health(), 750.0);
    u->heal(9999.0);
    EXPECT_DOUBLE_EQ(u->health(), 750.0); // heal caps at aggregated max
}
