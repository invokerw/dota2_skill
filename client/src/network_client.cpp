// src/network_client.cpp
// 网络客户端实现

#include "client/network_client.hpp"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #define NOUSER
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #undef near
  #undef far
#else
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

#include <chrono>
#include <iostream>
#include <cstring>

namespace dota::client {

// 跨平台辅助宏
#ifdef _WIN32
  #define CLOSE_SOCKET(s) closesocket(s)
  #define SOCKET_ERROR_CHECK(s) ((s) == INVALID_SOCKET)
#else
  #define CLOSE_SOCKET(s) close(s)
  #define SOCKET_ERROR_CHECK(s) ((s) < 0)
#endif

NetworkClient::NetworkClient(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

NetworkClient::~NetworkClient() {
  disconnect();
}

bool NetworkClient::connect(const std::string& player_name) {
  // 创建 UDP socket
  socket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (SOCKET_ERROR_CHECK(socket_)) {
    std::cerr << "[NetworkClient] Failed to create socket\n";
    return false;
  }

  evutil_make_socket_nonblocking(socket_);

  // 服务器地址
  memset(&server_addr_, 0, sizeof(server_addr_));
  server_addr_.sin_family = AF_INET;
  server_addr_.sin_port = htons(port_);

#ifdef _WIN32
  inet_pton(AF_INET, host_.c_str(), &server_addr_.sin_addr);
#else
  inet_aton(host_.c_str(), &server_addr_.sin_addr);
#endif

  // 创建 libevent
  ev_base_ = event_base_new();
  if (!ev_base_) {
    std::cerr << "[NetworkClient] Failed to create event base\n";
    CLOSE_SOCKET(socket_);
    return false;
  }

  // KCP 输出回调
  auto output_cb = [this](const char* buf, int len, void*) {
#ifdef _WIN32
    sendto(socket_, buf, len, 0,
           reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
#else
    sendto(socket_, reinterpret_cast<const void*>(buf), len, 0,
           reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
#endif
  };

  // 创建 KCP 会话（使用随机 conv ID）
  uint32_t conv = 1000 + (rand() % 9000);
  session_ = std::make_unique<network::KcpSession>(conv, ev_base_, output_cb);

  // 消息回调
  session_->set_message_callback([this](const uint8_t* data, size_t len) {
    on_message(data, len);
  });

  // 注册 UDP 读事件
  udp_event_ = event_new(ev_base_, socket_, EV_READ | EV_PERSIST,
    [](evutil_socket_t, short, void* arg) {
      auto* client = static_cast<NetworkClient*>(arg);
      client->on_udp_read();
    }, this);
  event_add(udp_event_, nullptr);

  // 发送连接消息
  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* connect = packet.mutable_connect();
  connect->set_player_name(player_name);
  connect->set_version("0.1.0");

  send_packet(packet);

  connected_ = true;
  std::cout << "[NetworkClient] Connected to " << host_ << ":" << port_ << "\n";
  return true;
}

void NetworkClient::disconnect() {
  if (udp_event_) {
    event_del(udp_event_);
    event_free(udp_event_);
    udp_event_ = nullptr;
  }

  if (ev_base_) {
    event_base_free(ev_base_);
    ev_base_ = nullptr;
  }

#ifdef _WIN32
  if (socket_ != INVALID_SOCKET) {
    CLOSE_SOCKET(socket_);
    socket_ = INVALID_SOCKET;
  }
#else
  if (socket_ >= 0) {
    CLOSE_SOCKET(socket_);
    socket_ = -1;
  }
#endif

  session_.reset();
  connected_ = false;
}

void NetworkClient::send_move_command(const Vec2& target) {
  if (!connected_) return;

  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* input = packet.mutable_input();
  input->set_client_tick(0);
  input->set_ack_tick(0);

  auto* cmd = input->add_commands();
  auto* move = cmd->mutable_move();
  move->mutable_target()->set_x(target.x);
  move->mutable_target()->set_y(target.y);

  send_packet(packet);
}

void NetworkClient::send_use_ability(uint32_t ability_slot, const Vec2* target_pos) {
  if (!connected_) return;

  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* input = packet.mutable_input();
  input->set_client_tick(0);
  input->set_ack_tick(0);

  auto* cmd = input->add_commands();
  auto* ability = cmd->mutable_use_ability();
  ability->set_ability_slot(ability_slot);

  if (target_pos) {
    ability->mutable_point()->set_x(target_pos->x);
    ability->mutable_point()->set_y(target_pos->y);
  }

  send_packet(packet);
}

void NetworkClient::send_stop_command() {
  if (!connected_) return;

  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* input = packet.mutable_input();
  input->set_client_tick(0);
  input->set_ack_tick(0);

  auto* cmd = input->add_commands();
  cmd->mutable_stop();

  send_packet(packet);
}

void NetworkClient::send_choose_skill(uint32_t skill_index) {
  if (!connected_) return;

  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* choose = packet.mutable_choose_skill();
  choose->set_skill_id(skill_index);

  send_packet(packet);
}

void NetworkClient::send_ping() {
  if (!connected_) return;

  network::Packet packet;
  packet.set_sequence(seq_++);
  packet.set_timestamp(get_time_ms());

  auto* ping = packet.mutable_ping();
  ping->set_client_timestamp(get_time_ms());

  send_packet(packet);
  last_ping_time_ = get_time_ms();
}

void NetworkClient::update() {
  if (!connected_) return;

  // 处理网络事件
  event_base_loop(ev_base_, EVLOOP_NONBLOCK);

  // 更新 KCP
  session_->update();
}

void NetworkClient::on_udp_read() {
  uint8_t buffer[65536];
#ifdef _WIN32
  int n = recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
#else
  ssize_t n = recv(socket_, buffer, sizeof(buffer), 0);
#endif
  if (n > 0) {
    session_->input(buffer, n);
  }
}

void NetworkClient::on_message(const uint8_t* data, size_t len) {
  network::Packet packet;
  if (!packet.ParseFromArray(data, len)) {
    std::cerr << "[NetworkClient] Failed to parse packet\n";
    return;
  }

  if (packet.has_welcome()) {
    player_id_ = packet.welcome().player_id();
    std::cout << "[NetworkClient] Welcome! player_id=" << player_id_ << "\n";
    if (welcome_callback_) {
      welcome_callback_(player_id_);
    }
  }
  else if (packet.has_pong()) {
    uint64_t now = get_time_ms();
    if (last_ping_time_ > 0) {
      latency_ = (now - last_ping_time_) / 1000.0f;
    }
  }
  else if (packet.has_snapshot()) {
    if (snapshot_callback_) {
      snapshot_callback_(packet.snapshot());
    }
  }
  else if (packet.has_delta_snapshot()) {
    if (delta_snapshot_callback_) {
      delta_snapshot_callback_(packet.delta_snapshot());
    }
  }
  else if (packet.has_level_up()) {
    if (level_up_callback_) {
      level_up_callback_(packet.level_up());
    }
  }
  else if (packet.has_skill_learned()) {
    if (skill_learned_callback_) {
      skill_learned_callback_(packet.skill_learned());
    }
  }
}

void NetworkClient::send_packet(const network::Packet& packet) {
  std::string data;
  packet.SerializeToString(&data);
  session_->send(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

uint64_t NetworkClient::get_time_ms() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()).count();
}

} // namespace dota::client
