// src/network/server_network.cpp
// 服务器网络层实现

#include "server/network/server_network.hpp"
#include "server/network/message_handler.hpp"
#include <arpa/inet.h>
#include <unistd.h>
#include <event2/event.h>
#include <iostream>

namespace dota::network {

ServerNetwork::ServerNetwork(uint16_t port) : port_(port) {}

ServerNetwork::~ServerNetwork() {
  stop();
}

bool ServerNetwork::start() {
  // 创建 UDP socket
  udp_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_socket_ < 0) {
    std::cerr << "[ServerNetwork] Failed to create socket\n";
    return false;
  }

  evutil_make_socket_nonblocking(udp_socket_);

  // 允许地址重用
  int opt = 1;
  setsockopt(udp_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // 绑定端口
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(udp_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "[ServerNetwork] Failed to bind port " << port_ << "\n";
    close(udp_socket_);
    return false;
  }

  // 创建 libevent 事件循环
  ev_base_ = event_base_new();
  if (!ev_base_) {
    std::cerr << "[ServerNetwork] Failed to create event base\n";
    close(udp_socket_);
    return false;
  }

  // 注册 UDP 读事件
  udp_event_ = event_new(ev_base_, udp_socket_, EV_READ | EV_PERSIST,
    [](evutil_socket_t fd, short events, void* arg) {
      static_cast<ServerNetwork*>(arg)->on_udp_read(fd, events);
    }, this);

  event_add(udp_event_, nullptr);

  running_ = true;
  std::cout << "[ServerNetwork] Server started on port " << port_ << "\n";
  return true;
}

void ServerNetwork::stop() {
  running_ = false;

  if (udp_event_) {
    event_del(udp_event_);
    event_free(udp_event_);
    udp_event_ = nullptr;
  }

  if (ev_base_) {
    event_base_loopexit(ev_base_, nullptr);
    event_base_free(ev_base_);
    ev_base_ = nullptr;
  }

  if (udp_socket_ >= 0) {
    close(udp_socket_);
    udp_socket_ = -1;
  }

  sessions_.clear();
  endpoint_to_client_.clear();
  client_to_endpoint_.clear();

  std::cout << "[ServerNetwork] Server stopped\n";
}

void ServerNetwork::run() {
  if (ev_base_ && running_) {
    std::cout << "[ServerNetwork] Entering event loop\n";
    event_base_dispatch(ev_base_);
  }
}

void ServerNetwork::tick() {
  if (ev_base_ && running_) {
    // 非阻塞处理事件
    event_base_loop(ev_base_, EVLOOP_NONBLOCK);
  }
}

void ServerNetwork::on_udp_read(int fd, short events) {
  uint8_t buffer[65536];
  sockaddr_in remote_addr;
  socklen_t addr_len = sizeof(remote_addr);

  ssize_t n = recvfrom(fd, buffer, sizeof(buffer), 0,
                       reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);
  if (n <= 0) return;

  RemoteEndpoint endpoint{
    inet_ntoa(remote_addr.sin_addr),
    ntohs(remote_addr.sin_port)
  };

  handle_raw_packet(endpoint, buffer, n);
}

void ServerNetwork::handle_raw_packet(const RemoteEndpoint& endpoint,
                                      const uint8_t* data, size_t len) {
  // 查找或创建会话
  auto it = endpoint_to_client_.find(endpoint);
  if (it == endpoint_to_client_.end()) {
    // 新连接: 解析 conv (KCP 会话 ID, 从首个包头提取)
    if (len < 4) return;
    uint32_t conv = *reinterpret_cast<const uint32_t*>(data);

    create_session(endpoint, conv);
    it = endpoint_to_client_.find(endpoint);
  }

  uint32_t client_id = it->second;
  auto session_it = sessions_.find(client_id);
  if (session_it != sessions_.end()) {
    session_it->second->input(data, len);
  }
}

void ServerNetwork::create_session(const RemoteEndpoint& endpoint, uint32_t conv) {
  uint32_t client_id = next_client_id_++;

  std::cout << "[ServerNetwork] New client " << client_id
            << " from " << endpoint.address << ":" << endpoint.port
            << " (conv=" << conv << ")\n";

  // KCP 输出回调: 发送 UDP 包
  auto output_cb = [this, endpoint](const char* buf, int len, void* user) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(endpoint.port);
    inet_aton(endpoint.address.c_str(), &addr.sin_addr);

    sendto(udp_socket_, buf, len, 0,
           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  };

  auto session = std::make_unique<KcpSession>(conv, ev_base_, output_cb);

  // 消息接收回调: 解析 Protobuf 并分发
  session->set_message_callback([this, client_id](const uint8_t* data, size_t len) {
    on_session_message(client_id, data, len);
  });

  endpoint_to_client_[endpoint] = client_id;
  client_to_endpoint_[client_id] = endpoint;
  sessions_[client_id] = std::move(session);

  // 通知应用层: 新客户端连接
  if (message_handler_) {
    message_handler_->on_client_connected(client_id);
  }
}

void ServerNetwork::remove_session(uint32_t client_id) {
  std::cout << "[ServerNetwork] Removing client " << client_id << "\n";

  auto ep_it = client_to_endpoint_.find(client_id);
  if (ep_it != client_to_endpoint_.end()) {
    endpoint_to_client_.erase(ep_it->second);
    client_to_endpoint_.erase(ep_it);
  }
  sessions_.erase(client_id);

  if (message_handler_) {
    message_handler_->on_client_disconnected(client_id);
  }
}

void ServerNetwork::on_session_message(uint32_t client_id, const uint8_t* data, size_t len) {
  // 反序列化 Protobuf
  Packet packet;
  if (!packet.ParseFromArray(data, len)) {
    std::cerr << "[ServerNetwork] Failed to parse packet from client " << client_id << "\n";
    remove_session(client_id);
    return;
  }

  // 分发到消息处理器
  if (message_handler_) {
    message_handler_->on_message(client_id, packet);
  }
}

void ServerNetwork::send_to_client(uint32_t client_id, const Packet& packet) {
  auto it = sessions_.find(client_id);
  if (it == sessions_.end()) return;

  // 序列化
  size_t size = packet.ByteSizeLong();
  std::vector<uint8_t> buffer(size);
  packet.SerializeToArray(buffer.data(), size);

  // 通过 KCP 发送
  it->second->send(buffer.data(), size);
}

void ServerNetwork::broadcast(const Packet& packet, uint32_t exclude_client) {
  for (auto& [client_id, session] : sessions_) {
    if (client_id != exclude_client) {
      send_to_client(client_id, packet);
    }
  }
}

} // namespace dota::network
