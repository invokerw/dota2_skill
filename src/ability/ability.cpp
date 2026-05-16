#include "dota/ability/ability.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

// --- AbilitySpecialValue ---------------------------------------------------
// 技能特殊值

double AbilitySpecialValue::get_float(int level) const {
    const int idx = std::clamp(level - 1, 0, static_cast<int>(
        is_int ? ints.size() : floats.size()) - 1);
    if (idx < 0) return 0.0;
    if (is_int) return static_cast<double>(ints[idx]);
    return floats[idx];
}

long AbilitySpecialValue::get_int(int level) const {
    const int idx = std::clamp(level - 1, 0, static_cast<int>(
        is_int ? ints.size() : floats.size()) - 1);
    if (idx < 0) return 0;
    if (is_int) return ints[idx];
    return static_cast<long>(floats[idx]);
}

// --- Ability ---------------------------------------------------------------
// 技能

Ability::Ability(std::string name, std::uint32_t behavior, TargetTeam team, Unit& caster)
    : name_(std::move(name)), behavior_(behavior), target_team_(team), caster_(caster) {}

double Ability::cooldown_for_level() const {
    if (cooldowns_.empty()) return 0.0;
    const int idx = std::clamp(level_ - 1, 0, static_cast<int>(cooldowns_.size()) - 1);
    return cooldowns_[idx];
}

double Ability::mana_cost_for_level() const {
    if (mana_costs_.empty()) return 0.0;
    const int idx = std::clamp(level_ - 1, 0, static_cast<int>(mana_costs_.size()) - 1);
    return mana_costs_[idx];
}

CastError Ability::validate_target(const CastTarget& target) const {
    if (has_flag(behavior_, BehaviorFlag::UnitTarget)) {
        if (!target.unit) return CastError::InvalidTarget;
        if (!target.unit->alive()) return CastError::InvalidTarget;

        const bool same_team = target.unit->team() == caster_.team();
        switch (target_team_) {
            case TargetTeam::Enemy:
                if (same_team) return CastError::InvalidTarget;
                break;
            case TargetTeam::Friendly:
                if (!same_team) return CastError::InvalidTarget;
                break;
            case TargetTeam::Both:
            case TargetTeam::None:
                break;
        }

        if (target_team_ == TargetTeam::Enemy &&
            !has_flag(behavior_, BehaviorFlag::IgnoreMagicImmune) &&
            target.unit->modifiers().has_state(ModifierState::MagicImmune)) {
            return CastError::TargetMagicImmune;
        }

        // Untargetable：不可被技能选中（默认）。不影响 AoE。
        if (!has_flag(behavior_, BehaviorFlag::IgnoreUntargetable) &&
            target.unit->modifiers().has_state(ModifierState::Untargetable)) {
            return CastError::InvalidTarget;
        }

        if (cast_range_ > 0.0) {
            const double r2 = cast_range_ * cast_range_;
            if (distance_sq(caster_.position(), target.unit->position()) > r2) {
                return CastError::OutOfRange;
            }
        }
    } else if (has_flag(behavior_, BehaviorFlag::PointTarget)) {
        if (!target.has_point) return CastError::InvalidTarget;
    }
    return CastError::None;
}

CastError Ability::can_cast(const CastTarget& target) const {
    if (is_passive()) return CastError::NotReady;
    if (!caster_.alive()) return CastError::CasterDead;
    if (phase_ == CastPhase::Casting || phase_ == CastPhase::Channelling)
        return CastError::NotReady;
    if (cooldown_ > 0.0) return CastError::OnCooldown;
    if (caster_.mana() < mana_cost_for_level()) return CastError::NotEnoughMana;

    const auto m = caster_.modifiers().aggregated_states();
    if ((m & state_bit(ModifierState::Stunned)) != 0) return CastError::Stunned;
    if ((m & state_bit(ModifierState::Hexed))   != 0) return CastError::Hexed;
    if ((m & state_bit(ModifierState::Silenced)) != 0 &&
        !has_flag(behavior_, BehaviorFlag::IgnoreSilence)) {
        return CastError::Silenced;
    }

    return validate_target(target);
}

