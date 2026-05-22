#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/enums.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dota {

class Unit;
struct AbilityExecutedInfo;

// 伤害类型. 在此处重复定义(除了将拥有它的 Stage 5 战斗头文件),
// 以便 Stage 2 可以在伤害前事件中推理魔法 vs 物理伤害.
enum class DamageType : std::uint8_t {
    Physical = 0,
    Magical,
    Pure,
};

// --- 修饰器可观察事件
//
// 事件携带引用, 修饰器可以修改其中的字段.
// PreTakeDamageEvent 中的伤害量有意设计为非 const -- 这允许护盾修饰器
// 在应用 resistance(抗性)*之前* absorption(吸收)部分伤害.
// Stage 5 伤害管线将自行发布这些事件; 对于 Stage 2,
// 测试调用 Unit::apply_damage(), 这是一个简单的包装器.

// 应用于 DamageInstance 的标志位掩码. 对应 Valve 的 DOTA_DAMAGE_FLAG_*
// -- 仅枚举 Stage 5 实际分支的标志.
enum class DamageFlag : std::uint32_t {
    None                  = 0,
    BypassMagicImmune     = 1u << 0,   // 魔法伤害穿透魔法免疫
    HPLoss                = 1u << 1,   // 跳过护盾 + 类型 resistance(抗性)
    NoSpellAmplification  = 1u << 2,   // 跳过伤害 amplification(增幅)
    Reflection            = 1u << 3,   // 标记为 reflection(反射)伤害 -- 永不再次反射
    NoLifesteal           = 1u << 4,   // 由 Stage 5 吸血修饰器使用
};

constexpr std::uint32_t to_mask(DamageFlag f) {
    return static_cast<std::uint32_t>(f);
}
constexpr std::uint32_t operator|(DamageFlag a, DamageFlag b) {
    return to_mask(a) | to_mask(b);
}
constexpr bool has_flag(std::uint32_t mask, DamageFlag f) {
    return (mask & to_mask(f)) != 0;
}

struct PreTakeDamageEvent {
    EntityId      attacker{kInvalidEntityId};
    EntityId      victim{kInvalidEntityId};
    DamageType    type{DamageType::Physical};
    std::uint32_t flags{0};
    double        amount{0.0};      // 可变
    double        absorbed{0.0};    // 修饰器在此记录 absorption(吸收)量
};

struct PostTakeDamageEvent {
    EntityId      attacker{kInvalidEntityId};
    EntityId      victim{kInvalidEntityId};
    DamageType    type{DamageType::Physical};
    std::uint32_t flags{0};
    double        amount{0.0};      // 最终应用的伤害量
};

// 治疗事件. 修饰器可以通过 on_pre_take_heal 减少(或放大)治疗.
struct PreTakeHealEvent {
    EntityId      healer{kInvalidEntityId};
    EntityId      target{kInvalidEntityId};
    double        amount{0.0};      // 可变
};

struct PostTakeHealEvent {
    EntityId      healer{kInvalidEntityId};
    EntityId      target{kInvalidEntityId};
    double        amount{0.0};      // 最终应用的治疗量
};

struct ModifierProvidedProperty {
    ModifierProperty property;
    double           value;
};

// 所有修饰器的基类. 子类化并重写 `declared_*` 钩子以参与属性聚合,
// 状态位和事件响应.
//
// 生命周期: 由 ModifierManager(目标单位的管理器)拥有. 持续时间和
// 思考周期由管理器驱动. duration_ < 0 的修饰器永久存在;
// think_interval_ <= 0 禁用思考.
class Modifier {
public:
    Modifier(std::string name, Unit& owner, double duration);
    virtual ~Modifier() = default;

    Modifier(const Modifier&) = delete;
    Modifier& operator=(const Modifier&) = delete;

    const std::string& name() const { return name_; }
    Unit&              owner()       { return owner_; }
    const Unit&        owner() const { return owner_; }

    double duration_remaining() const { return duration_; }
    bool   permanent()          const { return permanent_; }
    bool   expired()            const { return !permanent_ && duration_ <= 0.0; }

    int    stack_count() const { return stack_count_; }
    void   set_stack_count(int n);

    // 持续时间刷新: 调用者重置剩余持续时间(Dota 重新应用).
    // 负持续时间使修饰器再次变为永久.
    void refresh(double new_duration) {
        duration_  = new_duration;
        permanent_ = new_duration < 0.0;
    }

    // 由 ModifierManager 在每个世界 tick 时调用, 传入 dt(秒).
    void advance(double dt);

    // 子类重写以贡献数值加成. 每次管理器重新计算聚合时调用 -- 廉价的纯函数风格.
    virtual std::vector<ModifierProvidedProperty> declared_properties() const { return {}; }

    // 声明状态的位掩码. 使用 state_bit(ModifierState::X) | ...
    virtual std::uint32_t declared_states() const { return 0; }

    // 事件钩子. 默认为空操作.
    virtual void on_created()                      {}
    virtual void on_destroyed()                    {}
    virtual void on_stack_changed(int /*old*/,
                                  int /*new_*/)    {}
    // 由 Ability::set_level 触发, 用于 intrinsic modifier 重读 ability_special.
    // 也可以在 modifier 因刷新持续时间而重新应用时手动调用.
    virtual void on_refresh()                      {}
    virtual void on_interval_think()               {}  // 每 think_interval_ 触发一次
    virtual void on_pre_take_damage(PreTakeDamageEvent&)  {}
    virtual void on_post_take_damage(PostTakeDamageEvent&){}
    virtual void on_pre_take_heal(PreTakeHealEvent&)      {}
    virtual void on_post_take_heal(PostTakeHealEvent&)    {}

    // owner 完整释放完一个非 passive ability. interrupted 的 cast 不会触发.
    virtual void on_ability_executed(const AbilityExecutedInfo&) {}

    // Motion controller 钩子: 仅对 is_motion_controller_=true 的修饰器调用.
    // 在 ModifierManager::advance_motion 中, ability tick 之前由 World 驱动.
    virtual void on_motion_tick(double /*dt*/)            {}

    // 用于 dispel/purge 分类.
    virtual bool is_debuff() const { return false; }

    // --- Phase 0 增加的标志位 ---
    bool is_purgable() const         { return is_purgable_; }
    void set_purgable(bool b)        { is_purgable_ = b; }
    bool is_dispellable() const      { return is_dispellable_; }
    void set_dispellable(bool b)     { is_dispellable_ = b; }
    bool is_motion_controller() const{ return is_motion_controller_; }
    void set_motion_controller(bool b){ is_motion_controller_ = b; }
    int  motion_priority() const     { return motion_priority_; }
    void set_motion_priority(int p)  { motion_priority_ = p; }

protected:
    void set_think_interval(double s) { think_interval_ = s; think_accum_ = 0.0; }

private:
    std::string name_;
    Unit&       owner_;
    double      duration_;          // 有限时为秒数; 永久时未使用
    bool        permanent_;
    double      think_interval_{0.0};
    double      think_accum_{0.0};
    int         stack_count_{1};

    bool is_purgable_{true};
    bool is_dispellable_{true};
    bool is_motion_controller_{false};
    int  motion_priority_{0};
};

} // namespace dota
