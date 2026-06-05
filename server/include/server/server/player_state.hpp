// include/server/server/player_state.hpp
// 玩家状态 - Stage 2

#pragma once

#include <cstdint>
#include <string>

namespace dota::server {

/**
 * 玩家状态
 *
 * 存储玩家的网络信息和游戏数据
 */
struct PlayerState {
  // 基础信息
  uint32_t player_id = 0;
  std::string player_name;
  std::string client_version;
  uint32_t unit_id = 0;

  // 网络状态
  bool connected = false;
  uint64_t last_ping_time = 0;
  uint64_t last_activity_time = 0;
  float latency = 0.0f;

  // 游戏状态
  uint32_t level = 1;
  uint32_t exp = 0;
  uint32_t gold = 0;
};

} // namespace dota::server
