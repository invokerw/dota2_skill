// include/server/server/game_server_handler.hpp
// 游戏服务器消息处理器 - Stage 2

#pragma once

#include "server/network/message_handler.hpp"
#include "server/server/player_state.hpp"
#include "messages.pb.h"
#include <map>
#include <cstdint>
#include <chrono>

namespace dota::server {

class GameServer;

/**
 * 游戏服务器消息处理器
 *
 * 负责处理所有网络消息并分发到对应的处理函数
 */
class GameServerHandler : public dota::network::MessageHandler {
 public:
  explicit GameServerHandler(GameServer* server);

  // MessageHandler 接口实现
  void on_client_connected(uint32_t client_id) override;
  void on_client_disconnected(uint32_t client_id) override;
  void on_message(uint32_t client_id, const dota::network::Packet& packet) override;

  // 服务器 tick
  void tick();

  // 广播快照
  void broadcast_snapshot();

  // 获取玩家状态
  const std::map<uint32_t, PlayerState>& get_players() const { return players_; }

 private:
  // 消息处理函数
  void handle_connect(uint32_t client_id, const dota::network::C2S_Connect& msg);
  void handle_ping(uint32_t client_id, const dota::network::C2S_Ping& msg);
  void handle_input(uint32_t client_id, const dota::network::C2S_Input& msg);
  void handle_disconnect(uint32_t client_id, const dota::network::C2S_Disconnect& msg);
  void handle_choose_skill(uint32_t client_id, const dota::network::C2S_ChooseSkill& msg);

  // 内部方法
  void check_client_timeouts();
  uint64_t get_current_time_ms();

  GameServer* server_;
  std::map<uint32_t, PlayerState> players_;

  uint32_t server_tick_ = 0;
  uint32_t next_sequence_ = 1;
};

} // namespace dota::server
