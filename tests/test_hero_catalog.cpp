// HeroCatalog: 扫 data/heroes/, 收集英雄 + 技能元数据.
#include "dota/tools/hero_catalog.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace dota;
using namespace dota::tools;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;
} // namespace

TEST(HeroCatalog, ScansAllSixHeroes) {
    HeroCatalog cat;
    const std::size_t n = cat.scan(std::string(kDataDir) + "/heroes");
    EXPECT_GE(n, 6u);

    auto has = [&](const std::string& name) {
        for (const auto& h : cat.heroes()) if (h.yaml_name == name) return true;
        return false;
    };
    EXPECT_TRUE(has("lion"));
    EXPECT_TRUE(has("lina"));
    EXPECT_TRUE(has("pudge"));
    EXPECT_TRUE(has("sven"));
    EXPECT_TRUE(has("earthshaker"));
    EXPECT_TRUE(has("juggernaut"));
}

TEST(HeroCatalog, LionAbilitiesParsed) {
    HeroCatalog cat;
    cat.scan(std::string(kDataDir) + "/heroes");
    const HeroEntry* lion = cat.find("lion");
    ASSERT_NE(lion, nullptr);

    EXPECT_GT(lion->base_health, 0.0);
    EXPECT_DOUBLE_EQ(lion->magic_resist, 0.25);
    EXPECT_GE(lion->abilities.size(), 3u);

    auto find_ab = [&](const std::string& name) -> const AbilityMeta* {
        for (const auto& a : lion->abilities) if (a.name == name) return &a;
        return nullptr;
    };
    const AbilityMeta* spike = find_ab("lion_earth_spike");
    ASSERT_NE(spike, nullptr);
    EXPECT_TRUE(has_flag(spike->behavior, BehaviorFlag::UnitTarget));
    EXPECT_EQ(spike->target_team, TargetTeam::Enemy);
    EXPECT_GT(spike->cast_range, 0.0);
    EXPECT_GT(spike->cast_point, 0.0);
    EXPECT_FALSE(spike->is_passive);

    const AbilityMeta* finger = find_ab("lion_finger_of_death");
    ASSERT_NE(finger, nullptr);
    EXPECT_GT(finger->cast_range, 0.0);
}

TEST(HeroCatalog, PudgeMeatHookIsPointTarget) {
    HeroCatalog cat;
    cat.scan(std::string(kDataDir) + "/heroes");
    const HeroEntry* pudge = cat.find("pudge");
    ASSERT_NE(pudge, nullptr);
    auto it = std::find_if(pudge->abilities.begin(), pudge->abilities.end(),
        [](const AbilityMeta& a) { return a.name == "pudge_meat_hook"; });
    ASSERT_NE(it, pudge->abilities.end());
    EXPECT_TRUE(has_flag(it->behavior, BehaviorFlag::PointTarget));
}
