# 开发与测试

本文记录本仓库的本地开发, 测试和常见扩展流程. README 只保留项目概览和入口.

## 语言和格式

- 所有注释, Markdown, Lua/YAML 注释和提交信息使用中文.
- 文档和注释使用英文半角标点, 禁止中文全角标点.
- 游戏和技术术语可直接使用英文. 生僻术语首次出现时使用 `英文 (中文解释)`.
- Lua modifier spec 字段统一使用 PascalCase, 不保留 lower_case 旧别名.

## 构建

```sh
cmake -B build
cmake --build build -j
```

默认会构建 `skill_tester`, 因此首次配置会拉取 raylib, imgui 和 rlImGui. 如果只开发核心库和测试, 可关闭可视化目标:

```sh
cmake -B build -DBUILD_VISUAL=OFF
cmake --build build -j
```

需要 C++20 编译器. 所有第三方依赖通过 `cmake/CPM.cmake` 获取.

编译时定义:

- `DOTA_SCRIPT_DIR`: `data/scripts/` 的绝对路径, 设置在 `dota_core` 上.
- `DOTA_DATA_DIR`: `data/` 的绝对路径, 设置在测试和 `skill_tester` 目标上.

## 测试

```sh
# 全量测试
ctest --test-dir build --output-on-failure

# 按测试名筛选
ctest --test-dir build -R "HeroLionTest" --output-on-failure
ctest --test-dir build -R "Projectile" --output-on-failure
ctest --test-dir build -R "Motion" --output-on-failure

# 详细输出
ctest --test-dir build -V

# 并行运行
ctest --test-dir build -j8 --output-on-failure
```

当前构建有 170 个 GoogleTest 测试. 修改共享核心, Lua 绑定, YAML 解析或战斗管线后应运行全量测试. 只改单个英雄脚本时, 至少运行对应 `Hero*Test` 和相关 Lua/Projectile/Motion 测试.

项目没有单独 lint 步骤. C++ 修改后运行 `cmake --build build -j`.

## 添加新英雄

1. 新增 `data/heroes/<name>.yaml`.
2. 如果技能使用 Lua, 新增 `data/scripts/abilities/<ability_name>.lua`.
3. 如果需要 Lua modifier, 新增 `data/scripts/modifiers/<modifier_name>.lua` 并调用 `register_modifier`.
4. 新增 `tests/test_hero_<name>.cpp`.
5. 在 `CMakeLists.txt` 的 `dota_tests` 源文件列表中注册测试文件.
6. 运行新 hero 测试和全量构建.

YAML 顶层结构:

```yaml
hero:
  name: npc_dota_hero_example
  base_health: 600
  base_mana: 300
  base_armor: 1
  base_magic_resist: 0.25

abilities:
  - name: ability_example_q
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

DataDriven action 是单键 map, 当前支持:

- `damage`
- `heal`
- `apply_modifier`

`amount` 和 `duration` 可以用 `%key` 引用 `ability_special` 中的当前等级数值.

## 添加 Scripted 技能

YAML 中使用:

```yaml
- name: example_spell
  base_class: ability_lua
  script: abilities/example_spell.lua
  behavior: [POINT_TARGET]
  target_team: ENEMY
```

Lua 模块返回 table, 可实现以下钩子:

```lua
local M = {}

function M:on_spell_start(caster, target, world)
  local damage = self:get_special("damage")
  if target then
    target:apply_damage(DamageType.MAGICAL, damage, caster)
  end
end

function M:on_channel_think(caster, target, world, dt)
end

function M:on_channel_finish(caster, target, world, interrupted)
end

return M
```

`self` 表提供 `get_special`, `target_point`, `target_unit`, `level`, `get_caster`.

## 添加 Lua modifier

Lua modifier 通过 `register_modifier(name, spec)` 注册. spec 字段使用 PascalCase:

```lua
register_modifier("modifier_example", {
  IsHidden        = false,
  IsPurgable      = true,
  IsDispellable   = true,
  IsDebuff        = false,
  ThinkInterval   = 1.0,
  States          = { ModifierState.STUNNED },
  Properties      = {
    { ModifierProperty.ARMOR_BONUS, 5.0 },
  },
  OnCreated = function(self, owner)
  end,
  OnIntervalThink = function(self, owner)
  end,
  OnPreTakeDamage = function(self, owner, ev)
    return 0.0
  end,
})
```

伤害和治疗钩子签名是 `(self, owner, ev_table)`. 通过修改 `ev.amount` 或在 `OnPreTakeDamage` 返回吸收数值影响管线.

常用字段:

- 标志位 / 数值字段: `IsHidden`, `IsPurgable`, `IsDispellable`, `IsDebuff`, `IsMotionController`, `MotionPriority`, `RemoveOnDeath`, `ThinkInterval`.
- 列表字段: `States`, `Properties`, `CheckState`.
- 生命周期和事件钩子: `OnCreated`, `OnDestroyed`, `OnStackChanged`, `OnIntervalThink`, `OnPreTakeDamage`, `OnPostTakeDamage`, `OnPreTakeHeal`, `OnPostTakeHeal`, `OnMotionTick`.

## 添加新 modifier 属性

1. 在 `include/dota/modifier/enums.hpp` 添加 `ModifierProperty` 枚举条目.
2. 在同一文件的 `layer_of()` 中映射 aggregation (聚合)层级.
3. 在 `src/core/unit.cpp` 中通过对应 Unit 统计 getter 路由它.
4. 在 `src/script/bindings.cpp` 的 `property_table()` 中暴露给 Lua.
5. 补充 C++ 聚合测试和 Lua 绑定测试.

## 添加新 DataDriven action

1. 扩展 `include/dota/ability/datadriven.hpp` 中的 action 类型.
2. 更新 `src/ability/registry.cpp` 的 YAML 解析.
3. 更新 `src/ability/datadriven.cpp` 的执行逻辑.
4. 添加 loader 测试和端到端技能测试.

## 修改 Lua 绑定

1. 在 `src/script/bindings.cpp` 暴露 C++ API.
2. 如果绑定枚举, 同步 `tests/test_lua_bindings.cpp`.
3. 如果绑定 modifier registry 或 spec 字段, 同步 `tests/test_lua_modifier_registry.cpp`.
4. Lua 脚本开始依赖新 API 时, 补充对应 hero 或 smoke 测试.

## 修改 skill_tester

`skill_tester` 代码位于 `examples/skill_tester/`.

- `scene.*`: world 构建, 英雄加载, dummy 和渲染快照.
- `input.*`: 键鼠输入, 施法, 走点, 队列命令.
- `panels.*`: imgui 面板.
- `modifier_catalog.*`: Add Modifier 面板的 C++/Lua modifier catalog.

Add Modifier 面板从 `build_modifier_catalog(Scene&)` 获取条目. 新增可添加 modifier 时, 不要在 UI 控件分支中散落硬编码.

无窗口 smoke 测试在 `tests/test_skill_tester_smoke.cpp`. 图形交互仍需手动运行:

```sh
./build/skill_tester
```

## 编辑器

CMake 会导出 `build/compile_commands.json`, 供 C++ 语言服务器和 IDE 使用. 如果代码导航过时, 重新运行:

```sh
cmake -B build
```
