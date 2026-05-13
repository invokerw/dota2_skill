#include "dota/core/unit.hpp"

#include "dota/ability/manager.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <cmath>

namespace dota {

Unit::Unit(EntityId id, std::string name, Team team, UnitStats stats)
    : id_(id)
    , name_(std::move(name))
    , team_(team)
    , stats_(stats)
    , health_(stats.max_health)
    , mana_(stats.max_mana)
    , modifiers_(std::make_unique<ModifierManager>(*this))
    , abilities_(std::make_unique<AbilityManager>(*this)) {}

Unit::~Unit() = default;

double Unit::max_health() const {
    return stats_.max_health + modifiers_->aggregated(ModifierProperty::HealthBonus);
}

double Unit::max_mana() const {
    return stats_.max_mana + modifiers_->aggregated(ModifierProperty::ManaBonus);
}

double Unit::armor() const {
    return modifiers_->apply_stat(ModifierProperty::ArmorBonus,
                                   ModifierProperty::ArmorBonusPct,
                                   stats_.base_armor);
}

double Unit::attack_damage() const {
    return modifiers_->apply_stat(ModifierProperty::AttackDamageBonus,
                                   ModifierProperty::AttackDamageBonusPct,
                                   stats_.attack_damage);
}

double Unit::magic_resist() const {
    // Magic resist combines via (1 - (1 - a)(1 - b)…) in Dota. For Stage 2 we
    // store a single additive-to-base bonus; Stage 5 revisits this.
    const double bonus = modifiers_->aggregated(ModifierProperty::MagicResistBonus);
    return std::clamp(stats_.magic_resist + bonus, 0.0, 1.0);
}

double Unit::move_speed() const {
    return modifiers_->apply_stat(ModifierProperty::MoveSpeedBonusConstant,
                                   ModifierProperty::MoveSpeedBonusPct,
                                   stats_.move_speed);
}

double Unit::seconds_per_attack() const {
    const double bonus_as = modifiers_->aggregated(ModifierProperty::AttackSpeedBonusConstant);
    const double total_as = stats_.attack_speed + bonus_as;
    const double as = std::max(20.0, total_as);  // Dota floor
    return stats_.base_attack_time / (as / 100.0);
}

bool Unit::can_attack() const {
    if (!alive()) return false;
    const auto m = modifiers_->aggregated_states();
    constexpr std::uint32_t block = state_bit(ModifierState::Stunned) |
                                    state_bit(ModifierState::Disarmed) |
                                    state_bit(ModifierState::Hexed)    |
                                    state_bit(ModifierState::OutOfGame);
    return (m & block) == 0;
}

bool Unit::can_cast() const {
    if (!alive()) return false;
    const auto m = modifiers_->aggregated_states();
    constexpr std::uint32_t block = state_bit(ModifierState::Stunned)  |
                                    state_bit(ModifierState::Silenced) |
                                    state_bit(ModifierState::Hexed)    |
                                    state_bit(ModifierState::OutOfGame);
    return (m & block) == 0;
}

bool Unit::can_move() const {
    if (!alive()) return false;
    const auto m = modifiers_->aggregated_states();
    // Hex intentionally does NOT block movement in Dota.
    constexpr std::uint32_t block = state_bit(ModifierState::Stunned)   |
                                    state_bit(ModifierState::Rooted)    |
                                    state_bit(ModifierState::OutOfGame);
    return (m & block) == 0;
}

void Unit::heal(double amount) {
    if (amount <= 0.0 || !alive()) return;
    health_ = std::min(max_health(), health_ + amount);
}

void Unit::spend_mana(double amount) {
    if (amount <= 0.0) return;
    mana_ = std::max(0.0, mana_ - amount);
}

void Unit::set_health(double hp) {
    health_ = std::clamp(hp, 0.0, max_health());
}

double Unit::apply_raw_damage(double amount) {
    if (amount <= 0.0 || !alive()) return 0.0;
    const double before = health_;
    health_ = std::max(0.0, health_ - amount);
    return before - health_;
}

double Unit::apply_damage(DamageType type, double amount, EntityId attacker) {
    if (amount <= 0.0 || !alive()) return 0.0;

    // Pre-damage: modifiers may mutate amount or record absorption. Shields
    // are expected to decrement ev.amount directly; ev.absorbed is recorded
    // for observers (Stage 5 reflect/lifesteal) but we do not subtract it a
    // second time.
    PreTakeDamageEvent pre{attacker, id_, type, amount, 0.0};
    modifiers_->dispatch_pre_take_damage(pre);

    double effective = std::max(0.0, pre.amount);
    if (effective == 0.0) {
        PostTakeDamageEvent post{attacker, id_, type, 0.0};
        modifiers_->dispatch_post_take_damage(post);
        return 0.0;
    }

    // Magic immunity fully blocks Magical unless pre.amount already zeroed.
    if (type == DamageType::Magical &&
        modifiers_->has_state(ModifierState::MagicImmune)) {
        PostTakeDamageEvent post{attacker, id_, type, 0.0};
        modifiers_->dispatch_post_take_damage(post);
        return 0.0;
    }

    // Type resistance.
    double after_resist = effective;
    switch (type) {
        case DamageType::Physical: {
            const double a = armor();
            const double reduction = (0.06 * a) / (1.0 + 0.06 * std::abs(a));
            const double mult = a >= 0.0 ? 1.0 - reduction
                                         : 2.0 - std::pow(0.94, -a);
            after_resist = effective * mult;
            break;
        }
        case DamageType::Magical:
            after_resist = effective * (1.0 - magic_resist());
            break;
        case DamageType::Pure:
            break;
    }

    const double applied = apply_raw_damage(std::max(0.0, after_resist));

    PostTakeDamageEvent post{attacker, id_, type, applied};
    modifiers_->dispatch_post_take_damage(post);
    return applied;
}

void Unit::tick_attack_cd(double dt) {
    attack_cd_ = std::max(0.0, attack_cd_ - dt);
}

void Unit::tick_modifiers(double dt) {
    modifiers_->advance(dt);
}

void Unit::tick_abilities(double dt) {
    abilities_->advance(dt);
}

} // namespace dota
