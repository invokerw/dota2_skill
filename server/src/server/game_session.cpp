// src/server/game_session.cpp
// 游戏会话实现 - Stage 4

#include "server/server/game_session.hpp"
#include "server/mode/survivor_mode.hpp"
#include "dota/core/unit.hpp"
#include <iostream>

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

  // TODO: 从 World 移除单位
  // uint32_t unit_id = it->second;
  // world_->remove_unit(unit_id);

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

  // TODO: 发出移动指令
  // unit->move_to_position(target);

  std::cout << "[GameSession] Player " << player_id << " move to ("
            << target.x << ", " << target.y << ") [not implemented]\n";
}

void GameSession::handle_use_ability_command(uint32_t player_id, const dota::network::UseAbilityCommand& cmd) {
  uint32_t unit_id = get_player_unit(player_id);
  if (unit_id == 0) return;

  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  uint32_t slot = cmd.ability_slot();

  // TODO: 根据目标类型使用技能
  std::cout << "[GameSession] Player " << player_id << " use ability slot=" << slot << " [not implemented]\n";
}

void GameSession::handle_stop_command(uint32_t player_id) {
  uint32_t unit_id = get_player_unit(player_id);
  if (unit_id == 0) return;

  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  // TODO: 停止指令
  std::cout << "[GameSession] Player " << player_id << " stop [not implemented]\n";
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

  // TODO: 实现增量同步
  // 比较当前状态和 last_snapshot_，只发送变化的实体

  std::cout << "[GameSession] Generated delta snapshot base=" << base_tick
            << " tick=" << tick_count_ << "\n";
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

  // 位置 (暂时使用固定位置)
  auto* pos = state->mutable_position();
  pos->set_x(0.0);
  pos->set_y(0.0);

  // 速度
  auto* vel = state->mutable_velocity();
  vel->set_x(0.0);
  vel->set_y(0.0);

  // 旋转
  state->set_rotation(0.0f);

  // 生命值 (暂时使用固定值)
  state->set_health(1000.0f);
  state->set_max_health(1000.0f);

  // 队伍
  state->set_team(unit->team() == dota::Team::Radiant
    ? dota::network::TEAM_GOOD
    : dota::network::TEAM_BAD);

  // TODO: 添加更多状态
  // - 从 Unit 获取实际位置和生命值
  // - 技能冷却
  // - Modifier 列表
}

} // namespace dota::server
