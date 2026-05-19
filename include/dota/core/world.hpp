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

// 静态地形寻路接口. 默认 World 不挂 Pathfinder, issue_move 退化为单航点
// {target}. A* 实现是后续阶段的事; 此处仅留接口.
class Pathfinder {
public:
    virtual ~Pathfinder() = default;
    // 返回从 start 到 target 的航点序列. 第一个航点应是首个有意义的中转点
    // (不必包含 start). 失败时返回空 vector, World 会回退到 {target}.
    virtual std::vector<Vec2> find_path(Vec2 start, Vec2 target) = 0;
};

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
    EntityId pid;
    EntityId source;
    Vec2     origin;
    bool     linear{true};
    Vec2     dir{};
    double   speed{0.0};
    double   length{0.0};
    double   width{0.0};
    EntityId target{kInvalidEntityId};
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

    // 发出基础攻击命令. 阶段 1: 攻击者每次攻击冷却归零时
    // 自动攻击 `target`, 只要双方都存活.
    // 实际攻击发生在 `advance()` 内部.
    void order_attack(Unit& attacker, Unit& target);
    void stop_attack(Unit& attacker);

    // --- Pathfinder 接口(目前仅留 hook, A* 后续实现)
    void set_pathfinder(std::unique_ptr<Pathfinder> p) { pathfinder_ = std::move(p); }
    Pathfinder* pathfinder() const { return pathfinder_.get(); }

    // 由 Unit::issue_move 回调; 把 pathfinder 与 fallback 逻辑集中在 World.
    // 公开是因为 Unit 持有 World*, 不希望把策略散落到 unit.cpp.
    void fill_move_path(Unit& u, Vec2 target, MovePath& out);

private:
    struct AttackOrder {
        EntityId attacker;
        EntityId target;
    };

    void tick_once();
    void resolve_attack(Unit& attacker, Unit& target);

    // 玩家移动指令: 在 motion controller 之后, ability 之前推进每个单位的
    // move_path. 通过 set_position 改坐标 -- 末尾的 resolve_unit_collisions
    // 会兜底分离, "moved_this_tick" 标志位也会自动置位.
    void tick_movement(double dt);

    // 软碰撞分离: 把本 tick 起内任何重叠的单位沿连心线推开, 直到无重叠或迭代到上限.
    // "谁动谁推": 仅 a 动 -> 完全推 a; 仅 b 动 -> 完全推 b; 双方都动 -> 各一半.
    // 跳过死亡 / Neutral / 带 NoUnitCollision 状态的单位.
    void resolve_unit_collisions();

    std::vector<std::unique_ptr<Unit>> units_;
    std::vector<AttackOrder>           orders_;
    EventBus events_;
    EntityId next_id_{1};
    double   time_{0.0};
    std::uint64_t tick_count_{0};
    Rng      rng_;
    std::unique_ptr<ProjectileManager> projectiles_;
    std::unique_ptr<Pathfinder>        pathfinder_;
};

} // namespace dota
