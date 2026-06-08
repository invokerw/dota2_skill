// include/server/mode/survivor_mode.hpp
// 生存模式 - Stage 4

#pragma once

#include "server/mode/wave_spawner.hpp"
#include "server/mode/experience_system.hpp"
#include "server/mode/skill_pool.hpp"
#include "dota/core/types.hpp"
#include "messages.pb.h"
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <functional>

namespace dota {
class World;
class Unit;
}

namespace dota::server {

class GameSession;

/**
 * 生存模式
 *
 * 管理生存模式的核心玩法：
 * - 波次刷怪
 * - 经验和升级
 * - 技能选择
 * - 掉落物生成
 */
class SurvivorGameMode {
 public:
  explicit SurvivorGameMode(GameSession* session);
  ~SurvivorGameMode() = default;

  // 初始化模式
  void initialize();

  // Tick 更新
  void tick(float dt);

  // 玩家事件
  void on_player_joined(uint32_t player_id, uint32_t unit_id);
  void on_player_left(uint32_t player_id);

  // 单位事件
  void on_unit_killed(uint32_t victim_id, uint32_t killer_id);
  void on_unit_level_up(uint32_t unit_id, uint32_t new_level);

  // 技能选择
  void request_skill_choices(uint32_t player_id);
  void choose_skill(uint32_t player_id, const std::string& skill_id);

  // 按索引选技能 (proto 传 uint32 索引)
  void choose_skill_by_index(uint32_t player_id, uint32_t index);

  // 获取玩家待选技能列表
  const std::vector<std::string>& get_pending_choices(uint32_t player_id) const;

  // 消息发送回调 (由 GameSession 设置)
  using SendToPlayerFn = std::function<void(uint32_t player_id, const dota::network::Packet& packet)>;
  void set_send_callback(SendToPlayerFn fn) { send_to_player_ = std::move(fn); }

  // 获取状态
  uint32_t current_wave() const { return current_wave_; }
  float wave_time_remaining() const;
  bool is_wave_active() const;

 private:
  GameSession* session_;
  dota::World* world_;

  // 子系统
  std::unique_ptr<WaveSpawner> wave_spawner_;
  std::unique_ptr<ExperienceSystem> exp_system_;
  std::unique_ptr<SkillPool> skill_pool_;

  // 模式状态
  uint32_t current_wave_ = 0;
  float game_time_ = 0.0f;
  bool initialized_ = false;

  // 内部方法
  void start_first_wave();
  void spawn_experience_orb(const Vec2& position, uint32_t exp_value);
  void spawn_gold_coin(const Vec2& position, uint32_t gold_value);
  void tick_respawns();

  // 复活队列
  struct RespawnEntry {
    uint32_t unit_id;
    float respawn_time;
  };
  std::vector<RespawnEntry> pending_respawns_;

  // 玩家待选技能: player_id -> 可选 skill_id 列表
  std::map<uint32_t, std::vector<std::string>> pending_choices_;

  // 消息发送回调
  SendToPlayerFn send_to_player_;
};

} // namespace dota::server
