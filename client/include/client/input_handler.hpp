// include/client/input_handler.hpp
// 输入处理器

#pragma once

#include "client/network_client.hpp"
#include "client/renderer.hpp"
#include "client/game_state.hpp"
#include "raylib.h"

namespace dota::client {

/**
 * 输入处理器
 *
 * 处理键盘/鼠标输入并发送给服务器
 */
class InputHandler {
 public:
  InputHandler(NetworkClient* client, Renderer* renderer, GameState* game_state);

  // 处理输入
  void process();

 private:
  NetworkClient* client_;
  Renderer* renderer_;
  GameState* game_state_;
};

} // namespace dota::client
