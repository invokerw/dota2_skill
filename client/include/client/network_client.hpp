// include/client/network_client.hpp
// 网络客户端 - 处理与服务器的通信

#pragma once

#include "server/network/kcp_session.hpp"
#include "messages.pb.h"
#include "dota/core/types.hpp"
#include <event2/event.h>
#include <functional>
#include <string>
#include <cstdint>

namespace dota::client {

// 回调类型
using SnapshotCallback = std::function<void(const network::S2C_Snapshot&)>;
using DeltaSnapshotCallback = std::function<void(const network::S2C_DeltaSnapshot&)>;
using WelcomeCallback = std::function<void(uint32_t player_id)>;
using LevelUpCallback = std::function<void(const network::S2C_LevelUp&)>;
using SkillLearnedCallback = std::function<void(const network::S2C_SkillLearned&)>;

/**
 * 网络客户端
 *
 * 复用服务器的 KcpSession，处理与服务器的网络通信
 */
class NetworkClient {
 public:
  NetworkClient(const std::string& host, uint16_t port);
  ~NetworkClient();

  // 连接服务器
  bool connect(const std::string& player_name);

  // 断开连接
  void disconnect();

  // 发送输入指令
  void send_move_command(const Vec2& target);
  void send_use_ability(uint32_t ability_slot, const Vec2* target_pos = nullptr);
  void send_stop_command();
  void send_choose_skill(uint32_t skill_index);
  void send_ping();

  // 更新网络（调用 KCP update 和事件循环）
  void update();

  // 设置回调
  void set_snapshot_callback(SnapshotCallback cb) { snapshot_callback_ = cb; }
  void set_delta_snapshot_callback(DeltaSnapshotCallback cb) { delta_snapshot_callback_ = cb; }
  void set_welcome_callback(WelcomeCallback cb) { welcome_callback_ = cb; }
  void set_level_up_callback(LevelUpCallback cb) { level_up_callback_ = cb; }
  void set_skill_learned_callback(SkillLearnedCallback cb) { skill_learned_callback_ = cb; }

  // 状态查询
  bool is_connected() const { return connected_; }
  uint32_t player_id() const { return player_id_; }
  float latency() const { return latency_; }

 private:
  void on_udp_read();
  void on_message(const uint8_t* data, size_t len);
  void send_packet(const network::Packet& packet);
  uint64_t get_time_ms();

  std::string host_;
  uint16_t port_;

  int socket_ = -1;
  sockaddr_in server_addr_{};
  event_base* ev_base_ = nullptr;
  event* udp_event_ = nullptr;
  std::unique_ptr<network::KcpSession> session_;

  bool connected_ = false;
  uint32_t player_id_ = 0;
  uint32_t seq_ = 1;
  float latency_ = 0.0f;
  uint64_t last_ping_time_ = 0;

  // 回调
  SnapshotCallback snapshot_callback_;
  DeltaSnapshotCallback delta_snapshot_callback_;
  WelcomeCallback welcome_callback_;
  LevelUpCallback level_up_callback_;
  SkillLearnedCallback skill_learned_callback_;
};

} // namespace dota::client
