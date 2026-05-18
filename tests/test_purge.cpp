// Phase 4: Dispel / Purge 单测
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

TEST(Purge, DebuffOnlyByDefaultRemoves) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    hero->modifiers().attach(make_stunned(*hero, 5.0));   // debuff
    auto* heal = hero->modifiers().attach(make_periodic_heal(*hero, 10.0, 1.0, 5.0));
    (void)heal;

    Unit::PurgeOptions opt;
    opt.buffs   = false;
    opt.debuffs = true;
    hero->purge(opt);

    EXPECT_FALSE(hero->modifiers().has_state(ModifierState::Stunned));
    EXPECT_NE(hero->modifiers().find("modifier_periodic_heal"), nullptr);
}

TEST(Purge, BuffsOnlyKeepsDebuffs) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    hero->modifiers().attach(make_stunned(*hero, 5.0));
    hero->modifiers().attach(make_periodic_heal(*hero, 10.0, 1.0, 5.0));

    Unit::PurgeOptions opt;
    opt.buffs   = true;
    opt.debuffs = false;
    hero->purge(opt);

    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Stunned));
    EXPECT_EQ(hero->modifiers().find("modifier_periodic_heal"), nullptr);
}

TEST(Purge, NonPurgableSurvivesNormalDispel) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    auto* mod = hero->modifiers().attach(make_stunned(*hero, 5.0));
    mod->set_purgable(false);

    hero->purge({.buffs=false, .debuffs=true, .strong=false});
    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Stunned));
}

TEST(Purge, NonDispellableNeedsStrongDispel) {
    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0, 0});
    auto* mod = hero->modifiers().attach(make_stunned(*hero, 5.0));
    mod->set_dispellable(false);

    hero->purge({.buffs=false, .debuffs=true, .strong=false});
    EXPECT_TRUE(hero->modifiers().has_state(ModifierState::Stunned));

    hero->purge({.buffs=false, .debuffs=true, .strong=true});
    EXPECT_FALSE(hero->modifiers().has_state(ModifierState::Stunned));
}
