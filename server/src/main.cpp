#include "server/server/game_server.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>

namespace {
std::atomic<bool> g_running{true};

void signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    std::cout << "\nShutting down server...\n";
    g_running = false;
  }
}
}

int main(int argc, char* argv[]) {
  // 注册信号处理
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  std::cout << "Dota2 Skill - Game Server\n";
  std::cout << "========================\n\n";

  // 解析命令行参数
  uint16_t port = 7777;
  if (argc > 1) {
    port = static_cast<uint16_t>(std::atoi(argv[1]));
  }

  std::cout << "Starting server on port " << port << "...\n";

  // 创建服务器
  auto server = std::make_unique<dota::server::GameServer>(port);
  if (!server->start()) {
    std::cerr << "Failed to start server\n";
    return 1;
  }

  std::cout << "Server running. Press Ctrl+C to stop.\n\n";

  // 主线程运行游戏逻辑 tick（包含网络事件处理）
  while (g_running && server->is_running()) {
    server->tick();
  }

  server->stop();

  std::cout << "Server exited cleanly.\n";

  return 0;
}
