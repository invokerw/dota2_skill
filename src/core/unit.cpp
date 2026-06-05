#include "dota/core/unit.hpp"

#include "dota/ability/manager.hpp"
#include "dota/combat/damage.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

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

namespace {

// 解析 ability_index -> Ability*; 越界返回 nullptr.
Ability* lookup_ability(Unit& self, int idx) {
    const auto& abs = self.abilities().all();
    if (idx < 0 || static_cast<std::size_t>(idx) >= abs.size()) return nullptr;
    return abs[static_cast<std::size_t>(idx)].get();
}

// 派发内部跟随 move_state. 用于 Cast/Attack 派发时的"自动靠近", 不入 OrderQueue.
// 调用前可能 state 已存在 -- 我们直接覆盖.
//
// target 与起点的距离 < arrival_epsilon * cell_size 时视为"已到达", 不激活
// 移动状态. 调用方据此决定是否立即 consume 当前 Order.
void set_internal_move_path(Unit& self, MoveState& state, World* world, Vec2 target) {
    state             = MoveState{};
    const double dx = target.x - self.position().x;
    const double dy = target.y - self.position().y;
    const double cell = world ? world->nav_grid().cell_size() : 1.0;
    const double eps = pathfinding::MovementConfig::arrival_epsilon * cell;
    if (dx * dx + dy * dy <= eps * eps) {
        // 已经在终点附近, 不进入 active 状态; activate_front 会 consume 此 order.
        state.active = false;
        return;
    }
    state.destination = target;
    state.active      = true;
    // rough / smooth 留空 -> tick_movement 第一帧 A* 填充.
}

// cast 范围合法判定. point cast: distance <= range. unit cast: 加上目标 hull,
// 与 ability::validate_target 行为一致 (commit 4e62fd3).
bool in_cast_range_point(const Unit& caster, const Ability& ab, Vec2 pt) {
    const double r = ab.cast_range();
    if (r <= 0.0) return true;  // 无限程
    return distance_sq(caster.position(), pt) <= r * r;
}
bool in_cast_range_unit(const Unit& caster, const Ability& ab, const Unit& tgt) {
    const double r = ab.cast_range();
    if (r <= 0.0) return true;
    const double eff = r + tgt.hull_radius();
    return distance_sq(caster.position(), tgt.position()) <= eff * eff;
}

// 攻击距离合法判定: attack_range + 双方 hull. Dota 中 attack_range 从 hull 边
// 起算, 加上双方半径才是中心距离上限.
bool in_attack_range(const Unit& attacker, const Unit& target) {
    const double r = attacker.stats().attack_range
                   + attacker.hull_radius() + target.hull_radius();
    return distance_sq(attacker.position(), target.position()) <= r * r;
}

} // namespace

// 激活队首 Order: 派生 move_path / 立即施放无目标 cast / 立即清队等. 不需要每
// tick 调度的纯计算项立刻 pop. 需要等待 (move 走完, cast 完成, target 跟随) 的
// 项停在队首.
//
// 注意: 此处不调用 ability.order_cast -- 把派发统一交给 dispatch_front, 因为
// 距离判定 + 派生 move 与 issue_order 路径都需要. activate_front 只负责
// "刚入队那一刻的初始动作".
static void activate_front(Unit& self, std::deque<Order>& orders,
                            MoveState& move_path, World* world) {
    while (!orders.empty()) {
        Order& front = orders.front();
        bool consumed = false;
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OrderMoveToPoint>) {
                set_internal_move_path(self, move_path, world, v.point);
                // fill 后仍空 (target 与起点重合且 fill 没生成航点) -- 视作完成.
                if (move_path.empty()) consumed = true;
            } else if constexpr (std::is_same_v<T, OrderStop>) {
                move_path = {};
                consumed = true;
            } else {
                (void)v;  // Cast/Attack/MoveToUnit: 留给 dispatch_front 处理
            }
        }, front);
        if (!consumed) return;
        orders.pop_front();
    }
}

