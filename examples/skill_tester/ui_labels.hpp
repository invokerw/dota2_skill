#pragma once

// 标签 / 小工具集合: 把引擎枚举翻成显示文本, 提供 imgui 上的常用 helpers
// (DragFloat double 包装 + State mask 可视化). AimMode 也放这里, 因为它和瞄准
// UI 强相关, 不希望多拉一个头文件.

#include "dota/ability/ability.hpp"
#include "dota/ability/behavior.hpp"
#include "dota/core/types.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/enums.hpp"

#include "imgui.h"

#include <cstdint>

namespace dota::skill_tester {

// 瞄准状态机. None = 未选技能 / 已选 passive; 其余三种对应 ability behavior.
enum class AimMode {
    None,
    AwaitUnitTarget,
    AwaitPointTarget,
    AwaitConfirmNoTarget,
};

inline AimMode aim_for_behavior(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::PointTarget)) return AimMode::AwaitPointTarget;
    if (has_flag(b, BehaviorFlag::UnitTarget))  return AimMode::AwaitUnitTarget;
    if (has_flag(b, BehaviorFlag::NoTarget))    return AimMode::AwaitConfirmNoTarget;
    return AimMode::None;
}

inline const char* behavior_label(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::Channelled))   return "CHN";
    if (has_flag(b, BehaviorFlag::PointTarget))  return "PT";
    if (has_flag(b, BehaviorFlag::UnitTarget))   return "UT";
    if (has_flag(b, BehaviorFlag::NoTarget))     return "NT";
    return "?";
}

inline const char* cast_error_text(CastError e) {
    switch (e) {
        case CastError::None:              return "OK";
        case CastError::NotReady:          return "Not ready";
        case CastError::OnCooldown:        return "On cooldown";
        case CastError::NotEnoughMana:     return "Not enough mana";
        case CastError::Silenced:          return "Silenced";
        case CastError::Stunned:           return "Stunned";
        case CastError::Hexed:             return "Hexed";
        case CastError::CasterDead:        return "Caster dead";
        case CastError::InvalidTarget:     return "Invalid target";
        case CastError::TargetMagicImmune: return "Target magic immune";
        case CastError::OutOfRange:        return "Out of range";
        case CastError::NotLearned:        return "Not learned";
    }
    return "?";
}

inline const char* team_label(Team t) {
    switch (t) {
        case Team::Radiant: return "Radiant";
        case Team::Dire:    return "Dire";
        case Team::Neutral: return "Neutral";
    }
    return "?";
}

inline const char* state_label(ModifierState s) {
    switch (s) {
        case ModifierState::Stunned:         return "Stunned";
        case ModifierState::Silenced:        return "Silenced";
        case ModifierState::Rooted:          return "Rooted";
        case ModifierState::Disarmed:        return "Disarmed";
        case ModifierState::Hexed:           return "Hexed";
        case ModifierState::Invisible:       return "Invisible";
        case ModifierState::Invulnerable:    return "Invulnerable";
        case ModifierState::OutOfGame:       return "OutOfGame";
        case ModifierState::MagicImmune:     return "MagicImmune";
        case ModifierState::Untargetable:    return "Untargetable";
        case ModifierState::NoUnitCollision: return "NoUnitCollision";
        case ModifierState::NoHealthBar:     return "NoHealthBar";
        case ModifierState::Frozen:          return "Frozen";
        case ModifierState::Count_:          break;
    }
    return "?";
}

inline const char* property_label(ModifierProperty p) {
    switch (p) {
        case ModifierProperty::ArmorBonus:               return "ArmorBonus";
        case ModifierProperty::ArmorBonusPct:            return "ArmorBonusPct";
        case ModifierProperty::HealthBonus:              return "HealthBonus";
        case ModifierProperty::ManaBonus:                return "ManaBonus";
        case ModifierProperty::AttackDamageBonus:        return "AttackDamageBonus";
        case ModifierProperty::AttackDamageBonusPct:     return "AttackDamageBonusPct";
        case ModifierProperty::AttackSpeedBonusConstant: return "AttackSpeedBonus";
        case ModifierProperty::MagicResistBonus:         return "MagicResistBonus";
        case ModifierProperty::IncomingDamagePct:        return "IncomingDamagePct";
        case ModifierProperty::OutgoingDamagePct:        return "OutgoingDamagePct";
        case ModifierProperty::MoveSpeedBonusConstant:   return "MoveSpeedBonus";
        case ModifierProperty::MoveSpeedBonusPct:        return "MoveSpeedBonusPct";
        case ModifierProperty::HealAmpPct:               return "HealAmpPct";
        case ModifierProperty::Evasion:                  return "Evasion";
        case ModifierProperty::LifestealPct:             return "LifestealPct";
        case ModifierProperty::HealthRegen:              return "HealthRegen";
        case ModifierProperty::ManaRegen:                return "ManaRegen";
        case ModifierProperty::SpellAmplifyPct:          return "SpellAmplifyPct";
        case ModifierProperty::StatusResistancePct:      return "StatusResistancePct";
        case ModifierProperty::CooldownReductionPct:     return "CooldownReductionPct";
        case ModifierProperty::CastRangeBonus:           return "CastRangeBonus";
        case ModifierProperty::Count_:                   break;
    }
    return "?";
}

inline const char* phase_label(CastPhase p) {
    switch (p) {
        case CastPhase::Ready:       return "Ready";
        case CastPhase::Casting:     return "Casting";
        case CastPhase::Backswing:   return "Backswing";
        case CastPhase::Channelling: return "Channelling";
        case CastPhase::OnCooldown:  return "OnCooldown";
    }
    return "?";
}

// imgui DragFloat 的 double 包装. 通过 float 中转, 写回时再转 double.
inline bool drag_double(const char* label, double& value, float speed,
                        double min_v, double max_v, const char* fmt) {
    float f = static_cast<float>(value);
    if (!ImGui::DragFloat(label, &f, speed,
                          static_cast<float>(min_v),
                          static_cast<float>(max_v), fmt)) {
        return false;
    }
    value = static_cast<double>(f);
    return true;
}

inline void draw_state_mask(std::uint32_t mask) {
    bool any = false;
    for (int i = 0; i < static_cast<int>(ModifierState::Count_); ++i) {
        const auto state = static_cast<ModifierState>(i);
        if ((mask & state_bit(state)) == 0) continue;
        if (any) ImGui::SameLine();
        ImGui::TextUnformatted(state_label(state));
        any = true;
    }
    if (!any) ImGui::TextDisabled("(none)");
}

} // namespace dota::skill_tester
