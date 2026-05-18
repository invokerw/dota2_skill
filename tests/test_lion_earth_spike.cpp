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

// 端到端: YAML 解析 → 实例化 → 施法 → 伤害 + 眩晕生效
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

    // 施法前摇后
    w.advance(0.35);

    // 1 级: 80 魔法伤害 → 经过 25% 抗性后 60
    EXPECT_NEAR(hp_before - enemy->health(), 60.0, 0.5);

    // 眩晕持续 1.7 秒
    EXPECT_TRUE(enemy->modifiers().has_state(ModifierState::Stunned));
    EXPECT_FALSE(enemy->can_cast());

    // 总共 2 秒后(0.35 施法 + 1.7 秒眩晕应该在约 2.05 秒过期),
    // 眩晕应该消失
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

    // 在施法前摇结束前击杀目标
    enemy->apply_raw_damage(99999.0);

    const double mana_before_advance = lion->mana();
    w.advance(0.4);

    // 不会崩溃; 技能应该进入冷却且不造成伤害/眩晕
    EXPECT_EQ(spike->phase(), CastPhase::OnCooldown);
    (void)mana_before_advance; // 法力已经预先消耗
}
