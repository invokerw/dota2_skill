// Stage D: Player 把 Recorder 写出的 JSONL 解回 UnitView / ProjectileView.
// 端到端 round-trip: 录一段肉钩战斗 → Player 解析 → 检查关键状态.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/replay/player.hpp"
#include "dota/replay/recorder.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

using namespace dota;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health   = 1500.0;
    s.max_mana     = 600.0;
    s.magic_resist = 0.25;
    return s;
}
} // namespace

TEST(ReplayPlayer, RoundTripMeatHookScene) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);
    reg.load_file(std::string(kDataDir) + "/heroes/pudge.yaml");

    std::ostringstream out;
    World world;
    Recorder rec(world, out);
    rec.write_header("test");

    Unit* pudge = world.spawn("Pudge", Team::Radiant, hero_stats(), {0, 0});
    Unit* lina  = world.spawn("Lina",  Team::Dire,    hero_stats(), {500, 0});
    EntityId pudge_id = pudge->id();
    EntityId lina_id  = lina->id();

    Ability* hook = reg.instantiate("pudge_meat_hook", *pudge);
    ASSERT_NE(hook, nullptr);
    CastTarget t; t.point = {1300, 0}; t.has_point = true;
    hook->order_cast(t, world);

    world.advance(1.5);

    // 把录像喂给 Player
    std::istringstream in(out.str());
    replay::Player player;
    ASSERT_TRUE(player.load(in));
    EXPECT_EQ(player.tick_rate(), 30);
    EXPECT_EQ(player.scenario(), "test");
    EXPECT_GE(player.frame_count(), 30u);

    // seek 到末尾
    player.seek(player.duration() + 0.001);

    // unit_spawn 事件 + 后续 positions 应该让两个单位都已知
    const auto& units = player.units();
    ASSERT_EQ(units.count(pudge_id), 1u);
    ASSERT_EQ(units.count(lina_id),  1u);

    const auto& pv = units.at(pudge_id);
    const auto& lv = units.at(lina_id);
    EXPECT_EQ(pv.name, "Pudge");
    EXPECT_EQ(lv.name, "Lina");
    EXPECT_EQ(pv.team, Team::Radiant);
    EXPECT_EQ(lv.team, Team::Dire);
    EXPECT_DOUBLE_EQ(pv.max_hp, 1500.0);

    // 肉钩拖回应让 Lina 显著靠近 Pudge
    EXPECT_LT(lv.position.x, 500.0);

    // 至少收集到 1 条伤害和 1 条死亡 / 击中 / 治疗 中的事件 -- 肉钩肯定打了魔法伤害
    EXPECT_FALSE(player.damages().empty());
    bool magical = false;
    for (auto& d : player.damages()) {
        if (d.dst == lina_id && d.applied > 0.0 && d.type == DamageType::Magical) {
            magical = true; break;
        }
    }
    EXPECT_TRUE(magical);

    // Lina 应该挂上过 motion_knockback 类的修饰器 (拖拽). modifier_add 已被 Player
    // 应用到 lv.modifiers, 但拖拽结束后会被 modifier_remove 清掉, 所以这里检查 hp
    // 比初始低也行.
    EXPECT_LT(lv.hp, lv.max_hp);
}

TEST(ReplayPlayer, AdvanceMatchesSeek) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);
    reg.load_file(std::string(kDataDir) + "/heroes/pudge.yaml");

    std::ostringstream out;
    World world;
    Recorder rec(world, out);
    rec.write_header("test");
    world.spawn("Pudge", Team::Radiant, hero_stats(), {0, 0});
    world.spawn("Lina",  Team::Dire,    hero_stats(), {500, 0});
    world.advance(0.5);

    replay::Player a, b;
    std::istringstream sa(out.str()), sb(out.str());
    ASSERT_TRUE(a.load(sa));
    ASSERT_TRUE(b.load(sb));

    // 用 advance 推进 0.5 秒
    a.advance(0.5);
    // 用 seek(0.5)
    b.seek(0.5);

    EXPECT_EQ(a.units().size(), b.units().size());
    for (const auto& [id, ua] : a.units()) {
        ASSERT_EQ(b.units().count(id), 1u);
        const auto& ub = b.units().at(id);
        EXPECT_DOUBLE_EQ(ua.position.x, ub.position.x);
        EXPECT_DOUBLE_EQ(ua.position.y, ub.position.y);
        EXPECT_EQ(ua.alive, ub.alive);
    }
}
