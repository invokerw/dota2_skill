# Skill Tester Modifier Catalog Plan

## Problem

`skill_tester` 的 `Unit > Modifiers > Add` 入口最初直接在 UI 中维护一个固定
modifier 名单，并用下标分支决定要展示哪些参数、创建哪个 C++ modifier。这个方式会带来
两个问题：

- UI 很容易漏掉 C++ 已实现的 modifier 类型。
- Lua 脚本注册的 modifier 已经会自动加载，但 UI 无法枚举 registry，所以无法动态显示。

## Goals

- `skill_tester` 从 modifier catalog 读取可添加项，而不是在 UI 控件中散落硬编码。
- C++ 内置 modifier 通过一处 catalog 注册，描述名字、参数 schema 和 factory。
- Lua modifier 从 `LuaModifierRegistry` 动态枚举并加入 catalog。
- 添加面板根据参数 schema 自动渲染控件，后续新增参数类型时只扩展 schema 渲染逻辑。

## Non-Goals

- 不实现 C++ 运行时反射。C++ 类型仍需要显式注册 factory。
- 不要求 Lua modifier 立即支持完整自定义参数 schema。第一阶段先支持 duration/stacks。
- 不把 `skill_tester` 的调试 catalog 直接作为游戏运行时数据格式。

## Design

新增一个 UI 层 catalog：

- `ModifierParamSpec`: 描述参数 key、显示名、类型、默认值和输入范围。
- `ModifierParamValue`: 保存当前 UI 参数值。
- `ModifierAddSpec`: 描述 modifier 名字、来源、参数列表和创建函数。

Catalog 来源：

- C++ 内置 modifier 由 `build_builtin_modifier_specs()` 注册。
- Lua modifier 通过 `LuaModifierRegistry::names()` 枚举，再按名字创建 `ScriptedModifier`。

UI 流程：

1. 每帧从当前 `Scene` 构建 catalog。
2. 下拉框显示 catalog entry。
3. 选中项变化时重置参数为 schema 默认值。
4. 按 schema 渲染 number/int/property/vec2 控件。
5. 点击 Add 时调用 entry factory，并 attach 到当前选中 unit。

## Implementation Steps

1. 给 `LuaModifierRegistry` 增加 `names()`，返回已注册 Lua modifier 名字。
2. 在 `skill_tester` 引入 catalog 结构和 C++ 内置 modifier factory。
3. 将 Lua registry 的名字追加到 catalog。
4. 用 schema 渲染 Add Modifier 参数区，移除 UI 下标分支。
5. 补充 registry 枚举测试并运行 `skill_tester` 构建和测试。

## Future Work

- 允许 Lua modifier 在 spec 表中声明 `DebugParams`，让脚本也能提供自定义 UI 参数。
- 将内置 C++ modifier catalog 下沉到公共工具模块，供其他调试工具复用。
- 支持保存/加载 Unit 当前 modifiers 和参数快照，用于 scenario preset。
