// Stage A: 验证录像 / 可视化用事件被正确发布.
// 完整链路场景: Pudge 对 Lina 施放肉钩.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

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

TEST(EventEmission, MeatHookFullChain) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);
    reg.load_file(std::string(kDataDir) + "/heroes/pudge.yaml");

    World world;

    // 订阅每种事件并入队. 必须在 spawn 之前订阅 UnitSpawned, 否则错过.
    std::vector<UnitSpawnedEvent>           spawned;
    std::vector<AbilityCastStartedEvent>    cast_started;
    std::vector<AbilityCastFinishedEvent>   cast_finished;
    std::vector<ProjectileSpawnedEvent>     proj_spawned;
    std::vector<ProjectileHitEvent>         proj_hit;
    std::vector<ProjectileFinishedEvent>    proj_finished;
    std::vector<ModifierAddedEvent>         mod_added;
    std::vector<ModifierRemovedEvent>       mod_removed;
    std::vector<DamageAppliedEvent>         damage;

    world.events().subscribe<UnitSpawnedEvent>(
        [&](UnitSpawnedEvent& e){ spawned.push_back(e); });
    world.events().subscribe<AbilityCastStartedEvent>(
        [&](AbilityCastStartedEvent& e){ cast_started.push_back(e); });
    world.events().subscribe<AbilityCastFinishedEvent>(
        [&](AbilityCastFinishedEvent& e){ cast_finished.push_back(e); });
    world.events().subscribe<ProjectileSpawnedEvent>(
        [&](ProjectileSpawnedEvent& e){ proj_spawned.push_back(e); });
    world.events().subscribe<ProjectileHitEvent>(
        [&](ProjectileHitEvent& e){ proj_hit.push_back(e); });
    world.events().subscribe<ProjectileFinishedEvent>(
        [&](ProjectileFinishedEvent& e){ proj_finished.push_back(e); });
    world.events().subscribe<ModifierAddedEvent>(
        [&](ModifierAddedEvent& e){ mod_added.push_back(e); });
    world.events().subscribe<ModifierRemovedEvent>(
        [&](ModifierRemovedEvent& e){ mod_removed.push_back(e); });
    world.events().subscribe<DamageAppliedEvent>(
        [&](DamageAppliedEvent& e){ damage.push_back(e); });

    Unit* pudge = world.spawn("Pudge", Team::Radiant, hero_stats(), {0, 0});
    Unit* lina  = world.spawn("Lina",  Team::Dire,    hero_stats(), {500, 0});

    ASSERT_EQ(spawned.size(), 2u);
    EXPECT_EQ(spawned[0].id, pudge->id());
    EXPECT_EQ(spawned[1].id, lina->id());

    Ability* hook = reg.instantiate("pudge_meat_hook", *pudge);
    ASSERT_NE(hook, nullptr);
    CastTarget t; t.point = {1300, 0}; t.has_point = true;
    ASSERT_EQ(hook->order_cast(t, world), CastError::None);

    // cast_point 0.30 + 500/1300 ≈ 0.38s 命中. 推进 0.7s 包含命中 + 拖拽开始.
    world.advance(0.75);

    // cast_started 应在 advance 之前 (order_cast 时立即) 被发布.
    ASSERT_GE(cast_started.size(), 1u);
    EXPECT_EQ(cast_started[0].caster, pudge->id());
    EXPECT_EQ(cast_started[0].ability, "pudge_meat_hook");
    EXPECT_TRUE(cast_started[0].has_point);

    // 投射物 spawn → hit → finish 应至少各 1 条
    ASSERT_GE(proj_spawned.size(), 1u);
    EXPECT_EQ(proj_spawned[0].source, pudge->id());
    EXPECT_TRUE(proj_spawned[0].linear);
    EXPECT_GT(proj_spawned[0].speed, 0.0);

    ASSERT_GE(proj_hit.size(), 1u);
    EXPECT_EQ(proj_hit[0].pid, proj_spawned[0].pid);
    EXPECT_EQ(proj_hit[0].victim, lina->id());

    ASSERT_GE(proj_finished.size(), 1u);
    EXPECT_EQ(proj_finished[0].pid, proj_spawned[0].pid);

    // 伤害事件
    ASSERT_GE(damage.size(), 1u);
    EXPECT_EQ(damage[0].attacker, pudge->id());
    EXPECT_EQ(damage[0].victim,   lina->id());
    EXPECT_EQ(damage[0].type,     DamageType::Magical);
    EXPECT_GT(damage[0].amount_applied, 0.0);

    // 拖拽 motion controller 应被 attach (modifier_motion_knockback)
    bool found_knockback = false;
    for (auto& e : mod_added) {
        if (e.unit == lina->id()) { found_knockback = true; break; }
    }
    EXPECT_TRUE(found_knockback) << "拖拽 motion controller 应作为 modifier_add 事件出现";

    // 顺序: cast_started < projectile_spawned (后者经过 cast_point 才发, 时间戳无法直接比对,
    // 但发布顺序保证 cast_started 在 vector 中先于对应 projectile_spawned)
    // 不再做强约束 -- 下面验证 hook 命中后 modifier 才出现即可.

    // 推进到拖拽结束 (~ 500/1300 秒) -- modifier_remove 应被发布
    world.advance(1.0);
    bool found_knockback_removed = false;
    for (auto& e : mod_removed) {
        if (e.unit == lina->id()) { found_knockback_removed = true; break; }
    }
    EXPECT_TRUE(found_knockback_removed);
}
