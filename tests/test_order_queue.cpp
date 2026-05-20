// 指令队列 (PlayerOrder) 基础测试: Stage 1 仅覆盖 OrderMoveToPoint / OrderStop /
// 队列覆盖 vs 追加. Stage 3/4 上线后再补 Cast / Attack 用例.
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace dota;

namespace {
UnitStats stats(double speed = 300.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    return s;
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
