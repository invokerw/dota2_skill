// src/network/kcp_session.cpp
// KCP 会话封装实现

#include "server/network/kcp_session.hpp"
#include "ikcp.h"
#include <event2/event.h>
#include <chrono>
#include <cstring>
#include <iostream>

namespace dota::network {

namespace {
constexpr int kKcpUpdateInterval = 10;  // 10ms 更新一次
constexpr int kKcpMtu = 1400;
constexpr int kKcpRecvBufferSize = 1024 * 1024;  // 1MB
}

KcpSession::KcpSession(uint32_t conv, event_base* ev_base, KcpOutputCallback output_cb)
    : conv_(conv), ev_base_(ev_base), output_cb_(std::move(output_cb)) {

  // 创建 KCP 对象
  kcp_ = ikcp_create(conv, this);
  kcp_->output = &KcpSession::kcp_output_adapter;

  // 配置 KCP 为快速模式 (低延迟)
  // nodelay: 1=启用快速重传
  // interval: 内部 flush 间隔 (ms)
  // resend: 快速重传阈值
  // nc: 1=关闭流控
  ikcp_nodelay(kcp_, 1, 10, 2, 1);

  // 设置窗口大小
  ikcp_wndsize(kcp_, 128, 128);

  // 设置 MTU
  ikcp_setmtu(kcp_, kKcpMtu);

  // 接收缓冲区
  recv_buffer_.reserve(kKcpRecvBufferSize);

  // 创建定时器 (定期调用 ikcp_update)
  update_timer_ = event_new(ev_base_, -1, EV_PERSIST,
    [](evutil_socket_t, short, void* arg) {
      static_cast<KcpSession*>(arg)->update();
    }, this);

  timeval tv{0, kKcpUpdateInterval * 1000};  // 10ms
  event_add(update_timer_, &tv);
}

KcpSession::~KcpSession() {
  if (update_timer_) {
    event_del(update_timer_);
    event_free(update_timer_);
  }
  if (kcp_) {
    ikcp_release(kcp_);
  }
}

bool KcpSession::send(const uint8_t* data, size_t len) {
  if (!alive_ || !kcp_) return false;

  // KCP 发送 (内部分片)
  int ret = ikcp_send(kcp_, reinterpret_cast<const char*>(data), len);
  if (ret < 0) {
    return false;
  }

  // 立即 flush
  ikcp_flush(kcp_);
  return true;
}

void KcpSession::input(const uint8_t* data, size_t len) {
  if (!alive_ || !kcp_) return;

  // 喂给 KCP
  ikcp_input(kcp_, reinterpret_cast<const char*>(data), len);

  // 处理接收到的完整消息
  process_received_data();
}

void KcpSession::update() {
  if (!alive_ || !kcp_) return;

  // KCP 内部状态更新
  // 获取当前时间戳 (毫秒)
  auto now = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()
  ).count();

  ikcp_update(kcp_, static_cast<IUINT32>(ms));
}

int KcpSession::kcp_output_adapter(const char* buf, int len, IKCPCB* kcp, void* user) {
  auto* session = static_cast<KcpSession*>(user);
  session->output_cb_(buf, len, user);
  return 0;
}

void KcpSession::process_received_data() {
  while (true) {
    int msg_size = ikcp_peeksize(kcp_);
    if (msg_size < 0) break;  // 没有完整消息

    recv_buffer_.resize(msg_size);
    int ret = ikcp_recv(kcp_, reinterpret_cast<char*>(recv_buffer_.data()), msg_size);
    if (ret < 0) break;

    // 调用应用层回调
    if (message_cb_) {
      message_cb_(recv_buffer_.data(), ret);
    }
  }
}

void KcpSession::set_nodelay(int nodelay, int interval, int resend, int nc) {
  if (kcp_) {
    ikcp_nodelay(kcp_, nodelay, interval, resend, nc);
  }
}

void KcpSession::set_wndsize(int sndwnd, int rcvwnd) {
  if (kcp_) {
    ikcp_wndsize(kcp_, sndwnd, rcvwnd);
  }
}

void KcpSession::set_mtu(int mtu) {
  if (kcp_) {
    ikcp_setmtu(kcp_, mtu);
  }
}

} // namespace dota::network
