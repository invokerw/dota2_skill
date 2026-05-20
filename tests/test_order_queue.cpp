// 指令队列 (PlayerOrder) 基础测试: Stage 1 / 2 覆盖 OrderMoveToPoint / OrderStop /
// 队列覆盖 vs 追加; Stage 3 追加 Cast 自动靠近 / 中断清队 / 跟随 target. Stage 4
// 上线后再补 Attack 用例.
#include "dota/ability/ability.hpp"
#include "dota/ability/manager.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace dota;

namespace {
UnitStats stats(double speed = 300.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    return s;
}

constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats(double speed = 300.0) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.max_mana         = 500.0;
    s.attack_damage    = 50.0;
    s.base_attack_time = 1.0;
    s.attack_speed     = 100.0;
    s.move_speed       = speed;
    return s;
}

// 在 caster 上挂 Lion Earth Spike (UNIT_TARGET, cast_range=625, cast_point=0.3).
Ability* attach_earth_spike(AbilityRegistry& reg, Unit& caster) {
    reg.load_file(std::string(kDataDir) + "/heroes/lion.yaml");
    return reg.instantiate("lion_earth_spike", caster);
}

// 找到 ability 在 caster->abilities().all() 中的下标 (OrderCast* 用此 index).
int index_of(Unit& u, Ability* ab) {
    const auto& all = u.abilities().all();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].get() == ab) return static_cast<int>(i);
    }
    return -1;
}
} // namespace

TEST(OrderQueue, MoveToPointEquivalentToIssueMove) {
    // issue_order(OrderMoveToPoint{p}) 与 issue_move(p) 应当走出完全相同的轨迹.
    World wa;
    auto* a = wa.spawn("a", Team::Radiant, stats(300.0), {0.0, 0.0});
    a->issue_move({600.0, 0.0});

    World wb;
    auto* b = wb.spawn("b", Team::Radiant, stats(300.0), {0.0, 0.0});
    b->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});

    for (int i = 0; i < 30; ++i) {
        wa.advance(World::kTickDt);
        wb.advance(World::kTickDt);
        EXPECT_NEAR(a->position().x, b->position().x, 1e-9);
        EXPECT_NEAR(a->position().y, b->position().y, 1e-9);
    }
}

TEST(OrderQueue, OverrideClearsQueue) {
    // queue=false 默认覆盖整队
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    h->issue_order(OrderMoveToPoint{Vec2{0.0, 600.0}}, /*queue=*/true);
    ASSERT_EQ(h->orders().size(), 2u);

    h->issue_order(OrderMoveToPoint{Vec2{-300.0, 0.0}});
    ASSERT_EQ(h->orders().size(), 1u);
    ASSERT_TRUE(h->current_order());
    EXPECT_TRUE(std::holds_alternative<OrderMoveToPoint>(*h->current_order()));
}

TEST(OrderQueue, AppendKeepsCurrentMovePath) {
    // queue=true 追加: 当前正在走的指令不被打断, 新指令排到队尾
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    w.advance(0.5);
    const Vec2 p = h->position();

    h->issue_order(OrderMoveToPoint{Vec2{0.0, 600.0}}, /*queue=*/true);
    EXPECT_EQ(h->orders().size(), 2u);
    // move_target 仍指向第一个指令的终点
    ASSERT_TRUE(h->move_target().has_value());
    EXPECT_NEAR(h->move_target()->x, 600.0, 1e-9);
    EXPECT_NEAR(h->move_target()->y, 0.0,   1e-9);
    EXPECT_EQ(p.x, h->position().x);  // 没被新指令打断
    EXPECT_EQ(p.y, h->position().y);
}

TEST(OrderQueue, MoveToPointPopsOnArrival) {
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    EXPECT_EQ(h->orders().size(), 1u);

    w.advance(2.5);   // 充分超过 600/300 = 2.0s
    EXPECT_NEAR(h->position().x, 600.0, 1.0);
    EXPECT_TRUE(h->orders().empty());
    EXPECT_EQ(h->current_order(), nullptr);
}

