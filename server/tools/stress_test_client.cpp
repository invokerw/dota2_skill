// tools/stress_test_client.cpp
// 压力测试客户端 - Stage 5

#include "server/network/kcp_session.hpp"
#include "messages.pb.h"
#include <event2/event.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>

using namespace dota::network;

// 单个客户端实例
class BotClient {
 public:
  BotClient(uint32_t id, const std::string& host, uint16_t port, event_base* ev_base)
      : id_(id), host_(host), port_(port), ev_base_(ev_base) {}

  ~BotClient() {
    if (socket_ >= 0) close(socket_);
  }

  bool connect() {
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) return false;

    evutil_make_socket_nonblocking(socket_);

    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addr_.sin_family = AF_INET;
    server_addr_.sin_port = htons(port_);
    inet_aton(host_.c_str(), &server_addr_.sin_addr);

    // KCP 输出回调
    auto output_cb = [this](const char* buf, int len, void*) {
      sendto(socket_, buf, len, 0,
             reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_));
    };

    // 每个客户端使用不同的 conv ID
    uint32_t conv = 1000 + id_;
    session_ = std::make_unique<KcpSession>(conv, ev_base_, output_cb);

    session_->set_message_callback([this](const uint8_t* data, size_t len) {
      on_message(data, len);
    });

    // 注册 UDP 读事件
    udp_event_ = event_new(ev_base_, socket_, EV_READ | EV_PERSIST,
      [](evutil_socket_t, short, void* arg) {
        auto* client = static_cast<BotClient*>(arg);
        client->on_udp_read();
      }, this);
    event_add(udp_event_, nullptr);

    connected_ = true;
    return true;
  }

  void send_connect() {
    Packet packet;
    packet.set_sequence(seq_++);
    packet.set_timestamp(get_time_ms());

    auto* connect = packet.mutable_connect();
    connect->set_player_name("Bot_" + std::to_string(id_));

    send_packet(packet);
  }

  void send_ping() {
    Packet packet;
    packet.set_sequence(seq_++);
    packet.set_timestamp(get_time_ms());
    packet.mutable_ping();
    send_packet(packet);

    ping_count_++;
  }

  void send_move_command() {
    Packet packet;
    packet.set_sequence(seq_++);
    packet.set_timestamp(get_time_ms());

    auto* input = packet.mutable_input();
    input->set_client_tick(0);
    input->set_ack_tick(0);

    auto* cmd = input->add_commands();
    auto* move = cmd->mutable_move();

    // 随机移动
    float angle = (rand() % 360) * 3.14159f / 180.0f;
    float radius = 100.0f + (rand() % 200);
    move->mutable_target()->set_x(1600.0f + radius * std::cos(angle));
    move->mutable_target()->set_y(1600.0f + radius * std::sin(angle));

    send_packet(packet);
  }

  bool is_connected() const { return connected_; }
  uint64_t packets_sent() const { return packets_sent_; }
  uint64_t packets_received() const { return packets_received_; }
  uint64_t ping_count() const { return ping_count_; }

 private:
  void on_udp_read() {
    char buffer[2048];
    socklen_t addr_len = sizeof(server_addr_);
    ssize_t n = recvfrom(socket_, buffer, sizeof(buffer), 0,
                         reinterpret_cast<sockaddr*>(&server_addr_), &addr_len);
    if (n > 0) {
      session_->input(reinterpret_cast<const uint8_t*>(buffer), n);
      session_->update();
    }
  }

  void on_message(const uint8_t* data, size_t len) {
    Packet packet;
    if (!packet.ParseFromArray(data, len)) return;

    packets_received_++;

    if (packet.has_welcome()) {
      player_id_ = packet.welcome().player_id();
    }
    else if (packet.has_pong()) {
      // 计算 RTT
      uint64_t now = get_time_ms();
      uint64_t sent = packet.timestamp();
      if (now > sent) {
        rtt_sum_ += (now - sent);
        rtt_count_++;
      }
    }
    else if (packet.has_snapshot()) {
      snapshot_count_++;
    }
  }

  void send_packet(const Packet& packet) {
    std::string data;
    packet.SerializeToString(&data);
    session_->send(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    session_->update();
    packets_sent_++;
  }

  uint64_t get_time_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()).count();
  }

  uint32_t id_;
  std::string host_;
  uint16_t port_;
  event_base* ev_base_;

  int socket_ = -1;
  sockaddr_in server_addr_;
  event* udp_event_ = nullptr;
  std::unique_ptr<KcpSession> session_;

  bool connected_ = false;
  uint32_t player_id_ = 0;
  uint32_t seq_ = 0;

  uint64_t packets_sent_ = 0;
  uint64_t packets_received_ = 0;
  uint64_t ping_count_ = 0;
  uint64_t snapshot_count_ = 0;
  uint64_t rtt_sum_ = 0;
  uint64_t rtt_count_ = 0;
};

// 压力测试主类
class StressTest {
 public:
  StressTest(const std::string& host, uint16_t port, uint32_t num_clients)
      : host_(host), port_(port), num_clients_(num_clients) {}

  ~StressTest() {
    if (ev_base_) event_base_free(ev_base_);
  }

