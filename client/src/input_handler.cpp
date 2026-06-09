// src/input_handler.cpp
// 输入处理实现

#include "client/input_handler.hpp"

namespace dota::client {

InputHandler::InputHandler(NetworkClient* client, Renderer* renderer, GameState* game_state)
    : client_(client), renderer_(renderer), game_state_(game_state) {}

void InputHandler::process() {
  // 选技面板激活时, 拦截输入
  if (game_state_->has_pending_choices()) {
    process_skill_choice();
    return;
  }

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

void InputHandler::process_skill_choice() {
  const auto& choices = game_state_->pending_choices();

  // 数字键 1/2/3 选择
  if (IsKeyPressed(KEY_ONE) && choices.size() > 0) {
    client_->send_choose_skill(0);
    game_state_->clear_pending_choices();
    return;
  }
  if (IsKeyPressed(KEY_TWO) && choices.size() > 1) {
    client_->send_choose_skill(1);
    game_state_->clear_pending_choices();
    return;
  }
  if (IsKeyPressed(KEY_THREE) && choices.size() > 2) {
    client_->send_choose_skill(2);
    game_state_->clear_pending_choices();
    return;
  }

  // 鼠标点击卡片
  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
    Vector2 mouse = GetMousePosition();

    int card_w = 200;
    int card_h = 240;
    int card_gap = 16;
    int cards_total = static_cast<int>(choices.size()) * (card_w + card_gap) - card_gap;
    int panel_w = cards_total + 40;
    int panel_h = card_h + 80;
    int panel_x = (1280 - panel_w) / 2;
    int panel_y = (720 - panel_h) / 2;

    int cards_x = panel_x + (panel_w - cards_total) / 2;
    int cards_y = panel_y + 50;

    for (size_t i = 0; i < choices.size(); ++i) {
      int cx = cards_x + static_cast<int>(i) * (card_w + card_gap);
      int cy = cards_y;

      if (mouse.x >= cx && mouse.x <= cx + card_w &&
          mouse.y >= cy && mouse.y <= cy + card_h) {
        client_->send_choose_skill(static_cast<uint32_t>(i));
        game_state_->clear_pending_choices();
        return;
      }
    }
  }
}

} // namespace dota::client
