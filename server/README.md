# Dota2 Skill - Game Server

多人在线生存游戏服务器，基于 libevent + KCP + Protobuf 实现。

## 架构

```
Client 1, 2, 3...
    ↓ (UDP + KCP)
ServerNetwork (libevent 事件循环)
    ↓
MessageHandler (protobuf 消息分发)
    ↓
GameServer (会话管理)
    ↓
GameSession (游戏房间)
    ↓
World + SurvivorGameMode (游戏逻辑，复用核心库)
```

## 技术栈

- **网络**: libevent 2.x (事件驱动)
- **传输**: KCP (可靠 UDP，低延迟)
- **序列化**: Protobuf 3.x (类型安全)
- **游戏逻辑**: dota_core, dota_ability, dota_modifier 等核心库

## 目录结构

```
server/
├── proto/              # Protobuf 协议定义
├── include/server/     # 服务器头文件
├── src/                # 服务器实现
├── test/               # 单元测试
├── tools/              # 工具 (压力测试客户端等)
├── config/             # 配置文件
└── CMakeLists.txt
```

## 构建

```bash
# 从项目根目录
cmake -B build -DBUILD_SERVER=ON
cmake --build build -j

# 运行服务器
./build/server/game_server --port 7777

# 运行测试
ctest --test-dir build -R "server_"
```

## 依赖

- dota_core (游戏核心库)
- libevent (>= 2.1)
- protobuf (>= 3.0)
- kcp (ikcp.h/ikcp.c, 内置在 third_party/)

## 配置

服务器配置文件: `config/server.yaml`

```yaml
server:
  port: 7777
  max_players: 8
  tick_rate: 30          # 游戏逻辑 30Hz
  snapshot_rate: 20      # 快照发送 20Hz
  timeout: 30            # 客户端超时时间 (秒)

game:
  mode: survivor
  map_size: 2000
  spawn_distance: 1200
```

## 开发

参考主项目的 `IMPLEMENTATION_PLAN.md` 了解实施计划。
