# 单位指令队列 (PlayerOrder)

## 总目标

引入 Dota 2 风格的**单位指令队列** (PlayerOrder), 把目前并行的三个独立通道 (issue_move / order_attack / order_cast) 统一到一个 FIFO 队列下. 让玩家可以:

- 用 `S` 键打断当前所有动作.
- 越界施法时单位自动靠近到 `cast_range`, 进入范围立即施放.
- 施法生效那一瞬清掉派生的 move_path, 单位停下读条 (移动被施法打断).
- 施法被中断时清空队列 (Dota 默认).
- 后续 (Stage 5) 用 `Shift` 追加多步指令.

设计参考 Dota 2 PlayerOrder: 每个 Unit 持有 `std::deque<Order>`, 每 tick 在 motion controller 之后, tick_movement 之前由 `World::tick_orders` 派发. `move_path_` 由 OrderQueue 内部驱动, 不再被外部直写.

## 数据结构

新增 [include/dota/core/order.hpp](include/dota/core/order.hpp):

```cpp
struct OrderMoveToPoint  { Vec2 point; };
struct OrderMoveToUnit   { EntityId target; };   // 跟随
struct OrderAttackTarget { EntityId target; };   // 走到 attack_range 再 a
struct OrderCastNoTarget { int ability_idx; };
struct OrderCastPoint    { int ability_idx; Vec2 point; };
struct OrderCastTarget   { int ability_idx; EntityId target; };
struct OrderStop         {};                     // 清空队列, 单位站住

using Order = std::variant<OrderMoveToPoint, OrderMoveToUnit,
                           OrderAttackTarget, OrderCastNoTarget,
                           OrderCastPoint, OrderCastTarget, OrderStop>;
```

Unit 上新增:

```cpp
void issue_order(Order o, bool queue = false);   // queue=false 覆盖, true 追加
void clear_orders();
const std::deque<Order>& orders() const;
const Order* current_order() const;              // 队首; 空队 nullptr
```

## Stage 1: OrderQueue 骨架 + MoveToPoint 迁移

**Goal**: 引入数据结构, 把 `Unit::issue_move` 重路由到 OrderQueue. 行为不变.

**Success Criteria**:

- 新增 `Unit::issue_order` / `clear_orders` / `orders()` / `current_order()` 接口.
- `Unit::issue_move(p)` = `issue_order(OrderMoveToPoint{p})`. Lua 绑定 / skill_tester / 现有测试不动.
- `Unit::stop_move()` = `issue_order(OrderStop{})` (语义等价 -- 清队 + 清 move_path).
- `World::tick_orders(dt)` 在 modifier tick 后, motion controller 后, tick_movement 前推进队列:
  - `OrderMoveToPoint`: 队首存在则保证 move_path 指向该点; tick_movement 走完即 pop. (即 OrderQueue 派生 move_path, tick_movement 不变)
  - `OrderStop`: 清空 move_path 后立即 pop, 队列继续派发下一条.
- 现有 125+ 测试全过.

**Status**: Complete

### Stage 1 改动

- 新增 [include/dota/core/order.hpp](include/dota/core/order.hpp) -- variant + 类型定义.
- [include/dota/core/unit.hpp](include/dota/core/unit.hpp): 加 `std::deque<Order> orders_`, 接口 `issue_order` / `clear_orders` / `orders()` / `current_order()`. `issue_move` / `stop_move` 重写为薄包装.
- [src/core/unit.cpp](src/core/unit.cpp): 实现新接口. `issue_move` 内部调 `issue_order`.
- [include/dota/core/world.hpp](include/dota/core/world.hpp): 声明 `tick_orders(double dt)`.
- [src/core/world.cpp](src/core/world.cpp): 实现 `tick_orders`, 在 `tick_once` 里 motion controller 之后, `tick_movement` 之前调用. 派发 `OrderMoveToPoint` (确保 move_path 指向 point) 与 `OrderStop` (清队).
- 注意: `move_path` 走完时由 `tick_movement` 清空 -- `tick_orders` 检测 `move_path_.empty()` 来判定 MoveTo 已完成, pop 队首.

### Stage 1 测试

新增 [tests/test_order_queue.cpp](tests/test_order_queue.cpp):

