// CombatLog: 订阅 World 事件, 缓冲战斗记录条目.
//
// 覆盖场景:
//   - 伤害 / 治疗 / modifier add+remove / unit died 都会进入缓冲.
//   - source_name / target_name 在事件发布时 snapshot.
//   - 容量上限生效 (旧条目被丢弃).
//   - filter 拒绝时不入缓冲.
//   - format() 不会因为缺名字 / 缺 source 崩溃.

#include "dota/combat/damage.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/log/combat_log.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace dota;

namespace {

UnitStats default_stats(double max_hp = 1000.0) {
    UnitStats s;
    s.max_health = max_hp;
    s.max_mana   = 0.0;
    s.attack_damage = 0.0;
    s.magic_resist = 0.0;
    return s;
}

// 测试用 modifier: 默认无 property/state, 仅占名字.
class TestModifier : public Modifier {
public:
    TestModifier(std::string name, Unit& owner, double duration = -1.0)
        : Modifier(std::move(name), owner, duration) {}
};

} // namespace

TEST(CombatLog, RecordsDamageEntry) {
    World w;
    CombatLog log(w);

    Unit* atk = w.spawn("Atk", Team::Radiant, default_stats());
    Unit* vic = w.spawn("Vic", Team::Dire,    default_stats());

    DamageInstance d;
    d.attacker = atk;
    d.victim   = vic;
    d.type     = DamageType::Magical;
    d.amount   = 200.0;
    deal_damage(d);

    ASSERT_EQ(log.size(), 1u);
    const auto& e = log.entries().front();
    EXPECT_EQ(e.kind,        CombatLogKind::Damage);
    EXPECT_EQ(e.source,      atk->id());
    EXPECT_EQ(e.target,      vic->id());
    EXPECT_EQ(e.source_name, "Atk");
    EXPECT_EQ(e.target_name, "Vic");
    EXPECT_EQ(e.dtype,       DamageType::Magical);
    EXPECT_GT(e.amount,      0.0);
    EXPECT_GT(e.amount_pre,  0.0);
}

TEST(CombatLog, RecordsHealEntry) {
    World w;
    CombatLog log(w);

    Unit* healer = w.spawn("Healer", Team::Radiant, default_stats());
    Unit* target = w.spawn("Target", Team::Radiant, default_stats());
    target->set_health(100.0);

    HealInstance h;
    h.healer = healer;
    h.target = target;
    h.amount = 50.0;
    deal_heal(h);

    ASSERT_EQ(log.size(), 1u);
    const auto& e = log.entries().front();
    EXPECT_EQ(e.kind, CombatLogKind::Heal);
    EXPECT_EQ(e.source_name, "Healer");
    EXPECT_EQ(e.target_name, "Target");
    EXPECT_DOUBLE_EQ(e.amount, 50.0);
}

TEST(CombatLog, RecordsModifierAddRemove) {
    World w;
    CombatLog log(w);

    Unit* u = w.spawn("U", Team::Radiant, default_stats());
    u->modifiers().attach(std::make_unique<TestModifier>("test_mod", *u));
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().kind, CombatLogKind::ModifierAdded);
    EXPECT_EQ(log.entries().back().target, u->id());
    EXPECT_EQ(log.entries().back().name, "test_mod");

    u->modifiers().remove("test_mod");
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log.entries().back().kind, CombatLogKind::ModifierRemoved);
    EXPECT_EQ(log.entries().back().name, "test_mod");
}

TEST(CombatLog, RecordsUnitDied) {
    World w;
    CombatLog log(w);

    Unit* atk = w.spawn("Atk", Team::Radiant, default_stats());
    Unit* vic = w.spawn("Vic", Team::Dire, default_stats());

    // UnitDiedEvent 由 World 在普攻致死时主动 publish; 单元测试里直接 publish
    // 模拟该 hook (deal_damage 本身不发死亡事件).
    UnitDiedEvent died{vic->id(), atk->id()};
    w.events().publish(died);

    bool found_death = false;
    for (const auto& e : log.entries()) {
        if (e.kind == CombatLogKind::UnitDied) {
            EXPECT_EQ(e.target,      vic->id());
            EXPECT_EQ(e.source,      atk->id());
            EXPECT_EQ(e.target_name, "Vic");
            EXPECT_EQ(e.source_name, "Atk");
            found_death = true;
        }
    }
    EXPECT_TRUE(found_death);
}

