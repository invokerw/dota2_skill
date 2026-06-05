# 服务器开发快速入门

## 前置要求

### 系统依赖

**macOS:**
```bash
brew install libevent protobuf cmake
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libevent-dev libprotobuf-dev protobuf-compiler cmake build-essential
```

**Arch Linux:**
```bash
sudo pacman -S libevent protobuf cmake
```

### 项目依赖

KCP 会自动通过 CPM 下载，无需手动安装。

## 首次构建

```bash
# 从项目根目录
cd /path/to/dota2_skill

# 配置构建（启用服务器）
cmake -B build -DBUILD_SERVER=ON

# 构建
cmake --build build -j

# 运行服务器（占位版本）
./build/server/game_server
```

**注意**: 当前服务器只是占位程序，会提示 "Server is not implemented yet"。

## 开发流程

### 1. 实现 Stage 1 - 网络基础设施

#### 步骤 1.1: 实现 KcpSession

编辑 `server/src/network/kcp_session.cpp`，参考之前我设计的完整实现。

关键点：
- 包含 `ikcp.h` 和 `event2/event.h`
- 实现构造函数：创建 KCP 对象，配置参数，注册 libevent 定时器
- 实现 `send()`: 调用 `ikcp_send()` 和 `ikcp_flush()`
- 实现 `input()`: 调用 `ikcp_input()` 并处理接收数据
- 实现 `update()`: 调用 `ikcp_update()`

#### 步骤 1.2: 实现 ServerNetwork

编辑 `server/src/network/server_network.cpp`。

关键点：
- `start()`: 创建 UDP socket，绑定端口，注册 libevent 读事件
- `on_udp_read()`: 接收 UDP 包，分发到对应 KcpSession
- `create_session()`: 为新连接创建 KcpSession
- `send_to_client()`: Protobuf 序列化后通过 KCP 发送

#### 步骤 1.3: 实现 MessageHandler 基础版

编辑 `server/src/network/message_handler.cpp`（创建一个简单的日志实现用于测试）。

#### 步骤 1.4: 编写测试

取消注释 `server/test/test_kcp_session.cpp` 中的测试用例并补充。

运行测试：
```bash
ctest --test-dir build -R "server_test" --output-on-failure
```

#### 步骤 1.5: 编写 Echo Server

创建 `server/tools/echo_server.cpp` 和 `server/tools/echo_client.cpp` 验证通信：

```cpp
// echo_server.cpp - 简化示例
int main() {
  ServerNetwork network(7777);
  network.start();
  
  // 简单的 echo handler
  class EchoHandler : public MessageHandler {
    void on_message(uint32_t client_id, const Packet& packet) override {
      // 原样返回
      network_->send_to_client(client_id, packet);
    }
  };
  
  network.set_message_handler(std::make_shared<EchoHandler>());
  network.run();
}
```

### 2. 调试技巧

#### 启用详细日志

在 KCP 和 ServerNetwork 中添加日志：
```cpp
#include <iostream>
std::cout << "[KCP] Send " << len << " bytes\n";
```

#### 使用 Wireshark 抓包

```bash
# 抓取 UDP 7777 端口
sudo tcpdump -i lo0 udp port 7777 -w server.pcap
```

#### 使用 lldb/gdb 调试

```bash
lldb ./build/server/game_server
(lldb) b ServerNetwork::on_udp_read
(lldb) run
```

### 3. 常见问题

**Q: 编译时找不到 libevent**
```
A: 确保安装了 libevent-dev，并且 CMake 能找到它：
   cmake -B build -DBUILD_SERVER=ON -DCMAKE_PREFIX_PATH=/usr/local
```

**Q: 编译时找不到 protobuf**
```
A: 检查 protoc 版本：
   protoc --version
   如果版本不匹配，指定 Protobuf_ROOT：
   cmake -B build -DProtobuf_ROOT=/usr/local
```

**Q: KCP 相关的链接错误**
```
A: 确保 third_party/kcp/ikcp.c 存在。如果不存在：
   rm -rf build/_deps/kcp*
   cmake -B build -DBUILD_SERVER=ON  # 重新下载
```

**Q: Protobuf 生成的代码编译警告太多**
```
A: proto/CMakeLists.txt 中已配置忽略警告，如果还有问题，检查
   set_source_files_properties() 是否生效
```

### 4. 代码风格

遵循主项目的 `CLAUDE.md` 规范：
- 使用中文注释
- 英文标点符号
- 命名：snake_case (函数/变量), PascalCase (类/枚举)
- 每个阶段完成后提交 git commit

### 5. 测试策略

每个模块实现后立即编写测试：

```bash
# 只运行服务器测试
ctest --test-dir build -R "server_" --output-on-failure

# 运行特定测试
ctest --test-dir build -R "KcpSessionTest" --output-on-failure

# 详细输出
ctest --test-dir build -R "server_" --verbose
```

### 6. 性能检查

使用 perf 或 Instruments (macOS) 分析性能：

```bash
# Linux
perf record -g ./build/server/game_server
perf report

# macOS
instruments -t "Time Profiler" ./build/server/game_server
```

## 进度跟踪

在 `IMPLEMENTATION_PLAN.md` 中更新每个任务的状态：
- `[ ]` -> `[x]` 标记已完成的任务
- 更新 Stage Status: `Not Started` -> `In Progress` -> `Complete`

## 获取帮助

- 查看 `server/STATUS.md` 了解当前进度
- 查看 `IMPLEMENTATION_PLAN.md` 了解详细计划
- 查看主项目 `CLAUDE.md` 和 `AGENTS.md` 了解项目约定

## 下一步

完成 Stage 1 后，继续实施 Stage 2: 服务器核心架构。每个阶段结束时：
1. 运行所有测试
2. 更新 `STATUS.md`
3. 提交 git commit
4. 继续下一阶段