- `MoveToPointEquivalentToIssueMove`: issue_move 与 issue_order(OrderMoveToPoint) 走的轨迹相同.
- `OverrideClearsQueue`: queue=false 覆盖整队.
- `AppendKeepsCurrent`: queue=true 追加, 当前指令不变.
- `MoveToPointPopsOnArrival`: 走到目的地后 current_order 为 nullptr.

## Stage 2: OrderStop + skill_tester S 键

**Goal**: 玩家可以用 `S` 键中断当前所有动作.

**Success Criteria**:

- skill_tester: 按 `S` 键发 `OrderStop`. 当前移动 / 后续队列全部清空.
- 帮助文字加上 `S stop`.
- 单位施法的 cast point 期间按 S **不打断**当前 ability (Dota 行为: stop 不打断已开始施法的 ability), 仅清掉 OrderQueue 后续待处理项. 这条留作 Stage 3 的施法落地后验证.

**Status**: Complete

### Stage 2 改动

- [examples/skill_tester.cpp](examples/skill_tester.cpp): 在键盘分支处加 `IsKeyPressed(KEY_S)` -> `caster->issue_order(OrderStop{})`. 提示文字更新.

### Stage 2 测试

新增用例 (test_order_queue.cpp):

- `StopClearsQueue`: 走到一半发 OrderStop, 立即停, current_order 为 nullptr, move_target 为空.

## Stage 3: 自动靠近的施法 + 移动打断

**Goal**: 实现 `OrderCast{NoTarget,Point,Target}`, 距离不够先走, 施法时打断走位, 中断时清队.

**Success Criteria**:

- `OrderCastPoint` / `OrderCastTarget`: 派发时检查 `caster.position` 与目标距离:
  - 距离 ≤ `cast_range + caster.hull_radius()`(target 单位时再加 target.hull_radius) -> 调 `ability.order_cast`. 进入 cast point 那一帧 `clear_move_path()` (打断走位, 但 OrderQueue 保留这条 cast 项, 用于后续 pop).
  - 否则: 派生一条内部 move 到合法施法点 (point cast 走向 point, target cast 跟随 target.position). 不消费队首, 直到进入范围才尝试 cast.
- `OrderCastNoTarget`: 立即 `ability.order_cast` (cast_range=0, 不需靠近).
- 当前 cast 项的"完成"判定: `ability.phase()` 离开 Casting/Channelling 进入 Backswing/OnCooldown/Ready -> pop 队首.
- 中断 (Stun/Silence/Hex/target 死亡, 即 ability 在 advance() 内走 interrupt 分支) -> `clear_orders()` (Dota 默认).
- skill_tester 越界点击不再返回 `CastError::OutOfRange` toast: `try_cast` 改为 `issue_order(OrderCast...)` -> 单位自动走过去.
- target cast 跟随: target 移动则 OrderQueue 持续刷新派生 move 到新位置.

**Status**: Complete

### Stage 3 改动

- [include/dota/core/order.hpp](include/dota/core/order.hpp): 给 `OrderCastNoTarget` / `OrderCastPoint` / `OrderCastTarget` 各加一个 `bool dispatched = false` 标志, 防止 `dispatch_front` 重复调 `ability.order_cast`.
- [src/core/unit.cpp](src/core/unit.cpp): 引入三个内部静态函数, 取代了原计划放在 `World::tick_orders` 的派发逻辑:
  - `activate_front`: 队首"刚入队那帧"的初始动作 (MoveToPoint 派生 move_path; OrderStop 立即 pop 然后继续推).
  - `dispatch_front`: 每 tick 派发当前队首. Cast 三型按 `in_cast_range_*` 检距; 在范围内调 `ab->order_cast`, 设 `dispatched=true`, **同时清掉 move_path** (打断走位); 否则 `set_internal_move_path` 派生跟随 move (不入 OrderQueue, 防递归).
  - `front_complete`: 队首是否已完成 (MoveTo 看 path 空; Cast 看 phase 离开 Casting/Channelling).
  - `Unit::pump_orders` 替代了 `World::tick_orders`: 8-iter 循环 dispatch + 完成检测, pop 后衔接下一条.
  - `issue_order`: 队空时立即 `activate_front` + 一次 `dispatch_front`, 让无目标 / 范围内 cast 在同 tick 进入 Casting (与原 `ability::order_cast` 行为对齐).
