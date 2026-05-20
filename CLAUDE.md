# CLAUDE.md

本文件为 Claude Code (claude.ai/code) 在此代码库中工作时提供指导.

## 代码规范

**重要: 所有代码注释, 文档字符串, README 文件和其他文档必须使用中文编写.**
- C++ 代码注释使用中文
- Lua 脚本注释使用中文
- YAML 数据文件中的注释使用中文
- 测试用例的描述和注释使用中文
- 提交信息(commit message)使用中文
- 所有 Markdown 文档使用中文

### 标点符号规范

**所有文档, 代码注释, Markdown 内容必须使用英文 (半角) 标点, 禁止使用中文全角标点.** 这样与代码标识符混排时可读性更高, 且利于自动化工具处理.

常见替换 (左侧使用, 右侧禁用):

- 逗号: `, ` 替代 `，` `、`
- 句号: `. ` 替代 `。`
- 冒号: `: ` 替代 `：`
- 分号: `; ` 替代 `；`
- 问号 / 感叹号: `?` `!` 替代 `？` `！`
- 圆括号: `( )` 替代 `（ ）`
- 方括号: `[ ]` 替代 `【 】`
- 双引号 / 单引号: ASCII `"..."` `'...'` 替代 `“...”` `‘...’` `「...」` `『...』`
- 省略号: `... ` 替代 `…`
- 破折号: 半角 `-` 或 ASCII `--` 替代 `—` `–`
- 半角空格替代全角空格 (`　`)

**例外**: 中文 CJK 文字本身保留, 仅替换标点符号. 行末不留多余空格.

### 术语使用规范

- **游戏/MOBA/Dota 专业术语**: 可以直接使用英文 (如 buff, debuff, stun, silence, cooldown 等).
- **技术术语**: 常见的技术术语可以使用英文 (如 pipeline, tick, modifier, ability 等).
- **不常见术语**: 对于较生僻或可能引起歧义的词汇, 使用 "英文 (中文解释)" 格式.
  - 例如: `cast point (施法前摇)`, `backswing (施法后摇)`, `amplification (增幅)`, `aggregation (聚合)`, `resistance (抗性)`, `absorption (吸收)`, `reflect (反射/反伤)`.
- **代码标识符**: 类名, 函数名, 变量名等保持英文, 注释中引用时可直接使用.
  - 例如: `ModifierProperty` 枚举, `deal_damage()` 函数.

### Lua 修饰器 spec 字段约定

**所有 Lua modifier spec 字段统一使用 PascalCase, 禁止使用 lower_case_with_underscore 旧式字段.**
- 项目处于 demo 阶段, 不需要兼容旧模式; 引入新字段时应直接采用新约定, 不应保留旧别名.
- 标志位 / 数值字段: `IsHidden` / `IsPurgable` / `IsDispellable` / `IsDebuff` / `IsMotionController` / `MotionPriority` / `RemoveOnDeath` / `ThinkInterval`
- 列表字段: `States`(`ModifierState` 数组), `Properties`(`{ ModifierProperty.X, value | "FnName" }` 列表), `CheckState`(动态状态回调)
- 生命周期钩子: `OnCreated` / `OnDestroyed` / `OnStackChanged` / `OnIntervalThink` / `OnPreTakeDamage` / `OnPostTakeDamage` / `OnPreTakeHeal` / `OnPostTakeHeal` / `OnMotionTick`
- 伤害/治疗钩子的新签名是 `(self, owner, ev_table)`: 通过修改 `ev.amount` 或返回吸收数值(仅 `OnPreTakeDamage`)来影响伤害; 不再支持旧的 `(self, owner, amount, dtype)` 签名.
- 不论是 `register_modifier(name, spec)` 全局注册还是直接传裸 Lua 表给 `ScriptedModifier`, 字段名规则一致.

