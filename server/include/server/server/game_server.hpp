// include/server/server/game_server.hpp
// 游戏服务器主类 - Stage 3

#pragma once

#include <memory>
#include <cstdint>
#include <map>
#include <string>

namespace dota::network {
class ServerNetwork;
class MessageHandler;
class Packet;
}

namespace dota::server {

class GameServerHandler;
class GameSession;

/**
 * 游戏服务器
 *
 * 管理网络层、游戏会话、主 tick 循环。
 */
class GameServer {
 public:
  GameServer(uint16_t port, const std::string& data_dir);
  ~GameServer();

  // 启动和停止
  bool start();
  void stop();

  // 主 tick 循环 (30Hz)
  void tick();

  // 是否运行中
  bool is_running() const { return running_; }

  // 网络操作
  void send_to_client(uint32_t client_id, const dota::network::Packet& packet);
  void disconnect_client(uint32_t client_id);

  // 会话管理
  GameSession* get_or_create_session(uint32_t session_id);
  GameSession* get_session(uint32_t session_id);
  void remove_session(uint32_t session_id);

  // 获取默认会话（Stage 3 简化：所有玩家在同一会话）
  GameSession* default_session();

 private:
  uint16_t port_;
  std::string data_dir_;
  bool running_ = false;

  // 网络层
  std::unique_ptr<dota::network::ServerNetwork> network_;
  std::shared_ptr<GameServerHandler> handler_;

  // 游戏会话
  std::map<uint32_t, std::unique_ptr<GameSession>> sessions_;
  uint32_t next_session_id_ = 1;
};

} // namespace dota::server
