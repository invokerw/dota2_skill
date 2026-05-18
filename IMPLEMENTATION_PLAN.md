## 总目标

把现有的纯逻辑引擎可视化:

1. **Stage A** -- 在引擎关键路径上补齐 EventBus 事件 (技能 / 投射物 / 伤害 / 修饰器 / 单位 spawn-die-move).
2. **Stage C** -- 用 raylib 写实时渲染 demo, 直接读 `World` 状态.
3. **Stage B** -- 定义录像 schema, 写 Recorder 订阅 EventBus, 输出 JSONL.
4. **Stage D** (可选) -- raylib demo 加 `--replay` 模式.

每阶段都是一个可单独提交的独立增量, 完成即跑 `cmake --build build -j && ctest --test-dir build --output-on-failure`, 已有 125 个测试不能回归.

---

## Stage A: 引擎事件埋点

**Goal**: 在 [include/dota/core/world.hpp](include/dota/core/world.hpp) 增加事件结构体, 并在引擎关键路径 publish 它们. 仅扩展, 不改既有行为.

**Success Criteria**:
- 已有 125 个测试全部通过, 行为不变.
- 新增一个测试 `tests/test_event_emission.cpp`, 订阅每个新事件并断言肉钩场景 (Pudge -> Lina) 触发了 ProjectileSpawned -> ProjectileHit -> DamageApplied -> ModifierAdded 序列.
- `examples/duel.cpp` 不需要改动.

**Status**: Not Started

### 新增事件 (放在 `include/dota/core/world.hpp` 现有事件下方)

```cpp
struct UnitSpawnedEvent       { EntityId id; };
struct UnitMovedEvent         { EntityId id; Vec2 from; Vec2 to; };       // 仅在 set_position 改值时
struct AbilityCastStartedEvent{ EntityId caster; std::string ability; CastTarget target; };
struct AbilityCastFinishedEvent{ EntityId caster; std::string ability; bool interrupted; };
struct ProjectileSpawnedEvent { EntityId pid; EntityId source; Vec2 origin;
                                Vec2 dir; double speed; double length; double width;
                                bool tracking; EntityId target; };
struct ProjectileHitEvent     { EntityId pid; EntityId victim; Vec2 point; };
struct ProjectileFinishedEvent{ EntityId pid; };
struct ModifierAddedEvent     { EntityId unit; std::string name; double duration; int stacks; };
struct ModifierRemovedEvent   { EntityId unit; std::string name; };
struct DamageAppliedEvent     { EntityId attacker; EntityId victim;
                                DamageType type; double amount_pre; double amount_applied;
                                std::uint32_t flags; };
struct HealAppliedEvent       { EntityId healer; EntityId target; double amount; };
```

`AttackLandedEvent` 和 `UnitDiedEvent` 已存在, 复用.

### 埋点位置

