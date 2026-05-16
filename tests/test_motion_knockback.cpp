// Phase 5：Motion controller / 击退单测
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

using namespace dota;
using namespace dota::modifiers;

namespace {
UnitStats stats() {
    UnitStats s;
    s.max_health = 1000.0;
    return s;
}
} // namespace

TEST(Motion, KnockbackMovesOwnerOverDuration) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    hero->modifiers().attach(make_knockback(*hero, {1.0, 0.0}, /*distance=*/300.0, /*duration=*/1.0));

    w.advance(1.0);
    EXPECT_NEAR(hero->position().x, 300.0, 1.0);
    EXPECT_NEAR(hero->position().y, 0.0, 1e-6);
}

TEST(Motion, KnockbackAppliesStunDuringPush) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    hero->modifiers().attach(make_knockback(*hero, {0.0, 1.0}, 200.0, 0.5));

    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Stunned));
    w.advance(0.6);
    EXPECT_FALSE(hero->modifiers().has_state(ModifierState::Stunned));
}

TEST(Motion, HigherPriorityPreemptsExisting) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    auto* low  = hero->modifiers().attach(make_knockback(*hero, {1.0, 0.0}, 100.0, 1.0, /*prio=*/1));
    auto* high = hero->modifiers().attach(make_knockback(*hero, {0.0, 1.0}, 100.0, 1.0, /*prio=*/5));
    (void)low; (void)high;

    w.advance(0.5);
    // 高优先级 MC 替代了低优先级 — 移动方向为 +y 而非 +x
    EXPECT_LT(hero->position().x, 5.0);     // 没怎么往 x 推
    EXPECT_GT(hero->position().y, 30.0);
}

TEST(Motion, LowerPriorityRejectedAgainstActive) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    hero->modifiers().attach(make_knockback(*hero, {1.0, 0.0}, 100.0, 1.0, /*prio=*/5));
    auto* low = hero->modifiers().attach(make_knockback(*hero, {0.0, 1.0}, 100.0, 1.0, /*prio=*/1));
    EXPECT_EQ(low, nullptr);
}
