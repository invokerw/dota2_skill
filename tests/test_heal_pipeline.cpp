#include "dota/combat/damage.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats stats() {
    UnitStats s;
    s.max_health = 1000.0;
    s.max_mana   = 200.0;
    return s;
}

Unit* wounded(World& w) {
    auto* u = w.spawn("U", Team::Radiant, stats(), {});
    u->set_health(500.0);
    return u;
}

} // namespace

TEST(HealPipeline, BaseHealRestoresHp) {
    World w;
    auto* u = wounded(w);
    const double applied = deal_heal({nullptr, u, 200.0});
    EXPECT_DOUBLE_EQ(applied, 200.0);
    EXPECT_DOUBLE_EQ(u->health(), 700.0);
}

TEST(HealPipeline, BreakHealingReducesIncomingHeals) {
    World w;
    auto* u = wounded(w);
    u->modifiers().attach(modifiers::make_break_healing(*u, 0.40, 30.0));
    // 200 * (1 - 0.40) = 120
    const double applied = deal_heal({nullptr, u, 200.0});
    EXPECT_NEAR(applied, 120.0, 1e-6);
    EXPECT_NEAR(u->health(), 620.0, 1e-6);
}

TEST(HealPipeline, HealAmpStacks) {
    World w;
    auto* u = wounded(w);
    // +20% 治疗增强（例如幻术师斗篷）和 -40% 破坏。总和：-20%
    u->modifiers().attach(std::make_unique<modifiers::GenericStats>(
        *u, "heal_amp", 30.0,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::HealAmpPct, 0.20}}));
    u->modifiers().attach(modifiers::make_break_healing(*u, 0.40, 30.0));
    const double applied = deal_heal({nullptr, u, 200.0});
    EXPECT_NEAR(applied, 160.0, 1e-6);
}

TEST(HealPipeline, DeadUnitCannotBeHealed) {
    World w;
    auto* u = w.spawn("U", Team::Radiant, stats(), {});
    u->set_health(0.0);
    EXPECT_FALSE(u->alive());
    EXPECT_DOUBLE_EQ(deal_heal({nullptr, u, 500.0}), 0.0);
}

TEST(HealPipeline, HealClampsToMaxHp) {
    World w;
    auto* u = w.spawn("U", Team::Radiant, stats(), {});
    u->set_health(990.0);
    const double applied = deal_heal({nullptr, u, 200.0});
    EXPECT_NEAR(applied, 10.0, 1e-6);
    EXPECT_DOUBLE_EQ(u->health(), 1000.0);
}
