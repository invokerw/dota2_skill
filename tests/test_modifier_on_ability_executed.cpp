// Stage 2: 验证 ModifierManager::dispatch_ability_executed 的行为
//   - 主动 ability 完整释放 -> modifier OnAbilityExecuted 触发
//   - 主动 ability 被中断 (cast point 中眩晕) -> 不触发
//   - 被动 ability 通过 trigger_cast 早退 -> 不触发
//   - ev 携带 caster, ability, ability_name, is_passive
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/modifier/scripted.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace dota;

namespace {

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health    = 1000.0;
    s.max_mana      = 600.0;
    s.magic_resist  = 0.25;
    s.attack_damage = 50.0;
    return s;
}

// 注册一个可观察的 counter modifier, 把每次 OnAbilityExecuted 累加到全局表.
// 同时保留 ability_name 和 is_passive 让测试可读.
void register_counter_modifier(LuaState& lua) {
    const char* src = R"LUA(
        ability_exec_counter = {
            count = 0, last_name = "", last_passive = false, last_caster = nil
        }
        register_modifier("modifier_ability_exec_counter", {
            IsHidden = true, IsPurgable = false, RemoveOnDeath = false,
            OnAbilityExecuted = function(self, owner, ev)
                ability_exec_counter.count = ability_exec_counter.count + 1
                ability_exec_counter.last_name    = ev.ability_name
                ability_exec_counter.last_passive = ev.is_passive
                ability_exec_counter.last_caster  = ev.unit
                ability_exec_counter.last_level   = ev.ability:level()
            end,
        })
    )LUA";
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    ASSERT_TRUE(r.valid());
}

// 把 counter modifier 挂上 caster (永久, 1 stack).
ScriptedModifier* attach_counter(LuaState& lua, Unit& caster) {
    const auto* spec = lua.modifier_registry().find("modifier_ability_exec_counter");
    auto mod = std::make_unique<ScriptedModifier>(
        caster, "modifier_ability_exec_counter", -1.0, *spec, lua,
        caster.id(), nullptr);
    return static_cast<ScriptedModifier*>(
        caster.modifiers().attach(std::move(mod)));
}

class OnAbilityExecutedTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_.set_lua(&lua_);
        register_counter_modifier(lua_);
        reg_.load_file(std::string(kDataDir) + "/heroes/lina.yaml");
        caster_ = world_.spawn("Lina", Team::Radiant, hero_stats(), {0.0, 0.0});
        enemy_  = world_.spawn("Enemy", Team::Dire,    hero_stats(), {200.0, 0.0});
        attach_counter(lua_, *caster_);
    }

    int counter() { return lua_.state()["ability_exec_counter"]["count"]; }

    LuaState lua_;
    AbilityRegistry reg_;
    World world_;
    Unit* caster_{};
    Unit* enemy_{};
};

} // namespace

TEST_F(OnAbilityExecutedTest, FullyCastFiresHook) {
    Ability* ds = reg_.instantiate("lina_dragon_slave", *caster_);
    ASSERT_NE(ds, nullptr);

    CastTarget t; t.point = {200.0, 0.0}; t.has_point = true;
    ASSERT_EQ(ds->order_cast(t, world_), CastError::None);
    world_.advance(0.5);   // 走完 0.45 cast point + spell_start

    EXPECT_EQ(counter(), 1);
    EXPECT_EQ(lua_.state()["ability_exec_counter"]["last_name"]
              .get<std::string>(), "lina_dragon_slave");
    EXPECT_FALSE(lua_.state()["ability_exec_counter"]["last_passive"]
                 .get<bool>());
    EXPECT_EQ(lua_.state()["ability_exec_counter"]["last_caster"]
              .get<Unit*>(), caster_);
}

TEST_F(OnAbilityExecutedTest, InterruptedCastDoesNotFire) {
    Ability* ds = reg_.instantiate("lina_dragon_slave", *caster_);
    ASSERT_NE(ds, nullptr);

    CastTarget t; t.point = {200.0, 0.0}; t.has_point = true;
    ASSERT_EQ(ds->order_cast(t, world_), CastError::None);

    // cast point 0.45s 中途眩晕 -> 中断, 不应触发钩子
    world_.advance(0.1);
    caster_->modifiers().attach(modifiers::make_stunned(*caster_, 0.5));
    world_.advance(0.4);

    EXPECT_EQ(counter(), 0);
}

TEST_F(OnAbilityExecutedTest, PassiveAbilityDoesNotFire) {
    // lina_fiery_soul 是 PASSIVE, trigger_cast 在 is_passive 早退;
    // counter modifier 不应被触发.
    Ability* fs = reg_.instantiate("lina_fiery_soul", *caster_);
    ASSERT_NE(fs, nullptr);
    ASSERT_TRUE(fs->is_passive());

    CastTarget t;
    EXPECT_EQ(fs->trigger_cast(t, world_), CastError::NotReady);
    EXPECT_EQ(counter(), 0);
}

TEST_F(OnAbilityExecutedTest, MultipleCastsAccumulate) {
    Ability* ds = reg_.instantiate("lina_dragon_slave", *caster_);
    ASSERT_NE(ds, nullptr);
    ds->set_level(3);

    CastTarget t; t.point = {200.0, 0.0}; t.has_point = true;
    // trigger_cast 跳过 cooldown / mana, 适合连续触发
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(ds->trigger_cast(t, world_), CastError::None);
        world_.advance(0.5);
    }
    EXPECT_EQ(counter(), 3);
    EXPECT_EQ(lua_.state()["ability_exec_counter"]["last_level"]
              .get<int>(), 3);
}
