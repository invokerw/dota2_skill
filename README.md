# Dota 2 技能系统

基于 C++20 实现的生产级 Dota 2 风格技能和修饰器系统，集成 Lua 脚本（sol2）和 YAML 数据定义（yaml-cpp）。包含完整的战斗管线、属性 aggregation（聚合）系统和事件驱动架构。

![架构图](doc/img/dota2-architecture.drawio.png)

## 特性

- **两种技能类型**：DataDriven（纯 YAML）和 Scripted（Lua 钩子）
- **三部分修饰器模型**：属性 aggregation（聚合）、状态位掩码、事件钩子
- **分阶段战斗管线**：7 阶段伤害管线、4 阶段治疗管线
- **事件驱动核心**：30Hz 固定时钟的 World 和发布/订阅 EventBus
- **完整 Lua 集成**：所有核心类型和枚举的 sol2 绑定
- **三个可玩英雄**：Lion、Lina、Juggernaut，共 12 个独特技能
- **91 个通过测试**：使用 GoogleTest 的全面测试覆盖

## 快速开始

```sh
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure    # 运行全部 91 个测试
./build/duel                                   # 2v1 团战演示
```

需要 C++20 编译器（AppleClang 15+、Clang 15+、GCC 12+ 或 MSVC 19.30+）。所有依赖项（GoogleTest、yaml-cpp、Lua 5.4、sol2）在首次配置时通过 [CPM.cmake](cmake/CPM.cmake) 自动获取。

## 架构

### 分层结构

| 层级 | 位置 | 职责 |
|-------|----------|----------------|
| **数据** | `data/heroes/*.yaml` | 英雄属性、技能定义及每级数值 |
| **脚本** | `scripts/abilities/*.lua` | Lua 技能实现及生命周期钩子 |
| **技能框架** | `include/dota/ability/`, `src/ability/` | 施法状态机、DataDriven（YAML）+ Scripted（Lua）|
| **修饰器系统** | `include/dota/modifier/`, `src/modifier/` | 属性 aggregation（聚合，4 层）、状态位掩码、事件钩子 |
| **战斗管线** | `include/dota/combat/`, `src/combat/` | 分阶段伤害/治疗，支持修饰器干预 |
| **核心引擎** | `include/dota/core/`, `src/core/` | Unit、World（30Hz tick）、EventBus |
| **Lua 绑定** | `src/script/` | Unit/World/Vec2 的 sol2 用户类型、枚举表 |

### 关键设计模式

#### 技能系统

- **DataDriven**（`ability_datadriven`）：纯 YAML 动作列表（ApplyModifier、Damage、Heal、RunScript）
- **Scripted**（`ability_lua`）：Lua 表，包含 `on_spell_start`、`on_channel_think`、`on_channel_finish` 钩子
- **施法生命周期**：Ready → Casting（cast point 施法前摇）→ 触发 `on_spell_start` → Backswing（施法后摇）→ OnCooldown
- **合法性检查**：沉默、冷却、魔法值、目标有效性、魔法免疫

#### 修饰器系统

- **属性**：数值加成按 4 层 aggregation（聚合）（BONUS_CONSTANT → BONUS_PCT → TOTAL_PCT → OVERRIDE）
- **状态**：位掩码（眩晕、沉默、缠绕、无敌、隐身）
- **事件**：钩子如 `on_pre_take_damage`、`on_interval_think`、`on_attack_landed`
- 属性值会乘以 `stack_count` 以支持可叠加修饰器

#### 战斗管线

- **伤害**：OutgoingAmp（输出增幅）→ IncomingAmp（承受增幅）→ PreTake（护盾 absorption 吸收）→ MagicImmune 检查 → TypeResistance（类型 resistance 抗性）→ Apply → PostTake（reflect 反射/反伤）
- **治疗**：PreTakeHeal → HealAmpPct（治疗 amplification 增幅）→ 限制到最大生命值 → PostTakeHeal
- **DamageFlag 位掩码**：HP_LOSS、REFLECTION、BYPASS_MAGIC_IMMUNITY、NO_SPELL_AMPLIFICATION

#### Lua 集成

- ScriptedAbility self 表包含闭包：`get_special`、`target_point`、`target_unit`、`level`、`get_caster`
- 完整绑定 Unit、World、Ability、Modifier、DamageContext、Vec2
- DamageType、ModifierProperty、ModifierState、DamageFlag 的枚举表

## 项目结构

```text
dota2_skill/
├── include/dota/          # 公共头文件
│   ├── core/              # Unit、World、EventBus、Vec2
│   ├── modifier/          # Modifier、ModifierManager、枚举、库
│   ├── ability/           # Ability、DataDriven、Scripted
│   └── combat/            # 伤害/治疗管线、DamageContext
├── src/                   # 实现
│   ├── core/              # 核心引擎实现
│   ├── modifier/          # 修饰器系统 + 通用修饰器
│   ├── ability/           # 技能框架 + YAML 加载器
│   ├── combat/            # 战斗管线实现
│   └── script/            # Lua 绑定（sol2）
├── data/heroes/           # YAML 英雄定义
│   ├── lion.yaml          # Lion（全部 DataDriven）
│   ├── lina.yaml          # Lina（YAML + Lua 被动）
│   └── juggernaut.yaml    # Juggernaut（Lua 为主）
├── scripts/abilities/     # Lua 技能实现
├── tests/                 # GoogleTest 测试套件
│   ├── test_event_bus.cpp
│   ├── test_unit_basic.cpp
│   ├── test_modifier_*.cpp
│   ├── test_ability_*.cpp
│   ├── test_lua_*.cpp
│   ├── test_damage_pipeline.cpp
│   ├── test_hero_*.cpp    # 每个英雄的集成测试
│   └── test_three_hero_kit.cpp
└── examples/
    └── duel.cpp           # 2v1 团战演示
```

