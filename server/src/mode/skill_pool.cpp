// src/mode/skill_pool.cpp
// 技能池系统实现

#include "server/mode/skill_pool.hpp"
#include "dota/ability/registry.hpp"
#include "dota/ability/behavior.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <filesystem>

namespace fs = std::filesystem;

namespace dota::server {

SkillPool::SkillPool() {}

void SkillPool::initialize(const std::string& abilities_dir, const dota::AbilityRegistry* registry) {
  available_skills_.clear();

  if (!fs::exists(abilities_dir) || !fs::is_directory(abilities_dir)) {
    std::cerr << "[SkillPool] abilities_dir not found: " << abilities_dir << "\n";
    return;
  }

  for (const auto& entry : fs::directory_iterator(abilities_dir)) {
    if (!entry.is_regular_file()) continue;
    auto ext = entry.path().extension().string();
    if (ext != ".yaml" && ext != ".yml") continue;

    std::string skill_name = entry.path().stem().string();

    // 跳过纯被动技能 (只有 intrinsic modifier, 不能主动施放)
    if (registry) {
      const auto* def = registry->find(skill_name);
      if (def && has_flag(def->behavior, BehaviorFlag::Passive)) {
        continue;
      }
    }

    available_skills_.push_back(skill_name);
  }

  std::sort(available_skills_.begin(), available_skills_.end());

  std::cout << "[SkillPool] Loaded " << available_skills_.size()
            << " castable skills from " << abilities_dir << "\n";
}

bool SkillPool::has_full_slots(uint32_t player_id) const {
  return slot_count(player_id) >= kMaxSlots;
}

uint32_t SkillPool::slot_count(uint32_t player_id) const {
  auto it = player_skills_.find(player_id);
  if (it == player_skills_.end()) return 0;
  return static_cast<uint32_t>(it->second.size());
}

std::vector<std::string> SkillPool::generate_skill_choices(uint32_t player_id, uint32_t count) {
  std::vector<std::string> choices;

  if (!has_full_slots(player_id)) {
    // 初始阶段: 从玩家未拥有的技能中随机选
    std::vector<std::string> candidates;
    auto it = player_skills_.find(player_id);

    for (const auto& skill : available_skills_) {
      if (it == player_skills_.end() || it->second.find(skill) == it->second.end()) {
        candidates.push_back(skill);
      }
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);

    for (size_t i = 0; i < candidates.size() && choices.size() < count; ++i) {
      choices.push_back(candidates[i]);
    }
  } else {
    // 升级阶段: 从已拥有且未满级的技能中选
    auto it = player_skills_.find(player_id);
    if (it == player_skills_.end()) return choices;

    std::vector<std::string> candidates;
    for (const auto& [skill_id, level] : it->second) {
      if (level < kMaxSkillLevel) {
        candidates.push_back(skill_id);
      }
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);

    for (size_t i = 0; i < candidates.size() && choices.size() < count; ++i) {
      choices.push_back(candidates[i]);
    }
  }

  return choices;
}

bool SkillPool::choose_skill(uint32_t player_id, const std::string& skill_id) {
  if (!can_choose_skill(player_id, skill_id)) {
    std::cout << "[SkillPool] Player " << player_id
              << " cannot choose skill " << skill_id << "\n";
    return false;
  }

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
    info.display_name = skill_id;
    info.description = "";
    info.max_level = kMaxSkillLevel;
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

bool SkillPool::can_choose_skill(uint32_t player_id, const std::string& skill_id) const {
  if (std::find(available_skills_.begin(), available_skills_.end(), skill_id)
      == available_skills_.end()) {
    std::cout << "[SkillPool] Skill '" << skill_id << "' not in available_skills\n";
    return false;
  }

  uint32_t current_level = get_skill_level(player_id, skill_id);

  if (!has_full_slots(player_id)) {
    if (current_level != 0) {
      std::cout << "[SkillPool] Player " << player_id << " already has '"
                << skill_id << "' at level " << current_level << " (initial phase)\n";
    }
    return current_level == 0;
  } else {
    if (current_level == 0) {
      std::cout << "[SkillPool] Player " << player_id << " doesn't own '"
                << skill_id << "' (upgrade phase)\n";
    } else if (current_level >= kMaxSkillLevel) {
      std::cout << "[SkillPool] Player " << player_id << " skill '"
                << skill_id << "' already max level\n";
    }
    return current_level > 0 && current_level < kMaxSkillLevel;
  }
}

} // namespace dota::server
