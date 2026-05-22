// Stage 3: 法球框架. 验证 BehaviorFlag::Attack + intrinsic modifier 的组合:
//   - 蓝量 / cd 充足: 法球认领 record, bonus_damage + 命中后挂 debuff.
//   - 蓝量不够: 不认领, 普攻原样, 无 debuff.
//   - 沉默: 法球不触发 (intrinsic modifier 自检).
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats melee_hero(double dmg = 50.0, double mana = 200.0) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = mana;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.magic_resist     = 0.0;
    return s;
}

class OrbTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 加载 orb 用 lua modifier (intrinsic + debuff).
        auto r = lua_.state().safe_script_file(
            std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_orb_slow.lua",
            &sol::script_pass_on_error);
        ASSERT_TRUE(r.valid());

        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/abilities/test_orb_slow.yaml");

        caster_ = world_.spawn("caster", Team::Radiant, melee_hero(40.0), {0.0, 0.0});
        target_ = world_.spawn("target", Team::Dire,    melee_hero(40.0), {100.0, 0.0});

        // 实例化法球 ability -> 自动挂 intrinsic modifier 到 caster.
        Ability* a = reg_.instantiate("test_orb_slow", *caster_, &lua_);
        ASSERT_NE(a, nullptr);
        a->set_level(1);
        ability_ = a;
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* caster_{};
    Unit* target_{};
    Ability* ability_{};
};

TEST_F(OrbTest, OrbConsumesManaAndAddsBonusAndDebuff) {
    // 蓝量充足: 应该 use_resources_for_orb 成功 -> bonus damage + slow debuff.
    caster_->set_mana(100.0);
    const double mp_before = caster_->mana();
    const double hp_before = target_->health();

    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // base 40 + bonus 10 = 50 物理伤害.
    EXPECT_NEAR(hp_before - target_->health(), 50.0, 1e-6);
    // 扣蓝 25.
    EXPECT_NEAR(mp_before - caster_->mana(), 25.0, 1e-6);
    // 命中后 target 上有 debuff.
    EXPECT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") != nullptr);
}

TEST_F(OrbTest, NotEnoughManaDegradesToPlainAttack) {
    caster_->set_mana(10.0);  // < 25
    const double mp_before = caster_->mana();
    const double hp_before = target_->health();

    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // 仅 40 base, 没有 bonus.
    EXPECT_NEAR(hp_before - target_->health(), 40.0, 1e-6);
    // 蓝量不变.
    EXPECT_NEAR(caster_->mana(), mp_before, 1e-6);
    // 没挂 debuff.
    EXPECT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") == nullptr);
}

TEST_F(OrbTest, SilencedSuppressesOrb) {
    caster_->set_mana(100.0);
    caster_->modifiers().attach(modifiers::make_silenced(*caster_, 5.0));
    const double mp_before = caster_->mana();
    const double hp_before = target_->health();

    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // 沉默时 OnAttack 早退 -> 普攻原样, 不扣蓝, 无 bonus, 无 debuff.
    EXPECT_NEAR(hp_before - target_->health(), 40.0, 1e-6);
    EXPECT_NEAR(caster_->mana(), mp_before, 1e-6);
    EXPECT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") == nullptr);
}

TEST_F(OrbTest, AutocastOffSuppressesOrb) {
    caster_->set_mana(100.0);
    ability_->set_autocast_on(false);
    const double hp_before = target_->health();

    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    EXPECT_NEAR(hp_before - target_->health(), 40.0, 1e-6);
    EXPECT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") == nullptr);
}

TEST_F(OrbTest, OnCooldownDegradesToPlainAttack) {
    caster_->set_mana(100.0);
    // 第一次法球后, ability 进入 1.0s cd (yaml). 此处用一次普攻吃掉 cd, 然后立刻
    // 第二次普攻应该退化 -- 但 attack_cd 也是 1.0s, 需要在 attack 间隔内强制再发一次.
    // 简化做法: 直接手动让 caster 的 attack_cd 归零, 再发一次, 验证 cd 触发降级.
    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);
    EXPECT_NEAR(caster_->mana(), 75.0, 1e-6);  // 第一次扣蓝
    ASSERT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") != nullptr);

    // 移除 debuff 方便观察第二次是否再挂; 强制重置 attack_cd 让第二次普攻立刻发出.
    target_->modifiers().remove("modifier_test_orb_slow_debuff");
    caster_->set_attack_cd(0.0);
    const double mp_before = caster_->mana();
    caster_->issue_order(OrderAttackTarget{target_->id()});
    world_.advance(World::kTickDt);

    // ability 还在 cd -> 不扣蓝, 无 debuff.
    EXPECT_NEAR(caster_->mana(), mp_before, 1e-6);
    EXPECT_TRUE(target_->modifiers().find("modifier_test_orb_slow_debuff") == nullptr);
}

} // namespace
