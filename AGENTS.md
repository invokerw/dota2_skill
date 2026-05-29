# AGENTS.md

本文件给后续自动化 agent 在本仓库工作时使用. 内容以当前代码为准, 发现冲突时优先读源码和测试.

## 语言和格式

- 所有代码注释, 文档字符串, Markdown, Lua/YAML 注释和 commit message 使用中文.
- 文档和注释使用英文半角标点, 禁止中文全角标点. 例如使用 `, `, `. `, `: `, `( )`, `"..."`.
- 游戏和技术术语可直接使用英文, 生僻术语首次出现时使用 `英文 (中文解释)` 格式.
- 代码标识符保持英文, 注释中可直接引用类名, 函数名和枚举名.

## 项目概览

这是一个 C++20 实现的 Dota 2 风格技能和修饰器系统. 核心引擎用 C++, 技能数据用 YAML, Scripted 技能和 Lua modifier 用 Lua 5.4 + sol2.

主要模块:

- `include/dota/core/`, `src/core/`: `World`, `Unit`, `EventBus`, 固定 30Hz tick, 移动, 碰撞, 空间查询, thinker.
- `include/dota/ability/`, `src/ability/`: 施法状态机, `ability_datadriven`, `ability_lua`, YAML 加载和实例化.
- `include/dota/modifier/`, `src/modifier/`: 属性 aggregation (聚合), 状态位掩码, 生命周期和伤害/治疗事件钩子, Lua modifier registry.
- `include/dota/combat/`, `src/combat/`: `deal_damage` 和 `deal_heal` 分阶段战斗管线.
- `include/dota/projectile/`, `src/projectile/`: 直线和追踪投射物, `ProjectileManager`.
- `include/dota/pathfinding/`, `src/pathfinding/`: 寻路与避障. `NavGrid` 持网格 cell + 圆形障碍, 8 方向 A*, line-of-sight 简化; `ShapeCast` 解析 swept-circle (Minkowski 圆角矩形, 全闭式不采样); `WallTracer` Bug2 双向局部绕障; `MovementConfig` 全局可调参数; `CollisionGroups` 位掩码 (Terrain=1, Unit=2, All=3). `World::tick_movement` 每 tick 用 A* 出 rough 路径 + WallTracer 出 smooth 段 + ShapeCast 检测落点驱动单位推进.
- `include/dota/replay/`, `src/replay/`: JSONL 录像写入和回放.
- `include/dota/log/`, `src/log/`: `CombatLog`. 旁观订阅 World 上的 Damage / Heal / Modifier / Cast / Attack / Death 事件, 缓冲成结构化条目, 供 skill_tester 浮动窗口或外部观察者展示. 不影响游戏逻辑.
- `include/dota/tools/`, `src/tools/`: 编辑器/迁移用工具层. `HeroDoc`/`HeroCatalog`, `AbilityDoc`/`AbilityCatalog`, `*_ops` 文件级 CRUD, `ModifierScanner`, `Trash`.
- `src/script/`: sol2 绑定, 枚举表, `register_modifier`.
- `data/heroes/*.yaml`: 英雄定义, 只含 `hero:` 元字段和 `abilities: [name1, ...]` 引用列表.
- `data/abilities/*.yaml`: 独立 ability 定义, 平铺 (stem = ability name), 跨 hero 共享.
- `data/scripts/abilities/*.lua`: Lua 技能脚本.
- `data/scripts/modifiers/*.lua`: Lua modifier 脚本.
- `examples/skill_tester/`: raylib + imgui 交互式技能测试器.
- `examples/hero_workshop/`: raylib + imgui 数据编辑器. 左侧 Heroes / Abilities / Modifiers 三 tab, 各自管列表 + 详情 + dirty.
- `examples/pathfinding_demo/`: raylib 寻路可视化. 默认 8 unit + 3 圆障碍演示 A* + WallTracer + 头对头避让, 支持运行时加 cell / 圆障碍.
- `tests/`: GoogleTest 测试.

## 构建和测试

