#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats basic_stats() {
    UnitStats s;
    s.max_health       = 1000.0;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    s.attack_speed     = 100.0;
    return s;
}

} // namespace

TEST(ModifierState, StunGatesAttackAndCast) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    EXPECT_TRUE(u->can_attack());
    EXPECT_TRUE(u->can_cast());

    u->modifiers().attach(modifiers::make_stunned(*u, 1.5));
    EXPECT_FALSE(u->can_attack());
    EXPECT_FALSE(u->can_cast());
    EXPECT_FALSE(u->can_move());
}

TEST(ModifierState, SilenceOnlyBlocksCasting) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());

    u->modifiers().attach(modifiers::make_silenced(*u, 3.0));
    EXPECT_FALSE(u->can_cast());
    EXPECT_TRUE(u->can_attack());
    EXPECT_TRUE(u->can_move());
}

TEST(ModifierState, RootStopsMovementOnly) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    u->modifiers().attach(modifiers::make_rooted(*u, 2.0));
    EXPECT_FALSE(u->can_move());
    EXPECT_TRUE(u->can_attack());
    EXPECT_TRUE(u->can_cast());
}

TEST(ModifierState, HexBlocksBothAttackAndCastPermitsMovement) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    u->modifiers().attach(modifiers::make_hexed(*u, 2.0));
    EXPECT_FALSE(u->can_attack());
    EXPECT_FALSE(u->can_cast());
    EXPECT_TRUE(u->can_move());
}

TEST(ModifierState, DurationExpiresAndUnblocksActions) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    u->modifiers().attach(modifiers::make_stunned(*u, 1.0));
    EXPECT_FALSE(u->can_cast());

    w.advance(0.9);
    EXPECT_FALSE(u->can_cast());
    w.advance(0.2); // 总计 1.1 秒 -> 过期并清除
    EXPECT_TRUE(u->can_cast());
    EXPECT_EQ(u->modifiers().find("modifier_stunned"), nullptr);
}

TEST(ModifierState, StunOverlapsDoNotEndEarly) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* first = u->modifiers().attach(modifiers::make_stunned(*u, 0.5));
    auto* second = u->modifiers().attach(modifiers::make_stunned(*u, 1.5));
    (void)first; (void)second;

    w.advance(1.0);
    EXPECT_FALSE(u->can_cast()); // 第二个眩晕仍在持续
    w.advance(0.7);
    EXPECT_TRUE(u->can_cast());
}

TEST(ModifierState, StunnedAttackerStopsSwingingInWorld) {
    World w;
    auto* a = w.spawn("A", Team::Radiant, basic_stats());
    auto* b = w.spawn("B", Team::Dire,    basic_stats());
    w.order_attack(*a, *b);

    int hits = 0;
    w.events().subscribe<dota::AttackLandedEvent>(
        [&](dota::AttackLandedEvent&) { ++hits; });

    w.advance(0.5);             // 一次早期命中
    const int hits_before_stun = hits;
    EXPECT_GE(hits_before_stun, 1);

    a->modifiers().attach(modifiers::make_stunned(*a, 1.5));
    w.advance(1.0);             // 完全眩晕期间 -> 没有新的命中
    EXPECT_EQ(hits, hits_before_stun);

    w.advance(2.0);             // 眩晕过期 -> 攻击恢复
    EXPECT_GT(hits, hits_before_stun);
}