  bool initialize() {
    ev_base_ = event_base_new();
    if (!ev_base_) {
      std::cerr << "Failed to create event base\n";
      return false;
    }

    // 创建客户端
    for (uint32_t i = 0; i < num_clients_; i++) {
      auto client = std::make_unique<BotClient>(i, host_, port_, ev_base_);
      if (!client->connect()) {
        std::cerr << "Failed to create client " << i << "\n";
        return false;
      }
      clients_.push_back(std::move(client));
    }

    std::cout << "[StressTest] Created " << num_clients_ << " bot clients\n";
    return true;
  }

  void run(uint32_t duration_sec) {
    std::cout << "[StressTest] Starting test for " << duration_sec << " seconds\n";

    // 发送连接消息
    for (auto& client : clients_) {
      client->send_connect();
    }

    // 定时器: 每秒发送 Ping
    event* ping_timer = event_new(ev_base_, -1, EV_PERSIST,
      [](evutil_socket_t, short, void* arg) {
        auto* test = static_cast<StressTest*>(arg);
        test->send_all_pings();
      }, this);
    timeval ping_interval = {1, 0};  // 1 秒
    event_add(ping_timer, &ping_interval);

    // 定时器: 每 2 秒发送随机移动命令
    event* move_timer = event_new(ev_base_, -1, EV_PERSIST,
      [](evutil_socket_t, short, void* arg) {
        auto* test = static_cast<StressTest*>(arg);
        test->send_random_moves();
      }, this);
    timeval move_interval = {2, 0};  // 2 秒
    event_add(move_timer, &move_interval);

    // 定时器: 每 5 秒打印统计
    event* stats_timer = event_new(ev_base_, -1, EV_PERSIST,
      [](evutil_socket_t, short, void* arg) {
        auto* test = static_cast<StressTest*>(arg);
        test->print_stats();
      }, this);
    timeval stats_interval = {5, 0};  // 5 秒
    event_add(stats_timer, &stats_interval);

    // 定时器: duration_sec 后停止
    event* stop_timer = event_new(ev_base_, -1, 0,
      [](evutil_socket_t, short, void* arg) {
        auto* test = static_cast<StressTest*>(arg);
        test->stop();
      }, this);
    timeval stop_delay = {static_cast<time_t>(duration_sec), 0};
    event_add(stop_timer, &stop_delay);

    running_ = true;
    start_time_ = std::chrono::steady_clock::now();

    // 运行事件循环
    event_base_dispatch(ev_base_);

    std::cout << "[StressTest] Test completed\n";
    print_final_stats();
  }

 private:
  void send_all_pings() {
    for (auto& client : clients_) {
      if (client->is_connected()) {
        client->send_ping();
      }
    }
  }

  void send_random_moves() {
    for (auto& client : clients_) {
      if (client->is_connected()) {
        client->send_move_command();
      }
    }
  }

  void print_stats() {
    uint64_t total_sent = 0;
    uint64_t total_received = 0;
    uint64_t total_pings = 0;

    for (const auto& client : clients_) {
      total_sent += client->packets_sent();
      total_received += client->packets_received();
      total_pings += client->ping_count();
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - start_time_).count();

    std::cout << "\n[Stats] Time: " << elapsed << "s"
              << " | Clients: " << clients_.size()
              << " | Sent: " << total_sent
              << " | Received: " << total_received
              << " | Pings: " << total_pings << "\n";
  }

  void print_final_stats() {
    uint64_t total_sent = 0;
    uint64_t total_received = 0;

    for (const auto& client : clients_) {
      total_sent += client->packets_sent();
      total_received += client->packets_received();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
      end_time - start_time_).count();

    std::cout << "\n========== Final Statistics ==========\n";
    std::cout << "Duration: " << duration << " seconds\n";
    std::cout << "Clients: " << clients_.size() << "\n";
    std::cout << "Total packets sent: " << total_sent << "\n";
    std::cout << "Total packets received: " << total_received << "\n";
    std::cout << "Packets per second: " << (total_sent / std::max(1L, duration)) << "\n";
    std::cout << "======================================\n";
  }

  void stop() {
    running_ = false;
    event_base_loopbreak(ev_base_);
  }

  std::string host_;
  uint16_t port_;
  uint32_t num_clients_;

  event_base* ev_base_ = nullptr;
  std::vector<std::unique_ptr<BotClient>> clients_;

  bool running_ = false;
  std::chrono::steady_clock::time_point start_time_;
};

int main(int argc, char* argv[]) {
  uint32_t num_clients = 10;
  uint32_t duration = 60;  // 60 秒
  std::string host = "127.0.0.1";
  uint16_t port = 7777;

  // 解析命令行参数
  if (argc > 1) num_clients = std::atoi(argv[1]);
  if (argc > 2) duration = std::atoi(argv[2]);
  if (argc > 3) host = argv[3];
  if (argc > 4) port = std::atoi(argv[4]);

  std::cout << "========== Stress Test ==========\n";
  std::cout << "Server: " << host << ":" << port << "\n";
  std::cout << "Clients: " << num_clients << "\n";
  std::cout << "Duration: " << duration << " seconds\n";
  std::cout << "=================================\n\n";

  StressTest test(host, port, num_clients);
  if (!test.initialize()) {
    std::cerr << "Failed to initialize stress test\n";
    return 1;
  }

  test.run(duration);

  return 0;
}
