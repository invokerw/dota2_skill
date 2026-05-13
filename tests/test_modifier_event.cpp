#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats basic_stats(double hp = 1000.0) {
    UnitStats s;
    s.max_health       = hp;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    s.attack_speed     = 100.0;
    s.base_armor       = 0.0;
    s.magic_resist     = 0.25;
    return s;
}

} // namespace

TEST(ModifierEvent, ShieldAbsorbsDamageBeforeResistance) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats(1000.0));
    auto* shield = u->modifiers().attach_new<modifiers::ShieldAbsorb>(200.0, -1.0);

    const double applied = u->apply_damage(DamageType::Magical, 300.0);
    // 200 eaten by shield; remaining 100 hits magic resist (25% => 75 applied)
    EXPECT_DOUBLE_EQ(applied, 75.0);
    EXPECT_DOUBLE_EQ(shield->remaining(), 0.0);

    // Shield now consumed; the unit should no longer have the modifier after
    // an advance() purges it.
    w.advance(0.1);
    EXPECT_EQ(u->modifiers().find("modifier_shield_absorb"), nullptr);
}

TEST(ModifierEvent, MagicImmuneBlocksMagicalFully) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats(1000.0));
    u->modifiers().attach(modifiers::make_magic_immune(*u, 3.0));

    const double applied = u->apply_damage(DamageType::Magical, 500.0);
    EXPECT_DOUBLE_EQ(applied, 0.0);
    EXPECT_DOUBLE_EQ(u->health(), 1000.0);

    // Physical still goes through.
    const double phys = u->apply_damage(DamageType::Physical, 100.0);
    EXPECT_GT(phys, 0.0);
}

TEST(ModifierEvent, PostDamageEventCarriesFinalAmount) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats(1000.0));

    struct PostSpy : Modifier {
        double seen = -1.0;
        PostSpy(Unit& o) : Modifier("spy", o, -1.0) {}
        void on_post_take_damage(PostTakeDamageEvent& ev) override {
            seen = ev.amount;
        }
    };
    auto* spy = u->modifiers().attach_new<PostSpy>();

    u->apply_damage(DamageType::Pure, 123.0);
    EXPECT_DOUBLE_EQ(spy->seen, 123.0); // pure bypasses resistance
}

TEST(ModifierEvent, ArmorReducesBasicAttackThroughPipeline) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats(1000.0));

    // +20 armor via modifier -> ~54.5% physical reduction.
    u->modifiers().attach_new<modifiers::GenericStats>(
        "plated", -1.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 20.0}});

    const double applied = u->apply_damage(DamageType::Physical, 100.0);
    // expected mult = 1 - (0.06*20)/(1 + 0.06*20) = 1 - 1.2/2.2 ≈ 0.4545
    EXPECT_NEAR(applied, 45.45, 0.1);
}
