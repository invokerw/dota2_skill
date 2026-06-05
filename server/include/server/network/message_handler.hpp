// include/server/network/message_handler.hpp
// 消息处理器接口 - Stage 2

#pragma once

#include "messages.pb.h"
#include <cstdint>

namespace dota::network {

/**
 * 消息处理器基类
 *
 * 应用层继承此类实现具体的消息处理逻辑。
 */
class MessageHandler {
 public:
  virtual ~MessageHandler() = default;

  // 连接事件
  virtual void on_client_connected(uint32_t client_id) = 0;
  virtual void on_client_disconnected(uint32_t client_id) = 0;

  // 消息事件
  virtual void on_message(uint32_t client_id, const Packet& packet) = 0;
};

} // namespace dota::network
