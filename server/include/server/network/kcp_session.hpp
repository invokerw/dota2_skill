// include/server/network/kcp_session.hpp
// KCP 会话封装 - Stage 1

#pragma once

#include <functional>
#include <vector>
#include <cstdint>

// Forward declarations
struct IKCPCB;
struct event_base;
struct event;

namespace dota::network {

// KCP 输出回调 (KCP -> UDP socket)
using KcpOutputCallback = std::function<void(const char* buf, int len, void* user)>;

// 消息接收回调 (应用层)
using MessageCallback = std::function<void(const uint8_t* data, size_t len)>;

/**
 * KCP 会话封装
 *
 * 负责单个客户端连接的可靠传输，集成 libevent 定时器。
 */
class KcpSession {
 public:
  KcpSession(uint32_t conv, event_base* ev_base, KcpOutputCallback output_cb);
  ~KcpSession();

  // 禁止拷贝
  KcpSession(const KcpSession&) = delete;
  KcpSession& operator=(const KcpSession&) = delete;

  // 发送数据
  bool send(const uint8_t* data, size_t len);

  // 接收 UDP 数据 (喂给 KCP)
  void input(const uint8_t* data, size_t len);

  // KCP 更新 (定时器回调)
  void update();

  // 设置消息接收回调
  void set_message_callback(MessageCallback cb) {
    message_cb_ = std::move(cb);
  }

  // 配置 KCP 参数
  void set_nodelay(int nodelay, int interval, int resend, int nc);
  void set_wndsize(int sndwnd, int rcvwnd);
  void set_mtu(int mtu);

  // 获取会话 ID
  uint32_t conv() const { return conv_; }

  // 检查是否存活
  bool is_alive() const { return alive_; }
  void mark_dead() { alive_ = false; }

 private:
  static int kcp_output_adapter(const char* buf, int len, IKCPCB* kcp, void* user);
  void process_received_data();

  uint32_t conv_;
  IKCPCB* kcp_;
  event_base* ev_base_;
  event* update_timer_;

  KcpOutputCallback output_cb_;
  MessageCallback message_cb_;

  std::vector<uint8_t> recv_buffer_;
  bool alive_ = true;
};

} // namespace dota::network
