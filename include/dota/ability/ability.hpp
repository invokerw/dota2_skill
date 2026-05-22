#pragma once

#include "dota/ability/behavior.hpp"
#include "dota/core/types.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace dota {

class Unit;
class World;

// 技能施放的生命周期阶段. 对应 Dota 的 预施放 → cast point(施法前摇)→
// 施放 → backswing(施法后摇)→ 冷却 链条. 被动技能保持在 Ready 状态.
enum class CastPhase : std::uint8_t {
    Ready = 0,
    Casting,     // cast point(施法前摇)动画; 可被打断
    Backswing,   // backswing(施法后摇)动画; 不阻止新的施法
    Channelling, // 仅用于引导型技能
    OnCooldown,
};

// 施法失败的合法性原因. 用于 UI/调试; 测试会断言这些值.
enum class CastError : std::uint8_t {
    None = 0,
    NotReady,
    OnCooldown,
    NotEnoughMana,
    Silenced,
    Stunned,
    Hexed,
    CasterDead,
    InvalidTarget,
    TargetMagicImmune,
    OutOfRange,
    NotLearned,
};

// 每级标量值. 我们将 `ability_special` 建模为 名称 → 每级
// 向量(int 或 float)的字典. 查找时选择 `values[min(level-1, size-1)]`.
struct AbilitySpecialValue {
    // 以下二选一:
    std::vector<double> floats;
    std::vector<long>   ints;
    bool is_int = false;

    double get_float(int level) const;
    long   get_int(int level) const;
};

using AbilitySpecial = std::unordered_map<std::string, AbilitySpecialValue>;

// 下达施法命令时传递的目标. 未使用的字段会根据
// BehaviorFlag 被忽略.
struct CastTarget {
    Unit* unit       = nullptr;
    Vec2  point      = {};
    bool  has_point  = false;
};

// 施法实际生效时传递给子类的上下文. 刻意保持精简 --
// 随着流程扩展可以添加更多字段.
struct CastContext {
    Unit*      caster   = nullptr;
    World*     world    = nullptr;
    CastTarget target;
    int        level    = 1;
};

// modifier 监听 owner 释放 ability 的事件 (对应 Dota 的
// MODIFIER_EVENT_ON_ABILITY_EXECUTED / OnAbilityFullyCast). 由 Ability 在
// 施法成功完整结束 (非中断) 时由 ModifierManager 派发到 owner 上的所有 modifier.
// passive ability 不会触发该事件, 因为它们走不通 trigger_cast/order_cast 路径.
class Ability;
struct AbilityExecutedInfo {
    Unit*       caster      = nullptr;
    Ability*    ability     = nullptr;
    std::string ability_name;
    bool        is_passive  = false;
};

// 所有技能的抽象基类 -- DataDriven(YAML)和 Scripted
//(Lua, 阶段 4)共享. 生命周期: 由施法者的 AbilityManager 拥有.
class Ability {
public:
    Ability(std::string name,
            std::uint32_t behavior,
            TargetTeam target_team,
            Unit& caster);
    virtual ~Ability() = default;

    Ability(const Ability&) = delete;
    Ability& operator=(const Ability&) = delete;

    // --- 静态元数据 ---
    const std::string& name() const { return name_; }
    std::uint32_t      behavior() const { return behavior_; }
    TargetTeam         target_team() const { return target_team_; }

    Unit&              caster()       { return caster_; }
    const Unit&        caster() const { return caster_; }

    // --- 每级字段 ---
    int  level() const { return level_; }
    // 设置等级. 若实际改变了等级, 触发 on_upgrade(new_level), 默认实现会
    // 调用 caster 上同名 intrinsic modifier 的 on_refresh, 让它重读
    // ability_special.
    void set_level(int l);

    // --- Intrinsic modifier ---
    // ability 实例化时自动给 caster 挂的永久 modifier 名(可空). 由
    // AbilityRegistry::instantiate 设置, 由 Ability::on_upgrade 默认实现使用.
    const std::string& intrinsic_modifier_name() const { return intrinsic_modifier_; }
    void set_intrinsic_modifier_name(std::string n) { intrinsic_modifier_ = std::move(n); }

    double cast_point()    const { return cast_point_; }
    double backswing()     const { return backswing_; }
    double channel_time()  const { return channel_time_; }
    double cast_range()    const { return cast_range_; }
    double cooldown_for_level() const;
    double mana_cost_for_level() const;

    // 由 DataDriven 加载器用于从 YAML 填充. Scripted 技能可以
    // 直接从 Lua 调用相同的 setter.
    void set_cast_point(double t)   { cast_point_ = t; }
    void set_backswing(double t)    { backswing_ = t; }
    void set_channel_time(double t) { channel_time_ = t; }
    void set_cast_range(double r)   { cast_range_ = r; }
    void set_cooldown_levels(std::vector<double> v)  { cooldowns_ = std::move(v); }
    void set_mana_cost_levels(std::vector<double> v) { mana_costs_ = std::move(v); }
    void set_ability_special(AbilitySpecial s)       { special_ = std::move(s); }