TEST(OrderQueue, StopClearsQueue) {
    // OrderStop 立即清掉所有未完成指令, 单位停下
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 600.0}}, /*queue=*/true);
    w.advance(0.3);
    const double x_before = h->position().x;
    EXPECT_GT(x_before, 50.0);

    h->issue_order(OrderStop{});
    EXPECT_TRUE(h->orders().empty());
    EXPECT_FALSE(h->move_target().has_value());

    w.advance(1.0);
    EXPECT_NEAR(h->position().x, x_before, 1e-6);
}

TEST(OrderQueue, QueuedMoveAdvancesAfterFirstFinishes) {
    // 队列里两个 MoveToPoint, 第一个走完应自动衔接第二个
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(600.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 600.0}}, /*queue=*/true);

    w.advance(1.1);     // 600/600 = 1.0s 第一个完成
    EXPECT_NEAR(h->position().x, 600.0, 1.0);
    EXPECT_EQ(h->orders().size(), 1u);  // 只剩第二个
    ASSERT_TRUE(h->move_target().has_value());
    EXPECT_NEAR(h->move_target()->x, 600.0, 1e-9);
    EXPECT_NEAR(h->move_target()->y, 600.0, 1e-9);

    w.advance(1.1);     // 完成第二个
    EXPECT_NEAR(h->position().y, 600.0, 1.0);
    EXPECT_TRUE(h->orders().empty());
}

TEST(OrderQueue, ClearOrdersStopsImmediately) {
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 0.0}});
    h->issue_order(OrderMoveToPoint{Vec2{600.0, 600.0}}, /*queue=*/true);

    h->clear_orders();
    EXPECT_TRUE(h->orders().empty());
    EXPECT_FALSE(h->move_target().has_value());
}

TEST(OrderQueue, IssueMoveCompatPath) {
    // 老接口 issue_move 应该等价于 issue_order(OrderMoveToPoint{p})
    World w;
    auto* h = w.spawn("h", Team::Radiant, stats(300.0), {0.0, 0.0});
    h->issue_move({400.0, 0.0});
    ASSERT_EQ(h->orders().size(), 1u);
    ASSERT_TRUE(h->current_order());
    EXPECT_TRUE(std::holds_alternative<OrderMoveToPoint>(*h->current_order()));
}

// --- Stage 3: 自动靠近的施法 + 移动打断 + 中断清队 ---

