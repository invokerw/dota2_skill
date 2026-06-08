# Game Server

多人在线生存模式游戏服务器, 基于 libevent + KCP + Protobuf 实现.

## 架构

```
Client 1, 2, 3...
    | (UDP + KCP)
ServerNetwork (libevent 事件循环)
    |
GameServerHandler (protobuf 消息分发)
    |
GameServer (会话管理, 30Hz tick)
    |
GameSession (游戏房间, 持有 World)
    |
SurvivorGameMode (波次刷怪, 经验升级, 技能选择)
```

## 目录结构

```
server/
├── proto/              # Protobuf 协议定义
│   ├── common.proto    # 基础类型 (Vec2, EntityType, Team)
│   └── messages.proto  # 网络消息 (Input, Snapshot, LevelUp...)
├── include/server/
│   ├── network/        # KcpSession, ServerNetwork, MessageHandler
│   ├── server/         # GameServer, GameSession, GameServerHandler, PlayerState
│   └── mode/           # SurvivorGameMode, WaveSpawner, ExperienceSystem, SkillPool
├── src/
│   ├── main.cpp        # 服务器入口
│   ├── network/        # 网络层实现
│   ├── server/         # 服务器逻辑实现
│   └── mode/           # 生存模式实现
├── test/               # 单元测试
├── tools/              # 简易测试客户端, 压力测试
└── config/
    └── server.yaml     # 服务器配置
```

## 构建和运行

```bash
# 从项目根目录构建
cmake -B build
cmake --build build -j

# 启动服务器 (监听 UDP 7777)
./build/server/game_server

# 启动测试客户端
./build/server/simple_client
```

## 依赖

- **dota_core**: 游戏核心库 (World, Unit, Ability, Modifier)
- **libevent** (>= 2.1): 跨平台事件驱动 I/O
- **protobuf** (>= 3.0): 消息序列化
- **KCP**: 低延迟可靠 UDP 传输 (通过 CPM 自动获取)

安装系统依赖:

```bash
# macOS
brew install libevent protobuf

# Ubuntu/Debian
sudo apt-get install libevent-dev libprotobuf-dev protobuf-compiler
```

## 网络协议

使用 Protobuf 定义客户端-服务器消息:

- `C2S_Connect` - 连接请求, 携带玩家名
- `S2C_Welcome` - 分配 player_id, 会话信息
- `C2S_Input` - 移动/技能/停止指令
- `S2C_Snapshot` - 完整世界状态快照
- `S2C_DeltaSnapshot` - 增量更新 (变化的实体)
- `S2C_LevelUp` - 升级技能选择
- `C2S_ChooseSkill` - 选择技能
- `C2S_Ping / S2C_Pong` - 心跳和延迟测量

## 游戏逻辑

### 输入处理

客户端指令通过 order queue 驱动:

- **移动**: `OrderMoveToPoint` -> A* 寻路 + WallTracer 避障
- **技能**: `OrderCastPoint` / `OrderCastTarget` / `OrderCastNoTarget` -> 走到施法距离再释放
- **停止**: `OrderStop` -> 清空队列

### 生存模式 (SurvivorGameMode)

- **波次刷怪**: WaveSpawner 按波次在地图边缘生成敌人, 难度递增
- **经验系统**: 击杀敌人获得经验, 升级触发技能选择
- **技能池**: 每次升级从随机 3 个技能中选择学习或升级
- **玩家复活**: 死亡后 5 秒在出生点满血复活

### 状态同步

- 服务器 30Hz tick 驱动 World
- 每 3 tick (10Hz) 广播增量快照给客户端
- 新连接或丢失过多 tick 时发送完整快照

## 配置

服务器配置: `config/server.yaml`

```yaml
server:
  port: 7777
  max_players: 8
  tick_rate: 30
  timeout: 30       # 客户端超时 (秒)

game:
  mode: survivor
  map_size: 3200
```
