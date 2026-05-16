// Phase 9：Pudge 肉钩 + 肢解集成测试
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
    s.max_health = 1500.0;
    s.max_mana   = 600.0;
    s.magic_resist = 0.25;
    return s;
}
} // namespace

class HeroPudgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/pudge.yaml");
        pudge_ = world_.spawn("Pudge", Team::Radiant, hero_stats(), {0, 0});
        enemy_ = world_.spawn("Enemy", Team::Dire, hero_stats(), {500, 0});
    }
    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* pudge_{};
    Unit* enemy_{};
};

TEST_F(HeroPudgeTest, MeatHookDamagesAndPullsTarget) {
    Ability* hook = reg_.instantiate("pudge_meat_hook", *pudge_);
    ASSERT_NE(hook, nullptr);
    CastTarget t; t.point = {1300, 0}; t.has_point = true;
    EXPECT_EQ(hook->order_cast(t, world_), CastError::None);

    // cast_point 0.30 + 飞 500 单位 / 1300 ≈ 0.38s → 0.7s 后已命中
    world_.advance(0.75);
    const double dealt = 1500.0 - enemy_->health();
    EXPECT_GT(dealt, 80.0);    // 1 级 125 magical, 25% resist → ~93.75

    // 拉拽：等到拖回时间过完，target 应该靠近 pudge（< 100 单位）
    world_.advance(1.0);
    EXPECT_LT(enemy_->position().x, 100.0);
}

TEST_F(HeroPudgeTest, MeatHookStopsOnFirstHit) {
    auto* second = world_.spawn("Enemy2", Team::Dire, hero_stats(), {1000, 0});
    Ability* hook = reg_.instantiate("pudge_meat_hook", *pudge_);
    CastTarget t; t.point = {1300, 0}; t.has_point = true;
    hook->order_cast(t, world_);

    world_.advance(2.0);
    EXPECT_LT(enemy_->health(), 1500.0);
    EXPECT_DOUBLE_EQ(second->health(), 1500.0);  // 第二目标未被命中
}

TEST_F(HeroPudgeTest, DismemberChannelsDamageOverTime) {
    pudge_->set_position({100, 0});
    enemy_->set_position({150, 0});   // 进入施法距离 175

    Ability* dis = reg_.instantiate("pudge_dismember", *pudge_);
    ASSERT_NE(dis, nullptr);
    CastTarget t; t.unit = enemy_;
    EXPECT_EQ(dis->order_cast(t, world_), CastError::None);

    pudge_->set_health(500.0);
    const double pudge_hp_before = pudge_->health();
    world_.advance(1.6);   // cast point 0.30 + ~1.3 秒引导 → 约 2-3 次 tick

    // 1 级每 0.5s 造成 30 magical damage（魔抗 25%） → 22.5/tick
    // 引导期内造成的总伤害应明显超过 30
    const double dealt = 1500.0 - enemy_->health();
    EXPECT_GT(dealt, 30.0);
    // pudge 因 heal 应该回了一些血
    EXPECT_GT(pudge_->health(), pudge_hp_before);
}
