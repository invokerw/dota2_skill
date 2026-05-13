#include "dota/combat/damage.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace dota;

namespace {

UnitStats stats() {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 500.0;
    s.base_armor       = 0.0;
    s.magic_resist     = 0.25;
    s.attack_damage    = 100.0;
    s.base_attack_time = 1.0;
    return s;
}

} // namespace

// --- Type resistance --------------------------------------------------------

TEST(DamagePipeline, PhysicalRespectsArmorCurve) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    v->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *v, "armor_buff", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 5.0}}));

    // armor=5 → reduction = 0.06*5/(1+0.06*5) = 0.3/1.3 ≈ 0.2308
    const double before = v->health();
    deal_damage({a, v, DamageType::Physical, 100.0, 0});
    const double dealt = before - v->health();
    EXPECT_NEAR(dealt, 100.0 * (1.0 - 0.2307692), 0.5);
}

TEST(DamagePipeline, MagicResistHalvesMagical) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    v->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *v, "mr_buff", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::MagicResistBonus, 0.25}}));
    // base 0.25 + 0.25 = 0.50 → 200 magical → 100 applied.
    deal_damage({nullptr, v, DamageType::Magical, 200.0, 0});
    EXPECT_NEAR(stats().max_health - v->health(), 100.0, 0.5);
}

TEST(DamagePipeline, PureIgnoresResistance) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    v->modifiers().attach(modifiers::make_magic_immune(*v, 10.0));
    deal_damage({nullptr, v, DamageType::Pure, 250.0, 0});
    EXPECT_NEAR(stats().max_health - v->health(), 250.0, 0.5);
}

// --- Magic immunity + Bypass ------------------------------------------------

TEST(DamagePipeline, MagicImmuneZerosOutMagical) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    v->modifiers().attach(modifiers::make_magic_immune(*v, 10.0));
    deal_damage({nullptr, v, DamageType::Magical, 500.0, 0});
    EXPECT_DOUBLE_EQ(v->health(), stats().max_health);
}

TEST(DamagePipeline, BypassMagicImmuneFlagStillApplies) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    v->modifiers().attach(modifiers::make_magic_immune(*v, 10.0));
    deal_damage({nullptr, v, DamageType::Magical, 200.0,
                  to_mask(DamageFlag::BypassMagicImmune)});
    // 200 * (1 - 0.25) = 150
    EXPECT_NEAR(stats().max_health - v->health(), 150.0, 0.5);
}

// --- Outgoing / incoming amp ------------------------------------------------

TEST(DamagePipeline, OutgoingAndIncomingAmpStack) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    a->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *a, "out_amp", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::OutgoingDamagePct, 0.20}})); // +20%
    v->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *v, "in_amp", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::IncomingDamagePct, 0.10}})); // +10%
    // 100 * 1.20 * 1.10 = 132 pure → 132 applied.
    deal_damage({a, v, DamageType::Pure, 100.0, 0});
    EXPECT_NEAR(stats().max_health - v->health(), 132.0, 0.5);
}

TEST(DamagePipeline, NoSpellAmpFlagSkipsBothPcts) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    a->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *a, "out_amp", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::OutgoingDamagePct, 0.50}}));
    v->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *v, "in_amp", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::IncomingDamagePct, 0.50}}));
    deal_damage({a, v, DamageType::Pure, 100.0,
                  to_mask(DamageFlag::NoSpellAmplification)});
    EXPECT_NEAR(stats().max_health - v->health(), 100.0, 0.5);
}

// --- HPLoss -----------------------------------------------------------------

TEST(DamagePipeline, HPLossSkipsResistAndShield) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    // A shield that absorbs 300 pre-resist damage.
    v->modifiers().attach(std::make_unique<modifiers::ShieldAbsorb>(*v, 300.0, 30.0));
    // magic resist 0.25 would normally halve 200 to 150; HPLoss bypasses both.
    deal_damage({nullptr, v, DamageType::Magical, 200.0,
                  to_mask(DamageFlag::HPLoss)});
    EXPECT_NEAR(stats().max_health - v->health(), 200.0, 0.5);
}

// --- Reflection flag --------------------------------------------------------

TEST(DamagePipeline, ReflectFlagSkipsReflectLoop) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    // Both units carry Blade Mail at 50% reflect. Without the Reflection flag
    // guard, they'd bounce forever.
    a->modifiers().attach(modifiers::make_blade_mail(*a, 0.5, 30.0));
    v->modifiers().attach(modifiers::make_blade_mail(*v, 0.5, 30.0));

    const double a_before = a->health();
    const double v_before = v->health();
    deal_damage({a, v, DamageType::Pure, 100.0, 0});

    // v took 100 pure. a should take 50 reflected pure, but the reflected
    // damage carries Reflection flag so v's Blade Mail does NOT reflect again.
    EXPECT_NEAR(v_before - v->health(), 100.0, 0.5);
    EXPECT_NEAR(a_before - a->health(), 50.0,  0.5);
}
