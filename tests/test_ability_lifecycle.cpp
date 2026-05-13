#include "dota/ability/ability.hpp"
#include "dota/ability/datadriven.hpp"
#include "dota/ability/manager.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 500.0;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    s.attack_speed     = 100.0;
    return s;
}

// Helper: instantiate Lion's Earth Spike from YAML on `caster`.
Ability* attach_earth_spike(AbilityRegistry& reg, Unit& caster) {
    reg.load_file(std::string(kDataDir) + "/heroes/lion.yaml");
    return reg.instantiate("lion_earth_spike", caster);
}

} // namespace

TEST(AbilityLifecycle, FailsWhenCasterSilenced) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    ASSERT_NE(spike, nullptr);

    lion->modifiers().attach(modifiers::make_silenced(*lion, 3.0));

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->can_cast(t), CastError::Silenced);
}

TEST(AbilityLifecycle, FailsWhenMagicImmuneEnemyTargeted) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    enemy->modifiers().attach(modifiers::make_magic_immune(*enemy, 5.0));

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->can_cast(t), CastError::TargetMagicImmune);
}

TEST(AbilityLifecycle, FailsWhenNotEnoughMana) {
    AbilityRegistry reg;
    World w;
    UnitStats poor = hero_stats();
    poor.max_mana = 50.0;
    auto* lion  = w.spawn("Lion",  Team::Radiant, poor, {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->can_cast(t), CastError::NotEnoughMana);
}

TEST(AbilityLifecycle, FailsWhenOutOfRange) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0,   0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {2000.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->can_cast(t), CastError::OutOfRange);
}

TEST(AbilityLifecycle, CastPointResolveTriggersOnSpellStart) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->order_cast(t, w), CastError::None);
    EXPECT_EQ(spike->phase(), CastPhase::Casting);
    EXPECT_DOUBLE_EQ(enemy->health(), hero_stats().max_health); // not yet

    // Advance past cast point (0.3s).
    w.advance(0.35);
    EXPECT_LT(enemy->health(), hero_stats().max_health);
    EXPECT_EQ(spike->phase(), CastPhase::OnCooldown);
    EXPECT_GT(spike->cooldown_remaining(), 0.0);
}

TEST(AbilityLifecycle, StunDuringCastPointInterruptsAndRefundsNothing) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    const double hp_before = enemy->health();
    const double mana_before = lion->mana();

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->order_cast(t, w), CastError::None);
    // Stun during cast-point: the cast should not resolve.
    lion->modifiers().attach(modifiers::make_stunned(*lion, 0.5));
    w.advance(0.5);

    EXPECT_DOUBLE_EQ(enemy->health(), hp_before);
    EXPECT_LT(lion->mana(), mana_before);           // mana was already spent
    EXPECT_EQ(spike->phase(), CastPhase::OnCooldown); // Dota: cooldown still starts
}

TEST(AbilityLifecycle, CooldownBlocksRecastUntilElapsed) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    CastTarget t; t.unit = enemy;
    spike->order_cast(t, w);
    w.advance(0.5);
    EXPECT_EQ(spike->can_cast(t), CastError::OnCooldown);

    w.advance(13.0);
    EXPECT_EQ(spike->can_cast(t), CastError::None);
}

TEST(AbilityLifecycle, LevelChoosesCorrectDamageTier) {
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);

    spike->set_level(4);
    CastTarget t; t.unit = enemy;
    const double hp_before = enemy->health();

    spike->order_cast(t, w);
    w.advance(0.4);

    // Level 4: 320 magical → 240 after 25% magic resist.
    EXPECT_NEAR(hp_before - enemy->health(), 240.0, 0.5);
}
