// 软碰撞分离 pass: 在 World::tick_once 末尾, 把重叠的单位沿连心线推开.
// "谁动谁推": 仅 a 动 -> 完全推 a; 双方都动 -> 各推一半; 都没动 -> 不动.

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/library.hpp"

#include <cmath>
#include <gtest/gtest.h>

using dota::Team;
using dota::Unit;
using dota::UnitStats;
using dota::Vec2;
using dota::World;

namespace {

UnitStats hero_stats() {
    UnitStats s;
    s.max_health    = 1000.0;
    s.attack_damage = 0.0;       // 避免攻击指令对位置造成干扰
    s.move_speed    = 0.0;
    s.hull_radius   = 24.0;
    return s;
}

double dist(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

TEST(UnitCollision, StationaryPairKeepsInitialOverlap) {
    // 双方都没动过 -> 即使重叠也保留, 测试 fixture 不被 silently 改写.
    World w;
    Unit* a = w.spawn("a", Team::Radiant, hero_stats(), {0.0, 0.0});
    Unit* b = w.spawn("b", Team::Dire,    hero_stats(), {10.0, 0.0});
    w.advance(World::kTickDt);
    EXPECT_DOUBLE_EQ(a->position().x, 0.0);
    EXPECT_DOUBLE_EQ(b->position().x, 10.0);
}

TEST(UnitCollision, MoverIsPushedOutWhenOtherIsStationary) {
    // a 显式移动到 b 上面 -> 分离 pass 应只推 a, b 保持原位.
    World w;
    Unit* a = w.spawn("a", Team::Radiant, hero_stats(), {-200.0, 0.0});
    Unit* b = w.spawn("b", Team::Dire,    hero_stats(), {0.0,    0.0});
    w.advance(World::kTickDt);   // 让两者初始 tick_start 落地

    a->set_position({0.0, 0.0}); // 紧贴 b
    w.advance(World::kTickDt);

    EXPECT_DOUBLE_EQ(b->position().x, 0.0);
    EXPECT_DOUBLE_EQ(b->position().y, 0.0);
    // a 应该被沿 -x 方向推到刚好不重叠的距离 (= 双方 hull_radius 之和)
    EXPECT_NEAR(dist(a->position(), b->position()), 48.0, 1e-6);
    EXPECT_LT(a->position().x, b->position().x);
}

TEST(UnitCollision, BothMoversSplitOverlapEvenly) {
    // 双方都在本 tick 移动到相互重叠 -> 各推一半.
    World w;
    Unit* a = w.spawn("a", Team::Radiant, hero_stats(), {-200.0, 0.0});
    Unit* b = w.spawn("b", Team::Dire,    hero_stats(), {200.0,  0.0});
    w.advance(World::kTickDt);

    // 双方都跨步走到原点附近
    a->set_position({-10.0, 0.0});
    b->set_position({ 10.0, 0.0});
    w.advance(World::kTickDt);

    // 重叠 = 48 - 20 = 28. 各推 14 -> a 在 -24, b 在 +24, 距离 48.
    EXPECT_NEAR(dist(a->position(), b->position()), 48.0, 1e-6);
    EXPECT_NEAR(a->position().x, -24.0, 1e-6);
    EXPECT_NEAR(b->position().x,  24.0, 1e-6);
}

TEST(UnitCollision, NoUnitCollisionStateSkipsSeparation) {
    // 带 NoUnitCollision 的单位不参与分离 (例: hook 拉拽中, thinker).
    World w;
    Unit* a = w.spawn("a", Team::Radiant, hero_stats(), {-200.0, 0.0});
    Unit* b = w.spawn("b", Team::Dire,    hero_stats(), {0.0,    0.0});
    // 给 b 上一个永久 NoUnitCollision modifier.
    auto mod = std::make_unique<dota::modifiers::GenericState>(
        *b, "no_coll", /*duration=*/-1.0,
        dota::state_bit(dota::ModifierState::NoUnitCollision));
    b->modifiers().attach(std::move(mod));

    w.advance(World::kTickDt);
    a->set_position({0.0, 0.0}); // 与 b 完全重合
    w.advance(World::kTickDt);

    // a 应保持重叠状态, 因为 b 不参与碰撞.
    EXPECT_DOUBLE_EQ(a->position().x, 0.0);
    EXPECT_DOUBLE_EQ(b->position().x, 0.0);
}

TEST(UnitCollision, ExactOverlapResolvedDeterministically) {
    // 两单位完全同坐标 + 都动过 -> 仍能分离, 不产生 NaN.
    World w;
    Unit* a = w.spawn("a", Team::Radiant, hero_stats(), {-100.0, 0.0});
    Unit* b = w.spawn("b", Team::Dire,    hero_stats(), { 100.0, 0.0});
    w.advance(World::kTickDt);

    a->set_position({0.0, 0.0});
    b->set_position({0.0, 0.0});
    w.advance(World::kTickDt);

    EXPECT_FALSE(std::isnan(a->position().x));
    EXPECT_FALSE(std::isnan(a->position().y));
    EXPECT_FALSE(std::isnan(b->position().x));
    EXPECT_FALSE(std::isnan(b->position().y));
    EXPECT_NEAR(dist(a->position(), b->position()), 48.0, 1e-6);
}
