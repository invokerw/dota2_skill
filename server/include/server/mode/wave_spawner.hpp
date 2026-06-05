// include/server/mode/wave_spawner.hpp
// 波次刷怪系统 - Stage 4

#pragma once

#include "dota/core/types.hpp"
#include <cstdint>
#include <vector>
#include <string>

namespace dota {
class World;
class Unit;
}

namespace dota::server {

/**
 * 波次配置
 */
struct WaveConfig {
  uint32_t wave_number;
  uint32_t enemy_count;
  std::string enemy_type;
  float spawn_interval;  // 生成间隔（秒）
  float enemy_hp_multiplier;
  float enemy_damage_multiplier;
};

/**
 * 波次刷怪器
 *
 * 管理敌人波次的生成和进度
 */
class WaveSpawner {
 public:
  explicit WaveSpawner(dota::World* world);
  ~WaveSpawner() = default;

  // 开始新波次
  void start_wave(uint32_t wave_number);

  // Tick 更新
  void tick(float dt);

  // 查询状态
  bool is_wave_active() const { return wave_active_; }
  uint32_t current_wave() const { return current_wave_; }
  uint32_t remaining_enemies() const { return remaining_enemies_; }
  float time_until_next_spawn() const { return time_until_next_spawn_; }

  // 敌人被击杀通知
  void on_enemy_killed(uint32_t enemy_id);

 private:
  dota::World* world_;

  // 波次状态
  bool wave_active_ = false;
  uint32_t current_wave_ = 0;
  uint32_t remaining_enemies_ = 0;
  uint32_t alive_enemies_ = 0;
  float time_until_next_spawn_ = 0.0f;

  // 当前波次配置
  WaveConfig current_config_;

  // 内部方法
  WaveConfig get_wave_config(uint32_t wave_number);
  void spawn_enemy();
  Vec2 get_spawn_position();
};

} // namespace dota::server
