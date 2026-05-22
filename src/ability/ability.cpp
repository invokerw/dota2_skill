#include "dota/ability/ability.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

namespace {

void publish_cast_started(World* w, EntityId caster, const std::string& ability,
                          const CastTarget& tgt) {
    if (!w) return;
    AbilityCastStartedEvent ev{
        caster, ability,
        tgt.unit ? tgt.unit->id() : kInvalidEntityId,
        tgt.point, tgt.has_point,
    };
    w->events().publish(ev);
}

void publish_cast_finished(World* w, EntityId caster, const std::string& ability,
                           bool interrupted) {
    if (!w) return;
    AbilityCastFinishedEvent ev{caster, ability, interrupted};
    w->events().publish(ev);
}

// 把 cast 完整结束的事件派发给 caster 上的所有 modifier. passive ability
// 不会调用此函数 (它们在 trigger_cast / order_cast 入口就被早退). interrupted
// 也不调用 (与 Dota OnAbilityFullyCast 语义一致).
void dispatch_ability_executed(Ability& ability) {
    AbilityExecutedInfo info{
        &ability.caster(),
        &ability,
        ability.name(),
        ability.is_passive(),
    };
    ability.caster().modifiers().dispatch_ability_executed(info);
}

} // namespace

// --- AbilitySpecialValue
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

// --- Ability
// 技能

Ability::Ability(std::string name, std::uint32_t behavior, TargetTeam team, Unit& caster)
    : name_(std::move(name)), behavior_(behavior), target_team_(team), caster_(caster) {
    // 法球默认 autocast 开启. AutoCast 标志位与 Attack 共存仅表示玩家可关 -- 这里
    // 不模拟玩家 toggle, 调用方可通过 set_autocast_on(false) 显式关闭.
    if (has_flag(behavior_, BehaviorFlag::Attack)) autocast_on_ = true;
}

