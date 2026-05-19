// 模拟 skill_tester 的核心 Scene 行为, 在没有 raylib 的情况下重现崩溃.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/script/lua_state.hpp"
#include "dota/tools/hero_catalog.hpp"

#include <gtest/gtest.h>
#include <memory>

using namespace dota;

namespace {

// skill_tester Scene::rebuild_with_hero 的精简版. 核心要点是销毁顺序:
// World 持有 Unit -> AbilityManager -> ScriptedAbility, 后者通过 sol::table /
// sol::function 引用 LuaState 拥有的 lua_State; 必须先销毁 world_, 再销毁 lua_,
// 否则 sol 析构会对已销毁的 state 调 luaL_unref 触发 segfault.
struct Harness {
    std::unique_ptr<LuaState>        lua;
    std::unique_ptr<AbilityRegistry> reg;
    std::unique_ptr<World>           world;
    Unit*                            caster{nullptr};
    std::vector<Ability*>            abilities;

    void rebuild(const tools::HeroEntry& h) {
        abilities.clear();
        caster = nullptr;
        world.reset();
        reg.reset();
        lua.reset();

        lua = std::make_unique<LuaState>();
        reg = std::make_unique<AbilityRegistry>();
        reg->set_lua(lua.get());
        reg->load_file(h.yaml_path);
        world = std::make_unique<World>();
        UnitStats cs;
        cs.max_health = 1500.0; cs.max_mana = 700.0; cs.attack_damage = 55.0;
        cs.base_armor = 2.0; cs.magic_resist = 0.25; cs.base_attack_time = 1.7;
        caster = world->spawn(h.yaml_name, Team::Radiant, cs, {-600.0, 0.0});
        for (const auto& a : h.abilities) {
            if (a.is_passive) continue;
            if (Ability* ab = reg->instantiate(a.name, *caster)) {
                abilities.push_back(ab);
            }
        }
        UnitStats ds; ds.max_health = 6000.0; ds.magic_resist = 0.25;
        world->spawn("dummy", Team::Dire, ds, {600.0, 0.0});
    }
};

} // namespace

// 反复重建 World + LuaState + AbilityRegistry, 验证销毁顺序正确, 不会触发 sol
// 引用悬挂导致的 segfault. 这是用户切换英雄时崩溃的最小复现.
TEST(SkillTesterSmoke, RebuildAcrossAllHeroes) {
    tools::HeroCatalog cat;
    cat.scan(std::string(DOTA_DATA_DIR) + "/heroes");
    ASSERT_FALSE(cat.heroes().empty());

    Harness s;
    for (const auto& h : cat.heroes()) {
        s.rebuild(h);
        EXPECT_NE(s.caster, nullptr);
    }
    // 再来一轮, 确保第二次 rebuild 也安全
    for (const auto& h : cat.heroes()) {
        s.rebuild(h);
    }
}

// 回归: World::tick_once 用 range-for 迭代 units_, 但 ability/modifier 钩子
// 可能通过 Lua 回调 World::create_thinker/spawn 向 units_ push_back. 一旦
// vector 扩容旧 buffer 释放, range-for 拿到的 reference 立刻悬挂
// (heap-use-after-free). 用 ASan 跑这个用例可复现.
TEST(SkillTesterSmoke, TickWhileLuaSpawnsThinker) {
    tools::HeroCatalog cat;
    cat.scan(std::string(DOTA_DATA_DIR) + "/heroes");

    // 找一个已知会调 create_thinker 的英雄 (Lina laguna_blade 不会, 但 Pudge
    // meat_hook 不会, 真正会的是带 thinker 的技能). 这里直接用全部英雄, 让
    // 任意一个 Lua 技能能跑就够暴露问题了.
    Harness s;
    for (const auto& h : cat.heroes()) {
        s.rebuild(h);
        for (Ability* ab : s.abilities) {
            CastTarget tgt;
            if (has_flag(ab->behavior(), BehaviorFlag::PointTarget)) {
                tgt.point = {600.0, 0.0}; tgt.has_point = true;
            } else if (has_flag(ab->behavior(), BehaviorFlag::UnitTarget)) {
                for (Unit* u : s.world->units_on_team(Team::Dire)) {
                    tgt.unit = u; break;
                }
            }
            ab->order_cast(tgt, *s.world);
            for (int i = 0; i < 60; ++i) s.world->advance(0.05);
        }
    }
}
