#include "dota/core/unit.hpp"

#include "dota/ability/manager.hpp"
#include "dota/combat/damage.hpp"
#include "dota/core/world.hpp"
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
    // Dota 中魔抗通过 (1 - (1 - a)(1 - b)...) 组合. 阶段 2 中我们
    // 存储单个基础加成; 阶段 5 会重新审视此实现.
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
    const double as = std::max(20.0, total_as);  // Dota 下限
    return stats_.base_attack_time / (as / 100.0);
}

double Unit::evasion() const {
    return std::clamp(modifiers_->aggregated(ModifierProperty::Evasion), 0.0, 0.95);
}

double Unit::lifesteal_pct() const {
    return std::max(0.0, modifiers_->aggregated(ModifierProperty::LifestealPct));
}

double Unit::health_regen() const {
    return modifiers_->aggregated(ModifierProperty::HealthRegen);
}

double Unit::mana_regen() const {
    return modifiers_->aggregated(ModifierProperty::ManaRegen);
}

double Unit::spell_amp_pct() const {
    return modifiers_->aggregated(ModifierProperty::SpellAmplifyPct);
}

double Unit::status_resist() const {
    return std::clamp(modifiers_->aggregated(ModifierProperty::StatusResistancePct), 0.0, 1.0);
}

double Unit::cooldown_reduction_pct() const {
    return std::clamp(modifiers_->aggregated(ModifierProperty::CooldownReductionPct), 0.0, 1.0);
}

double Unit::cast_range_bonus() const {
    return modifiers_->aggregated(ModifierProperty::CastRangeBonus);
}

void Unit::purge(PurgeOptions opts) {
    // 收集要移除的 modifier 名称(不能在迭代过程中删除).
    std::vector<std::string> to_remove;
    for (const auto& m : modifiers_->all()) {
        if (!m) continue;
        if (!m->is_purgable()) continue;
        if (!opts.strong && !m->is_dispellable()) continue;
        const bool debuff = m->is_debuff();
        if (debuff && !opts.debuffs) continue;
        if (!debuff && !opts.buffs) continue;
        to_remove.push_back(m->name());
    }
    for (const auto& n : to_remove) {
        modifiers_->remove(n);
    }
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
    // Dota 中变羊故意不阻止移动
    constexpr std::uint32_t block = state_bit(ModifierState::Stunned)   |
                                    state_bit(ModifierState::Rooted)    |
                                    state_bit(ModifierState::OutOfGame);
    return (m & block) == 0;
}

void Unit::heal(double amount) {
    if (amount <= 0.0 || !alive()) return;
    // 通过治疗管线路由, 使治疗增强 modifier(包括阶段 5 的破坏治疗 debuff)一致触发
    deal_heal({nullptr, this, amount});
}

void Unit::spend_mana(double amount) {
    if (amount <= 0.0) return;
    mana_ = std::max(0.0, mana_ - amount);
}

void Unit::set_health(double hp) {
    health_ = std::clamp(hp, 0.0, std::max(0.0, max_health()));
}

void Unit::set_mana(double mana) {
    mana_ = std::clamp(mana, 0.0, std::max(0.0, max_mana()));
}

void Unit::set_stats(UnitStats stats) {
    stats_ = stats;
    health_ = std::clamp(health_, 0.0, std::max(0.0, max_health()));
    mana_ = std::clamp(mana_, 0.0, std::max(0.0, max_mana()));
}

double Unit::apply_raw_damage(double amount) {
    if (amount <= 0.0 || !alive()) return 0.0;
    const double before = health_;
    health_ = std::max(0.0, health_ - amount);
    return before - health_;
}

double Unit::apply_damage(DamageType type, double amount, EntityId attacker) {
    Unit* attacker_unit = (world_ && attacker != kInvalidEntityId)
                              ? world_->find(attacker)
                              : nullptr;
    return deal_damage({attacker_unit, this, type, amount, 0});
}

void Unit::tick_attack_cd(double dt) {
    attack_cd_ = std::max(0.0, attack_cd_ - dt);
}

void Unit::tick_modifiers(double dt) {
    modifiers_->advance(dt);

    if (!alive() || dt <= 0.0) return;

    // 生命/魔法回复(通过治疗管线, 使破坏治疗等修饰器一致生效).
    const double hr = health_regen();
    if (hr > 0.0 && health_ < max_health()) {
        deal_heal({nullptr, this, hr * dt});
    } else if (hr < 0.0) {
        // 负回复直接扣血, 不走治疗管线
        apply_raw_damage(-hr * dt);
    }
    const double mr = mana_regen();
    if (mr != 0.0) {
        mana_ = std::clamp(mana_ + mr * dt, 0.0, max_mana());
    }
}

void Unit::tick_abilities(double dt) {
    abilities_->advance(dt);
}

void Unit::issue_move(Vec2 target) {
    move_path_.waypoints.clear();
    move_path_.index = 0;
    if (world_) {
        world_->fill_move_path(*this, target, move_path_);
    } else {
        // 无 World 上下文时(测试用裸 Unit) 退化为单航点
        move_path_.waypoints.push_back(target);
    }
}

void Unit::stop_move() {
    move_path_ = {};
}

std::optional<Vec2> Unit::move_target() const {
    if (move_path_.empty()) return std::nullopt;
    return move_path_.final_point();
}

} // namespace dota
