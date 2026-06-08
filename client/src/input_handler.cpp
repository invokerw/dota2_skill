// src/input_handler.cpp
// 输入处理实现

#include "client/input_handler.hpp"

namespace dota::client {

InputHandler::InputHandler(NetworkClient* client, Renderer* renderer, GameState* game_state)
    : client_(client), renderer_(renderer), game_state_(game_state) {}

void InputHandler::process() {
  // 右键移动
  if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_move_command(world_pos);
    game_state_->set_player_move_target(world_pos);
  }

  // 技能按键 Q/W/E/R/D/F
  if (IsKeyPressed(KEY_Q)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(0, &world_pos);
  }
  if (IsKeyPressed(KEY_W)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(1, &world_pos);
  }
  if (IsKeyPressed(KEY_E)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(2, &world_pos);
  }
  if (IsKeyPressed(KEY_R)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(3, &world_pos);
  }
  if (IsKeyPressed(KEY_D)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(4, &world_pos);
  }
  if (IsKeyPressed(KEY_F)) {
    Vector2 mouse_pos = GetMousePosition();
    Vec2 world_pos = renderer_->screen_to_world(mouse_pos);
    client_->send_use_ability(5, &world_pos);
  }

  // 停止
  if (IsKeyPressed(KEY_S)) {
    client_->send_stop_command();
    game_state_->clear_player_move_target();
  }
}

} // namespace dota::client
