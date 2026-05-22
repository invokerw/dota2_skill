// Stage 5: Drow Ranger 冰箭 (法球): OnAttack 扣蓝认领 record + bonus damage,
// OnAttackLanded 给 target 上减速 debuff. 投射物挂粒子名供录像层使用.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/event_bus.hpp"
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

UnitStats ranged_hero(double dmg = 40.0, double mana = 200.0) {
    UnitStats s;
    s.max_health        = 1000.0;
    s.max_mana          = mana;
    s.attack_damage     = dmg;
    s.attack_speed      = 100.0;
    s.base_attack_time  = 1.0;
    s.attack_range      = 600.0;
    s.move_speed        = 300.0;
    s.magic_resist      = 0.0;
    s.ranged            = true;
    s.projectile_speed  = 1250.0;
    return s;
}

UnitStats melee_target() {
    UnitStats s = ranged_hero();
    s.attack_range     = 150.0;
    s.ranged           = false;
    s.projectile_speed = 0.0;
    return s;
}

class HeroDrowTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/drow_ranger.yaml");

        drow_   = world_.spawn("Drow", Team::Radiant, ranged_hero(),  {0.0, 0.0});
        target_ = world_.spawn("Tgt",  Team::Dire,    melee_target(), {300.0, 0.0});

        Ability* a = reg_.instantiate("drow_ranger_frost_arrows", *drow_, &lua_);
        ASSERT_NE(a, nullptr);
        a->set_level(1);
        ability_ = a;
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* drow_{};
    Unit* target_{};
    Ability* ability_{};
};

} // namespace

TEST_F(HeroDrowTest, FrostArrowAddsBonusDamageAndSlowDebuff) {
    drow_->set_mana(100.0);
    const double mp_before = drow_->mana();
    const double hp_before = target_->health();

    drow_->issue_order(OrderAttackTarget{target_->id()});
    // 远程普攻: cast point 0 + 投射物 300/1250 ≈ 0.24s.
    world_.advance(0.5);

    // base 40 + bonus 10 = 50 物理.
    EXPECT_NEAR(hp_before - target_->health(), 50.0, 1e-6);
    // 1 级蓝量 12.
    EXPECT_NEAR(mp_before - drow_->mana(), 12.0, 1e-6);
    EXPECT_TRUE(target_->modifiers().find("modifier_drow_ranger_frost_arrows_slow")
                != nullptr);
}

TEST_F(HeroDrowTest, FrostArrowProjectileCarriesParticleName) {
    drow_->set_mana(100.0);
    std::string captured;
    world_.events().subscribe<ProjectileSpawnedEvent>(
        [&captured](ProjectileSpawnedEvent& e) { captured = e.name; });

    drow_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    EXPECT_EQ(captured,
              "particles/units/heroes/hero_drow_ranger/drow_frost_arrow.vpcf");
}

TEST_F(HeroDrowTest, FrostArrowDegradesWithoutMana) {
    drow_->set_mana(5.0);  // < 12
    const double hp_before = target_->health();

    drow_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(0.5);

    EXPECT_NEAR(hp_before - target_->health(), 40.0, 1e-6);
    EXPECT_TRUE(target_->modifiers().find("modifier_drow_ranger_frost_arrows_slow")
                == nullptr);
}
