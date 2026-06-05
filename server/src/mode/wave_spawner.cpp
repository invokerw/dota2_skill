// src/mode/wave_spawner.cpp
// 波次刷怪系统实现 - Stage 4

#include "server/mode/wave_spawner.hpp"
#include "dota/core/world.hpp"
#include "dota/core/unit.hpp"
#include <iostream>
#include <cmath>

namespace dota::server {

WaveSpawner::WaveSpawner(dota::World* world) : world_(world) {}

void WaveSpawner::start_wave(uint32_t wave_number) {
  current_wave_ = wave_number;
  current_config_ = get_wave_config(wave_number);

  remaining_enemies_ = current_config_.enemy_count;
  alive_enemies_ = 0;
  time_until_next_spawn_ = 0.0f;
  wave_active_ = true;

  std::cout << "[WaveSpawner] Starting wave " << wave_number
            << " with " << current_config_.enemy_count << " enemies\n";
}

void WaveSpawner::tick(float dt) {
  if (!wave_active_) return;

  // 生成新敌人
  if (remaining_enemies_ > 0) {
    time_until_next_spawn_ -= dt;
    if (time_until_next_spawn_ <= 0.0f) {
      spawn_enemy();
      remaining_enemies_--;
      time_until_next_spawn_ = current_config_.spawn_interval;
    }
  }

  // 检查波次是否完成
  if (remaining_enemies_ == 0 && alive_enemies_ == 0) {
    wave_active_ = false;
    std::cout << "[WaveSpawner] Wave " << current_wave_ << " completed!\n";
  }
}

void WaveSpawner::on_enemy_killed(uint32_t enemy_id) {
  if (alive_enemies_ > 0) {
    alive_enemies_--;
  }
}

WaveConfig WaveSpawner::get_wave_config(uint32_t wave_number) {
  WaveConfig config;
  config.wave_number = wave_number;

  // 每波敌人数量递增
  config.enemy_count = 5 + (wave_number - 1) * 2;

  // 生成间隔
  config.spawn_interval = std::max(0.5f, 2.0f - wave_number * 0.1f);

  // 属性倍率
  config.enemy_hp_multiplier = 1.0f + (wave_number - 1) * 0.2f;
  config.enemy_damage_multiplier = 1.0f + (wave_number - 1) * 0.15f;

  // 敌人类型（简化）
  config.enemy_type = "melee_creep";

  return config;
}

void WaveSpawner::spawn_enemy() {
  Vec2 spawn_pos = get_spawn_position();

  // 创建敌人单位
  dota::UnitStats stats;
  stats.max_health = 100.0 * current_config_.enemy_hp_multiplier;
  stats.attack_damage = 10.0 * current_config_.enemy_damage_multiplier;
  stats.move_speed = 250.0;
  stats.attack_range = 150.0;

  dota::Unit* enemy = world_->spawn(
    current_config_.enemy_type,
    dota::Team::Dire,
    stats,
    spawn_pos
  );

  alive_enemies_++;

  std::cout << "[WaveSpawner] Spawned enemy " << enemy->id()
            << " at (" << spawn_pos.x << ", " << spawn_pos.y << ")\n";
}

Vec2 WaveSpawner::get_spawn_position() {
  // 简单的圆形生成位置
  static float angle = 0.0f;
  angle += 0.5f;

  float radius = 800.0f;
  float x = 1600.0f + radius * std::cos(angle);
  float y = 1600.0f + radius * std::sin(angle);

  return Vec2{x, y};
}

} // namespace dota::server
