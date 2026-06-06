// src/mode/experience_system.cpp
// 经验系统实现 - Stage 4

#include "server/mode/experience_system.hpp"
#include <cmath>
#include <iostream>

namespace dota::server {

ExperienceSystem::ExperienceSystem() {}

void ExperienceSystem::add_experience(uint32_t player_id, uint32_t exp) {
  auto& data = player_data_[player_id];
  data.experience += exp;

  std::cout << "[ExpSystem] Player " << player_id << " gained " << exp
            << " exp (total: " << data.experience << ")\n";

  check_level_up(player_id);
}

uint32_t ExperienceSystem::get_level(uint32_t player_id) const {
  auto it = player_data_.find(player_id);
  return (it != player_data_.end()) ? it->second.level : 1;
}

uint32_t ExperienceSystem::get_experience(uint32_t player_id) const {
  auto it = player_data_.find(player_id);
  return (it != player_data_.end()) ? it->second.experience : 0;
}

uint32_t ExperienceSystem::get_exp_for_next_level(uint32_t current_level) const {
  return calculate_exp_for_level(current_level + 1);
}

void ExperienceSystem::check_level_up(uint32_t player_id) {
  auto& data = player_data_[player_id];

  while (true) {
    uint32_t exp_required = calculate_exp_for_level(data.level + 1);

    if (data.experience >= exp_required) {
      data.level++;
      std::cout << "[ExpSystem] Player " << player_id
                << " leveled up to " << data.level << "!\n";

      // 触发升级回调
      if (level_up_callback_) {
        level_up_callback_(player_id, data.level);
      }
    } else {
      break;
    }
  }
}

uint32_t ExperienceSystem::calculate_exp_for_level(uint32_t level) const {
  // 经验需求公式: 100 * level^1.5
  return static_cast<uint32_t>(100.0 * std::pow(level, 1.5));
}

} // namespace dota::server
