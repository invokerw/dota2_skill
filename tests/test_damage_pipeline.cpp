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

// --- 类型抗性 --------------------------------------------------------

TEST(DamagePipeline, PhysicalRespectsArmorCurve) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    v->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *v, "armor_buff", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::ArmorBonus, 5.0}}));

    // 护甲=5 → 减伤 = 0.06*5/(1+0.06*5) = 0.3/1.3 ≈ 0.2308
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
    // 基础 0.25 + 0.25 = 0.50 → 200 魔法伤害 → 实际造成 100
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

// --- 魔法免疫 + 穿透 ------------------------------------------------

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

// --- 输出/承受伤害增幅 ------------------------------------------------

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
    // 100 * 1.20 * 1.10 = 132 纯粹伤害 → 实际造成 132
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

// --- 生命移除 -----------------------------------------------------------------

TEST(DamagePipeline, HPLossSkipsResistAndShield) {
    World w;
    auto* v = w.spawn("V", Team::Dire, stats(), {});
    // 一个吸收 300 点抗性前伤害的护盾
    v->modifiers().attach(std::make_unique<modifiers::ShieldAbsorb>(*v, 300.0, 30.0));
    // 魔法抗性 0.25 通常会将 200 减少到 150；HPLoss 绕过两者
    deal_damage({nullptr, v, DamageType::Magical, 200.0,
                  to_mask(DamageFlag::HPLoss)});
    EXPECT_NEAR(stats().max_health - v->health(), 200.0, 0.5);
}

// --- 反弹标记 --------------------------------------------------------

TEST(DamagePipeline, ReflectFlagSkipsReflectLoop) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, stats(), {});
    auto* v = w.spawn("V", Team::Dire,    stats(), {});
    // 两个单位都携带 50% 反弹的刃甲。如果没有反弹标记保护，
    // 它们会无限反弹
    a->modifiers().attach(modifiers::make_blade_mail(*a, 0.5, 30.0));
    v->modifiers().attach(modifiers::make_blade_mail(*v, 0.5, 30.0));

    const double a_before = a->health();
    const double v_before = v->health();
    deal_damage({a, v, DamageType::Pure, 100.0, 0});

    // v 受到 100 纯粹伤害。a 应该受到 50 反弹纯粹伤害，但反弹伤害
    // 携带反弹标记，所以 v 的刃甲不会再次反弹
    EXPECT_NEAR(v_before - v->health(), 100.0, 0.5);
    EXPECT_NEAR(a_before - a->health(), 50.0,  0.5);
}
