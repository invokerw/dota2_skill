// Phase 9: Earthshaker 沟壑集成测试
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;
UnitStats hero_stats() {
    UnitStats s;
    s.max_health   = 1500.0;
    s.max_mana     = 600.0;
    s.magic_resist = 0.25;
    return s;
}
} // namespace

class HeroEarthshakerTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/earthshaker.yaml");
        es_   = world_.spawn("ES", Team::Radiant, hero_stats(), {0, 0});
        e1_   = world_.spawn("e1", Team::Dire, hero_stats(), {300, 30});
        e2_   = world_.spawn("e2", Team::Dire, hero_stats(), {800, -20});
    }
    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* es_{};
    Unit* e1_{};
    Unit* e2_{};
};

TEST_F(HeroEarthshakerTest, FissureLineHitsAllOnPath) {
    Ability* f = reg_.instantiate("earthshaker_fissure", *es_);
    ASSERT_NE(f, nullptr);
    CastTarget t; t.point = {1500, 0}; t.has_point = true;
    EXPECT_EQ(f->order_cast(t, world_), CastError::None);
    world_.advance(0.55);   // cast point 0.43

    EXPECT_LT(e1_->health(), 1500.0);
    EXPECT_LT(e2_->health(), 1500.0);
    EXPECT_TRUE(e1_->modifiers().has_state(ModifierState::Stunned));
}

TEST_F(HeroEarthshakerTest, FissureKnocksTargetsAlongLine) {
    Ability* f = reg_.instantiate("earthshaker_fissure", *es_);
    CastTarget t; t.point = {1500, 0}; t.has_point = true;
    f->order_cast(t, world_);
    world_.advance(0.55);

    const double e1_x_before = 300.0;
    world_.advance(0.5);   // 让击退完成
    EXPECT_GT(e1_->position().x, e1_x_before + 50.0);  // 至少被推开
}

TEST_F(HeroEarthshakerTest, FissureSpawnsThinker) {
    auto* f = reg_.instantiate("earthshaker_fissure", *es_);
    CastTarget t; t.point = {1000, 0}; t.has_point = true;
    f->order_cast(t, world_);
    world_.advance(0.5);
    // thinker 应作为 Team::Neutral 的活单位存在
    bool found = false;
    for (Team team : {Team::Neutral, Team::Radiant, Team::Dire}) {
        for (auto* u : world_.units_on_team(team)) {
            if (u->name() == "npc_dota_thinker" && u->alive()) found = true;
        }
    }
    EXPECT_TRUE(found);
}