// 派发当前队首 (Cast / Attack / MoveToUnit): 距离够 -> 触发 ability.order_cast
// 并清掉 move_path; 否则派生 move_path 跟随目标. dispatched 标志保证 order_cast
// 只调一次 -- 之后等 ability.phase() 离开 Casting/Channelling -> 视为完成.
//
// 返回 true 表示队首应该被 pop (异常路径, 例如 ability_index 越界 / target 死亡).
static bool dispatch_front(Unit& self, std::deque<Order>& orders,
                            MoveState& move_path, World* world) {
    if (orders.empty() || world == nullptr) return false;
    Order& front = orders.front();
    bool pop = false;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OrderCastNoTarget>) {
            if (v.dispatched) return;
            Ability* ab = lookup_ability(self, v.ability_index);
            if (!ab) { pop = true; return; }
            CastTarget tgt;
            const CastError err = ab->order_cast(tgt, *world);
            if (err != CastError::None) { pop = true; return; }
            v.dispatched = true;
            move_path = {};  // 进入施法 -> 打断走位
        } else if constexpr (std::is_same_v<T, OrderCastPoint>) {
            if (v.dispatched) return;
            Ability* ab = lookup_ability(self, v.ability_index);
            if (!ab) { pop = true; return; }
            if (in_cast_range_point(self, *ab, v.point)) {
                CastTarget tgt;
                tgt.point = v.point;
                tgt.has_point = true;
                const CastError err = ab->order_cast(tgt, *world);
                if (err != CastError::None) { pop = true; return; }
                v.dispatched = true;
                move_path = {};
            } else {
                set_internal_move_path(self, move_path, world, v.point);
            }
        } else if constexpr (std::is_same_v<T, OrderCastTarget>) {
            if (v.dispatched) return;
            Ability* ab = lookup_ability(self, v.ability_index);
            if (!ab) { pop = true; return; }
            Unit* target = world->find(v.target);
            if (!target || !target->alive()) { pop = true; return; }
            if (in_cast_range_unit(self, *ab, *target)) {
                CastTarget tgt;
                tgt.unit = target;
                const CastError err = ab->order_cast(tgt, *world);
                if (err != CastError::None) { pop = true; return; }
                v.dispatched = true;
                move_path = {};
            } else {
                // 跟随移动: 每 tick 重新设置目标位置, 应对 target 移动.
                set_internal_move_path(self, move_path, world, target->position());
            }
        } else if constexpr (std::is_same_v<T, OrderAttackTarget>) {
            // 持续行为: 在 attack_range 内时, attack_cd<=0 且 can_attack 触发
            // 一次 resolve_attack; 否则派生跟随 move. target 死亡 / 消失 -> pop.
            Unit* target = world->find(v.target);
            if (!target || !target->alive()) { pop = true; return; }
            if (in_attack_range(self, *target)) {
                move_path = {};  // 已到位 -> 停下打
                if (self.can_attack() && self.attack_cd() <= 0.0) {
                    world->resolve_attack(self, *target);
                }
            } else {
                set_internal_move_path(self, move_path, world, target->position());
            }
        }
        // OrderMoveToPoint / OrderStop: activate_front 已处理.
        // OrderMoveToUnit: 暂未启用.
    }, front);
    return pop;
}

// 检测当前队首是否已完成. MoveTo 看 path 走完; Cast 看 dispatched 后 phase
// 是否离开 Casting/Channelling.
static bool front_complete(Unit& self, std::deque<Order>& orders,
                            MoveState& move_path) {
    if (orders.empty()) return false;
    Order& front = orders.front();
    bool done = false;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, OrderMoveToPoint>) {
            done = move_path.empty();
        } else if constexpr (std::is_same_v<T, OrderCastNoTarget> ||
                             std::is_same_v<T, OrderCastPoint> ||
                             std::is_same_v<T, OrderCastTarget>) {
            if (!v.dispatched) return;
            Ability* ab = lookup_ability(self, v.ability_index);
            if (!ab) { done = true; return; }
            const CastPhase p = ab->phase();
            // ability advance() 在 cast point / channel 完成后会进 Backswing /
            // OnCooldown / Ready. 这些都视为本 cast 项完成 -- 后摇期间允许新指令
            // 入队 (Dota: 后摇可被打断).
            done = (p != CastPhase::Casting && p != CastPhase::Channelling);
        }
        // OrderStop: 在 activate_front 已 pop, 不会到这.
    }, front);
    return done;
}

void Unit::issue_order(Order o, bool queue) {
    if (!queue) {
        orders_.clear();
        move_state_ = {};
    }
    // 覆盖模式 + OrderStop: 整队已清, 不入队.
    if (!queue && std::holds_alternative<OrderStop>(o)) return;
    const bool was_empty = orders_.empty();
    orders_.push_back(std::move(o));
    // 队列原本为空 (或刚被覆盖清掉) -> 立即激活新队首; 否则等队首走完后衔接.
    if (was_empty) {
        activate_front(*this, orders_, move_state_, world_);
        // 入队即派发一次, 让 NoTarget cast 在同一 tick 内立刻施放 (与现有
        // ability::order_cast 行为对齐). 距离不够的 Cast/Target 会派生 move,
        // 等待下个 tick 的 pump_orders. AttackTarget 不在此走 -- 与原
        // World::order_attack 行为一致, 第一次 swing 留到下一个 tick (这样
        // 测试在 issue 后再 subscribe events 仍能收到全部 attack_landed).
        if (!orders_.empty() &&
            !std::holds_alternative<OrderAttackTarget>(orders_.front())) {
            const bool pop = dispatch_front(*this, orders_, move_state_, world_);
            if (pop) {
                orders_.pop_front();
                activate_front(*this, orders_, move_state_, world_);
            }
        }
    }
}

void Unit::clear_orders() {
    orders_.clear();
    move_state_ = {};
}

void Unit::pump_orders() {
    // World 每 tick 在 motion controller 之后, tick_movement 之前调用.
    // 三个职责:
    //   1. 派发当前队首 (Cast 距离判定 / 派生跟随 move / 触发 order_cast)
    //   2. 检测队首是否已完成 -> pop + 激活下一条
    //   3. 异常 pop (ability_index 越界, target 死亡) 后衔接下一条
    constexpr int kMaxIters = 8;
    for (int i = 0; i < kMaxIters && !orders_.empty(); ++i) {
        const bool pop_dispatch = dispatch_front(*this, orders_, move_state_, world_);
        if (pop_dispatch) {
            orders_.pop_front();
            activate_front(*this, orders_, move_state_, world_);
            continue;
        }
        if (front_complete(*this, orders_, move_state_)) {
            orders_.pop_front();
            activate_front(*this, orders_, move_state_, world_);
            continue;
        }
        break;  // 队首仍在推进
    }
}

void Unit::issue_move(Vec2 target) {
    issue_order(OrderMoveToPoint{target});
}

void Unit::stop_move() {
    clear_orders();
}

std::optional<Vec2> Unit::move_target() const {
    if (move_state_.empty()) return std::nullopt;
    return move_state_.final_point();
}

} // namespace dota
