#pragma once

#include "dota/modifier/modifier.hpp"
#include "dota/modifier/registry.hpp"
#include "dota/script/lua_state.hpp"

#include <sol/sol.hpp>
#include <string>

namespace dota {

class Ability;

// Lua 端的修饰器实例, 与一个 LuaModifierRegistry::CompiledSpec 关联.
// 每次属性查询/事件钩子触发时, 按需调用 spec 表上的 Lua 函数.
//
// 所有 spec 字段统一使用 PascalCase 约定: IsHidden / IsPurgable / IsDispellable /
// IsDebuff / IsMotionController / MotionPriority / States / Properties /
// ThinkInterval / OnCreated / OnDestroyed / OnStackChanged / OnIntervalThink /
// OnPreTakeDamage / OnPostTakeDamage / OnPreTakeHeal / OnPostTakeHeal /
// OnMotionTick / CheckState 等.
//
// 唯一构造路径: 先在 Lua 端通过 `register_modifier(name, spec)` 注册,
// 再用注册中心查到的 CompiledSpec 实例化.
class ScriptedModifier : public Modifier {
public:
    ScriptedModifier(Unit& owner,
                     std::string name,
                     double duration,
                     const LuaModifierRegistry::CompiledSpec& spec,
                     LuaState& lua,
                     EntityId source_id = kInvalidEntityId,
                     Ability* ability   = nullptr);

    std::vector<ModifierProvidedProperty> declared_properties() const override;
    std::uint32_t declared_states() const override;

    void on_created() override;
    void on_destroyed() override;
    void on_stack_changed(int old_count, int new_count) override;
    void on_refresh() override;
    void on_interval_think() override;
    void on_pre_take_damage(PreTakeDamageEvent& ev) override;
    void on_post_take_damage(PostTakeDamageEvent& ev) override;
    void on_pre_take_heal(PreTakeHealEvent& ev) override;
    void on_post_take_heal(PostTakeHealEvent& ev) override;
    void on_motion_tick(double dt) override;
    void on_ability_executed(const AbilityExecutedInfo& info) override;

    bool is_debuff() const override { return is_debuff_; }

    // 暴露给 Lua self.GetCaster / GetAbility 类方法用. source 仅存 EntityId,
    // 通过 World::find 解析: 如果 source 单位已被销毁会得到 nullptr, 不会悬挂.
    EntityId source_id() const { return source_id_; }
    Unit*    source() const;
    Ability* ability() const { return ability_; }

private:
    void apply_compiled_flags(const LuaModifierRegistry::CompiledSpec& spec);

    LuaState*  lua_;
    sol::table table_;                               // 实例独有的可写 self 表
    sol::table spec_table_;                          // 共享的 spec 表(方法源)
    const LuaModifierRegistry::CompiledSpec* compiled_{nullptr};
    std::uint32_t state_mask_cache_ = 0;             // 静态状态位
    bool       is_debuff_ = false;
    EntityId   source_id_ = kInvalidEntityId;
    Ability*   ability_ = nullptr;
};

} // namespace dota
