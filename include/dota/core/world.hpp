#pragma once

#include "dota/core/event_bus.hpp"
#include "dota/core/random.hpp"
#include "dota/core/types.hpp"
#include "dota/core/unit.hpp"

#include <memory>
#include <string>
#include <vector>

namespace dota {

class ProjectileManager;
class LuaState;

// --- 标准事件(阶段 1)---

struct UnitDiedEvent {
    EntityId victim;
    EntityId killer; // 如果没有归属来源则为 kInvalidEntityId
};

// 每当基础攻击命中目标时触发. 阶段 1 使用原始
// 物理伤害; 阶段 5 将用伤害管线替换这个手工计算.
// `missed=true` 表示因 evasion 闪避, damage 为 0.
struct AttackLandedEvent {
    EntityId attacker;
    EntityId victim;
    double   damage;
    bool     missed{false};
};

class World {
public:
    // 30Hz 固定时钟模拟
    static constexpr double kTickRate = 30.0;
    static constexpr double kTickDt   = 1.0 / kTickRate;

    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // 在世界中创建一个单位; 返回一个非拥有指针, 该指针在
    // World 的生命周期内保持稳定.
    Unit* spawn(std::string name, Team team, UnitStats stats, Vec2 position = {});

    Unit*       find(EntityId id);
    const Unit* find(EntityId id) const;

    std::vector<Unit*> units_on_team(Team team);
    std::vector<Unit*> enemies_of(const Unit& u);

    // 返回 `origin` 的 `radius` 范围内 `source_team` 的存活敌人
    std::vector<Unit*> find_enemies_in_radius(Vec2 origin, double radius, Team source_team);

    // 线段附近: 候选投影到线段上 t∈[0,1], 到投影点距离 ≤ width/2.
    std::vector<Unit*> find_enemies_in_line(Vec2 start, Vec2 end, double width, Team source_team);

    // 圆锥: 距 origin ≤ length 且与 direction 夹角 ≤ half_angle_rad.
    std::vector<Unit*> find_enemies_in_cone(Vec2 origin, Vec2 direction, double length,
                                            double half_angle_rad, Team source_team);

    EventBus&       events()       { return events_; }
    const EventBus& events() const { return events_; }

    Rng&       rng()       { return rng_; }
    const Rng& rng() const { return rng_; }

    ProjectileManager&       projectiles();
    const ProjectileManager& projectiles() const;

    // 创建一个 thinker 单位: 隐身, 不可选中, 无碰撞, 挂载注册过的 Lua 修饰器
    // (提供 OnIntervalThink). `duration` 到期后单位自毁.
    // `lua` 可选: 当 modifier_name 为空时无需提供.
    Unit* create_thinker(Vec2 position, double duration,
                         const std::string& modifier_name,
                         LuaState* lua = nullptr,
                         Unit* source = nullptr);

    // 已过去的模拟时间(秒)
    double time() const { return time_; }

    // 前进 `dt` 秒. 内部细分为 kTickDt 切片.
    void advance(double dt);

    // 发出基础攻击命令. 阶段 1: 攻击者每次攻击冷却归零时
    // 自动攻击 `target`, 只要双方都存活.
    // 实际攻击发生在 `advance()` 内部.
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
    Rng      rng_;
    std::unique_ptr<ProjectileManager> projectiles_;
};

} // namespace dota
