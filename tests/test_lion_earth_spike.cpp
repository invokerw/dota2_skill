#include "dota/ability/datadriven.hpp"
#include "dota/ability/manager.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats stats() {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 500.0;
    s.magic_resist     = 0.25;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    return s;
}

} // namespace

// End-to-end: YAML parse → instantiate → cast → damage + stun applied.
TEST(LionEarthSpike, EndToEndDamageAndStun) {
    AbilityRegistry reg;
    reg.load_file(std::string(kDataDir) + "/heroes/lion.yaml");

    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    stats(), {100.0, 0.0});
    auto* spike = reg.instantiate("lion_earth_spike", *lion);
    ASSERT_NE(spike, nullptr);

    const double hp_before = enemy->health();

    CastTarget t; t.unit = enemy;
    EXPECT_EQ(spike->order_cast(t, w), CastError::None);

    // Advance past cast point.
    w.advance(0.35);

    // Level 1: 80 magical → 60 after 25% resist.
    EXPECT_NEAR(hp_before - enemy->health(), 60.0, 0.5);

    // Stun applied for 1.7s.
    EXPECT_TRUE(enemy->modifiers().has_state(ModifierState::Stunned));
    EXPECT_FALSE(enemy->can_cast());

    // After 2s total (0.35 cast + 1.7s stun should expire ~2.05s), the stun
    // should be gone.
    w.advance(1.8);
    EXPECT_FALSE(enemy->modifiers().has_state(ModifierState::Stunned));
    EXPECT_TRUE(enemy->can_cast());
}

TEST(LionEarthSpike, DeadTargetMidCastInterrupts) {
    AbilityRegistry reg;
    reg.load_file(std::string(kDataDir) + "/heroes/lion.yaml");

    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    stats(), {100.0, 0.0});
    auto* spike = reg.instantiate("lion_earth_spike", *lion);

    CastTarget t; t.unit = enemy;
    spike->order_cast(t, w);

    // Kill the target before cast-point elapses.
    enemy->apply_raw_damage(99999.0);

    const double mana_before_advance = lion->mana();
    w.advance(0.4);

    // No crash; the spike should be in cooldown and no damage/stun applied.
    EXPECT_EQ(spike->phase(), CastPhase::OnCooldown);
    (void)mana_before_advance; // mana was already spent upfront
}
