// src/mode/survivor_mode.cpp
// 生存模式实现 - Stage 4

#include "server/mode/survivor_mode.hpp"
#include "server/server/game_session.hpp"
#include "dota/core/world.hpp"
#include "dota/core/unit.hpp"
#include <iostream>

namespace dota::server {

SurvivorGameMode::SurvivorGameMode(GameSession* session)
    : session_(session), world_(session->world()) {}

void SurvivorGameMode::initialize() {
  if (initialized_) return;

  // 创建子系统
  wave_spawner_ = std::make_unique<WaveSpawner>(world_);
  exp_system_ = std::make_unique<ExperienceSystem>();
  skill_pool_ = std::make_unique<SkillPool>();

  // 初始化技能池
  skill_pool_->initialize();

  // 设置升级回调
  exp_system_->set_level_up_callback([this](uint32_t player_id, uint32_t new_level) {
    on_unit_level_up(player_id, new_level);
  });

  // 订阅 World 事件
  world_->events().subscribe<UnitDiedEvent>([this](const UnitDiedEvent& event) {
    on_unit_killed(event.victim, event.killer);
  });

  initialized_ = true;
  std::cout << "[SurvivorMode] Initialized\n";

  // 开始第一波
  start_first_wave();
}

void SurvivorGameMode::start_first_wave() {
  current_wave_ = 1;
  wave_spawner_->start_wave(current_wave_);
}

void SurvivorGameMode::tick(float dt) {
  if (!initialized_) return;

  game_time_ += dt;

  // Tick 刷怪系统
  wave_spawner_->tick(dt);

  // 检查是否需要开始下一波
  if (!wave_spawner_->is_wave_active() && current_wave_ > 0) {
    // 间隔 5 秒后开始下一波
    static float wave_cooldown = 5.0f;
    wave_cooldown -= dt;

    if (wave_cooldown <= 0.0f) {
      current_wave_++;
      wave_spawner_->start_wave(current_wave_);
      wave_cooldown = 5.0f;
    }
  }
}

void SurvivorGameMode::on_player_joined(uint32_t player_id, uint32_t unit_id) {
  std::cout << "[SurvivorMode] Player " << player_id
            << " joined (unit=" << unit_id << ")\n";

  // 初始化玩家经验
  exp_system_->add_experience(player_id, 0);
}

void SurvivorGameMode::on_player_left(uint32_t player_id) {
  std::cout << "[SurvivorMode] Player " << player_id << " left\n";
}

void SurvivorGameMode::on_unit_killed(uint32_t victim_id, uint32_t killer_id) {
  dota::Unit* victim = world_->find(victim_id);
  if (!victim) return;

  // 如果是敌人被击杀
  if (victim->team() == dota::Team::Dire) {
    wave_spawner_->on_enemy_killed(victim_id);

    // 掉落经验球
    uint32_t exp_value = 10 + current_wave_ * 2;
    spawn_experience_orb(victim->position(), exp_value);

    // 有概率掉落金币
    if ((rand() % 100) < 30) {  // 30% 概率
      uint32_t gold_value = 5 + current_wave_;
      spawn_gold_coin(victim->position(), gold_value);
    }

    std::cout << "[SurvivorMode] Enemy " << victim_id << " killed by " << killer_id << "\n";
  }
  // 如果是玩家被击杀
  else if (victim->team() == dota::Team::Radiant) {
    std::cout << "[SurvivorMode] Player unit " << victim_id << " died!\n";
    // TODO: 处理玩家死亡
  }
}

void SurvivorGameMode::on_unit_level_up(uint32_t unit_id, uint32_t new_level) {
  std::cout << "[SurvivorMode] Unit " << unit_id << " reached level " << new_level << "\n";

  // 发送技能选择请求
  request_skill_choices(unit_id);
}

void SurvivorGameMode::request_skill_choices(uint32_t player_id) {
  auto choices = skill_pool_->generate_skill_choices(player_id, 3);

  // TODO: 发送技能选择消息给客户端
  std::cout << "[SurvivorMode] Sending skill choices to player " << player_id << ": ";
  for (const auto& skill_id : choices) {
    std::cout << skill_id << " ";
  }
  std::cout << "\n";
}

void SurvivorGameMode::choose_skill(uint32_t player_id, const std::string& skill_id) {
  if (skill_pool_->choose_skill(player_id, skill_id)) {
    // TODO: 将技能添加到玩家单位
    std::cout << "[SurvivorMode] Player " << player_id << " successfully chose " << skill_id << "\n";
  }
}

float SurvivorGameMode::wave_time_remaining() const {
  return wave_spawner_->time_until_next_spawn();
}

bool SurvivorGameMode::is_wave_active() const {
  return wave_spawner_->is_wave_active();
}

void SurvivorGameMode::spawn_experience_orb(const Vec2& position, uint32_t exp_value) {
  // TODO: 创建经验球拾取物
  std::cout << "[SurvivorMode] Spawned exp orb (" << exp_value
            << " exp) at (" << position.x << ", " << position.y << ")\n";
}

void SurvivorGameMode::spawn_gold_coin(const Vec2& position, uint32_t gold_value) {
  // TODO: 创建金币拾取物
  std::cout << "[SurvivorMode] Spawned gold coin (" << gold_value
            << " gold) at (" << position.x << ", " << position.y << ")\n";
}

} // namespace dota::server
