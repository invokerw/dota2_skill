// include/server/mode/experience_system.hpp
// 经验和升级系统 - Stage 4

#pragma once

#include <cstdint>
#include <map>
#include <functional>

namespace dota::server {

/**
 * 经验系统
 *
 * 管理玩家经验、等级和升级
 */
class ExperienceSystem {
 public:
  ExperienceSystem();
  ~ExperienceSystem() = default;

  // 添加经验
  void add_experience(uint32_t player_id, uint32_t exp);

  // 查询状态
  uint32_t get_level(uint32_t player_id) const;
  uint32_t get_experience(uint32_t player_id) const;
  uint32_t get_exp_for_next_level(uint32_t current_level) const;

  // 升级回调
  using LevelUpCallback = std::function<void(uint32_t player_id, uint32_t new_level)>;
  void set_level_up_callback(LevelUpCallback callback) {
    level_up_callback_ = callback;
  }

 private:
  struct PlayerExpData {
    uint32_t level = 1;
    uint32_t experience = 0;
  };

  std::map<uint32_t, PlayerExpData> player_data_;
  LevelUpCallback level_up_callback_;

  // 内部方法
  void check_level_up(uint32_t player_id);
  uint32_t calculate_exp_for_level(uint32_t level) const;
};

} // namespace dota::server
