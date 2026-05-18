#include "dota/combat/damage.hpp"

#include "dota/core/unit.hpp"
#include "dota/modifier/manager.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

namespace {

double physical_multiplier(double armor) {
    // Dota 护甲 → 伤害倍率. 负护甲对称公式: 2 - 0.94^|a|
    const double reduction = (0.06 * armor) / (1.0 + 0.06 * std::abs(armor));
    return armor >= 0.0 ? 1.0 - reduction : 2.0 - std::pow(0.94, -armor);
}

} // namespace

double deal_damage(DamageInstance dmg) {
    Unit* victim = dmg.victim;
    if (!victim || !victim->alive() || dmg.amount <= 0.0) return 0.0;

    const EntityId attacker_id = dmg.attacker ? dmg.attacker->id()
                                              : kInvalidEntityId;

    double amount = dmg.amount;

    // --- (1) 攻击者的输出伤害增幅 ---
    if (dmg.attacker &&
        !has_flag(dmg.flags, DamageFlag::NoSpellAmplification)) {
        const double out_pct = dmg.attacker->modifiers().aggregated(
            ModifierProperty::OutgoingDamagePct);
        amount *= (1.0 + out_pct);
    }

    // --- (1b) 法术增伤(仅对法术/纯粹伤害生效; 与 OutgoingDamagePct 独立累加)---
    if (dmg.attacker &&
        (dmg.type == DamageType::Magical || dmg.type == DamageType::Pure) &&
        !has_flag(dmg.flags, DamageFlag::NoSpellAmplification)) {
        amount *= (1.0 + dmg.attacker->spell_amp_pct());
    }

    // --- (2) 受害者的承受伤害增幅 ---
    if (!has_flag(dmg.flags, DamageFlag::NoSpellAmplification)) {
        const double in_pct = victim->modifiers().aggregated(
            ModifierProperty::IncomingDamagePct);
        amount *= (1.0 + in_pct);
    }

    if (amount <= 0.0) {
        PostTakeDamageEvent post{attacker_id, victim->id(),
                                  dmg.type, dmg.flags, 0.0};
        victim->modifiers().dispatch_post_take_damage(post);
        return 0.0;
    }

    // --- (3) 承受伤害前: 护盾和吸收. HPLoss 跳过此层,
    // 使回血 tick 不会被 Pipe 风格的屏障削减
    PreTakeDamageEvent pre{attacker_id, victim->id(),
                            dmg.type, dmg.flags, amount, 0.0};
    if (!has_flag(dmg.flags, DamageFlag::HPLoss)) {
        victim->modifiers().dispatch_pre_take_damage(pre);
    }

    double effective = std::max(0.0, pre.amount);
    if (effective == 0.0) {
        PostTakeDamageEvent post{attacker_id, victim->id(),
                                  dmg.type, dmg.flags, 0.0};
        victim->modifiers().dispatch_post_take_damage(post);
        return 0.0;
    }

    // --- (4) 魔法免疫短路 ---
    if (dmg.type == DamageType::Magical &&
        !has_flag(dmg.flags, DamageFlag::BypassMagicImmune) &&
        victim->modifiers().has_state(ModifierState::MagicImmune)) {
        PostTakeDamageEvent post{attacker_id, victim->id(),
                                  dmg.type, dmg.flags, 0.0};
        victim->modifiers().dispatch_post_take_damage(post);
        return 0.0;
    }

    // --- (5) 类型抗性. HPLoss 也跳过抗性 ---
    double after_resist = effective;
    if (!has_flag(dmg.flags, DamageFlag::HPLoss)) {
        switch (dmg.type) {
            case DamageType::Physical:
                after_resist = effective * physical_multiplier(victim->armor());
                break;
            case DamageType::Magical:
                after_resist = effective * (1.0 - victim->magic_resist());
                break;
            case DamageType::Pure:
                break;
        }
    }

    // --- (6) 应用生命值变化 ---
    const double applied = victim->apply_raw_damage(std::max(0.0, after_resist));

    // --- (7) 承受伤害后: 反伤, 触发效果 ---
    PostTakeDamageEvent post{attacker_id, victim->id(),
                              dmg.type, dmg.flags, applied};
    victim->modifiers().dispatch_post_take_damage(post);

    // --- (8) 物理吸血. Pure/Magical 不触发; Reflection/NoLifesteal 跳过 ---
    if (applied > 0.0 &&
        dmg.type == DamageType::Physical &&
        dmg.attacker && dmg.attacker->alive() &&
        !has_flag(dmg.flags, DamageFlag::NoLifesteal) &&
        !has_flag(dmg.flags, DamageFlag::Reflection)) {
        const double ls = dmg.attacker->lifesteal_pct();
        if (ls > 0.0) {
            deal_heal({dmg.attacker, dmg.attacker, applied * ls});
        }
    }
    return applied;
}

double deal_heal(HealInstance heal) {
    Unit* target = heal.target;
    if (!target || !target->alive() || heal.amount <= 0.0) return 0.0;

    PreTakeHealEvent pre{heal.healer ? heal.healer->id() : kInvalidEntityId,
                          target->id(),
                          heal.amount};
    target->modifiers().dispatch_pre_take_heal(pre);
    if (pre.amount <= 0.0) return 0.0;

    // 应用治疗增幅(例如 -0.4 的破坏治疗, +0.2 的幻象护符等)
    const double amp = target->modifiers().aggregated(ModifierProperty::HealAmpPct);
    double amount = pre.amount * (1.0 + amp);
    if (amount <= 0.0) return 0.0;

    const double before = target->health();
    target->set_health(before + amount);
    const double applied = target->health() - before;

    PostTakeHealEvent post{heal.healer ? heal.healer->id() : kInvalidEntityId,
                            target->id(), applied};
    target->modifiers().dispatch_post_take_heal(post);
    return applied;
}

} // namespace dota
