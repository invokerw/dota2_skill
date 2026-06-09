// include/client/renderer.hpp
// 渲染器 - 使用 raylib

#pragma once

#include "client/game_state.hpp"
#include "raylib.h"

namespace dota::client {

/**
 * 渲染器
 *
 * 使用 raylib 渲染游戏画面
 */
class Renderer {
 public:
  Renderer();
  ~Renderer();

  // 初始化窗口
  bool init(int width, int height, const char* title);

  // 关闭
  void shutdown();

  // 开始/结束绘制
  void begin_draw();
  void end_draw();

  // 渲染游戏状态
  void draw(const GameState& state);

  // 渲染 UI
  void draw_ui(uint32_t player_id, float latency);
  void draw_ability_hud(const GameState& state);
  void draw_skill_choice(const GameState& state);

  // 窗口状态
  bool should_close() const;

  // 屏幕坐标转世界坐标
  Vec2 screen_to_world(Vector2 screen_pos) const;

  // 世界坐标转屏幕坐标
  Vector2 world_to_screen(Vec2 world_pos) const;

 private:
  void draw_entity(const ClientEntity& entity);
  void draw_health_bar(Vec2 position, float health, float max_health, float width);

  int width_ = 0;
  int height_ = 0;

  // 相机偏移（跟随玩家）
  Vec2 camera_offset_{0, 0};

  // 简单的贴图（可以后续替换）
  bool initialized_ = false;
};

} // namespace dota::client
