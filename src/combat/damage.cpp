#include "dota/combat/damage.hpp"

#include "dota/core/unit.hpp"
#include "dota/modifier/manager.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

namespace {

double physical_multiplier(double armor) {
    // Dota armor → damage multiplier. Negative armor symmetric: 2 - 0.94^|a|.
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

    // --- (1) Outgoing amplification on attacker ---
    if (dmg.attacker &&
        !has_flag(dmg.flags, DamageFlag::NoSpellAmplification)) {
        const double out_pct = dmg.attacker->modifiers().aggregated(
            ModifierProperty::OutgoingDamagePct);
        amount *= (1.0 + out_pct);
    }

    // --- (2) Incoming amplification on victim ---
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

    // --- (3) Pre-take-damage: shields & absorbs. HPLoss skips this layer so
    // regen ticks don't get ablated by Pipe-style barriers.
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

    // --- (4) Magic immunity short-circuit ---
    if (dmg.type == DamageType::Magical &&
        !has_flag(dmg.flags, DamageFlag::BypassMagicImmune) &&
        victim->modifiers().has_state(ModifierState::MagicImmune)) {
        PostTakeDamageEvent post{attacker_id, victim->id(),
                                  dmg.type, dmg.flags, 0.0};
        victim->modifiers().dispatch_post_take_damage(post);
        return 0.0;
    }

    // --- (5) Type resistance. HPLoss skips resistance too. ---
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

    // --- (6) Apply HP delta. ---
    const double applied = victim->apply_raw_damage(std::max(0.0, after_resist));

    // --- (7) Post-take-damage: reflect, lifesteal, on-hit triggers. ---
    PostTakeDamageEvent post{attacker_id, victim->id(),
                              dmg.type, dmg.flags, applied};
    victim->modifiers().dispatch_post_take_damage(post);
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

    // Apply heal-amp (break-the-healing at -0.4, Ilusion Amulet at +0.2, etc.)
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
