// src/server/game_session.cpp
// 游戏会话实现 - Stage 4

#include "server/server/game_session.hpp"
#include "server/mode/survivor_mode.hpp"
#include "dota/core/unit.hpp"
#include <iostream>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace dota::server {

namespace {
// 默认地图大小
constexpr float kMapWidth = 3200.0f;
constexpr float kMapHeight = 3200.0f;

// 玩家出生位置
Vec2 get_spawn_position(size_t player_index) {
  // 简单的圆形分布
  float angle = (2.0f * M_PI * player_index) / 8.0f;
  float radius = 500.0f;
  return Vec2{
    kMapWidth / 2 + radius * std::cos(angle),
    kMapHeight / 2 + radius * std::sin(angle)
  };
}
}

GameSession::GameSession(uint32_t session_id, const std::string& map_name)
    : session_id_(session_id) {

  // 创建 World（不需要参数，默认构造）
  world_ = std::make_unique<dota::World>();

  // 创建游戏模式
  game_mode_ = std::make_unique<SurvivorGameMode>(this);
  game_mode_->initialize();

  std::cout << "[GameSession] Created session " << session_id
            << " map=" << map_name << "\n";
}

GameSession::~GameSession() = default;

void GameSession::set_send_callback(SendToPlayerFn fn) {
  game_mode_->set_send_callback(std::move(fn));
}

bool GameSession::add_player(uint32_t player_id, const std::string& player_name) {
  if (has_player(player_id)) {
    std::cout << "[GameSession] Player " << player_id << " already in session\n";
    return false;
  }

  // 创建玩家单位
  Vec2 spawn_pos = get_spawn_position(player_units_.size());
  uint32_t unit_id = create_player_unit(player_name, spawn_pos);

  player_units_[player_id] = unit_id;

  // 通知游戏模式
  game_mode_->on_player_joined(player_id, unit_id);

  std::cout << "[GameSession] Added player " << player_id
            << " (" << player_name << ") unit=" << unit_id << "\n";
  return true;
}

void GameSession::remove_player(uint32_t player_id) {
  auto it = player_units_.find(player_id);
  if (it == player_units_.end()) return;

  // 通知游戏模式
  game_mode_->on_player_left(player_id);

  // 将单位生命值置零使其退出游戏逻辑
  uint32_t unit_id = it->second;
  dota::Unit* unit = world_->find(unit_id);
  if (unit) {
    unit->set_health(0);
  }

  player_units_.erase(it);

  std::cout << "[GameSession] Removed player " << player_id << "\n";
}

bool GameSession::has_player(uint32_t player_id) const {
  return player_units_.find(player_id) != player_units_.end();
}

uint32_t GameSession::get_player_unit(uint32_t player_id) const {
  auto it = player_units_.find(player_id);
  return (it != player_units_.end()) ? it->second : 0;
}

void GameSession::tick(float dt) {
  // Tick World
  world_->advance(dt);

  // Tick 游戏模式
  game_mode_->tick(dt);

  tick_count_++;
}

void GameSession::handle_move_command(uint32_t player_id, const dota::network::MoveCommand& cmd) {
  uint32_t unit_id = get_player_unit(player_id);
  if (unit_id == 0) return;

  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  const auto& target_vec = cmd.target();
  Vec2 target{target_vec.x(), target_vec.y()};

  unit->issue_order(OrderMoveToPoint{target});
}

void GameSession::handle_use_ability_command(uint32_t player_id, const dota::network::UseAbilityCommand& cmd) {
  uint32_t unit_id = get_player_unit(player_id);
  if (unit_id == 0) return;

  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  uint32_t slot = cmd.ability_slot();
  const auto& abilities = unit->abilities().all();
  if (slot >= abilities.size()) return;

  int idx = static_cast<int>(slot);

  switch (cmd.target_case()) {
    case dota::network::UseAbilityCommand::kPoint: {
      const auto& p = cmd.point();
      unit->issue_order(OrderCastPoint{idx, Vec2{p.x(), p.y()}});
      break;
    }
    case dota::network::UseAbilityCommand::kUnit: {
      unit->issue_order(OrderCastTarget{idx, cmd.unit()});
      break;
    }
    default:
      unit->issue_order(OrderCastNoTarget{idx});
      break;
  }
}

