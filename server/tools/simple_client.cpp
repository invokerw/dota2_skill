// tools/simple_client.cpp
// 简单的测试客户端 - Stage 2

#include "server/network/kcp_session.hpp"
#include "messages.pb.h"
#include <event2/event.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <chrono>

using namespace dota::network;

class SimpleClient {
 public:
  SimpleClient(const std::string& host, uint16_t port)
      : host_(host), port_(port) {}

  ~SimpleClient() {
    if (socket_ >= 0) close(socket_);
    if (ev_base_) event_base_free(ev_base_);
  }

  bool connect() {
    // 创建 UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
      std::cerr << "Failed to create socket\n";
      return false;
    }

    evutil_make_socket_nonblocking(socket_);

    // 服务器地址
    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port_);
    inet_aton(host_.c_str(), &server_addr_.sin_addr);

    // 创建 libevent
    ev_base_ = event_base_new();
    if (!ev_base_) {
      std::cerr << "Failed to create event base\n";
      return false;
    }

    // KCP 输出回调
    auto output_cb = [this](const char* buf, int len, void* user) {
      sendto(socket_, buf, len, 0,
             reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
    };

    // 创建 KCP 会话
    uint32_t conv = 123;  // 会话 ID
    session_ = std::make_unique<KcpSession>(conv, ev_base_, output_cb);

    // 消息回调
    session_->set_message_callback([this](const uint8_t* data, size_t len) {
      on_message(data, len);
    });

    // 注册 UDP 读事件
    udp_event_ = event_new(ev_base_, socket_, EV_READ | EV_PERSIST,
      [](evutil_socket_t fd, short, void* arg) {
        auto* client = static_cast<SimpleClient*>(arg);
        client->on_udp_read();
      }, this);
    event_add(udp_event_, nullptr);

    std::cout << "[Client] Connected to " << host_ << ":" << port_ << "\n";
    return true;
  }

  void send_connect(const std::string& name) {
    Packet packet;
    packet.set_sequence(seq_++);
    packet.set_timestamp(get_time_ms());

    auto* connect = packet.mutable_connect();
    connect->set_player_name(name);
    connect->set_version("0.1.0");

    send_packet(packet);
    std::cout << "[Client] Sent Connect: " << name << "\n";
  }

  void send_ping() {
    Packet packet;
    packet.set_sequence(seq_++);
    packet.set_timestamp(get_time_ms());

    auto* ping = packet.mutable_ping();
    ping->set_client_timestamp(get_time_ms());

    send_packet(packet);
    std::cout << "[Client] Sent Ping\n";
  }

  void run_for(int seconds) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
      // 运行事件循环一次
      event_base_loop(ev_base_, EVLOOP_NONBLOCK);

      // 手动触发 KCP update
      session_->update();

      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed >= std::chrono::seconds(seconds)) break;

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

 private:
  void on_udp_read() {
    uint8_t buffer[65536];
    ssize_t n = recv(socket_, buffer, sizeof(buffer), 0);
    if (n > 0) {
      session_->input(buffer, n);
    }
  }

  void on_message(const uint8_t* data, size_t len) {
    Packet packet;
    if (!packet.ParseFromArray(data, len)) {
      std::cerr << "[Client] Failed to parse packet\n";
      return;
    }

    std::cout << "[Client] Received message (seq=" << packet.sequence() << ")\n";

    if (packet.has_welcome()) {
      std::cout << "  -> Welcome: player_id=" << packet.welcome().player_id()
                << ", players=" << packet.welcome().players_size() << "\n";
    } else if (packet.has_pong()) {
      auto rtt = get_time_ms() - packet.pong().client_timestamp();
      std::cout << "  -> Pong: RTT=" << rtt << "ms\n";
    }
  }

  void send_packet(const Packet& packet) {
    size_t size = packet.ByteSizeLong();
    std::vector<uint8_t> buffer(size);
    packet.SerializeToArray(buffer.data(), size);
    session_->send(buffer.data(), size);
  }

  uint64_t get_time_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()
    ).count();
  }

  std::string host_;
  uint16_t port_;
  int socket_ = -1;
  sockaddr_in server_addr_{};
  event_base* ev_base_ = nullptr;
  event* udp_event_ = nullptr;
  std::unique_ptr<KcpSession> session_;
  uint32_t seq_ = 1;
};

int main() {
  std::cout << "Simple Test Client\n";
  std::cout << "==================\n\n";

  SimpleClient client("127.0.0.1", 7777);

  if (!client.connect()) {
    return 1;
  }

  // 发送 Connect
  client.send_connect("TestPlayer");
  client.run_for(1);

  // 发送 Ping
  client.send_ping();
  client.run_for(1);

  // 再发送一次 Ping
  client.send_ping();
  client.run_for(1);

  std::cout << "\n[Client] Test completed\n";
  return 0;
}