- [src/core/world.cpp](src/core/world.cpp) `tick_once`: 在 `tick_movement` 之后调 `tick_units([&](Unit& u) { u.pump_orders(); })`.
- [src/ability/ability.cpp](src/ability/ability.cpp) `advance` interrupt lambda: 在 `publish_cast_finished` 之后调 `caster_.clear_orders()` -- Dota 默认: cast 被中断 -> 清整队.
- [examples/skill_tester.cpp](examples/skill_tester.cpp) `try_cast`: 改为先调 `can_cast` 拦"魔不够 / 已死 / silence"等本地失败 (这些不该派生跟随 move), 距离不够 (`OutOfRange`) 走 OrderQueue 自动靠近. 取得 ability 在 `caster->abilities().all()` 中的 index 并构造对应 `OrderCast*` 入队.

### Stage 3 测试

新增 ([tests/test_order_queue.cpp](tests/test_order_queue.cpp)):

- `CastTargetAutoApproachAndFires`: caster (0,0) 移速 600 -> target (1200,0); cast_range=625. issue_order 后单位自动跟随, 进入范围才进入 Casting, 进入 Casting 那刻 move_path 被清, 推完 cast_point 后 target 实际掉血.
- `CastInRangeFiresImmediately`: caster 与 target 距离 100 (<<625), `issue_order` 入队即派发, 同 tick 进入 Casting.
- `CastInterruptedClearsQueue`: 队列 [Cast, Move]. Casting 期间上 stun -> ability interrupt -> queue 整队清空, 不衔接第二条 Move.
- `CastTargetFollowsMovingTarget`: target 边跑边被追, lion 派生 move 每 tick 刷新到 target 当前位置, 最终位置 y 偏离 0 证明跟随生效.
- `CastTargetDeadPopsAndContinues`: 跟随期间 target 死亡 -> cast 项 pop, 队列衔接到下一条 MoveToPoint.

## Stage 4: AttackTarget 迁移

**Goal**: 把 `World::order_attack` 移到 OrderQueue, 攻击距离 + hull_radius 自动靠近. 删除全局 `orders_` vector.

**Success Criteria**:

- `OrderAttackTarget{tid}`: tick_orders 派发:
  - target 死亡 / 不存在 -> pop, 继续下一条.
  - 距离 > `attack_range + caster.hull + target.hull` -> 派生跟随 move (同 cast target 处理).
  - 在范围且 `attack_cd <= 0` -> 调 `resolve_attack`.
  - 不 pop -- AttackTarget 是持续行为, 直到 target 死亡或被新指令覆盖.
- `World::order_attack(a, t)` 改为 `a.issue_order(OrderAttackTarget{t.id()})`, 保留以便不大改测试. (但你确认改全部测试, 因此一并改掉调用点.)
- `World::stop_attack(a)` 改为 `a.issue_order(OrderStop{})` 或保留为内部 helper -- 直接删除, 调用点改用 OrderStop.
- 移除 `World::orders_` 字段与 `AttackOrder` 结构, `tick_once` 末尾原 `orders_` 处理段落删掉.
- duel.cpp + 5 处测试调用点全部改为 OrderAttackTarget. dummy AI Charge 模式同样改用此指令.

**Status**: Complete

### Stage 4 改动