TEST(OrderQueue, CastTargetAutoApproachAndFires) {
    // caster 与 target 距离 1200 (超过 625 cast_range), 发 OrderCastTarget ->
    // 单位应自动走向 target, 进入施法范围后调 order_cast 并清掉派生 move_path.
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(600.0), {0.0,    0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(),       {1200.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    ASSERT_NE(spike, nullptr);

    const int idx = index_of(*lion, spike);
    ASSERT_GE(idx, 0);

    lion->issue_order(OrderCastTarget{idx, enemy->id()});
    // 还在范围外: 不应立刻进入 Casting.
    EXPECT_NE(spike->phase(), CastPhase::Casting);
    // 应该派生了一条跟随 move 朝 target 走.
    ASSERT_TRUE(lion->move_target().has_value());

    // 推到进入 cast_range. 1200 - 625 = 575, 600/s 速度需 ~0.96s, 留余量.
    for (int i = 0; i < 80 && spike->phase() != CastPhase::Casting; ++i) {
        w.advance(World::kTickDt);
    }
    EXPECT_EQ(spike->phase(), CastPhase::Casting);
    // 进入 Casting 那刻派生 move_path 应当被清掉, 单位停下读条.
    EXPECT_FALSE(lion->move_target().has_value());

    // 推完 cast_point + on_spell_start, 伤害应当结算.
    const double hp_before = enemy->health();
    w.advance(0.4);
    EXPECT_LT(enemy->health(), hp_before);
}

TEST(OrderQueue, CastInRangeFiresImmediately) {
    // caster 与 target 距离 100 (远小于 625 cast_range), issue_order 入队即派发,
    // 同 tick 内进入 Casting (与现有 ability::order_cast 行为对齐).
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    const int idx = index_of(*lion, spike);

    lion->issue_order(OrderCastTarget{idx, enemy->id()});
    EXPECT_EQ(spike->phase(), CastPhase::Casting);
    EXPECT_FALSE(lion->move_target().has_value());
}

TEST(OrderQueue, CastInterruptedClearsQueue) {
    // 队列里 [Cast, Move]. cast 进入 Casting 后 caster 被 stunned -> ability
    // 走 interrupt 分支 -> 应清空整队, 不会衔接到第二条 Move.
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(), {0.0, 0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(), {100.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    ASSERT_NE(spike, nullptr);
    const int idx = index_of(*lion, spike);

    lion->issue_order(OrderCastTarget{idx, enemy->id()});
    lion->issue_order(OrderMoveToPoint{Vec2{500.0, 500.0}}, /*queue=*/true);
    ASSERT_EQ(lion->orders().size(), 2u);

    // 推一帧让 dispatch_front 触发 order_cast (caster 与 target 距离 100, 远小于
    // cast_range, 立刻进入 Casting).
    w.advance(World::kTickDt);
    ASSERT_EQ(spike->phase(), CastPhase::Casting);

    // 上 stun -> ability::advance interrupt 分支应清队.
    lion->modifiers().attach(modifiers::make_stunned(*lion, 0.5));
    w.advance(World::kTickDt);
    EXPECT_TRUE(lion->orders().empty());
    // 没有衔接到 MoveToPoint
    EXPECT_FALSE(lion->move_target().has_value());
}

TEST(OrderQueue, CastTargetFollowsMovingTarget) {
    // target 在跑, caster 应每 tick 刷新派生 move 朝 target 当前位置走.
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(800.0), {0.0,    0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(200.0), {1500.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    const int idx = index_of(*lion, spike);

    lion->issue_order(OrderCastTarget{idx, enemy->id()});
    enemy->issue_move({1500.0, 1500.0});  // target 朝 +Y 跑

    // 推到 lion 进入施法范围.
    for (int i = 0; i < 200 && spike->phase() != CastPhase::Casting; ++i) {
        w.advance(World::kTickDt);
    }
    ASSERT_EQ(spike->phase(), CastPhase::Casting);
    // lion 应该跟着 enemy 偏离了正东方向 -- 起点 (0,0), enemy 跑向 (1500,1500),
    // lion 位置 y 应当 > 0.
    EXPECT_GT(lion->position().y, 50.0);
}

// --- Stage 4: AttackTarget 迁移 ---

TEST(OrderQueue, AttackTargetAutoApproach) {
    // attacker 与 target 距离 600 (远超 attack_range=150 + 双 hull). 发
    // OrderAttackTarget -> 单位应自动走到 attack_range 内才发出第一次 attack_landed.
    World w;
    UnitStats a_stats = hero_stats(400.0);
    a_stats.attack_range = 150.0;
    UnitStats t_stats = hero_stats();
    auto* a = w.spawn("A", Team::Radiant, a_stats, {0.0,   0.0});
    auto* t = w.spawn("T", Team::Dire,    t_stats, {600.0, 0.0});

    int hits_in_range = 0;
    bool any_hit = false;
    w.events().subscribe<AttackLandedEvent>([&](AttackLandedEvent& e) {
        any_hit = true;
        // 命中那一刻 attacker 应已走进 attack_range.
        const double r = a->stats().attack_range
                       + a->hull_radius() + t->hull_radius();
        if (distance_sq(a->position(), t->position()) <= r * r) {
            ++hits_in_range;
        }
        (void)e;
    });

    a->issue_order(OrderAttackTarget{t->id()});
    // 600 距离 -> 走到 ~198 (attack_range+hulls) 需要 ~1s, 留 2s 余量含 swing.
    w.advance(2.0);

    EXPECT_TRUE(any_hit);
    EXPECT_GT(hits_in_range, 0);
}

TEST(OrderQueue, AttackTargetPersistsAcrossSwings) {
    // 一次 issue_order, 应当持续攻击, 多次 attack_landed 直到对手死亡.
    World w;
    auto* a = w.spawn("A", Team::Radiant, hero_stats(), {0.0,   0.0});
    auto* t = w.spawn("T", Team::Dire,    hero_stats(), {100.0, 0.0});
    a->issue_order(OrderAttackTarget{t->id()});
    int hits = 0;
    w.events().subscribe<AttackLandedEvent>([&](AttackLandedEvent&) { ++hits; });
    w.advance(3.5);
    EXPECT_GT(hits, 1);
    // 还活着 -- 攻击仍在持续, current_order 不被 pop.
    EXPECT_TRUE(a->current_order());
    EXPECT_TRUE(std::holds_alternative<OrderAttackTarget>(*a->current_order()));
}

TEST(OrderQueue, AttackTargetPopsWhenTargetDies) {
    // target 死亡 -> attack 项 pop, 队列衔接到下一条.
    World w;
    UnitStats a_stats = hero_stats(150.0);  // 慢速, 让 MoveToPoint 还在走
    a_stats.attack_damage = 9999.0;          // 一击必杀
    auto* a = w.spawn("A", Team::Radiant, a_stats, {0.0,   0.0});
    auto* t = w.spawn("T", Team::Dire,    hero_stats(), {100.0, 0.0});
    a->issue_order(OrderAttackTarget{t->id()});
    a->issue_order(OrderMoveToPoint{Vec2{2000.0, 0.0}}, /*queue=*/true);

    w.advance(2.0);
    EXPECT_FALSE(t->alive());
    // 第一条 attack 已 pop, 衔接到 MoveToPoint (距离 2000, 速度 150 -> 还远没走完).
    ASSERT_TRUE(a->current_order());
    EXPECT_TRUE(std::holds_alternative<OrderMoveToPoint>(*a->current_order()));
}

TEST(OrderQueue, AttackTargetStopOverridesQueue) {
    // 攻击中发 OrderStop -> 整队清空, 不再继续攻击.
    World w;
    auto* a = w.spawn("A", Team::Radiant, hero_stats(), {0.0,   0.0});
    auto* t = w.spawn("T", Team::Dire,    hero_stats(), {100.0, 0.0});
    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(0.2);  // 至少一次 swing.

    a->issue_order(OrderStop{});
    EXPECT_TRUE(a->orders().empty());

    int hits_after_stop = 0;
    w.events().subscribe<AttackLandedEvent>([&](AttackLandedEvent&) {
        ++hits_after_stop;
    });
    w.advance(2.0);
    EXPECT_EQ(hits_after_stop, 0);
}

TEST(OrderQueue, CastTargetDeadPopsAndContinues) {
    // 跟随期间 target 死亡, 该 cast 项应被 pop, 队列里下一条衔接.
    AbilityRegistry reg;
    World w;
    auto* lion  = w.spawn("Lion",  Team::Radiant, hero_stats(300.0), {0.0,    0.0});
    auto* enemy = w.spawn("Enemy", Team::Dire,    hero_stats(),       {1500.0, 0.0});
    auto* spike = attach_earth_spike(reg, *lion);
    const int idx = index_of(*lion, spike);

    lion->issue_order(OrderCastTarget{idx, enemy->id()});
    lion->issue_order(OrderMoveToPoint{Vec2{0.0, 600.0}}, /*queue=*/true);
    ASSERT_EQ(lion->orders().size(), 2u);

    enemy->apply_raw_damage(enemy->max_health() + 1.0);  // 立即弄死
    ASSERT_FALSE(enemy->alive());
    // 推一帧 -- pump_orders 应当 pop 掉 cast (target 死亡), 衔接到 MoveToPoint.
    w.advance(World::kTickDt);
    ASSERT_EQ(lion->orders().size(), 1u);
    ASSERT_TRUE(lion->current_order());
    EXPECT_TRUE(std::holds_alternative<OrderMoveToPoint>(*lion->current_order()));
}
