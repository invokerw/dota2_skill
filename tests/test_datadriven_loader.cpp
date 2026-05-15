#include "dota/ability/datadriven.hpp"
#include "dota/ability/registry.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;
} // namespace

TEST(DataDrivenLoader, LoadsLionYaml) {
    AbilityRegistry reg;
    const std::string path = std::string(kDataDir) + "/heroes/lion.yaml";
    const auto n = reg.load_file(path);
    // lion.yaml 在第 6 阶段增长；加载器返回它实际加载的
    // 数据驱动条目的计数（Lua 条目被跳过）
    EXPECT_GE(n, 1u);

    const AbilityDef* def = reg.find("lion_earth_spike");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->base_class, "ability_datadriven");
    EXPECT_EQ(def->target_team, TargetTeam::Enemy);
    EXPECT_TRUE(has_flag(def->behavior, BehaviorFlag::UnitTarget));

    EXPECT_DOUBLE_EQ(def->cast_point, 0.3);
    ASSERT_EQ(def->cooldowns.size(), 4u);
    EXPECT_DOUBLE_EQ(def->cooldowns[0], 12.0);
    ASSERT_EQ(def->mana_costs.size(), 4u);
    EXPECT_DOUBLE_EQ(def->mana_costs[3], 160.0);
}

TEST(DataDrivenLoader, AbilitySpecialPerLevelLookup) {
    AbilityRegistry reg;
    reg.load_file(std::string(kDataDir) + "/heroes/lion.yaml");
    const AbilityDef* def = reg.find("lion_earth_spike");
    ASSERT_NE(def, nullptr);

    // damage: [80, 160, 240, 320]
    EXPECT_DOUBLE_EQ(resolve_expression("%damage", def->ability_special, 1), 80.0);
    EXPECT_DOUBLE_EQ(resolve_expression("%damage", def->ability_special, 3), 240.0);
    // 超过最大等级时限制到最后一个条目
    EXPECT_DOUBLE_EQ(resolve_expression("%damage", def->ability_special, 10), 320.0);

    // 浮点数列表也一样
    EXPECT_DOUBLE_EQ(resolve_expression("%stun_duration", def->ability_special, 2), 1.9);
}

TEST(DataDrivenLoader, PlainNumericLiteralPassesThrough) {
    AbilitySpecial empty;
    EXPECT_DOUBLE_EQ(resolve_expression("42", empty, 1), 42.0);
    EXPECT_DOUBLE_EQ(resolve_expression("3.14", empty, 1), 3.14);
}

TEST(DataDrivenLoader, UnknownSpecialKeyThrows) {
    AbilitySpecial empty;
    EXPECT_THROW(resolve_expression("%nope", empty, 1), std::runtime_error);
}
