# 游戏服务器实现计划

## 项目目标

实现一个基于 **libevent + kcp + protobuf** 的多人游戏服务器，支持选技生存模式 (Survivor Game)。服务器采用权威架构，负责游戏逻辑运算和状态同步，客户端负责渲染和输入。

## 重要说明

**服务器代码独立于主项目**，位于 `server/` 目录下。这样设计的好处：
- 清晰的模块边界
- 独立的依赖管理
- 方便将来单独部署
- 不影响现有的单机游戏逻辑

构建服务器：
```bash
cmake -B build -DBUILD_SERVER=ON
cmake --build build -j
./build/server/game_server
```

## 技术栈

- **网络层**: libevent (事件驱动) + KCP (可靠 UDP) + protobuf (序列化)
- **游戏逻辑**: 复用核心库 (dota_core, dota_ability, dota_modifier)
- **游戏模式**: 新增 SurvivorGameMode (刷怪, 经验, 升级, 技能选择)

## 架构概览

```
ServerNetwork (libevent + KCP)
    ↓
MessageHandler (protobuf 消息分发)
    ↓
GameServer (会话管理)
    ↓
GameSession (游戏房间)
    ↓
World + SurvivorGameMode (游戏逻辑，复用核心库)
```

## 目录结构

```
server/
├── proto/              # Protobuf 协议定义
├── include/server/     # 服务器头文件
│   ├── network/        # 网络层 (KCP, ServerNetwork)
│   ├── server/         # 服务器核心 (GameServer, GameSession)
│   └── mode/           # 游戏模式 (SurvivorMode, WaveSpawner)
├── src/                # 服务器实现
├── test/               # 单元测试
├── tools/              # 工具 (压力测试客户端等)
├── config/             # 配置文件
└── CMakeLists.txt
```

---

## Stage 1: 网络基础设施

**Goal**: 搭建 protobuf + kcp + libevent 的网络通信层，实现基础的连接和消息收发

**Success Criteria**:
- protobuf 消息定义编译成功
- KcpSession 能够发送和接收数据
- ServerNetwork 能够接受多个客户端连接
- 简单的 echo 测试通过 (客户端发送消息，服务器原样返回)

**Tests**:
- `test_kcp_session.cpp`: 单个 KCP 会话的发送接收
- `test_server_network.cpp`: 多客户端连接和消息路由
- `test_protobuf_serialization.cpp`: protobuf 序列化正确性

**Tasks**:
- [x] 添加 CMake 依赖: protobuf, libevent, kcp
- [x] 定义 protobuf 消息 (`proto/common.proto`, `proto/messages.proto`)
- [x] 编写 protobuf CMake 构建规则 (`proto/CMakeLists.txt`)
- [x] 实现 `KcpSession` 类 (`include/dota/network/kcp_session.hpp`)
  - [x] KCP 创建和配置 (fast mode)
  - [x] 发送和接收接口
  - [x] 与 libevent 定时器集成 (ikcp_update)
- [x] 实现 `ServerNetwork` 类 (`include/dota/network/server_network.hpp`)
  - [x] UDP socket 创建和绑定
  - [x] libevent 事件循环
  - [x] 会话管理 (endpoint -> client_id -> KcpSession)
  - [x] 消息接收和分发
- [x] 编写单元测试
- [x] 编写简单的 echo server 和 client 验证通信

**Status**: Complete ✅ (2026-06-05)

详见 `server/STAGE1_COMPLETED.md`

---

## Stage 2: 服务器核心架构

**Goal**: 实现服务器主框架，包括连接管理、消息处理器、游戏会话容器

**Success Criteria**:
- 支持客户端连接、断开、重连
- 消息处理器能够根据 protobuf oneof 类型分发到对应处理函数
- 支持多个独立的游戏会话 (房间)
- 心跳机制工作正常，超时自动断开

**Tests**:
- `test_message_handler.cpp`: 消息分发逻辑
- `test_game_server.cpp`: 会话创建和销毁
- `test_connection_management.cpp`: 连接超时和重连

**Tasks**:
- [x] 实现 `GameServerHandler` 消息处理器
  - [x] 处理 Connect: 创建 PlayerState，发送 Welcome
  - [x] 处理 Ping: 发送 Pong，更新延迟统计
  - [x] 处理 Input: 占位实现（Stage 3 完善）
  - [x] 处理 Disconnect: 清理玩家状态
  - [x] 处理 ChooseSkill: 占位实现（Stage 4 完善）