CastError Ability::trigger_cast(const CastTarget& target, World& world,
                                bool ignore_cooldown, bool ignore_mana,
                                bool ignore_state) {
    if (is_passive()) return CastError::NotReady;
    if (!caster_.alive()) return CastError::CasterDead;
    if (phase_ == CastPhase::Casting || phase_ == CastPhase::Channelling)
        return CastError::NotReady;
    if (!ignore_cooldown && cooldown_ > 0.0) return CastError::OnCooldown;
    if (!ignore_mana && caster_.mana() < mana_cost_for_level())
        return CastError::NotEnoughMana;
    if (!ignore_state) {
        const auto m = caster_.modifiers().aggregated_states();
        if ((m & state_bit(ModifierState::Stunned)) != 0) return CastError::Stunned;
        if ((m & state_bit(ModifierState::Hexed))   != 0) return CastError::Hexed;
        if ((m & state_bit(ModifierState::Silenced)) != 0 &&
            !has_flag(behavior_, BehaviorFlag::IgnoreSilence)) {
            return CastError::Silenced;
        }
    }
    if (CastError verr = validate_target(target); verr != CastError::None) return verr;

    if (!ignore_mana) caster_.spend_mana(mana_cost_for_level());
    pending_target_ = target;
    world_          = &world;

    if (cast_point_ <= 0.0) {
        CastContext ctx{&caster_, world_, pending_target_, level_};
        on_spell_start(ctx);

        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = ignore_cooldown ? cooldown_ : cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            phase_timer_ = 0.0;
        }
    } else {
        enter_phase(CastPhase::Casting, cast_point_);
    }
    return CastError::None;
}

CastError Ability::order_cast(const CastTarget& target, World& world) {
    const CastError err = can_cast(target);
    if (err != CastError::None) return err;

    caster_.spend_mana(mana_cost_for_level());
    pending_target_ = target;
    world_          = &world;

    // 零施法前摇 → 立即执行，这样瞬发技能不需要第一次 `advance()` 调用（测试依赖此行为）
    if (cast_point_ <= 0.0) {
        CastContext ctx{&caster_, world_, pending_target_, level_};
        on_spell_start(ctx);

        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            phase_timer_ = 0.0;
        }
    } else {
        enter_phase(CastPhase::Casting, cast_point_);
    }
    return CastError::None;
}

void Ability::enter_phase(CastPhase p, double timer) {
    phase_       = p;
    phase_timer_ = timer;
}

bool Ability::current_target_still_valid() const {
    if (!has_flag(behavior_, BehaviorFlag::UnitTarget)) return true;
    return pending_target_.unit && pending_target_.unit->alive();
}

void Ability::advance(double dt) {
    // 无论处于哪个阶段都要更新冷却时间
    if (cooldown_ > 0.0) {
        cooldown_ = std::max(0.0, cooldown_ - dt);
        if (cooldown_ == 0.0 && phase_ == CastPhase::OnCooldown) {
            phase_ = CastPhase::Ready;
        }
    }

    if (phase_ == CastPhase::Ready || phase_ == CastPhase::OnCooldown) return;

    // 如果施法者无法继续行动则中断施法
    const auto m = caster_.modifiers().aggregated_states();
    const bool stunned  = (m & state_bit(ModifierState::Stunned)) != 0;
    const bool silenced = (m & state_bit(ModifierState::Silenced)) != 0
                          && !has_flag(behavior_, BehaviorFlag::IgnoreSilence);
    const bool hexed    = (m & state_bit(ModifierState::Hexed)) != 0;
    const bool dead     = !caster_.alive();
    const bool target_lost = !current_target_still_valid();

    auto interrupt = [&](bool channelled) {
        if (channelled) {
            CastContext ctx{&caster_, world_, pending_target_, level_};
            on_channel_finish(ctx, /*interrupted*/ true);
        }
        // 中断时冷却时间仍然生效（Dota 惯例）
        cooldown_ = cooldown_for_level();
        phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
        phase_timer_ = 0.0;
        world_ = nullptr;
    };

    if (dead || stunned || silenced || hexed || target_lost) {
        const bool was_channel = (phase_ == CastPhase::Channelling);
        interrupt(was_channel);
        return;
    }

    phase_timer_ = std::max(0.0, phase_timer_ - dt);

    if (phase_ == CastPhase::Channelling) {
        // 持续施法期间每帧触发 think 回调
        CastContext ctx{&caster_, world_, pending_target_, level_};
        on_channel_think(ctx, dt);
        if (phase_timer_ == 0.0) {
            on_channel_finish(ctx, /*interrupted*/ false);
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            world_    = nullptr;
        }
        return;
    }

    if (phase_ == CastPhase::Casting && phase_timer_ == 0.0) {
        CastContext ctx{&caster_, world_, pending_target_, level_};
        on_spell_start(ctx);
        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            world_    = nullptr;
        }
        return;
    }

    if (phase_ == CastPhase::Backswing && phase_timer_ == 0.0) {
        cooldown_ = cooldown_for_level();
        phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
        world_    = nullptr;
    }
}

} // namespace dota