    const AbilitySpecial& ability_special() const { return special_; }

    // --- 运行时状态 ---
    CastPhase   phase()        const { return phase_; }
    double      phase_timer()  const { return phase_timer_; }
    double      cooldown_remaining() const { return cooldown_; }
    bool        is_passive()   const { return has_flag(behavior_, BehaviorFlag::Passive); }
    bool        is_channelled()const { return has_flag(behavior_, BehaviorFlag::Channelled); }
    bool        is_orb()       const { return has_flag(behavior_, BehaviorFlag::Attack); }

    // 法球 (orb) 的资源消耗入口. 由 intrinsic modifier 在 on_attack 钩子中调用,
    // 决定本次普攻是否升级为法球. 返回 true 表示成功扣蓝 + 进 cd, 调用方应认领
    // record; false 表示资源不够 (cd 中或蓝量不足), 调用方应忽略钩子, 普攻保持
    // 原样. 不检查沉默 / 眩晕等状态 -- 这些应由调用方 (modifier) 自行决定.
    bool        use_resources_for_orb();
    bool        can_use_resources_for_orb() const;

    // 自动施放: 法球默认是否启用. 玩家可通过 set_autocast_on 切换. AutoCast
    // 行为位决定初始值.
    bool        autocast_on() const { return autocast_on_; }
    void        set_autocast_on(bool b) { autocast_on_ = b; }

    // 合法性检查, 不改变状态. 将第一个找到的
    // 失败原因填充到 `err`.
    CastError can_cast(const CastTarget& target) const;

    // 下达施法命令. 成功时返回 CastError::None. 实际的
    // `on_spell_start` 在 World::advance() 内的 cast point(施法前摇)之后触发.
    CastError order_cast(const CastTarget& target, World& world);

    // 子技能触发: 跳过冷却 / 蓝量 / 状态检查(按可选标志).
    // 用于"被动触发型"技能(例如修饰器 OnPreTakeDamage 中触发).
    // 当 cast_point 仍 > 0 时同样会经过正常 cast/backswing 流程.
    CastError trigger_cast(const CastTarget& target, World& world,
                           bool ignore_cooldown = true,
                           bool ignore_mana     = true,
                           bool ignore_state    = true);

    // 按 dt 推进施法/引导/backswing(施法后摇)/冷却计时器. World 每个 tick 驱动此方法.
    // 处理施法中途被眩晕/沉默时的打断.
    void advance(double dt);

    // --- 子类钩子 ---
    // 当 cast point(施法前摇)成功完成时调用(即未被打断).
    virtual void on_spell_start(CastContext&) = 0;
    // 用于引导型技能: 引导期间每个 tick 触发.
    virtual void on_channel_think(CastContext&, double /*dt*/) {}
    // 用于引导型技能: 引导结束时触发一次.
    virtual void on_channel_finish(CastContext&, bool /*interrupted*/) {}
    // 可选: 升级钩子.
    virtual void on_upgrade(int /*new_level*/) {}

protected:
    // 子类可以重写以允许定制目标验证(例如
    // 阶段 6 中的仅可创建单位目标).
    virtual CastError validate_target(const CastTarget&) const;

private:
    void enter_phase(CastPhase p, double timer);
    bool current_target_still_valid() const;

    std::string name_;
    std::uint32_t behavior_;
    TargetTeam    target_team_;
    Unit&         caster_;

    int level_ = 1;

    // 时间字段(全部为秒).
    double cast_point_   = 0.0;
    double backswing_    = 0.0;
    double channel_time_ = 0.0;
    double cast_range_   = 0.0;

    std::vector<double> cooldowns_;   // 每级
    std::vector<double> mana_costs_;  // 每级
    AbilitySpecial      special_;
    std::string         intrinsic_modifier_;

    // 跨 tick 的目标快照: 仅存 EntityId / 点 / has_point. 每次派发到子类时
    // 通过 world_->find 重新解析成 Unit*; 若期间 target 已被销毁(指针失效),
    // 这里也只是 find 返回 nullptr, 不会悬挂.
    struct PendingCast {
        EntityId unit_id  = kInvalidEntityId;
        Vec2     point    = {};
        bool     has_point = false;
    };
    CastTarget resolve_pending() const;

    // 运行时.
    CastPhase   phase_       = CastPhase::Ready;
    double      phase_timer_ = 0.0;
    double      cooldown_    = 0.0;
    World*      world_       = nullptr; // 在施法/后摇期间有效
    PendingCast pending_target_{};
    bool        autocast_on_ = false;
};

} // namespace dota
