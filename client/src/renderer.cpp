// src/renderer.cpp
// 渲染器实现

#include "client/renderer.hpp"
#include <iostream>
#include <sstream>

namespace dota::client {

Renderer::Renderer() {}

Renderer::~Renderer() {
  if (initialized_) {
    shutdown();
  }
}

bool Renderer::init(int width, int height, const char* title) {
  width_ = width;
  height_ = height;

  InitWindow(width, height, title);
  SetTargetFPS(60);

  initialized_ = true;
  std::cout << "[Renderer] Initialized " << width << "x" << height << "\n";
  return true;
}

void Renderer::shutdown() {
  if (initialized_) {
    CloseWindow();
    initialized_ = false;
  }
}

void Renderer::begin_draw() {
  BeginDrawing();
  ClearBackground(Color{30, 30, 40, 255});
}

void Renderer::end_draw() {
  EndDrawing();
}

bool Renderer::should_close() const {
  return WindowShouldClose();
}

void Renderer::draw(const GameState& state) {
  // 相机跟随玩家
  const ClientEntity* player = state.get_player();
  if (player) {
    camera_offset_.x = width_ / 2.0f - player->position.x;
    camera_offset_.y = height_ / 2.0f - player->position.y;
  }

  // 绘制网格背景
  for (int x = 0; x < 3200; x += 100) {
    Vector2 start = world_to_screen(Vec2{static_cast<float>(x), 0});
    Vector2 end = world_to_screen(Vec2{static_cast<float>(x), 3200});
    DrawLineV(start, end, Color{50, 50, 60, 255});
  }
  for (int y = 0; y < 3200; y += 100) {
    Vector2 start = world_to_screen(Vec2{0, static_cast<float>(y)});
    Vector2 end = world_to_screen(Vec2{3200, static_cast<float>(y)});
    DrawLineV(start, end, Color{50, 50, 60, 255});
  }

  // 绘制所有实体
  for (const auto& [id, entity] : state.entities()) {
    draw_entity(entity);
  }
}

void Renderer::draw_entity(const ClientEntity& entity) {
  Vector2 screen_pos = world_to_screen(entity.position);

  Color color;
  if (entity.is_player) {
    color = BLUE;
  } else if (entity.is_enemy) {
    color = RED;
  } else {
    color = GRAY;
  }

  // 绘制圆形实体
  DrawCircleV(screen_pos, entity.radius, color);
  DrawCircleLinesV(screen_pos, entity.radius, WHITE);

  // 绘制血条
  draw_health_bar(entity.position, entity.health, entity.max_health, entity.radius * 2);

  // 绘制 ID (调试)
  std::string id_str = std::to_string(entity.id);
  Vector2 text_pos = {screen_pos.x - 10, screen_pos.y - entity.radius - 20};
  DrawText(id_str.c_str(), text_pos.x, text_pos.y, 12, WHITE);
}

void Renderer::draw_health_bar(Vec2 position, float health, float max_health, float width) {
  if (max_health <= 0) return;

  Vector2 bar_pos = world_to_screen(position);
  bar_pos.y -= 50;  // 血条在实体上方
  bar_pos.x -= width / 2;

  float health_ratio = health / max_health;
  float bar_height = 6;

  // 背景
  DrawRectangle(bar_pos.x, bar_pos.y, width, bar_height, Color{60, 60, 60, 255});

  // 血量
  Color health_color = GREEN;
  if (health_ratio < 0.3f) health_color = RED;
  else if (health_ratio < 0.6f) health_color = YELLOW;

  DrawRectangle(bar_pos.x, bar_pos.y, width * health_ratio, bar_height, health_color);

  // 边框
  DrawRectangleLines(bar_pos.x, bar_pos.y, width, bar_height, WHITE);
}

void Renderer::draw_ui(uint32_t player_id, float latency) {
  // 左上角信息
  std::ostringstream oss;
  oss << "Player ID: " << player_id;
  DrawText(oss.str().c_str(), 10, 10, 20, WHITE);

  oss.str("");
  oss << "Latency: " << static_cast<int>(latency * 1000) << " ms";
  DrawText(oss.str().c_str(), 10, 35, 20, WHITE);

  oss.str("");
  oss << "FPS: " << GetFPS();
  DrawText(oss.str().c_str(), 10, 60, 20, WHITE);

  // 操作提示
  const char* help =
    "Controls:\n"
    "  Right Click - Move\n"
    "  Q/W/E/R/D/F - Use Ability\n"
    "  S - Stop\n"
    "  ESC - Quit";
  DrawText(help, 10, height_ - 120, 16, LIGHTGRAY);
}

Vec2 Renderer::screen_to_world(Vector2 screen_pos) const {
  return Vec2{
    screen_pos.x - camera_offset_.x,
    screen_pos.y - camera_offset_.y
  };
}

Vector2 Renderer::world_to_screen(Vec2 world_pos) const {
  return Vector2{
    static_cast<float>(world_pos.x + camera_offset_.x),
    static_cast<float>(world_pos.y + camera_offset_.y)
  };
}

} // namespace dota::client
