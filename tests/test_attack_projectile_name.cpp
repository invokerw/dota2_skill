// Stage 4: ProjectileName 替换. 验证远程普攻 spawn 时, attacker 上 modifier
// 提供的 projectile_name() 会被引擎挑出并写到 ProjectileSpawnedEvent 的 name
// 字段上.
#include "dota/core/event_bus.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace dota;

namespace {

class NameProvidingModifier : public Modifier {
public:
    NameProvidingModifier(Unit& owner, std::string name)
        : Modifier("modifier_name_provider", owner, -1.0)
        , projectile_name_(std::move(name)) {}

    std::string projectile_name() const override { return projectile_name_; }

private:
    std::string projectile_name_;
};

UnitStats ranged_hero() {
    UnitStats s;
    s.max_health        = 1000.0;
    s.max_mana          = 200.0;
    s.attack_damage     = 40.0;
    s.attack_speed      = 100.0;
    s.base_attack_time  = 1.0;
    s.attack_range      = 600.0;
    s.move_speed        = 300.0;
    s.magic_resist      = 0.0;
    s.ranged            = true;
    s.projectile_speed  = 1250.0;
    return s;
}

UnitStats melee_hero() {
    UnitStats s = ranged_hero();
    s.attack_range     = 150.0;
    s.ranged           = false;
    s.projectile_speed = 0.0;
    return s;
}

} // namespace

TEST(ProjectileNameTest, RangedAttackPicksFirstNonEmptyName) {
    World world;
    Unit* attacker = world.spawn("attacker", Team::Radiant, ranged_hero(), {0.0, 0.0});
    Unit* target   = world.spawn("target",   Team::Dire,    melee_hero(),  {200.0, 0.0});

    // 先挂一个返回空字符串的 modifier (默认基类), 再挂一个真正提供名字的;
    // 引擎应该跳过空串, 挑后者. 顺序通过 attach 顺序保证.
    attacker->modifiers().attach(
        std::make_unique<NameProvidingModifier>(*attacker, std::string{}));
    attacker->modifiers().attach(
        std::make_unique<NameProvidingModifier>(*attacker, std::string{
            "particles/units/heroes/hero_drow_ranger/drow_frost_arrow.vpcf"}));

    std::string captured;
    world.events().subscribe<ProjectileSpawnedEvent>(
        [&captured](ProjectileSpawnedEvent& e) { captured = e.name; });

    attacker->issue_order(OrderAttackTarget{target->id()});
    world.advance(World::kTickDt);

    EXPECT_EQ(captured,
              "particles/units/heroes/hero_drow_ranger/drow_frost_arrow.vpcf");
}

TEST(ProjectileNameTest, RangedAttackEmptyWhenNoModifierProvides) {
    World world;
    Unit* attacker = world.spawn("attacker", Team::Radiant, ranged_hero(), {0.0, 0.0});
    Unit* target   = world.spawn("target",   Team::Dire,    melee_hero(),  {200.0, 0.0});

    bool seen = false;
    std::string captured = "<unset>";
    world.events().subscribe<ProjectileSpawnedEvent>(
        [&](ProjectileSpawnedEvent& e) { seen = true; captured = e.name; });

    attacker->issue_order(OrderAttackTarget{target->id()});
    world.advance(World::kTickDt);

    ASSERT_TRUE(seen);
    EXPECT_TRUE(captured.empty());
}
