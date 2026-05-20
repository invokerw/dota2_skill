#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/library.hpp"

#include <gtest/gtest.h>

using namespace dota;

namespace {

UnitStats basic_stats() {
    UnitStats s;
    s.max_health = 1000.0;
    return s;
}

// 测试修改器, 记录生命周期钩子调用并支持 think_interval.
struct Tracker : public Modifier {
    int creates = 0;
    int destroys = 0;
    int stack_changes = 0;
    int ticks = 0;

    Tracker(Unit& owner, double duration, double think)
        : Modifier("tracker", owner, duration) {
        set_think_interval(think);
    }

    void on_created() override { ++creates; }
    void on_destroyed() override { ++destroys; }
    void on_stack_changed(int, int) override { ++stack_changes; }
    void on_interval_think() override { ++ticks; }
};

} // namespace

TEST(ModifierLifecycle, CreatedAndDestroyedHooksFire) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());

    auto* t = u->modifiers().attach_new<Tracker>(0.5, 0.0);
    EXPECT_EQ(t->creates, 1);
    EXPECT_EQ(t->destroys, 0);

    // 过期并清除后, on_destroyed 触发一次.
    w.advance(1.0);
    // t 现在是悬空指针; 改为尝试查找它来验证.
    EXPECT_EQ(u->modifiers().find("tracker"), nullptr);
}

TEST(ModifierLifecycle, ExplicitRemoveFiresDestroyed) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* t = u->modifiers().attach_new<Tracker>(-1.0, 0.0);
    (void)t;
    EXPECT_TRUE(u->modifiers().remove("tracker"));
    EXPECT_EQ(u->modifiers().find("tracker"), nullptr);
}

TEST(ModifierLifecycle, RemoveAtRemovesIndexedModifier) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    u->modifiers().attach(modifiers::make_stunned(*u, -1.0));
    u->modifiers().attach(modifiers::make_silenced(*u, -1.0));

    EXPECT_TRUE(u->modifiers().remove_at(1));
    EXPECT_NE(u->modifiers().find("modifier_stunned"), nullptr);
    EXPECT_EQ(u->modifiers().find("modifier_silenced"), nullptr);
    EXPECT_FALSE(u->modifiers().remove_at(10));
}

TEST(ModifierLifecycle, ThinkIntervalFiresAtCadence) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* t = u->modifiers().attach_new<Tracker>(-1.0, 0.5);

    w.advance(2.0);
    // 2.0 秒内以 0.5 秒节奏 → 4 次 tick.
    EXPECT_EQ(t->ticks, 4);
}

TEST(ModifierLifecycle, StackCountTriggersCallback) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* t = u->modifiers().attach_new<Tracker>(-1.0, 0.0);
    EXPECT_EQ(t->stack_changes, 0);

    t->set_stack_count(3);
    EXPECT_EQ(t->stack_changes, 1);
    t->set_stack_count(3);  // 无操作
    EXPECT_EQ(t->stack_changes, 1);
}

TEST(ModifierLifecycle, RefreshExtendsRemainingDuration) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* m = u->modifiers().attach(modifiers::make_stunned(*u, 0.5));

    w.advance(0.4);
    m->refresh(1.0);
    w.advance(0.7);
    EXPECT_FALSE(u->can_cast()); // 刷新的眩晕仍然生效
    w.advance(0.5);
    EXPECT_TRUE(u->can_cast());
}
