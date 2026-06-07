// src/game_state.cpp
// 游戏状态实现

#include "client/game_state.hpp"
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

  // 根据 team 判断类型
  // 假设 team=0 是玩家，team=1 是敌人
  if (state.team() == 0) {
    entity.is_player = true;
    entity.radius = 40.0f;
  } else if (state.team() == 1) {
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
  // TODO: 客户端预测
  // 在收到服务器快照之间，根据输入预测玩家位置
  // 收到快照后进行修正
}

} // namespace dota::client
