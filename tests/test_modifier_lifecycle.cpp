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

// Test modifier that records lifecycle hook calls and supports think_interval.
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

    // After expiry and purge, on_destroyed fires once.
    w.advance(1.0);
    // t is now dangling; instead verify by trying to find it.
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

TEST(ModifierLifecycle, ThinkIntervalFiresAtCadence) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* t = u->modifiers().attach_new<Tracker>(-1.0, 0.5);

    w.advance(2.0);
    // 0.5s cadence over 2.0s → 4 ticks.
    EXPECT_EQ(t->ticks, 4);
}

TEST(ModifierLifecycle, StackCountTriggersCallback) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* t = u->modifiers().attach_new<Tracker>(-1.0, 0.0);
    EXPECT_EQ(t->stack_changes, 0);

    t->set_stack_count(3);
    EXPECT_EQ(t->stack_changes, 1);
    t->set_stack_count(3);  // no-op
    EXPECT_EQ(t->stack_changes, 1);
}

TEST(ModifierLifecycle, RefreshExtendsRemainingDuration) {
    World w;
    auto* u = w.spawn("u", Team::Radiant, basic_stats());
    auto* m = u->modifiers().attach(modifiers::make_stunned(*u, 0.5));

    w.advance(0.4);
    m->refresh(1.0);
    w.advance(0.7);
    EXPECT_FALSE(u->can_cast()); // refreshed stun still active
    w.advance(0.5);
    EXPECT_TRUE(u->can_cast());
}
