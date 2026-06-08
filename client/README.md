# Game Client

基于 raylib 的多人在线生存模式游戏客户端.

## 架构

```
main loop (60 FPS)
├── NetworkClient     - KCP + Protobuf 网络通信
├── GameState         - 服务器快照 + 客户端预测
├── InputHandler      - 鼠标/键盘输入 -> 发送指令
└── Renderer          - raylib 渲染 (相机跟随, 实体, UI)
```

## 目录结构

```
client/
├── include/client/
│   ├── network_client.hpp   # 网络层 (连接, 收发消息, 回调)
│   ├── game_state.hpp       # 世界状态管理, 客户端预测
│   ├── renderer.hpp         # raylib 渲染, 相机, UI
│   └── input_handler.hpp    # 输入处理
├── src/
│   ├── main.cpp             # 主循环
│   ├── network_client.cpp
│   ├── game_state.cpp
│   ├── renderer.cpp
│   └── input_handler.cpp
└── assets/                  # 资源文件
```

## 构建和运行

```bash
# 从项目根目录构建 (需要 raylib, 自动通过 CPM 获取)
cmake -B build
cmake --build build --target game_client -j

# 运行 (默认连接 localhost:7777)
./build/client/game_client

# 指定玩家名和服务器地址
./build/client/game_client <PlayerName> <host> <port>
```

## 操作

- **右键**: 移动到鼠标位置
- **Q/W/E/R/D/F**: 使用技能槽 0-5 (朝鼠标方向)
- **S**: 停止当前动作
- **ESC**: 退出游戏

## 网络同步

- 通过 KCP 可靠 UDP 连接服务器
- 接收服务器 10Hz 状态快照 (完整或增量)
- 在快照之间做客户端预测 (按 move_speed 向目标点插值)
- 收到新快照时用服务器位置校正本地状态

## 依赖

- **raylib**: 渲染和输入 (通过 CPM 自动获取)
- **protobuf**: 消息序列化
- **libevent**: 网络 I/O
- **KCP**: 可靠 UDP 传输
