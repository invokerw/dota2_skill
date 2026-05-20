#include "dota/core/unit.hpp"

#include "dota/ability/manager.hpp"
#include "dota/combat/damage.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>

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

// 激活队首 Order: 对 OrderMoveToPoint 派生 move_path; OrderStop 直接 pop;
// 其他类型 Stage 3/4 接入. 入参为引用以便递归 pop. 入队首激活后即返回.
static void activate_front(Unit& self, std::deque<Order>& orders,
                            MovePath& move_path, World* world) {
    while (!orders.empty()) {
        const Order& front = orders.front();
        bool consumed = false;
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OrderMoveToPoint>) {
                move_path.waypoints.clear();
                move_path.index = 0;
                if (world) {
                    world->fill_move_path(self, v.point, move_path);
                } else {
                    move_path.waypoints.push_back(v.point);
                }
                // fill 后仍空 (target 与起点重合且 fill 没生成航点) -- 视作完成.
                if (move_path.empty()) consumed = true;
            } else if constexpr (std::is_same_v<T, OrderStop>) {
                move_path = {};
                consumed = true;
            } else {
                (void)v;  // Stage 3/4: 暂不消费, 留待后续 stage 派发
            }
        }, front);
        if (!consumed) return;  // 已激活但未立即完成 -> 等待 tick 推进
        orders.pop_front();
    }
}

void Unit::issue_order(Order o, bool queue) {
    if (!queue) {
        orders_.clear();
        move_path_ = {};
    }
    // 覆盖模式 + OrderStop: 整队已清, 不入队.
    if (!queue && std::holds_alternative<OrderStop>(o)) return;
    const bool was_empty = orders_.empty();
    orders_.push_back(std::move(o));
    // 队列原本为空 (或刚被覆盖清掉) -> 立即激活新队首; 否则等队首走完后衔接.
    if (was_empty) activate_front(*this, orders_, move_path_, world_);
}

void Unit::clear_orders() {
    orders_.clear();
    move_path_ = {};
}

void Unit::pump_orders() {
    // World 每 tick 在 tick_movement 之后调用. 检测当前队首是否已完成
    // (MoveTo 看 move_path 是否走完), 完成则 pop + 激活下一条.
    while (!orders_.empty()) {
        const Order& front = orders_.front();
        bool done = false;
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OrderMoveToPoint>) {
                done = move_path_.empty();
            }
            // 其他类型 Stage 3/4 接入完成判定.
            (void)v;
        }, front);
        if (!done) break;
        orders_.pop_front();
        activate_front(*this, orders_, move_path_, world_);
        // activate_front 已经把"立即完成"的 Order 全部 pop 干净并激活了下一个,
        // 故下一轮 while 检查的就是新的活动队首.
        return;
    }
}

void Unit::issue_move(Vec2 target) {
    issue_order(OrderMoveToPoint{target});
}

void Unit::stop_move() {
    clear_orders();
}

std::optional<Vec2> Unit::move_target() const {
    if (move_path_.empty()) return std::nullopt;
    return move_path_.final_point();
}

} // namespace dota
