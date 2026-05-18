# 录像 (Replay) JSONL Schema -- v1

本文件定义引擎导出的录像格式. 文件后缀建议 `.jsonl` (JSON Lines): **每行一条独立 JSON 对象**, 第一行为 header, 之后每行为一帧 (frame).

设计原则:

- **全量快照**: 每个 tick 输出一帧, 即使没有事件也输出 (含位置差分, 用于回放定位).
- **原始坐标**: 位置直接是引擎 Vec2 双精度数, 不缩放, 渲染端自行投影.
- **顺序即真相**: tick 内事件按发布顺序写入 events 数组.
- **无第三方 JSON 依赖**: 字段固定, 引擎手写序列化.

## Header (第 0 行)

```json
{
  "v": 1,
  "tick_rate": 30,
  "started_at": "2026-05-18T12:00:00Z",
  "scenario": "duel"
}
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `v` | int | schema 版本; 当前 `1` |
| `tick_rate` | int | 每秒 tick 数 (= `World::kTickRate`) |
| `started_at` | string | ISO8601 时间戳, 录制起点 |
| `scenario` | string | 场景标签, 仅用于显示 |

## Frame (其余每行)

```json
{
  "t": 0.033,
  "tick": 1,
  "positions": [[1, 0.0, 0.0], [2, 500.0, 0.0]],
  "events": [
    {"type": "cast_start", "caster": 1, "ability": "pudge_meat_hook", "point": [1300, 0]},
    {"type": "projectile_spawn", "pid": 7, "src": 1, "origin": [0, 0], "dir": [1, 0], "speed": 1300, "length": 1300, "width": 100, "tracking": false},
    {"type": "projectile_hit", "pid": 7, "victim": 4, "point": [598, 0]},
    {"type": "damage", "src": 1, "dst": 4, "dtype": "magical", "pre": 125, "applied": 93}
  ]
}
```

| 字段 | 类型 | 含义 |
|---|---|---|
| `t` | float | 累计世界时间 (秒) |
| `tick` | int | 累计 tick 序号, 起始 `1` |
| `positions` | `[[id, x, y], ...]` | 当前所有存活单位的位置. 每帧全量输出 -- 简单, 可任意 seek. 单位 `[id:int, x:double, y:double]` |
| `events` | array | 本 tick 内发布的事件 (顺序保留) |

## 事件类型

`type` 是字符串枚举. 字段类型缩写: `id` 为 `EntityId` (uint32), `vec2` 为 `[x, y]` 双精度数组.

### `unit_spawn`

```json
{"type": "unit_spawn", "id": 1, "name": "Pudge", "team": 1, "max_hp": 1500, "max_mana": 280, "pos": [0, 0]}
```

- `team`: `0` 中立, `1` 天辉, `2` 夜魇

### `unit_died`

```json
{"type": "unit_died", "id": 4, "killer": 1}
```

`killer` 可为 `0` (无归属来源).

### `cast_start`

```json
{"type": "cast_start", "caster": 1, "ability": "pudge_meat_hook", "point": [1300, 0]}
{"type": "cast_start", "caster": 2, "ability": "pudge_dismember", "target": 4}
```

`point` 与 `target` 二选一 (依 `BehaviorFlag::PointTarget` / `UnitTarget`); 都缺失时为 None target (例如 NoTarget 技能).

### `cast_finish`

```json
{"type": "cast_finish", "caster": 1, "ability": "pudge_meat_hook", "interrupted": false}
```

### `projectile_spawn`

```json
{"type": "projectile_spawn", "pid": 7, "src": 1, "origin": [0,0],
 "dir": [1,0], "speed": 1300, "length": 1300, "width": 100, "tracking": false}
```

`tracking=true` 时 `dir`/`length`/`width` 字段为 `0`, 改为提供 `target` (id):

```json
{"type": "projectile_spawn", "pid": 8, "src": 5, "origin": [0,0],
 "speed": 900, "tracking": true, "target": 4}
```

### `projectile_hit`

```json
{"type": "projectile_hit", "pid": 7, "victim": 4, "point": [598, 0]}
```

### `projectile_finish`

```json
{"type": "projectile_finish", "pid": 7}
```

### `modifier_add`

```json
{"type": "modifier_add", "unit": 4, "name": "modifier_pudge_hook_drag", "duration": 0.46, "stacks": 1}
```

`duration < 0` 表示永久.

### `modifier_remove`

```json
{"type": "modifier_remove", "unit": 4, "name": "modifier_pudge_hook_drag"}
```

### `damage`

```json
{"type": "damage", "src": 1, "dst": 4, "dtype": "magical", "pre": 125, "applied": 93, "flags": 0}
```

- `dtype` ∈ `physical | magical | pure`
- `pre`: 进入伤害管线时的数值 (输出 / 承受 amplification 之后, resistance 之前)
- `applied`: 实际扣除生命值
- `src` 可为 `0` (环境伤害)

### `heal`

```json
{"type": "heal", "src": 0, "dst": 1, "amount": 30}
```

`src=0` 表示无 healer (例如 health regen / 自我治疗).

### `attack_landed`

```json
{"type": "attack_landed", "src": 2, "dst": 3, "dmg": 48, "missed": false}
```

`missed=true` 时 `dmg=0` (闪避).

## 兼容性策略

- 增加新事件类型: 不破坏 v1 (旧播放器忽略未知 type).
- 增加现有事件的可选字段: 不破坏 v1.
- 删除字段或改字段语义: 必须 bump `v` 到 `2`, 旧播放器拒绝加载.

## 文件大小估计

每帧约 100-300 字节 (positions 6 单位 ~ 80 字节 + 0-3 事件); 30 fps × 60 秒 ≈ 1800 帧 ≈ 200-500 KB. 不需要压缩.
