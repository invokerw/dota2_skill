// src/server/game_server.cpp
// 游戏服务器主类实现 - Stage 3

#include "server/server/game_server.hpp"
#include "server/server/game_server_handler.hpp"
#include "server/server/game_session.hpp"
#include "server/network/server_network.hpp"
#include <iostream>
#include <thread>
#include <chrono>

namespace dota::server {

namespace {
// 30Hz tick rate
constexpr float kTickRate = 30.0f;
constexpr auto kTickInterval = std::chrono::milliseconds(static_cast<int>(1000.0f / kTickRate));
}

GameServer::GameServer(uint16_t port) : port_(port) {
  // 创建网络层
  network_ = std::make_unique<dota::network::ServerNetwork>(port);

  // 创建消息处理器
  handler_ = std::make_shared<GameServerHandler>(this);
  network_->set_message_handler(handler_);
}

GameServer::~GameServer() {
  stop();
}

bool GameServer::start() {
  if (!network_->start()) {
    std::cerr << "[GameServer] Failed to start network layer\n";
    return false;
  }

  running_ = true;
  std::cout << "[GameServer] Server started successfully\n";
  return true;
}

void GameServer::stop() {
  if (!running_) return;

  running_ = false;
  network_->stop();

  std::cout << "[GameServer] Server stopped\n";
}

void GameServer::tick() {
  if (!running_) return;

  auto start_time = std::chrono::steady_clock::now();

  // 处理网络事件（非阻塞）
  network_->tick();

  // Tick 消息处理器
  handler_->tick();

  // Tick 所有游戏会话
  float dt = kTickInterval.count() / 1000.0f;  // 转换为秒
  for (auto& [session_id, session] : sessions_) {
    session->tick(dt);
  }

  // 固定帧率控制
  auto elapsed = std::chrono::steady_clock::now() - start_time;
  if (elapsed < kTickInterval) {
    std::this_thread::sleep_for(kTickInterval - elapsed);
  }
}

void GameServer::send_to_client(uint32_t client_id, const dota::network::Packet& packet) {
  network_->send_to_client(client_id, packet);
}

void GameServer::disconnect_client(uint32_t client_id) {
  // TODO: 实现断开客户端的逻辑
  // network_->disconnect_client(client_id);
  std::cout << "[GameServer] Request to disconnect client " << client_id << "\n";
}

GameSession* GameServer::get_or_create_session(uint32_t session_id) {
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    return it->second.get();
  }

  // 创建新会话
  auto session = std::make_unique<GameSession>(session_id, "default_map");
  auto* ptr = session.get();
  sessions_[session_id] = std::move(session);

  std::cout << "[GameServer] Created session " << session_id << "\n";
  return ptr;
}

GameSession* GameServer::get_session(uint32_t session_id) {
  auto it = sessions_.find(session_id);
  return (it != sessions_.end()) ? it->second.get() : nullptr;
}

void GameServer::remove_session(uint32_t session_id) {
  sessions_.erase(session_id);
  std::cout << "[GameServer] Removed session " << session_id << "\n";
}

GameSession* GameServer::default_session() {
  // Stage 3: 简化实现，所有玩家在会话 0
  return get_or_create_session(0);
}

} // namespace dota::server
