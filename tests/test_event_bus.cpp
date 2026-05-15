#include "dota/core/event_bus.hpp"

#include <gtest/gtest.h>

using dota::EventBus;

namespace {

struct Ping {
    int value;
};

struct Pong {
    int& counter;
};

} // namespace

TEST(EventBus, DeliversToSubscribersInOrder) {
    EventBus bus;
    std::vector<int> seen;
    bus.subscribe<Ping>([&](Ping& p) { seen.push_back(p.value); });
    bus.subscribe<Ping>([&](Ping& p) { seen.push_back(p.value * 10); });

    Ping ev{3};
    bus.publish(ev);

    ASSERT_EQ(seen.size(), 2u);
    EXPECT_EQ(seen[0], 3);
    EXPECT_EQ(seen[1], 30);
}

TEST(EventBus, MutableEventLetsHandlersChainModifications) {
    EventBus bus;
    bus.subscribe<Ping>([](Ping& p) { p.value += 1; });
    bus.subscribe<Ping>([](Ping& p) { p.value *= 2; });

    Ping ev{5};
    bus.publish(ev);

    // (5 + 1) * 2 == 12. 这就是伤害/治疗事件在后续阶段
    // 被修改器重写的方式。
    EXPECT_EQ(ev.value, 12);
}

TEST(EventBus, UnsubscribeStopsDelivery) {
    EventBus bus;
    int calls = 0;
    auto token = bus.subscribe<Ping>([&](Ping&) { ++calls; });

    Ping ev{0};
    bus.publish(ev);
    bus.unsubscribe<Ping>(token);
    bus.publish(ev);

    EXPECT_EQ(calls, 1);
}

TEST(EventBus, HandlerCanSubscribeDuringDispatchWithoutInvalidating) {
    EventBus bus;
    int late_calls = 0;
    bus.subscribe<Ping>([&](Ping&) {
        bus.subscribe<Ping>([&](Ping&) { ++late_calls; });
    });

    Ping ev{0};
    bus.publish(ev); // 此处添加的订阅者应仅在下次发布时触发
    EXPECT_EQ(late_calls, 0);

    bus.publish(ev);
    // 外部处理器再次运行并添加了另一个监听器，但在第一次分发期间
    // 注册的那个在这里只触发一次。
    EXPECT_GE(late_calls, 1);
}

TEST(EventBus, DistinctEventTypesAreIndependent) {
    EventBus bus;
    int pings = 0;
    int pongs = 0;
    bus.subscribe<Ping>([&](Ping&) { ++pings; });
    bus.subscribe<Pong>([&](Pong& p) { ++p.counter; ++pongs; });

    Ping p{0};
    bus.publish(p);
    int external = 0;
    Pong po{external};
    bus.publish(po);
    bus.publish(po);

    EXPECT_EQ(pings, 1);
    EXPECT_EQ(pongs, 2);
    EXPECT_EQ(external, 2);
}