## 构建与测试

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure    # 运行全部测试
ctest --test-dir build -R "HeroLinaTest"      # 运行单个测试套件
./build/skill_tester                           # 交互式技能测试器 (raylib + imgui)
```

需要 C++20(AppleClang 15+, Clang 15+ 或 GCC 12+). 所有依赖项(GoogleTest, yaml-cpp, Lua 5.4, sol2)在首次配置时通过 CPM.cmake 自动获取.

任何源代码修改后, 使用 `cmake --build build -j` 重新构建 -- 没有单独的 lint 步骤.

## 架构

这是一个 Dota 2 风格的技能和修饰器系统: C++20 引擎核心, Lua 5.4 脚本(通过 sol2), YAML 数据定义(通过 yaml-cpp).

### 分层结构

| 层级 | 位置 | 职责 |
| --- | --- | --- |
| 核心引擎 | `include/dota/core/`, `src/core/` | Unit, World(30Hz tick), EventBus, Rng, 空间查询, Thinker |
| 修饰器系统 | `include/dota/modifier/`, `src/modifier/` | 属性 aggregation(聚合), 状态位掩码, 生命周期钩子, Lua 修饰器注册表 |
| 技能框架 | `include/dota/ability/`, `src/ability/` | 施法状态机, DataDriven(YAML)+ Scripted(Lua) |
| 战斗管线 | `include/dota/combat/`, `src/combat/` | 分阶段伤害/治疗管线, 支持修饰器干预 |
| 投射物 | `include/dota/projectile/`, `src/projectile/` | LinearProjectile, TrackingProjectile, ProjectileManager |
| Lua 绑定 | `src/script/` | Unit/World/Vec2/Projectile 的 sol2 用户类型, 枚举表, `register_modifier` |
| 数据 | `data/heroes/*.yaml` | 英雄定义, 包含 ability_special 的每级数值 |
| 脚本 | `data/scripts/abilities/*.lua`, `data/scripts/modifiers/*.lua` | Lua 技能与 Lua 修饰器实现 |

### 关键设计模式

- **两种技能类型**: `ability_datadriven`(纯 YAML 动作列表)vs `ability_lua`(Lua 表, 包含 `on_spell_start`/`on_channel_think`/`on_channel_finish`). YAML 中的 `base_class` 字段控制使用哪种类型.
- **修饰器三部分模型**: `declared_properties()`(按层级 aggregation 聚合的数值加成), `declared_states()`(位掩码), 事件钩子(`on_pre_take_damage`, `on_interval_think` 等). 属性值会乘以 `stack_count`.
- **伤害管线**(`src/combat/damage.cpp` 中的 `deal_damage`): OutgoingAmp(输出增幅)→ IncomingAmp(承受增幅)→ PreTake(护盾 absorption 吸收)→ MagicImmune 检查 → TypeResistance(类型 resistance 抗性)→ Apply → PostTake(reflect 反射/反伤). DamageFlag 位掩码控制各阶段.
- **治疗管线**(`deal_heal`): PreTakeHeal → HealAmpPct(治疗 amplification 增幅)→ clamp → PostTakeHeal.
- **施法生命周期**: Ready → Casting(cast point 施法前摇)→ 触发 `on_spell_start` → Backswing(施法后摇)→ OnCooldown. 持续施法技能分支到 Channelling, 每 tick 调用 `on_channel_think`.
- **ScriptedAbility self 表**: Lua 钩子接收一个 `self` 表, 包含闭包(`get_special`, `target_point`, `target_unit`, `level`, `get_caster`). 前导的 `sol::object` 参数处理冒号调用语法.
- **投射物**: `World::projectiles()` 暴露 `ProjectileManager`. `LinearProjectile` 沿 direction 推进, 按 (prev,cur) 段扫描线宽内敌人, 可选穿透 / 单体; `TrackingProjectile` 追逐目标 Unit, 目标死亡或 Untargetable 时 fizzle. 两者支持 `on_hit` / `on_finish` Lua 闭包.
- **Motion Controller**: 标记 `is_motion_controller=true` 的修饰器在每 tick 由 World 在 ability tick *之前* 驱动 `on_motion_tick(dt)`. 同一 owner 上多个 MC 按 `motion_priority` 抢占 -- 高优先级会顶替低优先级. `MotionKnockback` 是内置 C++ MC; Lua 端通过 `register_modifier` 的 `IsMotionController` + `OnMotionTick` 实现自定义 MC(如肉钩拖拽).
- **Thinker 实体**: `World::create_thinker(pos, duration, modifier_name, source)` 创建隐身, 不可选中, 无碰撞的占位单位, 挂载注册的 Lua 修饰器; duration 到期后单位自毁(`ThinkerBase::on_destroyed` 通过 apply_raw_damage 致死).
- **Lua 修饰器注册**: `register_modifier(name, spec)` 在 Lua 端定义完整修饰器, spec 字段统一使用 PascalCase(`IsHidden` / `IsPurgable` / `IsDispellable` / `IsDebuff` / `IsMotionController` / `MotionPriority` / `States` / `Properties` / `ThinkInterval` / `OnCreated` / `OnDestroyed` / `OnIntervalThink` / `OnPreTakeDamage` / `OnPostTakeDamage` / `OnPreTakeHeal` / `OnPostTakeHeal` / `OnMotionTick` 等, 详见"Lua 修饰器 spec 字段约定"小节). Unit 端用 `add_modifier(name, source, ability, params)` 实例化.

### 编译时定义

- `DOTA_SCRIPT_DIR` -- `data/scripts/` 的绝对路径 (在 `dota_core` 上设置)
- `DOTA_DATA_DIR` -- `data/` 的绝对路径 (在测试和 skill_tester 目标上设置)

### 添加新英雄

1. 创建 `data/heroes/<name>.yaml`, 包含英雄属性和技能列表
2. 对于 `ability_lua` 条目, 创建 `data/scripts/abilities/<ability_name>.lua`, 返回包含生命周期钩子的表
3. 在 `tests/test_hero_<name>.cpp` 中添加集成测试, 并在 `CMakeLists.txt` 中注册该文件

### 添加新修饰器属性

1. 在 `include/dota/modifier/enums.hpp` 中添加枚举条目(`ModifierProperty`)
2. 在 `layer_of()` 中映射其层级(同一文件)
3. 在 `src/core/unit.cpp` 中通过相关的 Unit 统计获取器路由它
4. 在 `src/script/bindings.cpp` → `property_table()` 中暴露给 Lua
