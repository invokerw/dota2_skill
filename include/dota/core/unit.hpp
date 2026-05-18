#pragma once

#include "dota/ability/manager.hpp"
#include "dota/core/types.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"  // for DamageType used in apply_damage

#include <memory>
#include <string>

namespace dota {

class World;

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

    Vec2   position() const { return position_; }
    void   set_position(Vec2 p) { position_ = p; }

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

    // 应用原始生命值/魔法值变化. 由伤害管线在抗性计算后使用.
    void heal(double amount);
    void spend_mana(double amount);
    void set_health(double hp);

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

    std::unique_ptr<ModifierManager> modifiers_;
    std::unique_ptr<AbilityManager>  abilities_;
    World* world_{nullptr};
};

} // namespace dota
