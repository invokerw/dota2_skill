# 联网测试指南

## 测试环境要求

### 服务器端
- Linux 服务器（当前已编译）
- 监听 UDP 端口 7777

### 客户端
- **需要图形界面** - raylib 需要 X11 或 Wayland
- 本地 Linux 桌面环境
- 或 Windows/Mac（需重新编译）

---

## 测试步骤

### 方案 1: 本地测试（推荐）

**1. 在本地机器克隆代码**:
```bash
git clone <你的仓库>
cd dota2_skill
```

**2. 安装依赖**:
```bash
# Ubuntu/Debian
sudo apt install -y \
  build-essential cmake \
  libprotobuf-dev protobuf-compiler \
  libevent-dev \
  libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libgl1-mesa-dev

# Fedora/RHEL
sudo dnf install -y \
  gcc-c++ cmake \
  protobuf-devel libevent-devel \
  libX11-devel libXrandr-devel libXinerama-devel \
  libXcursor-devel libXi-devel mesa-libGL-devel
```

**3. 编译**:
```bash
cmake -B build -DBUILD_SERVER=ON -DBUILD_CLIENT=ON -DBUILD_VISUAL=ON
cmake --build build -j4
```

**4. 启动服务器（终端 1）**:
```bash
./build/server/game_server
```

**5. 启动客户端（终端 2）**:
```bash
./build/client/game_client Player1 127.0.0.1 7777
```

**6. 启动第二个客户端（终端 3，可选）**:
```bash
./build/client/game_client Player2 127.0.0.1 7777
```

---

### 方案 2: 远程服务器 + SSH X11 转发

**1. SSH 连接时启用 X11 转发**:
```bash
ssh -X user@server
```

**2. 设置 DISPLAY**:
```bash
export DISPLAY=:0
```

**3. 运行客户端**:
```bash
./build/client/game_client
```

> ⚠️ **注意**: X11 转发延迟较高，可能影响游戏体验

---

### 方案 3: 使用 simple_client 测试（无 UI）

**验证网络通信**:
```bash
# 启动服务器
./build/server/game_server &

# 运行简单客户端（无 UI）
./build/server/simple_client
```

**预期输出**:
```
[Client] Connected to 127.0.0.1:7777
[Client] Sent Connect: TestPlayer
[Client] Received message (seq=1)
  -> Welcome: player_id=1, players=1
[Client] Sent Ping
[Client] Received message (seq=2)
  -> Pong: RTT=35ms
```

---

## 测试清单

### 基础功能测试

- [ ] **连接测试**
  - 客户端成功连接服务器
  - 显示 Welcome 消息和 player_id
  
- [ ] **渲染测试**
  - 窗口正常打开（1280x720）
  - 看到蓝色圆圈（自己）
  - 网格背景显示
  - UI 显示正常（ID/延迟/FPS）

- [ ] **移动测试**
  - 右键点击，角色移动
  - 相机跟随玩家
  - 移动平滑无卡顿

- [ ] **快照同步测试**
  - 服务器生成快照（控制台输出）
  - 客户端接收快照
  - 实体位置正确同步

- [ ] **多客户端测试**
  - 启动两个客户端
  - 看到对方的角色
  - 双方移动都能看到

- [ ] **敌人显示测试**
  - 看到红色圆圈（敌人）
  - 敌人有血条
  - 敌人会移动

### 高级功能测试

- [ ] **战斗测试**
  - 按 Q/W/E/R/D/F 使用技能
  - 技能指令发送到服务器
  - （当前服务器未实现技能效果）

- [ ] **击杀测试**
  - 击杀敌人（需要服务器实现攻击）
  - 获得经验
  - 升级

- [ ] **性能测试**
  - FPS 稳定在 60
  - 延迟 < 100ms（本地）
  - 内存占用稳定

---

## 已知问题

### 1. 服务器未实现移动

**现象**: 右键移动，角色不动

**原因**: GameSession::handle_move_command 是占位实现
```cpp
// TODO: 发出移动指令
// unit->move_to_position(target);
```

**解决**: 需要实现单位移动逻辑

---

### 2. 技能无效果

**现象**: 按 Q/W/E/R，技能无效果

**原因**: 
1. GameSession::handle_use_ability_command 是占位
2. 服务器没有给玩家添加技能

**解决**: 
1. 实现技能使用逻辑
2. 玩家出生时添加默认技能

---

### 3. 客户端无技能选择 UI

**现象**: 升级后无法选择技能

**原因**: 客户端未实现 S2C_LevelUp 消息处理

**解决**: 添加技能选择界面

---

## 调试技巧

### 服务器日志

查看服务器输出：
```bash
[ServerNetwork] Server started on port 7777
[GameSession] Created session 0
[GameServerHandler] Client 1 connected
[GameServerHandler] Client 1 connect: Player1
[GameSession] Added player 1 (Player1) unit=2
[GameSession] Generated snapshot tick=3 entities=2
```

### 客户端日志

客户端会打印：
```bash
[NetworkClient] Connected to 127.0.0.1:7777
[NetworkClient] Welcome! player_id=1
[Client] Joined game as player 1
```

### 网络抓包

使用 tcpdump 查看通信：
```bash
sudo tcpdump -i lo -nn udp port 7777
```

---

## 性能基准

**目标**:
- 客户端 FPS: 60
- 网络延迟: < 50ms (本地)
- 网络延迟: < 200ms (公网)
- 内存占用: < 100MB

**实际**:
- 待测试

---

## 下一步

根据测试结果：

1. **如果连接失败** → 检查防火墙、端口
2. **如果渲染异常** → 检查 OpenGL 驱动
3. **如果移动无效** → 实现服务器移动逻辑
4. **如果一切正常** → 开始实现技能系统和战斗逻辑

---

**测试时间**: 预计 30 分钟
**测试人员**: 需要本地图形环境
**测试报告**: 记录测试结果和截图
