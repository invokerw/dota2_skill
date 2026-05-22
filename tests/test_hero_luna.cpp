// Stage 5: Luna 月之祝福 (PreAttack BonusDamage). 被动: 不认领 record, 仅在
// OnAttack 时往 ev.bonus_damage 加固定值. 普攻总伤 = base + bonus_damage.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats melee_hero(double dmg = 50.0) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 200.0;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.magic_resist     = 0.0;
    return s;
}

class HeroLunaTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/luna.yaml");

        luna_   = world_.spawn("Luna", Team::Radiant, melee_hero(),  {0.0, 0.0});
        target_ = world_.spawn("Tgt",  Team::Dire,    melee_hero(),  {100.0, 0.0});

        Ability* a = reg_.instantiate("luna_lunar_blessing", *luna_, &lua_);
        ASSERT_NE(a, nullptr);
        a->set_level(1);  // +10 bonus.
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* luna_{};
    Unit* target_{};
};

} // namespace

TEST_F(HeroLunaTest, IntrinsicModifierAttachedOnInstantiate) {
    EXPECT_TRUE(luna_->modifiers().find("modifier_luna_lunar_blessing") != nullptr);
}

TEST_F(HeroLunaTest, BonusDamageAppliedOnAttack) {
    const double hp_before = target_->health();

    luna_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // base 50 + bonus 10 = 60 物理.
    EXPECT_NEAR(hp_before - target_->health(), 60.0, 1e-6);
}

TEST_F(HeroLunaTest, BonusDamageScalesWithLevel) {
    Ability* a = luna_->abilities().find("luna_lunar_blessing");
    ASSERT_NE(a, nullptr);
    a->set_level(4);  // +40 bonus.

    const double hp_before = target_->health();
    luna_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // base 50 + bonus 40 = 90.
    EXPECT_NEAR(hp_before - target_->health(), 90.0, 1e-6);
}
