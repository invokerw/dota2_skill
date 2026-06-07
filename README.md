# Dota 2 技能系统

一个 C++20 实现的 Dota 2 风格技能和修饰器系统. 引擎核心用 C++, 技能数据用 YAML, Scripted 技能和 Lua modifier 用 Lua 5.4 + sol2.

![架构图](doc/img/dota2-architecture.drawio.png)

## 核心能力

- DataDriven 技能: 使用 YAML 动作列表描述 `damage`, `heal`, `apply_modifier`.
- Scripted 技能: 使用 Lua 实现 `on_spell_start`, `on_channel_think`, `on_channel_finish`.
- Modifier 系统: 支持属性 aggregation (聚合), 状态位掩码, 生命周期钩子和战斗事件钩子.
- 战斗管线: 统一处理伤害, 治疗, amplification (增幅), resistance (抗性), absorption (吸收), reflect (反射/反伤).
- 世界模拟: 30Hz 固定 tick, 事件总线, 移动, 软碰撞, 空间查询, thinker 实体.
- 寻路与避障: A* 长程网格 + Bug2 双向 wall trace 局部绕障 + 解析 swept-circle ShapeCast, 支持矩形 cell 阻挡和圆形障碍.
- 投射物: 支持直线投射物, 追踪投射物, 命中和结束回调.
- Motion Controller: 通过 modifier 驱动击退, 勾拉等位移效果, 按 priority 抢占.
- Lua 集成: 暴露 `Unit`, `World`, `Vec2`, `Projectile`, 常用枚举和 `register_modifier`.
- 可玩英雄数据: Lion, Lina, Juggernaut, Sven, Earthshaker, Pudge.
- 交互式调试工具: `skill_tester` 基于 raylib + imgui.

## 快速运行

```sh
cmake -B build
cmake --build build -j
./build/skill_tester
```

需要 C++20 编译器. 依赖通过 [CPM.cmake](cmake/CPM.cmake) 首次配置时获取, 包括 GoogleTest, yaml-cpp, Lua 5.4, sol2, raylib, imgui 和 rlImGui.

如果不需要 `skill_tester`, 可跳过图形目标:

```sh
cmake -B build -DBUILD_VISUAL=OFF
cmake --build build -j
```

完整开发和测试说明见 [doc/development.md](doc/development.md).

## 项目结构

| 路径 | 内容 |
| --- | --- |
| `include/dota/core/`, `src/core/` | `World`, `Unit`, `EventBus`, 移动, 碰撞, 空间查询, thinker |
| `include/dota/ability/`, `src/ability/` | 技能状态机, DataDriven YAML 加载, Scripted Lua 技能 |
| `include/dota/modifier/`, `src/modifier/` | modifier 生命周期, 属性聚合, 状态位, Lua modifier registry |
| `include/dota/combat/`, `src/combat/` | `deal_damage`, `deal_heal`, 战斗管线 |
| `include/dota/projectile/`, `src/projectile/` | 直线和追踪投射物 |
| `include/dota/pathfinding/`, `src/pathfinding/` | NavGrid + A*, ShapeCast (swept-circle), Bug2 WallTracer |
| `include/dota/replay/`, `src/replay/` | JSONL 录像写入和回放 |
| `src/script/` | sol2 绑定和 Lua API |
| `data/heroes/` | 英雄和技能 YAML 数据 |
| `data/scripts/abilities/` | Lua 技能脚本 |
| `data/scripts/modifiers/` | Lua modifier 脚本 |
| `examples/skill_tester/` | 交互式技能测试器 |
| `examples/pathfinding_demo/` | 寻路 / 避障可视化 |
| `tests/` | GoogleTest 测试 |

## 数据入口

英雄定义位于 `data/heroes/*.yaml`. 技能通过 `base_class` 选择实现方式:

```yaml
abilities:
  - name: lion_earth_spike
    base_class: ability_datadriven
    behavior: [UNIT_TARGET]
    target_team: ENEMY
    cast_point: 0.3
    cast_range: 625
    cooldown: [12, 12, 12, 12]
    mana_cost: [100, 120, 140, 160]
    ability_special:
      damage: [80, 160, 240, 320]
      stun_duration: [1.7, 1.9, 2.1, 2.3]
    on_spell_start:
      - apply_modifier:
          target: TARGET
          name: modifier_stunned
          duration: "%stun_duration"
      - damage:
          target: TARGET
          type: MAGICAL
          amount: "%damage"
```

Lua modifier 通过 `register_modifier(name, spec)` 注册, spec 字段使用 PascalCase:

```lua
register_modifier("modifier_test_evasion", {
  IsHidden   = true,
  IsPurgable = false,
  IsDebuff   = false,
  Properties = {
    { ModifierProperty.EVASION, 0.25 },
  },
})
```

## 文档

- [开发与测试](doc/development.md)
- [录像 JSONL schema](doc/recording_schema.md)
- [skill_tester modifier catalog 设计](doc/skill_tester_modifier_catalog_plan.md)

---

## 🎮 多人游戏服务器和客户端

### 游戏服务器（生存模式）✅

基于核心技能系统的多人在线生存模式服务器：

**功能**:
- UDP + KCP 可靠传输，支持 10-50 并发玩家
- 30Hz 游戏逻辑 tick，10Hz 状态同步
- 波次刷怪系统，经验升级，技能池
- 增量快照优化（节省 80% 带宽）

**编译**:
```bash
cmake -B build -DBUILD_SERVER=ON
cmake --build build -j
```

**运行**:
```bash
./build/server/game_server
```

详细文档: [server/PROGRESS.md](server/PROGRESS.md)

### 游戏客户端（raylib）✅

基于 raylib 的 2D 游戏客户端：

**功能**:
- 实时联网同步，增量快照
- 相机跟随，60 FPS 渲染
- 右键移动，QWER 技能使用
- UI 显示（玩家 ID、延迟、FPS）

**编译**:
```bash
cmake -B build -DBUILD_CLIENT=ON -DBUILD_VISUAL=ON
cmake --build build -j
```

**运行**:
```bash
./build/client/game_client [PlayerName] [host] [port]
```

详细文档: [client/CLIENT_COMPLETED.md](client/CLIENT_COMPLETED.md)

### 联网测试

1. 启动服务器: `./build/server/game_server`
2. 启动客户端: `./build/client/game_client`
3. 右键移动，Q/W/E/R/D/F 使用技能

测试指南: [TESTING.md](TESTING.md)

### 项目进度

- **服务器**: 90% 完成（5 个 Stage 完成）
- **客户端**: 80% 完成（基础框架完成）
- **整体**: 85% 完成，可进行游戏测试

---

## 许可证

MIT
