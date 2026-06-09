// include/server/mode/skill_pool.hpp
// 技能池系统 - Stage 4

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace dota {
class AbilityRegistry;
}

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
 * 管理可选技能和玩家已选技能.
 * 玩家加入时需要选满 kMaxSlots 个不同技能, 之后升级时提升已有技能等级.
 */
class SkillPool {
 public:
  static constexpr uint32_t kMaxSlots = 4;
  static constexpr uint32_t kMaxSkillLevel = 4;

  SkillPool();
  ~SkillPool() = default;

  // 从 AbilityRegistry 加载可用技能名 (过滤掉纯被动技能)
  void initialize(const std::string& abilities_dir, const dota::AbilityRegistry* registry = nullptr);

  // 玩家是否已选满初始技能
  bool has_full_slots(uint32_t player_id) const;

  // 玩家拥有的不同技能数量
  uint32_t slot_count(uint32_t player_id) const;

  // 为玩家生成技能选项
  // 初始阶段: 从未拥有的技能里随机选
  // 升级阶段: 从已拥有的未满级技能里随机选
  std::vector<std::string> generate_skill_choices(uint32_t player_id, uint32_t count = 3);

  // 玩家选择技能
  bool choose_skill(uint32_t player_id, const std::string& skill_id);

  // 查询玩家技能
  std::vector<SkillInfo> get_player_skills(uint32_t player_id) const;
  uint32_t get_skill_level(uint32_t player_id, const std::string& skill_id) const;

  // 获取所有可用技能
  const std::vector<std::string>& available_skills() const { return available_skills_; }

 private:
  // 所有可用技能
  std::vector<std::string> available_skills_;

  // 玩家已选技能: player_id -> (skill_id -> level)
  std::map<uint32_t, std::map<std::string, uint32_t>> player_skills_;

  // 内部方法
  bool can_choose_skill(uint32_t player_id, const std::string& skill_id) const;
};

} // namespace dota::server
