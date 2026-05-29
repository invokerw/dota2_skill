#pragma once

#include "dota/core/attack.hpp"
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

namespace pathfinding {
class NavGrid;
class WallTracer;
}

// --- 标准事件(阶段 1)---

struct UnitDiedEvent {
    EntityId victim;
    EntityId killer; // 如果没有归属来源则为 kInvalidEntityId
};

// 每当基础攻击命中目标时触发. 阶段 1 使用原始
// 物理伤害; 阶段 5 将用伤害管线替换这个手工计算.
// `missed=true` 表示因 evasion 闪避, damage 为 0.
// `record_id` 关联到生成此次攻击的 AttackRecord, 便于录像 / 法球关联本次普攻.
struct AttackLandedEvent {
    EntityId attacker;
    EntityId victim;
    double   damage;
    bool     missed{false};
    EntityId record_id{kInvalidEntityId};
};

// --- 录像 / 可视化用事件(Stage A)---
// 这些事件不影响游戏逻辑, 仅供 Recorder / 可视化层订阅.

struct UnitSpawnedEvent {
    EntityId id;
};

struct AbilityCastStartedEvent {
    EntityId    caster;
    std::string ability;
    EntityId    target_unit{kInvalidEntityId};
    Vec2        target_point{};
    bool        has_point{false};
};

struct AbilityCastFinishedEvent {
    EntityId    caster;
    std::string ability;
    bool        interrupted{false};
};

// 投射物 spawn. linear=true 时 dir/length/width 有效;
// linear=false (tracking) 时 target 有效, dir/length/width 为零.
struct ProjectileSpawnedEvent {
    EntityId    pid;
    EntityId    source;
    Vec2        origin;
    bool        linear{true};
    Vec2        dir{};
    double      speed{0.0};
    double      length{0.0};
    double      width{0.0};
    EntityId    target{kInvalidEntityId};
    // particle / 美术资源名 (例如 "particles/units/heroes/hero_drow_ranger/drow_base_attack.vpcf").
    // 录像层照搬, 不影响游戏逻辑. 空表示走默认普攻投射物.
    std::string name;
};

struct ProjectileHitEvent {
    EntityId pid;
    EntityId victim;
    Vec2     point;
};

struct ProjectileFinishedEvent {
    EntityId pid;
};

struct ModifierAddedEvent {
    EntityId    unit;
    std::string name;
    double      duration;   // < 0 表示永久
    int         stacks;
};

struct ModifierRemovedEvent {
    EntityId    unit;
    std::string name;
};

struct DamageAppliedEvent {
    EntityId      attacker;        // 可能为 kInvalidEntityId
    EntityId      victim;
    DamageType    type;
    double        amount_pre;      // 进入管线时(放大之后, resistance 之前)的数值
    double        amount_applied;  // 实际扣血量
    std::uint32_t flags;
};

struct HealAppliedEvent {
    EntityId healer;   // 可能为 kInvalidEntityId
    EntityId target;
    double   amount;
};

// 每个固定 tick 结束时发布. Recorder 依赖此事件作为 flush 边界.
struct TickEndEvent {
    double   time;     // World::time()
    uint64_t tick;     // 累计 tick 序号 (从 1 开始)
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

    // --- 静态地形寻路 ---
    // 默认 World 持有一个空 NavGrid (无 blocked cell, 无 circle obstacle), 既能
    // 让所有 ShapeCast / WallTracer 调用统一从 World 取上下文, 也保证不挂网格的
    // 测试场景行为退化为"只看 unit 碰撞". 调用方可重新 set_nav_grid 注入带障碍
    // 的网格 (比如 pathfinding_demo).
    void set_nav_grid(std::shared_ptr<pathfinding::NavGrid> g);
    pathfinding::NavGrid&       nav_grid()       { return *nav_grid_; }
    const pathfinding::NavGrid& nav_grid() const { return *nav_grid_; }

    // 普通攻击结算 (闪避 + 物理伤害管线 + 设置 attack_cd). 由 OrderAttackTarget
    // 在 dispatch_front 检距通过, 攻击 cd 归零, 双方存活时调用. 公开是因为派发
    // 在 unit.cpp 里, 这条路径不希望再绕到 World 内部 helper.
    //
    // 内部分两段:
    //   begin_attack -> 创建 AttackRecord, 派发 on_attack 给 attacker 上所有
    //   modifier (法球认领 record). 近战立即 complete_attack; 远程 spawn
    //   TrackingProjectile, 命中回调里 complete_attack, finish 回调里 destroy.
    void resolve_attack(Unit& attacker, Unit& target);

private:
    void tick_once();

    // 玩家移动指令: 在 motion controller 之后, ability 之前推进每个单位的
    // move_path. 通过 set_position 改坐标 -- 末尾的 resolve_unit_collisions
    // 会兜底分离, "moved_this_tick" 标志位也会自动置位.
    void tick_movement(double dt);

    // 软碰撞分离: 把本 tick 起内任何重叠的单位沿连心线推开, 直到无重叠或迭代到上限.
    // "谁动谁推": 仅 a 动 -> 完全推 a; 仅 b 动 -> 完全推 b; 双方都动 -> 各一半.
    // 跳过死亡 / Neutral / 带 NoUnitCollision 状态的单位.
    void resolve_unit_collisions();

    std::vector<std::unique_ptr<Unit>> units_;
    EventBus events_;
    EntityId next_id_{1};
    EntityId next_attack_record_id_{1};
    double   time_{0.0};
    std::uint64_t tick_count_{0};
    Rng      rng_;
    std::unique_ptr<ProjectileManager> projectiles_;
    std::shared_ptr<pathfinding::NavGrid>   nav_grid_;
};

} // namespace dota