- [x] 实现 `PlayerState` 结构体
  - [x] 基础信息：player_id, player_name, client_version
  - [x] 网络状态：connected, last_ping_time, last_activity_time, latency
  - [x] 游戏状态：level, exp, gold
- [x] 实现连接流程
  - [x] Connect → Welcome 握手
  - [x] 发送在线玩家列表
- [x] 实现心跳机制
  - [x] Ping → Pong 响应
  - [x] RTT 计算
- [x] 实现超时检测
  - [x] 每个 tick 检查客户端活动时间
  - [x] 30 秒超时自动断开
- [x] 优化服务器 tick 循环
  - [x] 添加 `ServerNetwork::tick()` 非阻塞事件处理
  - [x] 在主循环中调用，简化线程模型
- [x] 编写测试客户端
  - [x] `simple_client` 验证完整通信流程
- [x] 验证
  - [x] 编译通过
  - [x] 端到端测试通过（Connect → Welcome → Ping → Pong）

**Status**: Complete ✅ (2026-06-05)

详见 `server/STAGE2_COMPLETED.md`
  - [ ] 主 tick 循环 (30Hz)
- [ ] 实现 `PlayerState` 类 (`include/dota/server/player_state.hpp`)
  - [ ] 玩家基础信息 (ID, 名字, Unit 引用)
  - [ ] 网络状态 (延迟, 最后心跳时间)
- [ ] 实现心跳机制
  - [ ] 服务器定期检查客户端超时
  - [ ] 客户端定期发送 C2S_Ping
  - [ ] 延迟统计
- [ ] 实现连接流程
  - [ ] C2S_Connect -> 分配 player_id -> S2C_Welcome
  - [ ] C2S_Disconnect -> 清理会话
- [ ] 编写测试

**Status**: Not Started

---

## Stage 3: 游戏会话和状态同步

**Goal**: 实现 GameSession, 将 World 集成到服务器, 实现基础的状态快照同步

**Success Criteria**:
- GameSession 能够创建和管理 World 实例
- 服务器以 30Hz tick World
- 服务器以 20Hz 向客户端发送状态快照
- 客户端能够接收快照并重建游戏场景
- 基础的移动输入能够工作 (客户端发送移动指令, 服务器更新 Unit 位置)

**Tests**:
- `test_game_session.cpp`: World tick 和快照生成
- `test_snapshot_serialization.cpp`: 快照序列化大小和正确性
- `test_input_processing.cpp`: 输入指令处理

**Tasks**:
- [ ] 实现 `GameSession` 类 (`include/dota/server/game_session.hpp`)
  - [ ] 持有 World 实例
  - [ ] 玩家列表管理
  - [ ] tick 循环 (调用 World::tick)
  - [ ] 输入队列和处理
- [ ] 实现快照生成 (`Snapshot create_snapshot()`)
  - [ ] 遍历 World 中所有 Unit
  - [ ] 提取关键状态: position, velocity, health, team
  - [ ] 序列化为 S2C_Snapshot protobuf
- [ ] 实现输入处理 (`void on_player_input(...)`)
  - [ ] MoveCommand: 调用 Unit::issue_order(OrderMoveToPoint)
  - [ ] UseAbilityCommand: 调用 Ability::cast
- [ ] 优化快照大小
  - [ ] 只发送客户端可见的实体
  - [ ] 浮点数量化 (位置精度 0.1, 血量百分比)
- [ ] 实现基础的客户端 (用于测试)
  - [ ] 连接服务器
  - [ ] 发送移动输入
  - [ ] 接收快照并打印实体状态
- [ ] 编写集成测试 (服务器 + 客户端)

**Status**: Not Started

---

## Stage 4: 生存模式逻辑

**Goal**: 实现选技生存游戏模式的核心玩法循环

**Success Criteria**:
- 敌人持续从边缘刷新并追击玩家
- 击杀敌人获得经验, 掉落经验宝石
- 玩家升级时游戏暂停, 发送技能选项
- 客户端选择技能后服务器学习/升级技能
- 玩家死亡后游戏结束并显示统计

**Tests**:
- `test_wave_spawner.cpp`: 刷怪逻辑
- `test_experience_system.cpp`: 经验和升级
- `test_skill_selection.cpp`: 技能池和选项生成
- `test_survivor_game_mode.cpp`: 完整游戏循环

**Tasks**:
- [ ] 实现 `SurvivorGameMode` 类 (`include/dota/server/survivor_mode.hpp`)
  - [ ] 游戏状态管理 (Running, Paused, GameOver)
  - [ ] 游戏时间计时
  - [ ] 玩家生命管理