| 事件 | 文件:位置 | 新加几行 |
|---|---|---|
| `UnitSpawnedEvent` | [src/core/world.cpp:33](src/core/world.cpp#L33) `World::spawn` 末尾 | publish |
| `UnitMovedEvent` | `Unit::set_position` (在 [include/dota/core/unit.hpp:54](include/dota/core/unit.hpp#L54)) -- 改成调用 `world_->events()` 的内联实现需要 .cpp 化 | 需要把 `set_position` 移到 [src/core/unit.cpp](src/core/unit.cpp), 比较位置, 改了再 publish. **注意**: motion controller / projectile 每 tick 都会大量调用, 不能造成 N 次 publish. → 改方案: 不在 set_position 里 publish, 改由 Stage B 的 Recorder 在每 tick 末尾扫描所有 unit 的 position 增量 (Recorder 自己保存上一 tick 的 pos 字典). Stage A 不需要 UnitMovedEvent. **从 schema 删除该事件.** |
| `AbilityCastStartedEvent` | [src/ability/ability.cpp:177](src/ability/ability.cpp#L177) `enter_phase(Casting)` 之前 / 当 cast_point<=0 立即触发分支也要发 | 需要 `Ability::world_` 拿 EventBus -- 已有 |
| `AbilityCastFinishedEvent` | [src/ability/ability.cpp](src/ability/ability.cpp) `interrupt`, backswing 结束, on_spell_start 完成进入 OnCooldown 三处 | publish |
| `ProjectileSpawnedEvent` | [src/projectile/manager.cpp:9](src/projectile/manager.cpp#L9) `spawn` -- 但 manager 没拿到 World&, 要改签名 | 改 `spawn(unique_ptr<Projectile>, World&)` 或在 `World::projectiles().spawn(...)` 包装. **简化**: 直接给 `Projectile` 基类加 `EntityId pid_`, `pid` 由 manager 在 `spawn()` 里分配 (内置静态 counter 或 manager 成员); 然后 manager 在 spawn 里 publish 需要 World&, 所以给 `ProjectileManager` 加 `World* world_` 反指针 (在 World 构造时 set), 或者把 publish 推迟到 advance 第一次 tick (引入"未发布"标志). **方案**: 给 manager 加 `World* owner_`, `World` 构造时 `projectiles_->set_world(this)`. |
| `ProjectileHitEvent` | [src/projectile/projectile.cpp:42](src/projectile/projectile.cpp#L42) (Linear) 和 [src/projectile/projectile.cpp:90](src/projectile/projectile.cpp#L90) (Tracking) `on_hit_` 调用前 | publish via `world.events()` -- advance 已有 World& |
| `ProjectileFinishedEvent` | 上述两个 .cpp 中所有 `on_finish_` 之前 | publish |
| `ModifierAddedEvent` | [src/modifier/manager.cpp:15](src/modifier/manager.cpp#L15) `attach` 末尾 + [src/modifier/manager.cpp:46](src/modifier/manager.cpp#L46) `attach_motion` 末尾 | publish. **问题**: ModifierManager 没有 World*. → 通过 `owner_.world()` 取得. 当 owner 在 World 外 (单元测试) 时跳过 publish. |
| `ModifierRemovedEvent` | [src/modifier/manager.cpp:61](src/modifier/manager.cpp#L61) `remove`, [src/modifier/manager.cpp:71](src/modifier/manager.cpp#L71) `remove_all`, [src/modifier/manager.cpp:96](src/modifier/manager.cpp#L96) expire 块 | publish |
| `DamageAppliedEvent` | [src/combat/damage.cpp:101](src/combat/damage.cpp#L101) 应用生命值变化之后 | publish via `victim->world()->events()` |
| `HealAppliedEvent` | [src/combat/damage.cpp:138](src/combat/damage.cpp#L138) | publish |

### 已知风险与对策

- **set_position 不发事件**: 见上, Stage B 的 Recorder 用差分快照解决, schema 把 UnitMoved 划归"由 Recorder 合成的事件".
- **ModifierManager / Ability 拿 EventBus 的途径**: 都通过 `owner / caster -> world() -> events()`. 测试里直接 new Unit 不挂 World 的, 只要 `world() == nullptr` 就跳过, 已有测试不破.
- **Projectile id 分配**: `ProjectileManager` 内 `EntityId next_pid_{1}`. Projectile 基类增加 `EntityId pid_{kInvalidEntityId}`, manager 在 `spawn()` 里赋值并发事件.

### 测试 (新增 `tests/test_event_emission.cpp`)

只测一个完整链路: 用 Pudge YAML 装载肉钩, Pudge 锁定 Lina, advance 直到 hook 命中, 收集所有事件, 断言:
- 至少 1 条 `AbilityCastStartedEvent` (ability="pudge_meat_hook")
- 至少 1 条 `ProjectileSpawnedEvent`
- 至少 1 条 `ProjectileHitEvent` victim==Lina.id()
- 至少 1 条 `DamageAppliedEvent` victim==Lina.id() amount_applied>0
- 至少 1 条 `ModifierAddedEvent` (motion knockback 修饰器)
- 序列顺序: Cast < Spawn < Hit < Damage

### Tests 命令

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

---

## Stage C: raylib 实时渲染 demo

**Goal**: 一个新的可执行 `duel_visual`, 在窗口里实时绘制 `examples/duel.cpp` 的同一场战斗. 不录像, 不重放, 直接每帧 `world.advance(dt)` + 读 World 状态绘制.

**Success Criteria**:
- `cmake --build build -j --target duel_visual` 成功.
- `./build/duel_visual` 打开窗口, 显示 6 个英雄 + HP 条, 自动开战, 看见肉钩飞行 + 拖拽 + 伤害飘字 + 单位死亡淡出.
- 已有测试不变.

**Status**: Not Started

### 依赖引入

在 [CMakeLists.txt](CMakeLists.txt) CPM 段末尾追加:

```cmake
CPMAddPackage(
    NAME raylib
    GITHUB_REPOSITORY raysan5/raylib
    GIT_TAG 5.0
    OPTIONS
        "BUILD_EXAMPLES OFF"
        "WITH_PIC ON"
)
```

新建 `add_executable(duel_visual examples/duel_visual.cpp)`, link `dota_core` + `raylib`, 设 `DOTA_DATA_DIR`.

### 渲染设计

- 世界坐标直接是引擎里的 Vec2 (不缩放), 视图按窗口分辨率自动 fit (求所有单位包围盒, 留 200 px 边距, 算 zoom).
- 60 fps 实时渲染, 每帧调 `world.advance(GetFrameTime())` -- World 内部细分为 30 Hz tick, 不需要我们管.
- 元素:
  | 元素 | 数据来源 | 视觉 |
  |---|---|---|
  | 单位 | `world.units_on_team(...)` | 圆 (天辉绿 / 夜魇红 / 中立灰), 半径 30; 死亡变灰 |
  | HP 条 | `unit.health() / unit.max_health()` | 圆上方矩形 |
  | 名字 | `unit.name()` | HP 条上方小字 |
  | 投射物 | `world.projectiles()` -- **需要 manager 暴露 const &live_** | 黄线段 (linear) / 黄圆 (tracking). 需要从 `Projectile` 基类暴露 `pos()`. |
  | 修饰器图标 | `unit.modifiers().all()` | 单位下方一行小方块, 颜色按 is_debuff |
  | 施法读条 | 遍历 ability, phase==Casting | 单位下方蓝条 (1 - phase_timer/cast_point) |
  | 伤害飘字 | 订阅 `DamageAppliedEvent`, push 到本地队列 (text+pos+expire) | 红字向上飘 1 秒 |

### 需要让引擎暴露的小接口

- `ProjectileManager::live() const -> const std::vector<unique_ptr<Projectile>>&` (现已有 `live_count`, 加一个 `live()` getter).
- `Projectile::position() const -> Vec2` 纯虚或基类存 `Vec2 pos_` 让子类 update.
  - 现状: `LinearProjectile` 有 `pos_`, `TrackingProjectile` 有 `pos_`. 改基类: `protected: Vec2 pos_; public: Vec2 position() const { return pos_; }`. 子类构造里通过基类直接初始化.
- `Projectile::is_linear() const`, `Projectile::direction() const`, `Projectile::width() const` 仅 LinearProjectile 用 -- 给 base 加 `virtual` 默认返回值.

### duel_visual.cpp 大致结构 (约 250 行)

```cpp
// 设置 raylib 窗口 1280x720
// 复用 examples/duel.cpp 的英雄装载 / 排兵代码 (考虑提取一个 helper, 但保持简单先复制)
// 订阅 DamageAppliedEvent / UnitDiedEvent 推 floating_texts
// 主循环:
//   dt = GetFrameTime() (capped to 0.1)
//   if !paused: world.advance(dt)
//   BeginDrawing / camera transform / 绘制单位 / 投射物 / 修饰器图标 / 飘字 / EndDrawing
// 键: SPACE 暂停, R 重开 (重新构建 World)
```

### 风险

- raylib 在 macOS Apple Silicon 上 CPM 编译: 已知 5.0 + CMake 3.20+ OK, 不需要额外配置.
- 字体: 用 raylib 默认字体, 不引入额外字体文件.

### Tests

不写 GTest (UI 改动测不动). 手动验收:
- 跑 `./build/duel_visual`, 看到肉钩从 Pudge 飞出击中并拖回 Lina.
- 看到伤害飘字.
- 6 个英雄都能死, 死亡变灰.

---

## Stage B: 录像 schema + Recorder

**Goal**: 定义 JSONL 录像格式; 写 `Recorder` 订阅 Stage A 的所有事件, 输出文件; duel.cpp 加 `--record path` 选项.

**Success Criteria**:
- `./build/duel --record /tmp/duel.jsonl` 跑完产生有效 JSONL.
- 文件第一行是 header (含 schema_version, kTickRate, hero list); 之后每个事件一行.
- `tests/test_recorder.cpp`: 加载肉钩场景, record, 重新解析 JSONL, 断言事件计数 / 关键字段非空.

**Status**: Not Started

### Schema (写到 [doc/recording_schema.md](doc/recording_schema.md))

每行一条 JSON 对象, 字段约定:

```jsonc
// 第 0 行: header
{"v":1,"tick_rate":30,"started_at":"2026-05-18T12:00:00Z",
 "units":[{"id":1,"name":"Pudge","team":1,"max_hp":1500,"max_mana":280,"pos":[0,0]},...]}

// 之后每行: tick frame, 事件按时间顺序
{"t":0.033,"events":[
  {"type":"unit_moved","id":1,"to":[3.2,0]},
  {"type":"cast_start","caster":1,"ability":"pudge_meat_hook","point":[600,0]},
  {"type":"projectile_spawn","pid":7,"src":1,"origin":[0,0],"dir":[1,0],
   "speed":1300,"length":1300,"width":100,"tracking":false},
  {"type":"projectile_hit","pid":7,"victim":4,"point":[598,0]},
  {"type":"damage","src":1,"dst":4,"dtype":"magical","pre":225,"applied":169},
  {"type":"modifier_add","unit":4,"name":"motion_knockback","duration":0.46},
  {"type":"projectile_finish","pid":7},
  {"type":"unit_died","id":4,"killer":1}
]}
```

字段速查:

| type | 必填字段 |
|---|---|
| `unit_spawn` | id, name, team, max_hp, max_mana, pos |
| `unit_moved` | id, to (合成事件: Recorder 在每 tick 末尾比对 prev_pos, 变化才输出) |
| `unit_died` | id, killer |
| `cast_start` | caster, ability, target_id?(unit target) / point?(point target) |
| `cast_finish` | caster, ability, interrupted |
| `projectile_spawn` | pid, src, origin, dir, speed, length?, width?, tracking, target?(tracking only) |
| `projectile_hit` | pid, victim, point |
| `projectile_finish` | pid |
| `modifier_add` | unit, name, duration, stacks |
| `modifier_remove` | unit, name |
| `damage` | src, dst, dtype, pre, applied |
| `heal` | src, dst, amount |
| `attack_landed` | src, dst, dmg, missed |

类型枚举字符串: `dtype` ∈ `physical|magical|pure`; `team` ∈ `0|1|2` (复用 Team 数值).

时间 `t` 单位秒, 起点 0 = world spawn 完毕. 位置直接是引擎 Vec2 双精度数, 不做缩放.

### 实现

新增 `include/dota/replay/recorder.hpp` + `src/replay/recorder.cpp`:

```cpp
class Recorder {
public:
    Recorder(World& world, std::ostream& out);  // 构造时订阅所有事件
    ~Recorder();
    void write_header(const std::string& started_at);
    void flush_tick();   // 每 tick 末尾把累积的 events 写一行 JSON
private:
    // event handlers append to current_frame_events_
    // 还要维护 prev_positions_ -> 在 flush_tick 里合成 unit_moved 事件
};
```

JSON 输出**手写**, 不引依赖 -- 字段固定, 几个 helper 拼字符串够用. 需要的话 200 行内.

CMakeLists.txt 把 `src/replay/recorder.cpp` 加进 dota_core.

duel.cpp 用 `argc/argv` 解析 `--record`, 在 World 构造后立刻 new Recorder, 主循环每次 advance 后调 `recorder->flush_tick()`. **问题**: World::advance 内部 30 次 tick, Recorder 该不该在每次 tick_once 末尾 flush? 应该: 在 World 增加一个"tick 边界"事件或者每 tick `advance` 1 帧 (duel.cpp 改成手动 30 fps 循环). **决定**: duel.cpp 改成 `for (int i = 0; i < total_ticks; ++i) { world.advance(World::kTickDt); recorder->flush_tick(); }`.

### Tests

`tests/test_recorder.cpp`:
- 模拟 1 秒肉钩场景, record 到 stringstream.
- 拆行解析 (粗解析: 找 `"type":"projectile_hit"` 子串就够了, 不写完整 JSON parser).
- 断言: header 出现, projectile_spawn / hit / damage / modifier_add 都出现.

---

## Stage D: raylib --replay 模式 (可选, 视前面进度决定)

**Goal**: `duel_visual --replay file.jsonl` 不跑 World, 直接读文件按时间戳重放.

**Success Criteria**:
- `./build/duel --record /tmp/d.jsonl` 然后 `./build/duel_visual --replay /tmp/d.jsonl` 视觉效果与实时模式接近一致 (位置 / 投射物 / 死亡).

**Status**: Not Started

### 实现

加 `include/dota/replay/player.hpp`: 简单 JSONL 解析器 (够用即可, 字段固定), 维护 `unordered_map<EntityId, UnitView>` + `unordered_map<EntityId, ProjectileView>`. duel_visual 抽出 `RenderState` 结构, 让 live 模式和 replay 模式都填这个结构再绘制.

跳过实现细节, 等 A/B/C 跑通再回来定.

---

## 执行顺序与提交节奏

按 A → C → B → D, 每个阶段一个 commit:
1. `feat: 引擎事件埋点 (Stage A)` -- 包含 world.hpp 事件结构 + 各 .cpp publish + 新测试.
2. `feat: raylib 实时渲染 demo (Stage C)` -- CMake 引入 raylib, 新增 duel_visual.
3. `feat: 录像 schema + Recorder (Stage B)` -- doc + replay/ 目录 + duel --record + 新测试.
4. (可选) `feat: replay 模式 (Stage D)`.

每次 commit 前必跑 `cmake --build build -j && ctest --test-dir build --output-on-failure`.
