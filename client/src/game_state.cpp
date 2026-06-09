// src/game_state.cpp
// 游戏状态实现

#include "client/game_state.hpp"
#include <cmath>
#include <iostream>

namespace dota::client {

void GameState::apply_snapshot(const network::S2C_Snapshot& snapshot) {
  server_tick_ = snapshot.tick();

  // 清空旧实体
  entities_.clear();

  // 应用所有实体
  for (const auto& entity_state : snapshot.entities()) {
    apply_entity_state(entity_state);
  }

  // 应用玩家状态
  for (const auto& ps : snapshot.players()) {
    if (ps.unit_id() == player_id_) {
      apply_player_state(ps);
    }
  }
}

void GameState::apply_delta_snapshot(const network::S2C_DeltaSnapshot& delta) {
  server_tick_ = delta.tick();

  // 应用更新的实体
  for (const auto& entity_state : delta.updated_entities()) {
    apply_entity_state(entity_state);
  }

  // 删除实体
  for (uint32_t removed_id : delta.removed_entities()) {
    entities_.erase(removed_id);
  }

  // 应用玩家状态
  for (const auto& ps : delta.updated_players()) {
    if (ps.unit_id() == player_id_) {
      apply_player_state(ps);
    }
  }
}

void GameState::apply_entity_state(const network::EntityState& state) {
  Vec2 server_pos{state.position().x(), state.position().y()};

  auto it = entities_.find(state.id());
  if (it != entities_.end()) {
    // 已有实体: 更新非位置字段
    ClientEntity& entity = it->second;
    entity.health = state.health();
    entity.max_health = state.max_health();

    if (state.id() == player_id_ && entity.has_move_target) {
      // 本地玩家正在预测移动中, 不覆盖位置, 只记录服务器权威位置用于校正
      entity.server_position = server_pos;
      entity.has_server_position = true;
    } else {
      // 非玩家实体或玩家静止: 插值目标设为服务器位置
      entity.prev_position = entity.position;
      entity.target_position = server_pos;
      entity.interp_t = 0.0f;
    }
  } else {
    // 新实体
    ClientEntity entity;
    entity.id = state.id();
    entity.position = server_pos;
    entity.prev_position = server_pos;
    entity.target_position = server_pos;
    entity.interp_t = 1.0f;
    entity.health = state.health();
    entity.max_health = state.max_health();

    if (state.team() == dota::network::TEAM_GOOD) {
      entity.is_player = true;
      entity.radius = 40.0f;
    } else if (state.team() == dota::network::TEAM_BAD) {
      entity.is_enemy = true;
      entity.radius = 32.0f;
    }

    entities_[entity.id] = entity;
  }
}

const ClientEntity* GameState::get_entity(uint32_t id) const {
  auto it = entities_.find(id);
  return (it != entities_.end()) ? &it->second : nullptr;
}

void GameState::predict(float dt) {
  // 插值所有非玩家实体
  for (auto& [id, entity] : entities_) {
    if (id == player_id_) continue;
    if (entity.interp_t < 1.0f) {
      entity.interp_t += dt * 30.0f;  // 一个 tick (1/30s) 内完成插值
      if (entity.interp_t > 1.0f) entity.interp_t = 1.0f;
      float t = entity.interp_t;
      entity.position.x = entity.prev_position.x + (entity.target_position.x - entity.prev_position.x) * t;
      entity.position.y = entity.prev_position.y + (entity.target_position.y - entity.prev_position.y) * t;
    }
  }

  // 本地玩家移动预测
  auto it = entities_.find(player_id_);
  if (it == entities_.end()) return;

  ClientEntity& player = it->second;

  // 服务器位置校正
  if (player.has_server_position) {
    float correction_dx = player.server_position.x - player.position.x;
    float correction_dy = player.server_position.y - player.position.y;
    float correction_dist = std::sqrt(correction_dx * correction_dx + correction_dy * correction_dy);

    // 偏差过大: 直接 snap 到服务器位置 (碰到阻挡时客户端会跑偏)
    constexpr float snap_threshold = 50.0f;
    if (correction_dist > snap_threshold) {
      player.position = player.server_position;
      player.has_server_position = false;
    } else if (correction_dist > 1.0f) {
      // 小偏差: 平滑校正
      float blend = std::min(1.0f, dt * 10.0f);
      player.position.x += correction_dx * blend;
      player.position.y += correction_dy * blend;
    } else {
      player.has_server_position = false;
    }
  }

  if (!player.has_move_target) return;

  float dx = player.move_target.x - player.position.x;
  float dy = player.move_target.y - player.position.y;
  float dist = std::sqrt(dx * dx + dy * dy);

  float step = player.move_speed * dt;
  if (dist <= step) {
    player.position = player.move_target;
    player.has_move_target = false;
  } else {
    player.position.x += dx / dist * step;
    player.position.y += dy / dist * step;
  }
}

void GameState::set_player_move_target(Vec2 target) {
  auto it = entities_.find(player_id_);
  if (it == entities_.end()) return;

  it->second.move_target = target;
  it->second.has_move_target = true;
}

void GameState::clear_player_move_target() {
  auto it = entities_.find(player_id_);
  if (it == entities_.end()) return;

  it->second.has_move_target = false;
}

void GameState::apply_player_state(const network::PlayerState& ps) {
  abilities_.clear();
  for (const auto& as : ps.abilities()) {
    ClientAbility ab;
    ab.slot = as.slot();
    ab.name = as.name();
    ab.level = as.level();
    ab.cooldown_remaining = as.cooldown_remaining();
    ab.is_passive = as.is_passive();
    ab.mana_cost = as.mana_cost();
    ab.max_cooldown = as.max_cooldown();
    ab.cast_range = as.cast_range();
    abilities_.push_back(ab);
  }
}

void GameState::apply_level_up(const network::S2C_LevelUp& msg) {
  pending_choices_.clear();
  for (const auto& opt : msg.options()) {
    SkillChoice choice;
    choice.index = opt.skill_id();
    choice.name = opt.skill_name();
    choice.description = opt.description();
    choice.current_level = opt.current_level();
    choice.max_level = opt.max_level();
    for (const auto& sv : opt.special_values()) {
      SkillSpecialValue ssv;
      ssv.key = sv.key();
      for (int i = 0; i < sv.values_size(); ++i) {
        ssv.values.push_back(sv.values(i));
      }
      choice.specials.push_back(ssv);
    }
    pending_choices_.push_back(choice);
  }
}

void GameState::apply_skill_learned(const network::S2C_SkillLearned& msg) {
  (void)msg;
  pending_choices_.clear();
}

} // namespace dota::client