- [ ] 实现 `WaveSpawner` (`include/dota/server/wave_spawner.hpp`)
  - [ ] 根据游戏时间调整刷怪频率和强度
  - [ ] 在屏幕边缘外生成敌人
  - [ ] 敌人类型池 (近战, 远程, 坦克, 精英)
  - [ ] 简单 AI: 追击最近的玩家
- [ ] 实现 `ExperienceSystem` (`include/dota/server/experience_system.hpp`)
  - [ ] 经验值计算 (基于敌人等级)
  - [ ] 升级阈值曲线
  - [ ] 升级事件触发
- [ ] 实现 `SkillPool` (`include/dota/server/skill_pool.hpp`)
  - [ ] 定义可选技能列表
  - [ ] 技能等级和前置条件
  - [ ] 随机生成 3-4 个合法选项
  - [ ] 技能学习和升级
- [ ] 实现掉落物系统 (`include/dota/server/pickup_system.hpp`)
  - [ ] 敌人死亡时生成经验宝石
  - [ ] 玩家靠近自动拾取 (或磁吸)
  - [ ] 经验宝石类型 (小, 中, 大)
- [ ] 实现升级流程
  - [ ] 检测玩家经验达到阈值
  - [ ] 暂停该玩家的游戏 (标记 PlayerState)
  - [ ] 生成技能选项并发送 S2C_LevelUp
  - [ ] 接收 C2S_ChooseSkill 并应用
  - [ ] 恢复游戏
- [ ] 集成到 GameSession
  - [ ] tick 时调用 SurvivorGameMode::update
  - [ ] 处理升级消息
  - [ ] 广播游戏事件 (S2C_GameEvent)
- [ ] 编写测试

**Status**: Not Started

---

## Stage 5: 优化和压力测试

**Goal**: 优化性能, 减少带宽, 支持 4-8 人同时游戏, 稳定性测试

**Success Criteria**:
- 服务器 CPU 占用 < 30% (8 玩家)
- 单客户端带宽 < 50 KB/s (下行)
- 延迟稳定在 < 100ms (局域网)
- 无内存泄漏
- 支持客户端中途加入和离开

**Tests**:
- `test_performance.cpp`: 性能基准测试
- `test_bandwidth.cpp`: 带宽统计
- `test_stress.cpp`: 压力测试 (多客户端)
- `test_reconnect.cpp`: 重连和断线恢复

**Tasks**:
- [ ] 实现增量快照 (DeltaSnapshot)
  - [ ] 客户端缓存最近几个快照
  - [ ] 服务器只发送变化的实体
  - [ ] removed / updated / new 三类增量
- [ ] 实现快照压缩
  - [ ] 位置量化 (0.1 单位精度)
  - [ ] 血量百分比 (uint8)
  - [ ] 只发送可见实体 (AOI 兴趣范围管理)
- [ ] 实现 AOI (Area of Interest)
  - [ ] 九宫格或其他空间分区
  - [ ] 只向客户端发送附近实体
- [ ] KCP 参数调优
  - [ ] 根据延迟动态调整窗口大小
  - [ ] 拥塞控制参数
- [ ] 实现对象池
  - [ ] Unit, Projectile, Modifier 对象池
  - [ ] 避免频繁 new/delete
- [ ] 多线程优化 (可选)
  - [ ] 网络线程和游戏逻辑线程分离
  - [ ] 无锁队列传递消息
- [ ] 实现重连机制
  - [ ] 客户端断开后保留会话 N 秒
  - [ ] 重连时发送最近快照 + 输入历史
- [ ] 编写压力测试工具
  - [ ] 模拟多个客户端连接
  - [ ] 随机移动和施法
  - [ ] 统计延迟, 丢包, 带宽
- [ ] 内存泄漏检测
  - [ ] Valgrind / AddressSanitizer
  - [ ] 长时间运行测试
- [ ] 编写性能分析
  - [ ] 火焰图 (perf / gperftools)
  - [ ] 识别热点函数

**Status**: Not Started

---

## 完成标准

全部阶段完成后, 服务器应该达到:

- [ ] 支持 4-8 人同时游戏
- [ ] 稳定运行 1 小时无崩溃
- [ ] 客户端延迟 < 100ms
- [ ] 带宽 < 50 KB/s per client
- [ ] 基础的选技生存玩法完整可玩
- [ ] 通过所有单元测试和集成测试

## 备注

- 每个阶段完成后更新 Status 为 `In Progress` 或 `Complete`
- 每个阶段结束后提交 git commit
- 测试先行: 优先编写测试用例再实现功能
- 遇到阻塞超过 3 次尝试时, 记录问题并寻求其他方案
