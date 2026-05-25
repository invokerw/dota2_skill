// Stage 1: 验证 ability 的 intrinsic_modifier 自动 attach 流程, 以及
// set_level 触发 OnRefresh 钩子.
#include "dota/ability/ability.hpp"
#include "dota/ability/datadriven.hpp"
#include "dota/ability/manager.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

UnitStats stats() {
    UnitStats s;
    s.max_health    = 1000.0;
    s.attack_damage = 50.0;
    return s;
}

// 注册一个最小的 lua modifier, 仅保留计数器 + Properties 用作回归.
// _G.intrinsic_test = { created = 0, refresh = 0 } 让 C++ 端可读.
void register_intrinsic_test_modifier(LuaState& lua) {
    const char* src = R"LUA(
        intrinsic_test = { created = 0, refresh = 0 }
        register_modifier("modifier_intrinsic_test", {
            IsHidden       = true,
            IsPurgable     = false,
            RemoveOnDeath  = false,
            OnCreated      = function(self, owner) intrinsic_test.created = intrinsic_test.created + 1 end,
            OnRefresh      = function(self, owner) intrinsic_test.refresh = intrinsic_test.refresh + 1 end,
        })
    )LUA";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());
}

AbilityDef make_passive_def() {
    AbilityDef def;
    def.name               = "test_passive";
    def.base_class         = "ability_datadriven";
    def.behavior           = static_cast<std::uint32_t>(BehaviorFlag::Passive);
    def.intrinsic_modifier = "modifier_intrinsic_test";
    def.cooldowns          = {0.0};
    def.mana_costs         = {0.0};
    return def;
}

} // namespace

TEST(PassiveIntrinsic, AttachOnInstantiateAndOnRefreshOnUpgrade) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    register_intrinsic_test_modifier(lua);
    reg.register_def(make_passive_def());

    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});

    Ability* a = reg.instantiate("test_passive", *hero);
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->is_passive());
    EXPECT_EQ(a->intrinsic_modifier_name(), "modifier_intrinsic_test");

    // intrinsic modifier 已挂在 caster 身上, OnCreated 触发一次.
    Modifier* m = hero->modifiers().find("modifier_intrinsic_test");
    ASSERT_NE(m, nullptr);
    EXPECT_TRUE(m->permanent());
    EXPECT_EQ(lua.state()["intrinsic_test"]["created"], 1);
    EXPECT_EQ(lua.state()["intrinsic_test"]["refresh"], 0);

    // 升级 -> OnRefresh.
    a->set_level(2);
    EXPECT_EQ(lua.state()["intrinsic_test"]["refresh"], 1);

    // 等级未变 -> 不再触发 (set_level 内对相同等级早退).
    a->set_level(2);
    EXPECT_EQ(lua.state()["intrinsic_test"]["refresh"], 1);

    // 再升一级 -> 再触发一次.
    a->set_level(3);
    EXPECT_EQ(lua.state()["intrinsic_test"]["refresh"], 2);
}

TEST(PassiveIntrinsic, MissingModifierReportsErrorAndDoesNotCrash) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    // 故意不注册 lua modifier.
    AbilityDef def              = make_passive_def();
    def.intrinsic_modifier      = "modifier_does_not_exist";
    reg.register_def(std::move(def));

    bool saw_error = false;
    lua.set_error_handler([&](const std::string& msg) {
        if (msg.find("modifier_does_not_exist") != std::string::npos) saw_error = true;
    });

    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    Ability* a = reg.instantiate("test_passive", *hero);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(hero->modifiers().find("modifier_does_not_exist"), nullptr);
    EXPECT_TRUE(saw_error);
}

TEST(PassiveIntrinsic, RemoveAbilityCleansUpIntrinsicModifier) {
    // 删除 ability 时, intrinsic_modifier 必须一起清理: 否则 modifier 内部持有的
    // ability_ 指针会随 ability 析构而悬空.
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    register_intrinsic_test_modifier(lua);
    reg.register_def(make_passive_def());

    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    ASSERT_NE(reg.instantiate("test_passive", *hero), nullptr);
    ASSERT_NE(hero->modifiers().find("modifier_intrinsic_test"), nullptr);

    EXPECT_TRUE(hero->abilities().remove_at(0));
    EXPECT_EQ(hero->modifiers().find("modifier_intrinsic_test"), nullptr);
}

TEST(PassiveIntrinsic, NonPassiveAbilityCanAlsoDeclareIntrinsic) {
    // 主动技能也可以挂常驻 modifier(例如龙骑士龙血).
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);

    register_intrinsic_test_modifier(lua);
    AbilityDef def         = make_passive_def();
    def.behavior           = static_cast<std::uint32_t>(BehaviorFlag::NoTarget);
    reg.register_def(std::move(def));

    World w;
    auto* hero = w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    Ability* a = reg.instantiate("test_passive", *hero);
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(a->is_passive());
    EXPECT_NE(hero->modifiers().find("modifier_intrinsic_test"), nullptr);
}
