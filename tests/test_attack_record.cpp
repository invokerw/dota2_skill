// Stage 1: AttackRecord 框架 + 远程普攻投射物.
// 验证 Modifier 的 on_attack / on_attack_landed / on_attack_fail /
// on_attack_record_destroy 钩子顺序与远程 / 近战路径下的命中时序.
#include "dota/core/attack.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace dota;

namespace {

UnitStats melee_hero(double dmg = 50.0) {
    UnitStats s;
    s.max_health       = 1000.0;
    s.attack_damage    = dmg;
    s.attack_speed     = 100.0;
    s.base_attack_time = 1.0;
    s.attack_range     = 150.0;
    s.move_speed       = 300.0;
    s.ranged           = false;
    return s;
}

UnitStats ranged_hero(double dmg = 50.0) {
    UnitStats s = melee_hero(dmg);
    s.attack_range     = 600.0;
    s.ranged           = true;
    s.projectile_speed = 1200.0;
    return s;
}

// Spy modifier: 记录每个 record 钩子触发的顺序, 默认在 on_attack 中认领自己.
struct AttackSpyModifier : public Modifier {
    std::vector<std::string>* log;
    bool                      claim;

    AttackSpyModifier(std::string name, Unit& owner,
                      std::vector<std::string>* l, bool claim_record)
        : Modifier(std::move(name), owner, /*duration=*/-1.0)
        , log(l), claim(claim_record) {}

    void on_attack(AttackRecord& r) override {
        log->push_back("on_attack");
        if (claim) r.orb_listeners.push_back(this);
    }
    void on_attack_landed(const AttackRecord&) override {
        log->push_back("on_attack_landed");
    }
    void on_attack_fail(const AttackRecord&) override {
        log->push_back("on_attack_fail");
    }
    void on_attack_record_destroy(const AttackRecord&) override {
        log->push_back("on_attack_record_destroy");
    }
};

} // namespace

TEST(AttackRecord, MeleeImmediateLandedHooksFire) {
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(40.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(40.0), {100.0, 0.0});

    std::vector<std::string> log;
    a->modifiers().attach(std::make_unique<AttackSpyModifier>(
        "spy", *a, &log, /*claim=*/true));

    const double hp_before = t->health();
    a->issue_order(OrderAttackTarget{t->id()});
    // 近战在指令队列内立即结算 (与远程在 projectile 命中时不同).
    w.advance(World::kTickDt);

    EXPECT_LT(t->health(), hp_before);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "on_attack");
    EXPECT_EQ(log[1], "on_attack_landed");
    EXPECT_EQ(log[2], "on_attack_record_destroy");
}

TEST(AttackRecord, RangedDelaysCompleteUntilProjectileHits) {
    World w;
    auto* a = w.spawn("a", Team::Radiant, ranged_hero(40.0), {0.0, 0.0});
    // 拉到 attack_range 边缘附近, 让投射物明显延迟一段时间命中.
    auto* t = w.spawn("t", Team::Dire,    melee_hero(40.0),  {600.0, 0.0});

    std::vector<std::string> log;
    a->modifiers().attach(std::make_unique<AttackSpyModifier>(
        "spy", *a, &log, /*claim=*/true));

    a->issue_order(OrderAttackTarget{t->id()});

    // begin_attack 派发 on_attack 后, projectile 才 spawn -- swing 那个 tick
    // 已经看到 on_attack, 但还没看到 landed.
    w.advance(World::kTickDt);
    ASSERT_FALSE(log.empty());
    EXPECT_EQ(log.front(), "on_attack");
    for (const auto& e : log) EXPECT_NE(e, "on_attack_landed");

    // 等到 projectile 完整飞抵 (600 px / 1200 px/s = 0.5s)
    w.advance(0.6);

    bool saw_landed  = false;
    bool saw_destroy = false;
    for (const auto& e : log) {
        if (e == "on_attack_landed")          saw_landed  = true;
        if (e == "on_attack_record_destroy")  saw_destroy = true;
    }
    EXPECT_TRUE(saw_landed);
    EXPECT_TRUE(saw_destroy);
    EXPECT_LT(t->health(), 1000.0);
}

TEST(AttackRecord, RangedTargetDeathInFlightFiresFail) {
    // 攻击者远程, 攻击发出后投射物在飞, 此时 target 被外部杀死.
    // projectile finish 兜底走 on_attack_fail + on_attack_record_destroy, 不写血.
    World w;
    auto* a = w.spawn("a", Team::Radiant, ranged_hero(40.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(40.0),  {600.0, 0.0});
    t->set_remove_on_death(true);

    std::vector<std::string> log;
    a->modifiers().attach(std::make_unique<AttackSpyModifier>(
        "spy", *a, &log, /*claim=*/true));

    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);
    // 中途处死目标
    t->set_health(0.0);
    w.advance(0.7);

    bool saw_fail = false, saw_destroy = false, saw_landed = false;
    for (const auto& e : log) {
        if (e == "on_attack_fail")            saw_fail    = true;
        if (e == "on_attack_record_destroy")  saw_destroy = true;
        if (e == "on_attack_landed")          saw_landed  = true;
    }
    EXPECT_TRUE(saw_fail);
    EXPECT_TRUE(saw_destroy);
    EXPECT_FALSE(saw_landed);
}

TEST(AttackRecord, NonClaimingModifierGetsOnAttackButNotLanded) {
    // 不认领的 modifier 在 on_attack 之外不应收到 record 钩子.
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(20.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(20.0), {100.0, 0.0});

    std::vector<std::string> log;
    a->modifiers().attach(std::make_unique<AttackSpyModifier>(
        "noclaim", *a, &log, /*claim=*/false));

    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], "on_attack");
}

TEST(AttackRecord, AttackLandedEventCarriesRecordId) {
    // 同一次普攻发出的 AttackLandedEvent 与 record 上的 id 一致, 且后续
    // record_id 单调递增.
    World w;
    auto* a = w.spawn("a", Team::Radiant, melee_hero(20.0), {0.0, 0.0});
    auto* t = w.spawn("t", Team::Dire,    melee_hero(20.0), {100.0, 0.0});

    std::vector<EntityId> ids;
    w.events().subscribe<AttackLandedEvent>([&](AttackLandedEvent& ev){
        ids.push_back(ev.record_id);
    });

    a->issue_order(OrderAttackTarget{t->id()});
    w.advance(World::kTickDt);
    // 第二次普攻 (等 cd)
    w.advance(1.1);

    ASSERT_GE(ids.size(), 2u);
    EXPECT_NE(ids[0], kInvalidEntityId);
    EXPECT_LT(ids[0], ids[1]);
}
