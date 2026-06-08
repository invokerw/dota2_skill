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
}

void GameState::apply_entity_state(const network::EntityState& state) {
  ClientEntity entity;
  entity.id = state.id();
  entity.position.x = state.position().x();
  entity.position.y = state.position().y();
  entity.health = state.health();
  entity.max_health = state.max_health();

  // TEAM_NONE=0, TEAM_GOOD=1 (Radiant/玩家方), TEAM_BAD=2 (Dire/敌方)
  if (state.team() == dota::network::TEAM_GOOD) {
    entity.is_player = true;
    entity.radius = 40.0f;
  } else if (state.team() == dota::network::TEAM_BAD) {
    entity.is_enemy = true;
    entity.radius = 32.0f;
  }

  entities_[entity.id] = entity;
}

const ClientEntity* GameState::get_entity(uint32_t id) const {
  auto it = entities_.find(id);
  return (it != entities_.end()) ? &it->second : nullptr;
}

void GameState::predict(float dt) {
  // 对本地玩家做移动预测: 按 move_speed 向 move_target 移动
  auto it = entities_.find(player_id_);
  if (it == entities_.end()) return;

  ClientEntity& player = it->second;
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

} // namespace dota::client