## 添加新英雄

1. **创建英雄定义**：`data/heroes/<name>.yaml`

   ```yaml
   hero_name: hero_example
   base_health: 600
   base_mana: 300
   abilities:
     - ability_example_q
     - ability_example_w
   ```

2. **对于 DataDriven 技能**：在 YAML 中定义动作

   ```yaml
   ability_example_q:
     base_class: ability_datadriven
     ability_behavior: UNIT_TARGET
     on_spell_start:
       - action: Damage
         target: TARGET
         damage: "%damage"
   ```

3. **对于 Scripted 技能**：创建 `scripts/abilities/<ability_name>.lua`

   ```lua
   return {
     on_spell_start = function(self)
       local caster = self:get_caster()
       local damage = self:get_special("damage")
       -- 实现代码
     end
   }
   ```

4. **添加集成测试**：`tests/test_hero_<name>.cpp`

   ```cpp
   TEST(HeroExampleTest, AbilityQ_DamagesTarget) {
     // 测试实现
   }
   ```

5. **在 CMakeLists.txt 中注册**：将测试文件添加到 `add_executable(dota_tests ...)`

## 添加新修饰器属性

1. 在 [include/dota/modifier/enums.hpp](include/dota/modifier/enums.hpp) 中添加枚举条目：

   ```cpp
   enum class ModifierProperty {
     // ...
     MyNewProperty,
   };
   ```

2. 在 `layer_of()` 中映射其聚合层级（同一文件）

3. 在 [src/core/unit.cpp](src/core/unit.cpp) 中通过 Unit 统计获取器路由它

4. 在 [src/script/bindings.cpp](src/script/bindings.cpp) → `property_table()` 中暴露给 Lua

## 运行测试

```sh
# 所有测试（共 91 个）
ctest --test-dir build --output-on-failure

# 特定测试套件
ctest --test-dir build -R "HeroLionTest"

# 详细输出
ctest --test-dir build -V

# 并行运行测试
ctest --test-dir build -j8
```

## 示例

### DataDriven 技能（Lion Earth Spike）

```yaml
lion_earth_spike:
  base_class: ability_datadriven
  ability_behavior: UNIT_TARGET
  ability_target_team: ENEMY
  ability_target_type: HERO | BASIC
  cast_point: 0.3
  cast_range: 600
  cooldown: 12.0
  mana_cost: 100
  ability_special:
    damage: [100, 175, 250, 325]
    stun_duration: [1.0, 1.5, 2.0, 2.5]
  on_spell_start:
    - action: Damage
      target: TARGET
      damage: "%damage"
      damage_type: MAGICAL
    - action: ApplyModifier
      target: TARGET
      modifier: modifier_stunned
      duration: "%stun_duration"
```

### Scripted 技能（Juggernaut Blade Fury）

```lua
return {
  on_spell_start = function(self)
    local caster = self:get_caster()
    caster:apply_modifier("modifier_juggernaut_blade_fury", self:get_special("duration"), caster, self)
  end,
  
  on_channel_think = function(self)
    local caster = self:get_caster()
    local radius = self:get_special("radius")
    local damage = self:get_special("damage_per_tick")
    
    local enemies = caster:get_world():find_enemies_in_radius(caster:position(), radius, caster:team())
    for _, enemy in ipairs(enemies) do
      caster:deal_damage(enemy, damage, DamageType.MAGICAL, self)
    end
  end
}
```

### 自定义修饰器（Lina Fiery Soul）

```lua
return {
  declared_properties = function(self)
    return {
      [ModifierProperty.AttackSpeedBonus] = self:get_special("attack_speed_bonus"),
      [ModifierProperty.MoveSpeedBonus] = self:get_special("move_speed_bonus")
    }
  end,
  
  on_ability_executed = function(self, event)
    if event.ability:owner() == self:parent() then
      self:increment_stack_count()
      self:refresh_duration()
    end
  end
}
```

## 编译时定义

- `DOTA_SCRIPT_DIR`：`scripts/` 的绝对路径（在 `dota_core` 目标上设置）
- `DOTA_DATA_DIR`：`data/` 的绝对路径（在测试和 duel 目标上设置）

## 编辑器设置

CMake 项目导出 `build/compile_commands.json` 供 C++ 语言服务器和 IDE 代码导航使用。运行 `cmake -B build` 后，VS Code 的 Microsoft C/C++ 扩展将使用 [.vscode/settings.json](.vscode/settings.json) 中的工作区设置。

如果代码导航看起来过时，重置 IntelliSense 数据库：

```sh
# VS Code 命令面板：
# C/C++: Reset IntelliSense Database
# Developer: Reload Window
```

## 实现状态

全部 6 个阶段完成（91/91 测试通过）：

- ✅ **阶段 1**：脚手架 + 核心实体 + 事件总线
- ✅ **阶段 2**：修饰器系统
- ✅ **阶段 3**：技能框架 + DataDriven（YAML）加载器
- ✅ **阶段 4**：Lua 集成
- ✅ **阶段 5**：伤害/治疗管线
- ✅ **阶段 6**：三个英雄 + CLI 团战演示

详细阶段分解见 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md)（完成后已移除）。

## 技能实现参考
- [update · Issue #2 · vulkantsk/SpellLibraryLua](https://github.com/vulkantsk/SpellLibraryLua)
- [Pizzalol/SpellLibrary: Repo for recreating the original dota skills](https://github.com/Pizzalol/SpellLibrary?utm_source=chatgpt.com)

## 许可证

MIT
