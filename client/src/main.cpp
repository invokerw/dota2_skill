// src/main.cpp
// 客户端主程序

#include "client/network_client.hpp"
#include "client/game_state.hpp"
#include "client/renderer.hpp"
#include "client/input_handler.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace dota::client;

int main(int argc, char* argv[]) {
  std::cout << "Dota2 Skill - Game Client\n";
  std::cout << "==========================\n\n";

  // 解析参数
  std::string host = "127.0.0.1";
  uint16_t port = 7777;
  std::string player_name = "Player";

  if (argc > 1) player_name = argv[1];
  if (argc > 2) host = argv[2];
  if (argc > 3) port = std::atoi(argv[3]);

  std::cout << "Connecting to " << host << ":" << port << " as " << player_name << "\n\n";

  // 初始化渲染器
  Renderer renderer;
  if (!renderer.init(1280, 720, "Dota2 Skill - Survivor Mode")) {
    std::cerr << "Failed to initialize renderer\n";
    return 1;
  }

  // 初始化游戏状态
  GameState game_state;

  // 连接服务器
  NetworkClient client(host, port);
  if (!client.connect(player_name)) {
    std::cerr << "Failed to connect to server\n";
    return 1;
  }

  // 设置回调
  client.set_welcome_callback([&game_state](uint32_t player_id) {
    game_state.set_player_id(player_id);
    std::cout << "[Client] Joined game as player " << player_id << "\n";
  });

  client.set_snapshot_callback([&game_state](const dota::network::S2C_Snapshot& snapshot) {
    game_state.apply_snapshot(snapshot);
  });

  client.set_delta_snapshot_callback([&game_state](const dota::network::S2C_DeltaSnapshot& delta) {
    game_state.apply_delta_snapshot(delta);
  });

  // 输入处理器
  InputHandler input_handler(&client, &renderer);

  // 主循环
  auto last_ping = std::chrono::steady_clock::now();
  auto last_frame = std::chrono::steady_clock::now();

  std::cout << "[Client] Entering main loop\n";

  while (!renderer.should_close()) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame).count();
    last_frame = now;

    // 1. 网络更新（接收快照）
    client.update();

    // 2. 输入处理
    input_handler.process();

    // 3. 客户端预测（可选）
    game_state.predict(dt);

    // 4. 渲染
    renderer.begin_draw();
    renderer.draw(game_state);
    renderer.draw_ui(game_state.player_id(), client.latency());
    renderer.end_draw();

    // 5. 定期发送 Ping
    auto ping_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_ping).count();
    if (ping_elapsed >= 1) {
      client.send_ping();
      last_ping = now;
    }

    // 限制帧率（raylib 会自动处理）
  }

  std::cout << "\n[Client] Shutting down\n";
  client.disconnect();
  renderer.shutdown();

  return 0;
}
