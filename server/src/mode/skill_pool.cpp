// src/mode/skill_pool.cpp
// 技能池系统实现 - Stage 4

#include "server/mode/skill_pool.hpp"
#include <iostream>
#include <algorithm>
#include <random>

namespace dota::server {

SkillPool::SkillPool() {}

void SkillPool::initialize() {
  // 注册一些示例技能
  register_skill("fireball");
  register_skill("ice_blast");
  register_skill("lightning_strike");
  register_skill("heal");
  register_skill("shield");
  register_skill("speed_boost");
  register_skill("damage_aura");
  register_skill("critical_strike");

  std::cout << "[SkillPool] Initialized with " << available_skills_.size() << " skills\n";
}

std::vector<std::string> SkillPool::generate_skill_choices(uint32_t player_id, uint32_t count) {
  std::vector<std::string> choices;
  std::vector<std::string> candidates = available_skills_;

  // 随机打乱
  std::random_device rd;
  std::mt19937 gen(rd());
  std::shuffle(candidates.begin(), candidates.end(), gen);

  // 选择前 N 个可选技能
  for (const auto& skill_id : candidates) {
    if (can_choose_skill(player_id, skill_id)) {
      choices.push_back(skill_id);
      if (choices.size() >= count) break;
    }
  }

  std::cout << "[SkillPool] Generated " << choices.size()
            << " skill choices for player " << player_id << "\n";

  return choices;
}

bool SkillPool::choose_skill(uint32_t player_id, const std::string& skill_id) {
  if (!can_choose_skill(player_id, skill_id)) {
    std::cout << "[SkillPool] Player " << player_id
              << " cannot choose skill " << skill_id << "\n";
    return false;
  }

  // 增加技能等级
  auto& skills = player_skills_[player_id];
  skills[skill_id]++;

  std::cout << "[SkillPool] Player " << player_id << " chose skill "
            << skill_id << " (level " << skills[skill_id] << ")\n";

  return true;
}

std::vector<SkillInfo> SkillPool::get_player_skills(uint32_t player_id) const {
  std::vector<SkillInfo> result;

  auto it = player_skills_.find(player_id);
  if (it == player_skills_.end()) return result;

  for (const auto& [skill_id, level] : it->second) {
    SkillInfo info;
    info.skill_id = skill_id;
    info.display_name = skill_id;  // TODO: 从配置读取
    info.description = "Skill description";
    info.max_level = 5;
    info.current_level = level;
    result.push_back(info);
  }

  return result;
}

uint32_t SkillPool::get_skill_level(uint32_t player_id, const std::string& skill_id) const {
  auto it = player_skills_.find(player_id);
  if (it == player_skills_.end()) return 0;

  auto skill_it = it->second.find(skill_id);
  return (skill_it != it->second.end()) ? skill_it->second : 0;
}

void SkillPool::register_skill(const std::string& skill_id) {
  available_skills_.push_back(skill_id);
}

bool SkillPool::can_choose_skill(uint32_t player_id, const std::string& skill_id) const {
  // 检查技能是否存在
  if (std::find(available_skills_.begin(), available_skills_.end(), skill_id)
      == available_skills_.end()) {
    return false;
  }

  // 检查是否已达到最大等级
  uint32_t current_level = get_skill_level(player_id, skill_id);
  constexpr uint32_t max_level = 5;

  return current_level < max_level;
}

} // namespace dota::server