void Ability::set_level(int l) {
    const int new_level = std::max(1, l);
    if (new_level == level_) return;
    level_ = new_level;
    // 让 intrinsic modifier 重读 ability_special.
    if (!intrinsic_modifier_.empty()) {
        if (Modifier* m = caster_.modifiers().find(intrinsic_modifier_)) {
            m->on_refresh();
        }
    }
    on_upgrade(new_level);
}

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

        // Untargetable: 不可被技能选中(默认). 不影响 AoE.
        if (!has_flag(behavior_, BehaviorFlag::IgnoreUntargetable) &&
            target.unit->modifiers().has_state(ModifierState::Untargetable)) {
            return CastError::InvalidTarget;
        }

        if (cast_range_ > 0.0) {
            const double effective_range = cast_range_ + target.unit->hull_radius();
            const double r2 = effective_range * effective_range;
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

bool Ability::can_use_resources_for_orb() const {
    if (!is_orb()) return false;
    if (cooldown_ > 0.0) return false;
    if (caster_.mana() < mana_cost_for_level()) return false;
    return true;
}

bool Ability::use_resources_for_orb() {
    if (!can_use_resources_for_orb()) return false;
    caster_.spend_mana(mana_cost_for_level());
    cooldown_ = cooldown_for_level();
    if (cooldown_ > 0.0 && phase_ == CastPhase::Ready) {
        phase_ = CastPhase::OnCooldown;
    }
    return true;
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
    pending_target_ = {target.unit ? target.unit->id() : kInvalidEntityId,
                       target.point, target.has_point};
    world_          = &world;

    const CastTarget snap = resolve_pending();
    publish_cast_started(world_, caster_.id(), name_, snap);

    if (cast_point_ <= 0.0) {
        CastContext ctx{&caster_, world_, snap, level_};
        on_spell_start(ctx);

        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = ignore_cooldown ? cooldown_ : cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            phase_timer_ = 0.0;
            publish_cast_finished(world_, caster_.id(), name_, false);
            dispatch_ability_executed(*this);
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
    pending_target_ = {target.unit ? target.unit->id() : kInvalidEntityId,
                       target.point, target.has_point};
    world_          = &world;

    const CastTarget snap = resolve_pending();
    publish_cast_started(world_, caster_.id(), name_, snap);

    // 零施法前摇 → 立即执行, 这样瞬发技能不需要第一次 `advance()` 调用(测试依赖此行为)
    if (cast_point_ <= 0.0) {
        CastContext ctx{&caster_, world_, snap, level_};
        on_spell_start(ctx);

        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            phase_timer_ = 0.0;
            publish_cast_finished(world_, caster_.id(), name_, false);
            dispatch_ability_executed(*this);
        }
    } else {
        enter_phase(CastPhase::Casting, cast_point_);
    }
    return CastError::None;
}

CastTarget Ability::resolve_pending() const {
    CastTarget t;
    t.point     = pending_target_.point;
    t.has_point = pending_target_.has_point;
    if (pending_target_.unit_id != kInvalidEntityId && world_) {
        t.unit = world_->find(pending_target_.unit_id);
    }
    return t;
}

void Ability::enter_phase(CastPhase p, double timer) {
    phase_       = p;
    phase_timer_ = timer;
}

bool Ability::current_target_still_valid() const {
    if (!has_flag(behavior_, BehaviorFlag::UnitTarget)) return true;
    if (pending_target_.unit_id == kInvalidEntityId || !world_) return false;
    Unit* u = world_->find(pending_target_.unit_id);
    return u && u->alive();
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
            CastContext ctx{&caster_, world_, resolve_pending(), level_};
            on_channel_finish(ctx, /*interrupted*/ true);
        }
        publish_cast_finished(world_, caster_.id(), name_, /*interrupted*/ true);
        // 中断时冷却时间仍然生效(Dota 惯例)
        cooldown_ = cooldown_for_level();
        phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
        phase_timer_ = 0.0;
        world_ = nullptr;
        // Dota 默认: cast 被中断 -> 清掉指令队列, 包括队列里待执行的后续 cast / move.
        caster_.clear_orders();
    };

    if (dead || stunned || silenced || hexed || target_lost) {
        const bool was_channel = (phase_ == CastPhase::Channelling);
        interrupt(was_channel);
        return;
    }

    phase_timer_ = std::max(0.0, phase_timer_ - dt);

    if (phase_ == CastPhase::Channelling) {
        // 持续施法期间每帧触发 think 回调
        CastContext ctx{&caster_, world_, resolve_pending(), level_};
        on_channel_think(ctx, dt);
        if (phase_timer_ == 0.0) {
            on_channel_finish(ctx, /*interrupted*/ false);
            publish_cast_finished(world_, caster_.id(), name_, /*interrupted*/ false);
            dispatch_ability_executed(*this);
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            world_    = nullptr;
        }
        return;
    }

    if (phase_ == CastPhase::Casting && phase_timer_ == 0.0) {
        CastContext ctx{&caster_, world_, resolve_pending(), level_};
        on_spell_start(ctx);
        if (is_channelled() && channel_time_ > 0.0) {
            enter_phase(CastPhase::Channelling, channel_time_);
        } else if (backswing_ > 0.0) {
            enter_phase(CastPhase::Backswing, backswing_);
        } else {
            cooldown_ = cooldown_for_level();
            phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
            publish_cast_finished(world_, caster_.id(), name_, /*interrupted*/ false);
            dispatch_ability_executed(*this);
            world_    = nullptr;
        }
        return;
    }

    if (phase_ == CastPhase::Backswing && phase_timer_ == 0.0) {
        cooldown_ = cooldown_for_level();
        phase_    = cooldown_ > 0.0 ? CastPhase::OnCooldown : CastPhase::Ready;
        publish_cast_finished(world_, caster_.id(), name_, /*interrupted*/ false);
        dispatch_ability_executed(*this);
        world_    = nullptr;
    }
}

} // namespace dota
