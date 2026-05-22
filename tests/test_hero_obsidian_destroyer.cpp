// Stage 5: OD 奥术飞弹 (法球 + 改 damage_type 为 Magical + 命中 AoE 溅射).
// OnAttack 扣蓝认领 record, 改 damage_type=Magical, 加 bonus_damage; OnAttackLanded
// 给 target 周围敌人溅射 splash_pct * (base + bonus) 魔法伤害.
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

UnitStats ranged_hero(double dmg = 60.0, double mana = 400.0) {
    UnitStats s;
    s.max_health        = 1000.0;
    s.max_mana          = mana;
    s.attack_damage     = dmg;
    s.attack_speed      = 100.0;
    s.base_attack_time  = 1.0;
    s.attack_range      = 600.0;
    s.move_speed        = 300.0;
    s.magic_resist      = 0.0;       // 让魔法伤害量好预测
    s.base_armor        = 100.0;     // 把物理几乎完全吃掉, 用来 sanity check 实际走 magical
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

class HeroObsidianTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/obsidian_destroyer.yaml");

        od_     = world_.spawn("OD",  Team::Radiant, ranged_hero(),  {0.0, 0.0});
        main_   = world_.spawn("Tgt", Team::Dire,    melee_target(), {200.0, 0.0});

        Ability* a = reg_.instantiate("obsidian_destroyer_arcane_orb", *od_, &lua_);
        ASSERT_NE(a, nullptr);
        a->set_level(1);
    }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* od_{};
    Unit* main_{};
};

} // namespace

TEST_F(HeroObsidianTest, ArcaneOrbDealsMagicalAndConsumesMana) {
    od_->set_mana(200.0);
    const double mp_before = main_->health();
    const double od_mp_before = od_->mana();

    od_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(0.3);

    // base 60 + bonus 60 = 120 magical, 0% magic resist -> 120 实际伤害.
    EXPECT_NEAR(mp_before - main_->health(), 120.0, 1e-6);
    // 扣蓝 120.
    EXPECT_NEAR(od_mp_before - od_->mana(), 120.0, 1e-6);
}

TEST_F(HeroObsidianTest, ArcaneOrbSplashesNearbyEnemies) {
    od_->set_mana(200.0);
    auto* side = world_.spawn("Side", Team::Dire, melee_target(), {300.0, 100.0});
    const double side_before = side->health();

    od_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(0.3);

    // splash = 120 * 40% = 48 magical -> 48 实际.
    EXPECT_NEAR(side_before - side->health(), 48.0, 1e-6);
}

TEST_F(HeroObsidianTest, ArcaneOrbDegradesWithoutMana) {
    od_->set_mana(50.0);  // < 120
    const double mp_before = main_->health();

    od_->issue_order(OrderAttackTarget{main_->id()});
    world_.advance(0.3);

    // 无认领, 退化为普攻. 60 物理伤害 - 100 armor 接近 0.
    // armor 不会负伤害, 但目标 base_armor=100 几乎全免, 验证伤害远小于 magical 路径.
    EXPECT_LT(mp_before - main_->health(), 30.0);
}
