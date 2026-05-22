// Stage 2: PreAttack BonusDamage + on_attack 钩子.
// 验证 Modifier::on_attack 可往 AttackRecord.bonus_damage 加值, complete_attack
// 时合并进伤害. 同时覆盖 C++ 静态 modifier 与 Lua OnAttack 两条路径.
#include "dota/core/attack.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace dota;

namespace {

UnitStats melee_hero(double dmg = 50.0) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.ranged           = false;
    s.magic_resist     = 0.0;     // 简化: 让 base+bonus 与最终伤害严格相等.
    return s;
}

// 静态 BonusDamage modifier: 每次 on_attack 给 record.bonus_damage 累加固定值.
// 不认领 record (bonus damage 是 PreAttack 阶段的, 命中阶段不需要钩回去).
struct BonusDamageModifier : public Modifier {
    double bonus;
    BonusDamageModifier(Unit& owner, double b)
        : Modifier("modifier_test_static_bonus", owner, /*duration=*/-1.0)
        , bonus(b) {}
    void on_attack(AttackRecord& r) override {
        r.bonus_damage += bonus;
    }
};

} // namespace

TEST(AttackBonusDamage, StaticCppModifierAddsBonusToDamage) {
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(40.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(40.0), {100.0, 0.0});

    a->modifiers().attach(std::make_unique<BonusDamageModifier>(*a, 30.0));

    const double hp_before = t->health();
    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);

    // base 40 + bonus 30 = 70 物理伤害, 双方 0 护甲所以全额命中.
    const double dealt = hp_before - t->health();
    EXPECT_NEAR(dealt, 70.0, 1e-6);
}

TEST(AttackBonusDamage, AttackLandedEventReflectsCombinedDamage) {
    // AttackLandedEvent.damage 应该是 base+bonus 经过伤害管线后的最终值.
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(40.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(40.0), {100.0, 0.0});

    a->modifiers().attach(std::make_unique<BonusDamageModifier>(*a, 60.0));

    double last_dmg = 0.0;
    bool last_missed = true;
    w.events().subscribe<AttackLandedEvent>([&](AttackLandedEvent& ev){
        last_dmg = ev.damage;
        last_missed = ev.missed;
    });

    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);

    EXPECT_FALSE(last_missed);
    EXPECT_NEAR(last_dmg, 100.0, 1e-6);  // 40 + 60
}

TEST(AttackBonusDamage, LuaOnAttackHookWritesBonus) {
    LuaState lua;
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(50.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(50.0), {100.0, 0.0});

    auto r = lua.state().safe_script_file(
        std::string(DOTA_SCRIPT_DIR) + "/modifiers/modifier_test_bonus_damage.lua",
        &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());

    const auto* spec = lua.modifier_registry().find("modifier_test_bonus_damage");
    ASSERT_NE(spec, nullptr);

    a->modifiers().attach(std::make_unique<ScriptedModifier>(
        *a, "modifier_test_bonus_damage", -1.0, *spec, lua));

    const double hp_before = t->health();
    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);

    // Lua spec 默认 bonus=25, dtype=PHYSICAL -> base 50 + 25 = 75 物理.
    const double dealt = hp_before - t->health();
    EXPECT_NEAR(dealt, 75.0, 1e-6);
}
