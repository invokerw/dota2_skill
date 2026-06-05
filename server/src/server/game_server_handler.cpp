// src/server/game_server_handler.cpp
// 游戏服务器消息处理器实现 - Stage 3

#include "server/server/game_server_handler.hpp"
#include "server/server/game_server.hpp"
#include "server/server/game_session.hpp"
#include <iostream>

namespace dota::server {

GameServerHandler::GameServerHandler(GameServer* server) : server_(server) {}

void GameServerHandler::on_client_connected(uint32_t client_id) {
  std::cout << "[GameServerHandler] Client " << client_id << " connected\n";

  // 创建玩家状态
  players_[client_id] = PlayerState{};
  players_[client_id].player_id = client_id;
  players_[client_id].connected = true;
  players_[client_id].last_ping_time = get_current_time_ms();
}

void GameServerHandler::on_client_disconnected(uint32_t client_id) {
  std::cout << "[GameServerHandler] Client " << client_id << " disconnected\n";

  // 移除玩家状态
  players_.erase(client_id);
}

void GameServerHandler::on_message(uint32_t client_id, const dota::network::Packet& packet) {
  // 更新最后活动时间
  auto it = players_.find(client_id);
  if (it != players_.end()) {
    it->second.last_activity_time = get_current_time_ms();
  }

  // 根据消息类型分发
  if (packet.has_connect()) {
    handle_connect(client_id, packet.connect());
  } else if (packet.has_ping()) {
    handle_ping(client_id, packet.ping());
  } else if (packet.has_input()) {
    handle_input(client_id, packet.input());
  } else if (packet.has_disconnect()) {
    handle_disconnect(client_id, packet.disconnect());
  } else if (packet.has_choose_skill()) {
    handle_choose_skill(client_id, packet.choose_skill());
  } else {
    std::cout << "[GameServerHandler] Unknown message type from client " << client_id << "\n";
  }
}

void GameServerHandler::handle_connect(uint32_t client_id, const dota::network::C2S_Connect& msg) {
  std::cout << "[GameServerHandler] Client " << client_id
            << " connect: " << msg.player_name() << "\n";

  auto it = players_.find(client_id);
  if (it == players_.end()) return;

  // 保存玩家信息
  it->second.player_name = msg.player_name();
  it->second.client_version = msg.version();

  // 将玩家添加到默认游戏会话
  GameSession* session = server_->default_session();
  if (session) {
    session->add_player(client_id, msg.player_name());
    it->second.unit_id = session->get_player_unit(client_id);
  }

  // 发送 Welcome 消息
  dota::network::Packet response;
  response.set_sequence(next_sequence_++);
  response.set_timestamp(get_current_time_ms());

  auto* welcome = response.mutable_welcome();
  welcome->set_player_id(client_id);
  welcome->set_session_id(0);  // 默认会话
  welcome->set_server_tick(server_tick_);

  // 添加其他在线玩家信息
  for (const auto& [pid, pstate] : players_) {
    if (pid != client_id && pstate.connected) {
      auto* player_info = welcome->add_players();
      player_info->set_player_id(pid);
      player_info->set_player_name(pstate.player_name);
      player_info->set_level(pstate.level);
    }
  }

  server_->send_to_client(client_id, response);
}

void GameServerHandler::handle_ping(uint32_t client_id, const dota::network::C2S_Ping& msg) {
  // 发送 Pong 响应
  dota::network::Packet response;
  response.set_sequence(next_sequence_++);
  response.set_timestamp(get_current_time_ms());

  auto* pong = response.mutable_pong();
  pong->set_client_timestamp(msg.client_timestamp());
  pong->set_server_timestamp(get_current_time_ms());
  pong->set_server_tick(server_tick_);

  server_->send_to_client(client_id, response);

  // 更新延迟统计
  auto it = players_.find(client_id);
  if (it != players_.end()) {
    it->second.last_ping_time = get_current_time_ms();
  }
}

void GameServerHandler::handle_input(uint32_t client_id, const dota::network::C2S_Input& msg) {
  GameSession* session = server_->default_session();
  if (!session) return;

  // 处理每个命令
  for (int i = 0; i < msg.commands_size(); ++i) {
    const auto& cmd = msg.commands(i);

    if (cmd.has_move()) {
      session->handle_move_command(client_id, cmd.move());
    } else if (cmd.has_use_ability()) {
      session->handle_use_ability_command(client_id, cmd.use_ability());
    } else if (cmd.has_stop()) {
      session->handle_stop_command(client_id);
    }
  }
}

void GameServerHandler::handle_disconnect(uint32_t client_id, const dota::network::C2S_Disconnect& msg) {
  std::cout << "[GameServerHandler] Client " << client_id
            << " disconnect: " << msg.reason() << "\n";

  // 标记为断开
  auto it = players_.find(client_id);
  if (it != players_.end()) {
    it->second.connected = false;
  }
}

void GameServerHandler::handle_choose_skill(uint32_t client_id, const dota::network::C2S_ChooseSkill& msg) {
  std::cout << "[GameServerHandler] Client " << client_id
            << " choose skill: " << msg.skill_id() << "\n";

  // TODO: Stage 4 处理技能选择
}

void GameServerHandler::tick() {
  server_tick_++;

  // 检查客户端超时
  check_client_timeouts();

  // 每 3 个 tick 广播一次快照（10Hz）
  if (server_tick_ % 3 == 0) {
    broadcast_snapshot();
  }
}

void GameServerHandler::broadcast_snapshot() {
  GameSession* session = server_->default_session();
  if (!session) return;

  // 生成快照
  dota::network::Packet packet;
  packet.set_sequence(next_sequence_++);
  packet.set_timestamp(get_current_time_ms());

  auto* snapshot = packet.mutable_snapshot();
  session->generate_snapshot(snapshot);

  // 广播给所有在线玩家
  for (const auto& [client_id, player] : players_) {
    if (player.connected) {
      server_->send_to_client(client_id, packet);
    }
  }
}

void GameServerHandler::check_client_timeouts() {
  constexpr uint64_t kTimeoutMs = 30000;  // 30 秒超时
  uint64_t now = get_current_time_ms();

  std::vector<uint32_t> timeout_clients;

  for (const auto& [client_id, player] : players_) {
    if (player.connected && (now - player.last_activity_time) > kTimeoutMs) {
      std::cout << "[GameServerHandler] Client " << client_id << " timeout\n";
      timeout_clients.push_back(client_id);
    }
  }

  // 断开超时客户端
  for (uint32_t client_id : timeout_clients) {
    server_->disconnect_client(client_id);
  }
}

uint64_t GameServerHandler::get_current_time_ms() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()
  ).count();
}

} // namespace dota::server