void GameSession::handle_stop_command(uint32_t player_id) {
  uint32_t unit_id = get_player_unit(player_id);
  if (unit_id == 0) return;

  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  unit->issue_order(OrderStop{});
}

void GameSession::generate_snapshot(dota::network::S2C_Snapshot* snapshot) {
  snapshot->set_tick(tick_count_);

  // 序列化所有单位
  auto radiant_units = world_->units_on_team(dota::Team::Radiant);
  auto dire_units = world_->units_on_team(dota::Team::Dire);

  for (dota::Unit* unit : radiant_units) {
    auto* entity = snapshot->add_entities();
    serialize_entity(unit, entity);
  }

  for (dota::Unit* unit : dire_units) {
    auto* entity = snapshot->add_entities();
    serialize_entity(unit, entity);
  }

  std::cout << "[GameSession] Generated snapshot tick=" << tick_count_
            << " entities=" << snapshot->entities_size() << "\n";
}

void GameSession::generate_delta_snapshot(dota::network::S2C_DeltaSnapshot* delta, uint32_t base_tick) {
  delta->set_base_tick(base_tick);
  delta->set_tick(tick_count_);

  // 生成当前完整快照
  std::map<uint32_t, dota::network::EntityState> current_snapshot;

  auto radiant_units = world_->units_on_team(dota::Team::Radiant);
  auto dire_units = world_->units_on_team(dota::Team::Dire);

  for (dota::Unit* unit : radiant_units) {
    dota::network::EntityState state;
    serialize_entity(unit, &state);
    current_snapshot[unit->id()] = state;
  }

  for (dota::Unit* unit : dire_units) {
    dota::network::EntityState state;
    serialize_entity(unit, &state);
    current_snapshot[unit->id()] = state;
  }

  // 比较当前状态和上次快照
  // 1. 发送新增或变化的实体
  for (const auto& [id, state] : current_snapshot) {
    auto it = last_snapshot_.find(id);
    if (it == last_snapshot_.end()) {
      // 新实体
      *delta->add_updated_entities() = state;
    } else {
      // 检查是否有变化 (简化: 比较序列化后的字符串)
      std::string old_data, new_data;
      it->second.SerializeToString(&old_data);
      state.SerializeToString(&new_data);

      if (old_data != new_data) {
        // 实体有变化
        *delta->add_updated_entities() = state;
      }
    }
  }

  // 2. 发送删除的实体 ID
  for (const auto& [id, state] : last_snapshot_) {
    if (current_snapshot.find(id) == current_snapshot.end()) {
      delta->add_removed_entities(id);
    }
  }

  // 更新上次快照
  last_snapshot_ = std::move(current_snapshot);

  // std::cout << "[GameSession] Generated delta snapshot base=" << base_tick
  //           << " tick=" << tick_count_
  //           << " updated=" << delta->updated_entities_size()
  //           << " removed=" << delta->removed_entities_size() << "\n";
}

uint32_t GameSession::create_player_unit(const std::string& player_name, const Vec2& spawn_pos) {
  // 创建玩家单位统计数据
  dota::UnitStats stats;
  stats.max_health = 1000.0;
  stats.max_mana = 500.0;
  stats.move_speed = 350.0;
  stats.attack_damage = 50.0;
  stats.attack_speed = 100.0;
  stats.attack_range = 150.0;

  // 生成单位
  dota::Unit* unit = world_->spawn(player_name, dota::Team::Radiant, stats, spawn_pos);

  return unit->id();
}

void GameSession::serialize_entity(const dota::Unit* unit, dota::network::EntityState* state) {
  state->set_id(unit->id());
  state->set_type(dota::network::ENTITY_PLAYER);

  auto* pos = state->mutable_position();
  pos->set_x(unit->position().x);
  pos->set_y(unit->position().y);

  auto* vel = state->mutable_velocity();
  vel->set_x(0.0);
  vel->set_y(0.0);

  state->set_rotation(0.0f);

  state->set_health(static_cast<float>(unit->health()));
  state->set_max_health(static_cast<float>(unit->max_health()));

  state->set_team(unit->team() == dota::Team::Radiant
    ? dota::network::TEAM_GOOD
    : dota::network::TEAM_BAD);
}

} // namespace dota::server
