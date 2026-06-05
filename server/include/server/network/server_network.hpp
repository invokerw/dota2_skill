// include/server/network/server_network.hpp
// 服务器网络层 - Stage 1 & 2

#pragma once

#include "server/network/kcp_session.hpp"
#include "messages.pb.h"
#include <event2/event.h>
#include <map>
#include <memory>
#include <string>

namespace dota::network {

struct RemoteEndpoint {
  std::string address;
  uint16_t port;

  bool operator<(const RemoteEndpoint& other) const {
    if (address != other.address) return address < other.address;
    return port < other.port;
  }
};

class MessageHandler;

/**
 * 服务器网络层
 *
 * 负责 UDP 监听、KCP 会话管理、消息收发。
 */
class ServerNetwork {
 public:
  explicit ServerNetwork(uint16_t port);
  ~ServerNetwork();

  // 启动服务器
  bool start();
  void stop();

  // 事件循环 (在独立线程运行)
  void run();

  // 处理一次事件 (非阻塞)
  void tick();

  // 发送消息到客户端
  void send_to_client(uint32_t client_id, const Packet& packet);
  void broadcast(const Packet& packet, uint32_t exclude_client = 0);

  // 设置消息处理器
  void set_message_handler(std::shared_ptr<MessageHandler> handler) {
    message_handler_ = std::move(handler);
  }

  // 获取连接信息
  size_t get_client_count() const { return sessions_.size(); }
  bool is_running() const { return running_; }

 private:
  void on_udp_read(int fd, short events);
  void handle_raw_packet(const RemoteEndpoint& endpoint,
                        const uint8_t* data, size_t len);

  void create_session(const RemoteEndpoint& endpoint, uint32_t conv);
  void remove_session(uint32_t client_id);

  void on_session_message(uint32_t client_id, const uint8_t* data, size_t len);

  uint16_t port_;
  int udp_socket_ = -1;
  event_base* ev_base_ = nullptr;
  event* udp_event_ = nullptr;

  // 客户端会话管理
  std::map<RemoteEndpoint, uint32_t> endpoint_to_client_;
  std::map<uint32_t, std::unique_ptr<KcpSession>> sessions_;
  std::map<uint32_t, RemoteEndpoint> client_to_endpoint_;

  uint32_t next_client_id_ = 1;

  std::shared_ptr<MessageHandler> message_handler_;

  bool running_ = false;
};

} // namespace dota::network
