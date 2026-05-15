#pragma once

#include "dota/core/event_bus.hpp"
#include "dota/core/types.hpp"
#include "dota/core/unit.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

// --- 标准事件（阶段 1）---

struct UnitDiedEvent {
    EntityId victim;
    EntityId killer; // 如果没有归属来源则为 kInvalidEntityId
};

// 每当基础攻击命中目标时触发。阶段 1 使用原始
// 物理伤害；阶段 5 将用伤害管线替换这个手工计算。
struct AttackLandedEvent {
    EntityId attacker;
    EntityId victim;
    double   damage;
};

class World {
public:
    // 30Hz 固定时钟模拟
    static constexpr double kTickRate = 30.0;
    static constexpr double kTickDt   = 1.0 / kTickRate;

    World();

    // 在世界中创建一个单位；返回一个非拥有指针，该指针在
    // World 的生命周期内保持稳定。
    Unit* spawn(std::string name, Team team, UnitStats stats, Vec2 position = {});

    Unit*       find(EntityId id);
    const Unit* find(EntityId id) const;

    std::vector<Unit*> units_on_team(Team team);
    std::vector<Unit*> enemies_of(const Unit& u);

    // 返回 `origin` 的 `radius` 范围内 `source_team` 的存活敌人
    std::vector<Unit*> find_enemies_in_radius(Vec2 origin, double radius, Team source_team);

    EventBus&       events()       { return events_; }
    const EventBus& events() const { return events_; }

    // 已过去的模拟时间（秒）
    double time() const { return time_; }

    // 前进 `dt` 秒。内部细分为 kTickDt 切片。
    void advance(double dt);

    // 发出基础攻击命令。阶段 1：攻击者每次攻击冷却归零时
    // 自动攻击 `target`，只要双方都存活。
    // 实际攻击发生在 `advance()` 内部。
    void order_attack(Unit& attacker, Unit& target);
    void stop_attack(Unit& attacker);

private:
    struct AttackOrder {
        EntityId attacker;
        EntityId target;
    };

    void tick_once();
    void resolve_attack(Unit& attacker, Unit& target);

    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<AttackOrder>           orders_;
    EventBus events_;
    EntityId next_id_{1};
    double   time_{0.0};
};

} // namespace dota
