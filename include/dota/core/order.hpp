#pragma once

#include "dota/core/types.hpp"

#include <variant>

namespace dota {

// 单位指令队列(PlayerOrder)的指令类型. 参考 Dota 2 PlayerOrder:
// 玩家所有的"走/打/放"操作都进同一个 FIFO 队列, 由 World::tick_orders
// 在 modifier / motion controller 之后, tick_movement / ability tick 之前
// 派发. 队列由 Unit 持有(`std::deque<Order>`).
//
// Stage 1 仅落地 OrderMoveToPoint / OrderStop; 其余类型在 Stage 3 / Stage 4
// 启用. 提前定义全部 variant alternatives 让后续 stage 不需要回头改 header.

// 走到指定地点
struct OrderMoveToPoint  { Vec2 point; };

// 跟随单位 (Stage 3 用作 cast target / Stage 4 用作 attack 派生跟随的内部表示)
struct OrderMoveToUnit   { EntityId target; };

// 走到 attack_range 再 a (Stage 4)
struct OrderAttackTarget { EntityId target; };

// 立即施放无目标技能 (Stage 3). dispatched=true 表示已调过 ability.order_cast,
// 在等待 ability.phase() 离开 Casting/Channelling 后 pop.
struct OrderCastNoTarget { int ability_index; bool dispatched = false; };

// 走到 cast_range 再向 point 施放 (Stage 3)
struct OrderCastPoint    { int ability_index; Vec2 point; bool dispatched = false; };

// 走到 cast_range 再向 unit 施放 (Stage 3)
struct OrderCastTarget   { int ability_index; EntityId target; bool dispatched = false; };

// 立即清空队列, 停在原地
struct OrderStop         {};

using Order = std::variant<OrderMoveToPoint,
                           OrderMoveToUnit,
                           OrderAttackTarget,
                           OrderCastNoTarget,
                           OrderCastPoint,
                           OrderCastTarget,
                           OrderStop>;

} // namespace dota
