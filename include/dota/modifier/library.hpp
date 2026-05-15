#pragma once

#include "dota/combat/damage.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <initializer_list>

namespace dota::modifiers {

// --- 仅状态修饰器（名称驱动的眩晕/沉默/定身等） -------------

class GenericState : public Modifier {
public:
    // 参数顺序与 ModifierManager::attach_new 匹配，后者在前面添加 owner。
    GenericState(Unit& owner, std::string name, double duration, std::uint32_t state_mask)
        : Modifier(std::move(name), owner, duration), mask_(state_mask) {}

    std::uint32_t declared_states() const override { return mask_; }

private:
    std::uint32_t mask_;
};

inline std::unique_ptr<GenericState>
make_stunned(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_stunned", duration, state_bit(ModifierState::Stunned));
}

inline std::unique_ptr<GenericState>
make_silenced(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_silenced", duration, state_bit(ModifierState::Silenced));
}

inline std::unique_ptr<GenericState>
make_rooted(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_rooted", duration, state_bit(ModifierState::Rooted));
}

inline std::unique_ptr<GenericState>
make_hexed(Unit& owner, double duration) {
    // 妖术（狮子）在 Dota 中禁用施法/攻击但不禁用移动。仅 Hexed
    // 状态位就足够了 — can_attack/can_cast 会阻止它，但
    // can_move 不会。
    return std::make_unique<GenericState>(
        owner, "modifier_hexed", duration,
        state_bit(ModifierState::Hexed));
}

inline std::unique_ptr<GenericState>
make_invisible(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_invisible", duration, state_bit(ModifierState::Invisible));
}

inline std::unique_ptr<GenericState>
make_magic_immune(Unit& owner, double duration) {
    return std::make_unique<GenericState>(
        owner, "modifier_magic_immune", duration, state_bit(ModifierState::MagicImmune));
}

// --- 数值属性修饰器 -------------------------------------------------

class GenericStats : public Modifier {
public:
    GenericStats(Unit& owner, std::string name, double duration,
                 std::initializer_list<ModifierProvidedProperty> props)
        : Modifier(std::move(name), owner, duration), props_(props) {}

    std::vector<ModifierProvidedProperty> declared_properties() const override {
        return props_;
    }

private:
    std::vector<ModifierProvidedProperty> props_;
};

// --- 护盾/吸收修饰器 ------------------------------------------------
//
// Dota 示例：洞察烟斗的屏障、美杜莎的魔法护盾等。这个修饰器
// 简单地吸收最多 `capacity` 点伤害（跨任意次数的攻击），
// 并在耗尽时自毁。抗性前吸收反映了 Dota 解决大多数"护盾"机制的方式。
class ShieldAbsorb : public Modifier {
public:
    ShieldAbsorb(Unit& owner, double capacity, double duration)
        : Modifier("modifier_shield_absorb", owner, duration)
        , remaining_(capacity) {}

    double remaining() const { return remaining_; }

    void on_pre_take_damage(PreTakeDamageEvent& ev) override {
        if (remaining_ <= 0.0 || ev.amount <= 0.0) return;
        const double eat = std::min(remaining_, ev.amount);
        ev.amount   -= eat;
        ev.absorbed += eat;
        remaining_  -= eat;
        if (remaining_ <= 0.0) {
            // 通过将剩余持续时间归零来消耗护盾。
            // 管理器将在下一次 advance() 时清除。
            refresh(0.0);
        }
    }

private:
    double remaining_;
};

// --- 周期性治疗（治疗守卫风格） ------------------------------------
//
// 每 `interval` 秒对拥有者治疗 `heal_per_tick`，总持续 `duration` 秒。
// 通过治疗管线运行，因此破坏治疗有效。
class PeriodicHeal : public Modifier {
public:
    PeriodicHeal(Unit& owner, double heal_per_tick, double interval, double duration)
        : Modifier("modifier_periodic_heal", owner, duration)
        , amount_(heal_per_tick) {
        set_think_interval(interval);
    }

    void on_interval_think() override {
        deal_heal({nullptr, &owner(), amount_});
    }

private:
    double amount_;
};

inline std::unique_ptr<PeriodicHeal>
make_periodic_heal(Unit& owner, double heal_per_tick, double interval, double duration) {
    return std::make_unique<PeriodicHeal>(owner, heal_per_tick, interval, duration);
}

// --- 反射（刃甲风格） --------------------------------------------
//
// 将抗性前伤害的一部分作为纯粹伤害反射回攻击者，并设置 DamageFlag::Reflection，
// 管线通过跳过攻击者上的任何反射修饰器来遵守此标志（防止无限循环）。
class ReflectDamage : public Modifier {
public:
    ReflectDamage(Unit& owner, double fraction, double duration)
        : Modifier("modifier_reflect_damage", owner, duration)
        , fraction_(fraction) {}

    void on_pre_take_damage(PreTakeDamageEvent& ev) override {
        // 记录抗性前的数量以便反射 — Dota 中的刃甲
        // 反射目标将承受的数量，而不是抗性前的标题数字。
        // 这是 Stage 5 测试可接受的近似值。
        pending_reflect_ = fraction_ * ev.amount;
        pending_attacker_ = ev.attacker;
    }

    void on_post_take_damage(PostTakeDamageEvent& ev) override {
        if (pending_reflect_ <= 0.0) return;
        if (has_flag(ev.flags, DamageFlag::Reflection)) {
            pending_reflect_ = 0.0;     // 永不反射已反射的伤害
            return;
        }
        Unit* attacker = owner().world()
                             ? owner().world()->find(pending_attacker_)
                             : nullptr;
        if (attacker && attacker->alive()) {
            deal_damage({attacker, attacker, DamageType::Pure,
                         pending_reflect_,
                         to_mask(DamageFlag::Reflection)});
        }
        pending_reflect_ = 0.0;
    }

private:
    double   fraction_;
    double   pending_reflect_{0.0};
    EntityId pending_attacker_{kInvalidEntityId};
};

inline std::unique_ptr<ReflectDamage>
make_blade_mail(Unit& owner, double fraction, double duration) {
    return std::make_unique<ReflectDamage>(owner, fraction, duration);
}

// --- 破坏治疗 -----------------------------------------------------
//
// 一个减少承受治疗量的减益效果（例如 0.4 表示 -40%）。
inline std::unique_ptr<GenericStats>
make_break_healing(Unit& owner, double fraction, double duration) {
    return std::make_unique<GenericStats>(
        owner, "modifier_break_healing", duration,
        std::initializer_list<ModifierProvidedProperty>{
            {ModifierProperty::HealAmpPct, -fraction}});
}

} // namespace dota::modifiers