TEST(CombatLog, CapacityRingDropsOldest) {
    World w;
    CombatLog log(w, /*capacity=*/3);

    Unit* u = w.spawn("U", Team::Radiant, default_stats());
    for (int i = 0; i < 5; ++i) {
        u->modifiers().attach(std::make_unique<TestModifier>(
            "m" + std::to_string(i), *u));
    }
    ASSERT_EQ(log.size(), 3u);
    // 应保留最后 3 条 (m2, m3, m4).
    EXPECT_EQ(log.entries().front().name, "m2");
    EXPECT_EQ(log.entries().back().name,  "m4");
}

TEST(CombatLog, FilterRejectsEntries) {
    World w;
    CombatLog log(w);
    log.set_filter([](const CombatLogEntry& e) {
        return e.kind == CombatLogKind::Damage;
    });

    Unit* atk = w.spawn("Atk", Team::Radiant, default_stats());
    Unit* vic = w.spawn("Vic", Team::Dire,    default_stats());

    DamageInstance d;
    d.attacker = atk; d.victim = vic; d.amount = 50.0;
    d.type = DamageType::Physical;
    deal_damage(d);

    HealInstance h;
    h.target = atk; h.amount = 10.0;
    deal_heal(h);

    EXPECT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().front().kind, CombatLogKind::Damage);
}

TEST(CombatLog, FormatHandlesAllKinds) {
    CombatLogEntry e;
    e.time = 1.5;
    e.source_name = "Lina";
    e.target_name = "Dummy";

    e.kind = CombatLogKind::Damage;
    e.dtype = DamageType::Magical;
    e.amount = 250.0; e.amount_pre = 333.0;
    EXPECT_NE(CombatLog::format(e).find("Lina"),  std::string::npos);
    EXPECT_NE(CombatLog::format(e).find("Dummy"), std::string::npos);
    EXPECT_NE(CombatLog::format(e).find("magical"), std::string::npos);

    e.kind = CombatLogKind::Heal; e.amount = 30.0;
    EXPECT_NE(CombatLog::format(e).find("heals"), std::string::npos);

    e.kind = CombatLogKind::ModifierAdded;
    e.name = "modifier_x"; e.amount = 5.0; e.stacks = 2; e.flag = false;
    EXPECT_NE(CombatLog::format(e).find("gains"),       std::string::npos);
    EXPECT_NE(CombatLog::format(e).find("modifier_x"),  std::string::npos);

    e.kind = CombatLogKind::ModifierRemoved;
    EXPECT_NE(CombatLog::format(e).find("loses"),       std::string::npos);

    e.kind = CombatLogKind::AbilityCastStarted;
    e.name = "lina_ult";
    e.target = 0; e.target_name.clear();
    EXPECT_NE(CombatLog::format(e).find("casts"),       std::string::npos);

    e.kind = CombatLogKind::AbilityCastFinished;
    e.flag = true;
    EXPECT_NE(CombatLog::format(e).find("interrupted"), std::string::npos);

    e.kind = CombatLogKind::AttackLanded;
    e.flag = true; e.target_name = "Dummy";
    EXPECT_NE(CombatLog::format(e).find("missed"),      std::string::npos);

    e.kind = CombatLogKind::UnitDied;
    e.source = 1; e.source_name = "Lina";
    EXPECT_NE(CombatLog::format(e).find("died"),        std::string::npos);
}

TEST(CombatLog, FormatTolerantToMissingNames) {
    CombatLogEntry e;
    e.kind = CombatLogKind::Damage;
    e.source = 7; e.target = 9;
    e.amount = 10.0; e.amount_pre = 12.0;
    const std::string s = CombatLog::format(e);
    EXPECT_NE(s.find("#7"), std::string::npos);
    EXPECT_NE(s.find("#9"), std::string::npos);
}
