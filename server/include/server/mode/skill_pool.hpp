// include/server/mode/skill_pool.hpp
// 技能池系统 - Stage 4

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace dota::server {

/**
 * 技能信息
 */
struct SkillInfo {
  std::string skill_id;
  std::string display_name;
  std::string description;
  uint32_t max_level;
  uint32_t current_level;
};

/**
 * 技能池
 *
 * 管理可选技能和玩家已选技能
 */
class SkillPool {
 public:
  SkillPool();
  ~SkillPool() = default;

  // 初始化技能池
  void initialize();

  // 为玩家生成技能选项（通常 3 个）
  std::vector<std::string> generate_skill_choices(uint32_t player_id, uint32_t count = 3);

  // 玩家选择技能
  bool choose_skill(uint32_t player_id, const std::string& skill_id);

  // 查询玩家技能
  std::vector<SkillInfo> get_player_skills(uint32_t player_id) const;
  uint32_t get_skill_level(uint32_t player_id, const std::string& skill_id) const;

 private:
  // 所有可用技能
  std::vector<std::string> available_skills_;

  // 玩家已选技能: player_id -> (skill_id -> level)
  std::map<uint32_t, std::map<std::string, uint32_t>> player_skills_;

  // 内部方法
  void register_skill(const std::string& skill_id);
  bool can_choose_skill(uint32_t player_id, const std::string& skill_id) const;
};

} // namespace dota::server