- [include/dota/core/world.hpp](include/dota/core/world.hpp): 删除 `order_attack` / `stop_attack` / `AttackOrder` 结构 / `orders_` 字段. `resolve_attack` 改为 public, 由 `dispatch_front` 在 unit.cpp 调用.
- [src/core/world.cpp](src/core/world.cpp): 删除 `tick_once` 末尾整段 `orders_` 处理 (snapshot 遍历 + 死亡清理). 把 `pump_orders` tick 调用挪到 `tick_attack_cd` 之后, 让 AttackTarget 派发能看到本 tick 已归零的 cd. `resolve_attack` 中 target 死亡分支由 `stop_attack(target)` 改为 `target.clear_orders()`.
- [src/core/unit.cpp](src/core/unit.cpp) `dispatch_front` 加 `OrderAttackTarget` 分支: target 死亡 -> pop; 在 `attack_range + 双 hull` 内 -> 清 move_path + (cd<=0 && can_attack) 调 `world->resolve_attack`; 否则派生跟随 move. **持续行为**, 不 pop. 新增内部 helper `in_attack_range`.
- `Unit::issue_order` 跳过 `OrderAttackTarget` 的入队即派发 -- 与原 `World::order_attack` 行为对齐 (第一次 swing 留到下个 tick, 这样调用方在 issue 后再 subscribe 也能收到).
- [examples/duel.cpp](examples/duel.cpp): 3 处 `world.order_attack` -> `unit->issue_order(OrderAttackTarget{...})`.
- [examples/skill_tester.cpp](examples/skill_tester.cpp) Charge 模式: 改为 `d->issue_order(OrderAttackTarget{caster->id()})`. 同一目标避免重复 issue (用 current_order + std::get_if 判定).
- [tests/test_unit_basic.cpp](tests/test_unit_basic.cpp), [tests/test_modifier_state.cpp](tests/test_modifier_state.cpp): 4 处 `w.order_attack(...)` 改为 `unit->issue_order(OrderAttackTarget{...})`.

### Stage 4 测试

新增 ([tests/test_order_queue.cpp](tests/test_order_queue.cpp)):

- `AttackTargetAutoApproach`: 600 距离, attack_range=150 -> 单位先走到 attack_range 内才发出第一次 attack_landed (订阅函数验证 attacker 在攻击范围内).
- `AttackTargetPersistsAcrossSwings`: 一次 issue_order, 推 3.5s 多次 attack_landed; current_order 仍是 OrderAttackTarget (持续行为不 pop).
- `AttackTargetPopsWhenTargetDies`: attack_damage 一击必杀, 后接 MoveToPoint -> target 死亡 -> attack 项 pop, 衔接到 MoveToPoint.
- `AttackTargetStopOverridesQueue`: 攻击中 issue OrderStop -> 整队清空, 后续 attack_landed 计数为 0.

修改: `tests/test_unit_basic.cpp` (3 处) + `tests/test_modifier_state.cpp` (1 处) 中的 `w.order_attack(...)` 调用.

## Stage 5: shift 队列追加 + skill_tester UI

**Goal**: skill_tester 支持 shift 追加, 画出队列航点.

**Success Criteria**:

- Shift+RMB = `issue_order(OrderMoveToPoint{p}, /*queue=*/true)`.
- 技能瞄准时 Shift+LMB = 追加该 cast 指令.
- 战场上画一条虚线把 caster 当前位置 → 队列里所有航点串起来 (debug overlay), 让玩家看到队列状态.
- 帮助文字: `LMB cast   RMB move   S stop   Shift queue`.

**Status**: Not Started

### Stage 5 改动

- [examples/skill_tester.cpp](examples/skill_tester.cpp):
  - 检测 `IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)` -> queue 标志.
  - RMB / LMB 分支根据 queue 标志选 issue_order 第二参数.
  - 新增渲染: 遍历 `caster->orders()`, 取每条 Order 的"目的点" (Move 用 point, MoveToUnit/AttackTarget/CastTarget 用 target 当前位置, CastPoint 用 point), 画虚线段 + 序号标签.

### Stage 5 测试

[tests/test_skill_tester_smoke.cpp](tests/test_skill_tester_smoke.cpp) 增加: 模拟 shift+RMB 序列, 断言 orders().size() == 期望值.

## 提交节奏

每个 stage 一个 commit, 每次跑全量 ctest:

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

1. `feat: 指令队列 Stage 1 -- OrderQueue 骨架 + MoveToPoint 迁移`
2. `feat: 指令队列 Stage 2 -- OrderStop + skill_tester S 键`
3. `feat: 指令队列 Stage 3 -- 自动靠近施法 + 移动打断 + 中断清队`
4. `feat: 指令队列 Stage 4 -- AttackTarget 迁移, 移除 World::orders_`
5. `feat: 指令队列 Stage 5 -- Shift 追加 + 队列可视化`
