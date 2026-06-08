// src/mode/survivor_mode.cpp
// 生存模式实现 - Stage 4

#include "server/mode/survivor_mode.hpp"
#include "server/server/game_session.hpp"
#include "dota/core/world.hpp"
#include "dota/core/unit.hpp"
#include "dota/ability/registry.hpp"
#include "dota/ability/ability.hpp"
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

  // 处理复活
  tick_respawns();

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

    // 计算经验和金币奖励
    uint32_t exp_value = 10 + current_wave_ * 2;
    uint32_t gold_value = 5 + current_wave_;

    // 给击杀者经验 (简化: 假设 killer_id 就是玩家 ID)
    if (killer_id != 0) {
      exp_system_->add_experience(killer_id, exp_value);
      std::cout << "[SurvivorMode] Player " << killer_id << " gained " << exp_value
                << " exp from killing enemy " << victim_id << "\n";
    }

    // 生成拾取物 (当前只是占位)
    spawn_experience_orb(victim->position(), exp_value);

    // 30% 概率掉落金币
    if ((rand() % 100) < 30) {
      spawn_gold_coin(victim->position(), gold_value);
    }

    std::cout << "[SurvivorMode] Enemy " << victim_id << " killed by " << killer_id << "\n";
  }
  // 如果是玩家被击杀
  else if (victim->team() == dota::Team::Radiant) {
    std::cout << "[SurvivorMode] Player unit " << victim_id << " died!\n";

    // 设置 5 秒后复活
    constexpr float kRespawnDelay = 5.0f;
    pending_respawns_.push_back({victim_id, game_time_ + kRespawnDelay});

    std::cout << "[SurvivorMode] Player unit " << victim_id
              << " will respawn in " << kRespawnDelay << "s\n";
  }
}

void SurvivorGameMode::on_unit_level_up(uint32_t unit_id, uint32_t new_level) {
  std::cout << "[SurvivorMode] Unit " << unit_id << " reached level " << new_level << "\n";

  // 发送技能选择请求
  request_skill_choices(unit_id);
}

void SurvivorGameMode::request_skill_choices(uint32_t player_id) {
  auto choices = skill_pool_->generate_skill_choices(player_id, 3);

  // 保存待选列表
  pending_choices_[player_id] = choices;

  uint32_t current_level = exp_system_->get_level(player_id);

  // 构造并发送 S2C_LevelUp 消息
  if (send_to_player_) {
    dota::network::Packet packet;
    auto* level_up = packet.mutable_level_up();
    level_up->set_new_level(current_level);

    for (size_t i = 0; i < choices.size(); ++i) {
      auto* option = level_up->add_options();
      option->set_skill_id(static_cast<uint32_t>(i));
      option->set_skill_name(choices[i]);
      option->set_description("Skill: " + choices[i]);
      option->set_current_level(skill_pool_->get_skill_level(player_id, choices[i]));
      option->set_max_level(5);
    }

    send_to_player_(player_id, packet);
  }

  std::cout << "[SurvivorMode] Sent skill choices to player " << player_id << ": ";
  for (const auto& skill_id : choices) {
    std::cout << skill_id << " ";
  }
  std::cout << "\n";
}

void SurvivorGameMode::choose_skill(uint32_t player_id, const std::string& skill_id) {
  if (!skill_pool_->choose_skill(player_id, skill_id)) {
    std::cout << "[SurvivorMode] Player " << player_id << " failed to choose " << skill_id << "\n";
    return;
  }

  // 获取玩家单位
  uint32_t unit_id = session_->get_player_unit(player_id);
  dota::Unit* unit = world_->find(unit_id);
  if (!unit) {
    std::cout << "[SurvivorMode] Player " << player_id << " unit not found\n";
    return;
  }

  // 加载并实例化技能
  // 注意: AbilityRegistry 需要有技能定义, 这里简化处理
  // 在实际游戏中应该在初始化时预加载所有技能定义
  dota::AbilityRegistry registry;

  try {
    // 尝试实例化技能 (会自动添加到 unit 的 AbilityManager)
    dota::Ability* ability = registry.instantiate(skill_id, *unit);
    if (ability) {
      std::cout << "[SurvivorMode] Player " << player_id << " learned " << skill_id
                << " (level " << skill_pool_->get_skill_level(player_id, skill_id) << ")\n";
    } else {
      std::cout << "[SurvivorMode] Failed to instantiate ability " << skill_id
                << " (not registered)\n";
    }
  } catch (const std::exception& e) {
    std::cout << "[SurvivorMode] Error instantiating ability " << skill_id
              << ": " << e.what() << "\n";
  }
}

float SurvivorGameMode::wave_time_remaining() const {
  return wave_spawner_->time_until_next_spawn();
}

bool SurvivorGameMode::is_wave_active() const {
  return wave_spawner_->is_wave_active();
}

void SurvivorGameMode::spawn_experience_orb(const Vec2& position, uint32_t exp_value) {
  // 经验已在击杀时直接结算给击杀者, 此处为占位
  (void)position;
  (void)exp_value;
}

void SurvivorGameMode::spawn_gold_coin(const Vec2& position, uint32_t gold_value) {
  // 当前占位实现: 经验已在击杀时直接结算, 金币系统待实现
  (void)position;
  (void)gold_value;
}

void SurvivorGameMode::choose_skill_by_index(uint32_t player_id, uint32_t index) {
  auto it = pending_choices_.find(player_id);
  if (it == pending_choices_.end() || index >= it->second.size()) {
    std::cout << "[SurvivorMode] Player " << player_id
              << " invalid skill index " << index << "\n";
    return;
  }

  const std::string& skill_id = it->second[index];
  choose_skill(player_id, skill_id);
  pending_choices_.erase(it);
}

const std::vector<std::string>& SurvivorGameMode::get_pending_choices(uint32_t player_id) const {
  static const std::vector<std::string> empty;
  auto it = pending_choices_.find(player_id);
  return (it != pending_choices_.end()) ? it->second : empty;
}

void SurvivorGameMode::tick_respawns() {
  for (auto it = pending_respawns_.begin(); it != pending_respawns_.end(); ) {
    if (game_time_ >= it->respawn_time) {
      dota::Unit* unit = world_->find(it->unit_id);
      if (unit) {
        unit->set_health(unit->max_health());
        // 重置到地图中心出生点
        unit->set_position(Vec2{1600.0f, 1600.0f});
        std::cout << "[SurvivorMode] Unit " << it->unit_id << " respawned\n";
      }
      it = pending_respawns_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace dota::server
