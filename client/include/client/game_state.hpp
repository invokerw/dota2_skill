// include/client/game_state.hpp
// 客户端游戏状态管理

#pragma once

#include "dota/core/types.hpp"
#include "messages.pb.h"
#include <map>
#include <vector>
#include <string>
#include <cstdint>

namespace dota::client {

// 客户端技能槽状态
struct ClientAbility {
  uint32_t slot = 0;
  std::string name;
  uint32_t level = 0;
  float cooldown_remaining = 0.0f;
  bool is_passive = false;
  float mana_cost = 0.0f;
  float max_cooldown = 0.0f;
  float cast_range = 0.0f;
};

// 技能数值条目
struct SkillSpecialValue {
  std::string key;
  std::vector<float> values;
};

// 待选技能选项
struct SkillChoice {
  uint32_t index = 0;
  std::string name;
  std::string description;
  uint32_t current_level = 0;
  uint32_t max_level = 0;
  std::vector<SkillSpecialValue> specials;
};

/**
 * 客户端实体
 *
 * 从服务器快照同步的实体状态
 */
struct ClientEntity {
  uint32_t id = 0;
  Vec2 position{0, 0};
  float health = 0;
  float max_health = 0;
  float radius = 32.0f;
  bool is_player = false;
  bool is_enemy = false;

  // 用于客户端预测
  Vec2 move_target{0, 0};
  bool has_move_target = false;
  float move_speed = 350.0f;

  // 插值 (非玩家实体)
  Vec2 prev_position{0, 0};
  Vec2 target_position{0, 0};
  float interp_t = 1.0f;

  // 服务器权威位置 (本地玩家校正用)
  Vec2 server_position{0, 0};
  bool has_server_position = false;

};

/**
 * 游戏状态
 *
 * 管理客户端的游戏世界状态
 */
class GameState {
 public:
  GameState() = default;

  // 处理服务器快照
  void apply_snapshot(const network::S2C_Snapshot& snapshot);
  void apply_delta_snapshot(const network::S2C_DeltaSnapshot& delta);

  // 设置玩家 ID
  void set_player_id(uint32_t id) { player_id_ = id; }
  uint32_t player_id() const { return player_id_; }

  // 获取实体
  const ClientEntity* get_entity(uint32_t id) const;
  const ClientEntity* get_player() const { return get_entity(player_id_); }

  // 获取所有实体
  const std::map<uint32_t, ClientEntity>& entities() const { return entities_; }

  // 客户端预测
  void predict(float dt);

  // 设置本地玩家移动目标 (用于预测)
  void set_player_move_target(Vec2 target);
  void clear_player_move_target();

  // 获取服务器 tick
  uint32_t server_tick() const { return server_tick_; }

  // 技能状态
  void apply_level_up(const network::S2C_LevelUp& msg);
  void apply_skill_learned(const network::S2C_SkillLearned& msg);

  // 玩家技能列表 (从快照同步)
  const std::vector<ClientAbility>& abilities() const { return abilities_; }

  // 待选技能
  bool has_pending_choices() const { return !pending_choices_.empty(); }
  const std::vector<SkillChoice>& pending_choices() const { return pending_choices_; }
  void clear_pending_choices() { pending_choices_.clear(); }

 private:
  void apply_entity_state(const network::EntityState& state);
  void apply_player_state(const network::PlayerState& ps);

  uint32_t player_id_ = 0;
  uint32_t server_tick_ = 0;
  std::map<uint32_t, ClientEntity> entities_;

  // 技能
  std::vector<ClientAbility> abilities_;
  std::vector<SkillChoice> pending_choices_;
};

} // namespace dota::client