常用命令:

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
ctest --test-dir build -R "HeroLinaTest" --output-on-failure
./build/skill_tester
```

如果只需要库和测试, 或本机图形依赖不可用, 可跳过可视化目标:

```sh
cmake -B build -DBUILD_VISUAL=OFF
cmake --build build -j
ctest --test-dir build --output-on-failure
```

依赖通过 `cmake/CPM.cmake` 首次配置时获取: GoogleTest, yaml-cpp, Lua 5.4, sol2, 以及默认启用的 raylib/imgui/rlImGui. 没有单独 lint 步骤. 修改 C++/Lua/YAML 后至少运行相关 `ctest -R ...`, 共享核心改动运行全量测试.

编译时定义:

- `DOTA_SCRIPT_DIR`: `data/scripts/` 的绝对路径, 设置在 `dota_core` 上.
- `DOTA_DATA_DIR`: `data/` 的绝对路径, 设置在测试和 `skill_tester` 目标上.

## 架构约定

- `World::advance(dt)` 会切成 `World::kTickDt` 固定步进, 当前 tick 顺序包含 motion controller, 移动/碰撞, 技能, 投射物, 事件 flush.
- 施法生命周期是 `Ready -> Casting -> on_spell_start -> Backswing -> OnCooldown`, 引导技能进入 `Channelling` 并每 tick 调用 `on_channel_think`.
- `ModifierManager` 拥有 modifier, 负责持续时间, interval think, 属性聚合和状态位查询.
- 属性聚合层级由 `layer_of(ModifierProperty)` 静态决定. 新属性必须同步 `include/dota/modifier/enums.hpp`, `src/core/unit.cpp`, `src/script/bindings.cpp`.
- 伤害入口优先使用 `deal_damage(DamageInstance)` 或 Lua 的 `Unit:apply_damage(...)`, 不要绕过战斗管线调用 `apply_raw_damage`, 除非实现自毁/测试等明确需要原始扣血.
- Lua 回调通过 `sol::protected_function` 做错误隔离. 新增 Lua 调用路径也应报告错误而不是让主循环崩溃.
- 录像事件不影响游戏逻辑, schema 维护在 `doc/recording_schema.md`.
- 移动指令通过 `Unit::issue_order(OrderMoveToPoint)` 进 OrderQueue. 每 tick `World::tick_movement` 推进 `MoveState` (rough = `A*` 输出, smooth = WallTracer 当前段); 落点 `shape_cast_circle` 命中 unit -> 等若干帧重做 trace, 命中 terrain -> 累积 seg_block / seg_miss, 阈值后跳 waypoint 或 full `A*` replan. `World` 默认持空 `NavGrid`, 不挂障碍时退化为只看 unit 碰撞.

## 数据和脚本约定

英雄 yaml 只持元字段和 ability 名字引用:

```yaml
# data/heroes/example.yaml
hero:
  name: npc_dota_hero_example
  base_health: 600
  base_mana: 300
  base_armor: 1
  base_magic_resist: 0.25
  attack_type: ranged       # melee | ranged, 缺省 melee
  attack_range: 600         # 普攻射程, 远程必填; 近战通常 150
  projectile_speed: 1200    # 远程普攻投射物速度, 近战可省略

abilities:
  - example_spell
  - example_passive
```

Ability 是独立资源, 跨 hero 共享:

```yaml
# data/abilities/example_spell.yaml
name: example_spell
base_class: ability_datadriven
behavior: [UNIT_TARGET]
target_team: ENEMY
cast_point: 0.2
cooldown: [12, 12, 12, 12]
mana_cost: [100, 110, 120, 130]
cast_range: 600
ability_special:
  damage: [100, 175, 250, 325]
on_spell_start:
  - damage:
      target: TARGET
      type: MAGICAL
      amount: "%damage"
