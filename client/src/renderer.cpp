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
  // 相机平滑跟随玩家
  const ClientEntity* player = state.get_player();
  if (player) {
    float target_x = width_ / 2.0f - player->position.x;
    float target_y = height_ / 2.0f - player->position.y;
    float smoothing = 0.15f;
    camera_offset_.x += (target_x - camera_offset_.x) * smoothing;
    camera_offset_.y += (target_y - camera_offset_.y) * smoothing;
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

void Renderer::draw_ability_hud(const GameState& state) {
  const auto& abilities = state.abilities();
  if (abilities.empty()) return;

  const char* keys[] = {"Q", "W", "E", "R", "D", "F"};
  int slot_size = 64;
  int slot_gap = 10;
  int total_width = static_cast<int>(abilities.size()) * (slot_size + slot_gap) - slot_gap;
  int start_x = (width_ - total_width) / 2;
  int start_y = height_ - slot_size - 40;

  for (size_t i = 0; i < abilities.size() && i < 6; ++i) {
    const auto& ab = abilities[i];
    int x = start_x + static_cast<int>(i) * (slot_size + slot_gap);
    int y = start_y;

    // 背景色
    Color bg;
    if (ab.is_passive) {
      bg = Color{45, 55, 45, 230};
    } else if (ab.cooldown_remaining > 0.0f) {
      bg = Color{35, 35, 45, 230};
    } else {
      bg = Color{55, 75, 115, 230};
    }
    DrawRectangle(x, y, slot_size, slot_size, bg);
    DrawRectangleLines(x, y, slot_size, slot_size,
                       ab.cooldown_remaining > 0.0f ? Color{80, 80, 90, 255} : WHITE);

    // 快捷键
    DrawText(keys[i], x + 3, y + 2, 12, Color{200, 200, 220, 255});

    // 等级指示 (右上角小点)
    for (uint32_t lv = 0; lv < ab.level && lv < 4; ++lv) {
      int dot_x = x + slot_size - 6 - static_cast<int>(lv) * 8;
      DrawCircle(dot_x, y + 6, 3, GOLD);
    }

    // 蓝耗 (左下)
    if (ab.mana_cost > 0.0f) {
      std::string mana_str = std::to_string(static_cast<int>(ab.mana_cost));
      DrawText(mana_str.c_str(), x + 3, y + slot_size - 13, 10, Color{100, 160, 255, 255});
    }

    // 冷却覆盖
    if (ab.cooldown_remaining > 0.0f) {
      // 半透明遮罩
      DrawRectangle(x + 1, y + 1, slot_size - 2, slot_size - 2, Color{0, 0, 0, 100});
      std::string cd_text = std::to_string(static_cast<int>(ab.cooldown_remaining + 0.9f));
      int tw = MeasureText(cd_text.c_str(), 22);
      DrawText(cd_text.c_str(), x + (slot_size - tw) / 2, y + (slot_size - 22) / 2, 22, WHITE);
    } else if (ab.is_passive) {
      int tw = MeasureText("P", 16);
      DrawText("P", x + (slot_size - tw) / 2, y + (slot_size - 16) / 2, 16, Color{120, 180, 120, 255});
    }

    // 技能名 (槽下方)
    if (!ab.name.empty()) {
      // 去掉英雄前缀 (如 lina_dragon_slave -> dragon_slave)
      std::string display_name = ab.name;
      auto pos = display_name.find('_');
      if (pos != std::string::npos) {
        display_name = display_name.substr(pos + 1);
      }
      int tw = MeasureText(display_name.c_str(), 9);
      int text_x = x + (slot_size - tw) / 2;
      DrawText(display_name.c_str(), text_x, y + slot_size + 3, 9, Color{170, 170, 190, 255});
    }

    // 鼠标悬停显示详细信息
    Vector2 mouse = GetMousePosition();
    if (mouse.x >= x && mouse.x <= x + slot_size &&
        mouse.y >= y && mouse.y <= y + slot_size) {
      int tip_w = 180;
      int tip_h = 80;
      int tip_x = x;
      int tip_y = y - tip_h - 6;
      if (tip_x + tip_w > width_) tip_x = width_ - tip_w - 4;
      if (tip_y < 0) tip_y = y + slot_size + 16;

      DrawRectangle(tip_x, tip_y, tip_w, tip_h, Color{20, 20, 30, 240});
      DrawRectangleLines(tip_x, tip_y, tip_w, tip_h, Color{100, 100, 130, 255});

      DrawText(ab.name.c_str(), tip_x + 6, tip_y + 4, 12, WHITE);

      std::string info = "Level: " + std::to_string(ab.level);
      DrawText(info.c_str(), tip_x + 6, tip_y + 20, 11, YELLOW);

      std::string stats;
      if (ab.max_cooldown > 0) {
        stats += "CD: " + std::to_string(static_cast<int>(ab.max_cooldown)) + "s";
      }
      if (ab.mana_cost > 0) {
        if (!stats.empty()) stats += "  ";
        stats += "Mana: " + std::to_string(static_cast<int>(ab.mana_cost));
      }
      if (!stats.empty()) {
        DrawText(stats.c_str(), tip_x + 6, tip_y + 36, 11, Color{150, 170, 220, 255});
      }

      if (ab.cast_range > 0) {
        std::string range_str = "Range: " + std::to_string(static_cast<int>(ab.cast_range));
        DrawText(range_str.c_str(), tip_x + 6, tip_y + 52, 11, Color{150, 170, 220, 255});
      }

      if (ab.is_passive) {
        DrawText("Passive", tip_x + 6, tip_y + 64, 11, Color{120, 180, 120, 255});
      }
    }
  }
}

void Renderer::draw_skill_choice(const GameState& state) {
  if (!state.has_pending_choices()) return;

  const auto& choices = state.pending_choices();

  // 半透明覆盖
  DrawRectangle(0, 0, width_, height_, Color{0, 0, 0, 120});

  // 选技面板
  int card_w = 200;
  int card_h = 240;
  int card_gap = 16;
  int cards_total = static_cast<int>(choices.size()) * (card_w + card_gap) - card_gap;
  int panel_w = cards_total + 40;
  int panel_h = card_h + 80;
  int panel_x = (width_ - panel_w) / 2;
  int panel_y = (height_ - panel_h) / 2;

  DrawRectangle(panel_x, panel_y, panel_w, panel_h, Color{25, 25, 40, 240});
  DrawRectangleLines(panel_x, panel_y, panel_w, panel_h, GOLD);

  DrawText("Choose a Skill", panel_x + 20, panel_y + 12, 24, GOLD);

  int cards_x = panel_x + (panel_w - cards_total) / 2;
  int cards_y = panel_y + 50;

  for (size_t i = 0; i < choices.size(); ++i) {
    const auto& choice = choices[i];
    int cx = cards_x + static_cast<int>(i) * (card_w + card_gap);
    int cy = cards_y;

    Vector2 mouse = GetMousePosition();
    bool hover = (mouse.x >= cx && mouse.x <= cx + card_w &&
                  mouse.y >= cy && mouse.y <= cy + card_h);

    Color card_bg = hover ? Color{60, 65, 100, 255} : Color{40, 42, 60, 255};
    DrawRectangle(cx, cy, card_w, card_h, card_bg);
    DrawRectangleLines(cx, cy, card_w, card_h, hover ? GOLD : Color{100, 100, 120, 255});

    // 按键提示
    std::string key_hint = "[" + std::to_string(i + 1) + "]";
    DrawText(key_hint.c_str(), cx + card_w - 28, cy + 4, 14, Color{180, 180, 200, 255});

    // 技能名
    DrawText(choice.name.c_str(), cx + 8, cy + 8, 14, WHITE);

    // 等级
    std::string lv_text;
    if (choice.current_level == 0) {
      lv_text = "NEW";
    } else {
      lv_text = "Lv " + std::to_string(choice.current_level) + " -> " +
                std::to_string(choice.current_level + 1);
    }
    DrawText(lv_text.c_str(), cx + 8, cy + 28, 12,
             choice.current_level == 0 ? GREEN : YELLOW);

    // 描述 (CD / Mana / Range)
    if (!choice.description.empty()) {
      DrawText(choice.description.c_str(), cx + 8, cy + 46, 11, Color{160, 170, 200, 255});
    }

    // 特殊数值 (最多显示 4 条)
    int spec_y = cy + 66;
    int shown = 0;
    for (const auto& sv : choice.specials) {
      if (shown >= 4) break;
      std::string line = sv.key + ": ";
      for (size_t j = 0; j < sv.values.size(); ++j) {
        if (j > 0) line += "/";
        float v = sv.values[j];
        if (v == static_cast<int>(v)) {
          line += std::to_string(static_cast<int>(v));
        } else {
          char buf[16];
          snprintf(buf, sizeof(buf), "%.1f", v);
          line += buf;
        }
      }
      DrawText(line.c_str(), cx + 8, spec_y, 11, Color{200, 200, 220, 255});
      spec_y += 14;
      ++shown;
    }
  }
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
