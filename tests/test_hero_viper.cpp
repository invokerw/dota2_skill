// Stage 5: Viper 毒性攻击 (法球 + 叠层 DOT). OnAttack 扣蓝, OnAttackLanded 给
// target 上 / 叠层 DOT debuff. 不修改 bonus_damage -- 攻击伤害不变.
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

UnitStats ranged_hero(double dmg = 50.0, double mana = 200.0) {
    UnitStats s;
    s.max_health        = 1000.0;
    s.max_mana          = mana;
    s.attack_damage     = dmg;
    s.attack_speed      = 100.0;
    s.base_attack_time  = 1.0;
    s.attack_range      = 600.0;
    s.move_speed        = 300.0;
    s.magic_resist      = 0.0;
    s.base_armor        = 0.0;
    s.ranged            = true;
    s.projectile_speed  = 1500.0;
    return s;
}

UnitStats melee_target() {
    UnitStats s = ranged_hero();
    s.attack_range     = 150.0;
    s.ranged           = false;
    s.projectile_speed = 0.0;
    return s;
}

class HeroViperTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/viper.yaml");

        viper_  = world_.spawn("Viper", Team::Radiant, ranged_hero(),  {0.0, 0.0});
        target_ = world_.spawn("Tgt",   Team::Dire,    melee_target(), {200.0, 0.0});

        Ability* a = reg_.instantiate("viper_poison_attack", *viper_, &lua_);
        ASSERT_NE(a, nullptr);
        a->set_level(1);
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* viper_{};
    Unit* target_{};
};

} // namespace

TEST_F(HeroViperTest, AttackLandsAndAppliesDotDebuff) {
    viper_->set_mana(200.0);
    const double mp_before = viper_->mana();

    viper_->issue_order(OrderAttackTarget{target_->id()});
    // 远程 200/1500 ≈ 0.13s.
    world_.advance(0.3);

    // 扣蓝 20.
    EXPECT_NEAR(mp_before - viper_->mana(), 20.0, 1e-6);
    auto* dot = target_->modifiers().find("modifier_viper_poison_attack_debuff");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(dot->stack_count(), 1);
}

TEST_F(HeroViperTest, MultipleAttacksStackDot) {
    viper_->set_mana(200.0);

    for (int i = 0; i < 3; ++i) {
        viper_->set_attack_cd(0.0);
        viper_->issue_order(OrderAttackTarget{target_->id()});
        world_.advance(0.3);
    }

    auto* dot = target_->modifiers().find("modifier_viper_poison_attack_debuff");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(dot->stack_count(), 3);
}

TEST_F(HeroViperTest, DotTicksDamageOverTime) {
    viper_->set_mana(200.0);
    viper_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(0.3);

    // 普攻命中: 50 物理. DOT 还没跳过 (think 0.5s).
    const double after_attack = target_->health();
    ASSERT_NEAR(1000.0 - after_attack, 50.0, 1e-6);

    // 防止 Viper 在 BAT 1.0s 后又自动开第二枪干扰测量, 取消攻击命令.
    viper_->clear_orders();

    // 推进 1.0s -> 应该跳 2 次 think (0.5, 1.0), 1 stack * 10 dps = 10 总伤害.
    world_.advance(1.0);
    EXPECT_LT(target_->health(), after_attack);
    EXPECT_GT(target_->health(), after_attack - 15.0);
}