```

加载入口: `AbilityRegistry::load_hero(hero_yaml, abilities_dir)` 解析引用列表并按需 load 对应文件; `load_dir(abilities_dir)` 一次性扫整个目录. 顶层 `abilities[]` 只接受 scalar 名, 不再支持内嵌 map 老格式.

DataDriven action 是单键 map, 当前支持 `damage`, `heal`, `apply_modifier`. `amount` 和 `duration` 可以引用 `ability_special` 中的 `%key`.

Scripted 技能:

- YAML 中 `base_class: ability_lua`, `script: abilities/<name>.lua`.
- Lua 模块返回 table, 可实现 `on_spell_start`, `on_channel_think`, `on_channel_finish`.
- 约定使用冒号语法, 通过 `self:get_special(...)`, `self:target_point()`, `self:target_unit()`, `self:get_caster()` 取上下文.

Lua modifier:

- 通过 `register_modifier(name, spec)` 注册.
- spec 字段统一 PascalCase, 不保留旧 lower_case 别名.
- 常用字段: `IsHidden`, `IsPurgable`, `IsDispellable`, `IsDebuff`, `IsMotionController`, `MotionPriority`, `RemoveOnDeath`, `ThinkInterval`, `States`, `Properties`, `CheckState`.
- 常用钩子: `OnCreated`, `OnDestroyed`, `OnRefresh`, `OnStackChanged`, `OnIntervalThink`, `OnAbilityExecuted`, `OnPreTakeDamage`, `OnPostTakeDamage`, `OnPreTakeHeal`, `OnPostTakeHeal`, `OnMotionTick`.
- 伤害/治疗钩子签名是 `(self, owner, ev_table)`. 通过修改 `ev.amount` 或在 `OnPreTakeDamage` 返回吸收数值影响管线.
- ScriptedModifier 在构造时把 `self.handle` (`Modifier` 句柄: `stack_count` / `set_stack_count` / `refresh` / `duration_remaining` / `permanent` / `name`) 和 `self.ability` (intrinsic ability 句柄, 提供 `level` / `is_passive` / `get_special` / `caster`) 注入实例 self 表, 钩子里直接用即可.

被动技能 (intrinsic modifier):

- 推荐用 `intrinsic_modifier:` 字段实现, 与 Dota `GetIntrinsicModifierName` 等价: ability 是壳子, 真正逻辑在 modifier 里.
- yaml 写 `behavior: [PASSIVE]`, `base_class: ability_datadriven`, 加 `intrinsic_modifier: <modifier_name>`. `AbilityRegistry::instantiate` 会自动给 caster 挂一个永久 ScriptedModifier, ability 句柄存在 modifier 上.
- ability 升级 (`set_level`) 时引擎对该 modifier 调用 `OnRefresh`, 用来重读 `ability_special`.
- 主动 ability 完整释放 (非 interrupted, 非 passive) 后, 引擎对 caster 上所有 modifier 触发 `OnAbilityExecuted(self, owner, ev)`, ev 含 `unit / ability / ability_name / is_passive`. 与 Dota `OnAbilityFullyCast` 语义一致.
- 典型范例见 `data/scripts/modifiers/modifier_lina_fiery_soul.lua` (叠层 + 持续时间衰减 + 动态 `Properties`).

## 常见改动流程

新增英雄:

1. 为每个新 ability 新增 `data/abilities/<ability_name>.yaml`. 已有 ability 直接复用引用名, 不需要复制文件.
2. 新增 `data/heroes/<name>.yaml`, `abilities:` 列表只填 ability 名引用.
3. `ability_lua` 类型 ability 新增 `data/scripts/abilities/<ability_name>.lua`.
4. 被动 ability 推荐用 `behavior: [PASSIVE]` + `intrinsic_modifier:`, 不写 `script:` 字段, 真正逻辑放进 `data/scripts/modifiers/<modifier_name>.lua` 的 `OnAbilityExecuted` / `OnRefresh` 等钩子里.
5. 如需要 Lua modifier, 新增 `data/scripts/modifiers/<modifier_name>.lua` 并用 PascalCase spec.
6. 新增 `tests/test_hero_<name>.cpp`.
7. 在 `CMakeLists.txt` 的 `dota_tests` 源文件列表中注册测试.
8. 运行相关 hero 测试和全量构建.

新增 / 编辑 ability:

- 直接编辑 `data/abilities/<name>.yaml`, 多个 hero 的引用会同步生效.
- 重命名 ability 应通过 `AbilityOps::rename_ability_file` 或 `hero_workshop` Abilities tab 触发, 它会扫所有 hero yaml 同步替换引用名.
- 删除 ability 应通过 `AbilityOps::delete_ability_file` 或 UI 触发, 被任何 hero 引用时拒绝并列出 referencing hero.

新增 DataDriven action:

1. 扩展 `include/dota/ability/datadriven.hpp` 中的 action 类型.
2. 更新 `src/ability/registry.cpp` 的 YAML 解析.
3. 更新 `src/ability/datadriven.cpp` 的执行逻辑.
4. 添加 loader 测试和端到端技能测试.

新增 Lua 绑定:

1. 在 `src/script/bindings.cpp` 暴露 C++ API.
2. 如果绑定枚举或 modifier 字段, 同步测试 `tests/test_lua_bindings.cpp` 或 `tests/test_lua_modifier_registry.cpp`.
3. Lua 脚本中使用新 API 时补充对应 hero 或 smoke 测试.

修改 `skill_tester`:

- UI 逻辑分散在 `examples/skill_tester/input.cpp`, `panels.cpp`, `scene.cpp`, `modifier_catalog.cpp`.
- Add Modifier 面板从 `build_modifier_catalog(Scene&)` 获取 C++ 和 Lua modifier 列表, 不要在 UI 分支里硬编码新增项.
- `test_skill_tester_smoke.cpp` 负责无窗口 smoke 测试, 图形交互仍需手动运行 `./build/skill_tester`.

## 工作边界

- 不要编辑 `build/` 下的生成文件, 除非用户明确要求.
- 不要回滚用户已有改动. 如果工作树有无关改动, 忽略它们.
- 文档示例必须跟当前解析器和 Lua 绑定一致. 改 schema 或绑定时同步 `README.md`, 本文件和相关 `doc/`. (`CLAUDE.md` 是 `@AGENTS.md` 的引用桩, 不需要改.)
- 引入新依赖前先确认必要性. 现有项目偏向少依赖, 录像 JSONL 是手写序列化.
