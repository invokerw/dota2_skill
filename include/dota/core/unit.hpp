#pragma once

#include "dota/ability/manager.hpp"
#include "dota/core/order.hpp"
#include "dota/core/types.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"  // for DamageType used in apply_damage

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dota {

class World;

// 移动指令路径. v1 仅使用单一航点(vector size == 1); 预留 vector + index
// 以便未来 A* 一次返回多个航点, 走完一个再切到下一个.
struct MovePath {
    std::vector<Vec2> waypoints;
    std::size_t       index{0};

    bool empty() const { return index >= waypoints.size(); }
    Vec2 current()     const { return waypoints[index]; }
    Vec2 final_point() const { return waypoints.back(); }
};

// 可控实体(英雄或小兵)的基础属性和战斗状态
//
// 阶段 1 保持这个结构体简单具体 -- 还没有修饰器和技能.
// 后续阶段会将大部分 getter 通过聚合器路由, 这样修饰器可以
// 注入加成(护甲/攻速等)而不触碰基础字段.
struct UnitStats {
    double max_health        = 100.0;
    double max_mana          = 0.0;
    double base_armor        = 0.0;
    double magic_resist      = 0.25;   // 0..1, 魔法伤害 resistance(抗性)减免比例
    double attack_damage     = 10.0;
    double attack_speed      = 100.0;  // 基础 100 表示 1 BAT 间隔攻击
    double base_attack_time  = 1.7;    // 100 攻速时的攻击间隔(秒)
    double move_speed        = 300.0;
    double attack_range      = 150.0;
    double hull_radius       = 24.0;   // 碰撞 / 命中判定半径(Dota 2 默认英雄 24)
    bool   ranged            = false;  // true 表示远程, 普攻走 TrackingProjectile
    double projectile_speed  = 900.0;  // 远程普攻投射物速度(像素/秒); 仅 ranged=true 时使用
};

class Unit {
public:
    Unit(EntityId id, std::string name, Team team, UnitStats stats);
    ~Unit();

    Unit(const Unit&) = delete;
    Unit& operator=(const Unit&) = delete;

    EntityId  id()   const { return id_; }
    const std::string& name() const { return name_; }
    Team      team() const { return team_; }

    const UnitStats& stats() const { return stats_; }

    double health() const { return health_; }
    double mana()   const { return mana_; }
    double max_health() const;
    double max_mana()   const;
    bool   alive()  const { return health_ > 0.0; }

    // 死亡后是否由 World 自动从 units_ 中 erase. thinker 默认 true(到期自毁后立即清理),
    // 普通 spawn 默认 false(尸体保留, 与 Dota 大多数英雄/小兵在死亡后仍存在一段时间一致).
    // 一旦真正被 erase, 任何缓存的 Unit* 都会失效 -- 跨 tick 的引用必须改用 EntityId.
    bool remove_on_death() const { return remove_on_death_; }
    void set_remove_on_death(bool b) { remove_on_death_ = b; }

    Vec2   position() const { return position_; }
    void   set_position(Vec2 p) { position_ = p; }

    // 单位的碰撞 / 命中判定半径. 不走 modifier 聚合 -- 与 Dota 2 一致, hull
    // 几乎不被 buff 改变, 直接读 stats.
    double hull_radius() const { return stats_.hull_radius; }

    // 本 tick 起始位置. World 在 tick 开始时快照, tick 末的软碰撞分离 pass
    // 据此判断谁是"本 tick 的移动者", 仅推开移动者(双方都动则各推一半).
    Vec2 tick_start_position() const { return tick_start_pos_; }
    void snapshot_tick_position() {
        tick_start_pos_   = position_;
        force_moved_flag_ = false;
    }

    // 强制下一次分离 pass 把本单位视为"动过". 用于 hook 拖拽 / 击退结束等
    // 场景: 单位本 tick 没有再 set_position, 但 NoUnitCollision 状态刚解除,
    // 需要被分离 pass 推出重叠. 由对应 modifier 在 on_destroyed 中调用.
    void force_moved_for_collision() { force_moved_flag_ = true; }
    bool moved_this_tick_for_collision() const {
        if (force_moved_flag_) return true;
        return position_.x != tick_start_pos_.x ||
               position_.y != tick_start_pos_.y;
    }

    // --- 战斗属性(通过 ModifierManager aggregation 聚合)---
    double armor()          const;
    double attack_damage()  const;
    double magic_resist()   const;
    double move_speed()     const;

    // --- 扩展属性(Phase 0)---
    double evasion()        const;     // 0..0.95 闪避率
    double lifesteal_pct()  const;     // 物理吸血百分比
    double health_regen()   const;     // 每秒生命恢复
    double mana_regen()     const;     // 每秒魔法恢复
    double spell_amp_pct()  const;     // 法术增伤
    double status_resist()  const;     // 控制 resistance
    double cooldown_reduction_pct() const;
    double cast_range_bonus() const;

    // 根据当前攻速计算的攻击间隔(秒)
    double seconds_per_attack() const;

    // Dispel/Purge 选项
    struct PurgeOptions {
        bool buffs   = true;
        bool debuffs = false;
        bool strong  = false;   // 强驱散: 连同 is_dispellable=false 也清除(不可净化的除外)
    };
    void purge(PurgeOptions opts);

    // --- 行动限制(查询修饰器状态)---
    bool can_attack() const;
    bool can_cast()   const;
    bool can_move()   const;

    // --- 指令队列(Dota 2 PlayerOrder 风格)---
    //
    // 所有"走/打/放"指令都进同一个 FIFO. World::tick_orders 在每 tick 内
    // 派发队首: MoveTo 派生 move_path, Cast/Attack 在 Stage 3/4 启用.
    //
    // queue=false (默认) 覆盖整队; queue=true 追加到队尾(shift 排队).
    void issue_order(Order o, bool queue = false);
    // 清空整个队列, 同时清掉派生的 move_path. 等价于 issue_order(OrderStop{}).
    void clear_orders();
    const std::deque<Order>& orders() const { return orders_; }
    // 队首; 空队返回 nullptr.
    const Order* current_order() const {
        return orders_.empty() ? nullptr : &orders_.front();
    }
    // 内部(World::tick_orders 用): 弹出已完成的队首并激活新的队首.
    // - 完成判定: OrderMoveToPoint 在 move_path 已空; OrderStop 立即清队.
    // - 激活: 为 OrderMoveToPoint 派生 move_path. Stage 3/4 后扩展.
    // 幂等 -- 重复调用安全, 中途路径仍非空时无副作用.
    void pump_orders();

    // --- 移动指令(便捷 wrapper, 内部走 issue_order)
    // 设置目的地. World 上若挂了 Pathfinder 会用 find_path 填充多航点; 否则单航点.
    void issue_move(Vec2 target);
    // 清除当前指令.
    void stop_move();
    // 当前最终目的地; 没有指令时 nullopt.
    std::optional<Vec2> move_target() const;
    // 当前路径只读视图(供 World::tick_movement 与调试 UI 用).
    const MovePath& move_path() const { return move_path_; }
    // 内部: tick_movement 在到达终点航点后清空.
    void clear_move_path()       { move_path_ = {}; }
    // 内部: 推进到下个航点.
    void advance_move_waypoint() { ++move_path_.index; }

    // 应用原始生命值/魔法值变化. 由伤害管线在抗性计算后使用.
    void heal(double amount);
    void spend_mana(double amount);
    void set_health(double hp);
    void set_mana(double mana);
    void set_stats(UnitStats stats);

    // 应用原始伤害并限制到零. 返回实际应用的数值.
    // 不发布事件; 调用 apply_damage() 以使用完整管线.
    double apply_raw_damage(double amount);

    // 阶段 2 伤害入口点: 向此单位上的修饰器分发伤害前事件
    // (它们可能修改 `amount` 或记录 `absorbed`), 应用类型
    // resistance(抗性)(物理通过护甲, 魔法通过魔抗, 纯粹不变),
    // 扣除生命值, 分发伤害后事件. 返回实际扣除的生命值.
    // 阶段 5 管线会增加更多层(护盾, reflect 反射/反伤)但保持此签名.
    double apply_damage(DamageType type, double amount, EntityId attacker = 0);

    ModifierManager&       modifiers()       { return *modifiers_; }
    const ModifierManager& modifiers() const { return *modifiers_; }

    AbilityManager&       abilities()       { return *abilities_; }
    const AbilityManager& abilities() const { return *abilities_; }

    // World 反向指针. 由 World::spawn 设置, 以便伤害/治疗管线可以
    // 将攻击者 EntityId 解析为 Unit*. 对于在 World 外构建的测试 Unit 为 null.
    void   set_world(World* w) { world_ = w; }
    World* world() const       { return world_; }

    // 攻击冷却簿记(距离下次攻击的剩余秒数)
    double attack_cd() const { return attack_cd_; }
    void   set_attack_cd(double t) { attack_cd_ = t; }
    void   tick_attack_cd(double dt);

    // 每 tick 由 World 调用一次; 推进此单位上的修饰器
    void tick_modifiers(double dt);
    // 每 tick 由 World 调用一次; 推进此单位上的技能
    void tick_abilities(double dt);

private:
    EntityId    id_;
    std::string name_;
    Team        team_;
    UnitStats   stats_;

    double health_{0.0};
    double mana_{0.0};
    double attack_cd_{0.0};
    Vec2   position_{};
    Vec2   tick_start_pos_{};
    bool   force_moved_flag_{false};
    bool   remove_on_death_{false};

    std::unique_ptr<ModifierManager> modifiers_;
    std::unique_ptr<AbilityManager>  abilities_;
    World* world_{nullptr};

    MovePath move_path_{};
    std::deque<Order> orders_{};
};

} // namespace dota
