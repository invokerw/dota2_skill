// src/mode/survivor_mode.cpp
// 生存模式实现

#include "server/mode/survivor_mode.hpp"
#include "server/server/game_session.hpp"
#include "dota/core/world.hpp"
#include "dota/core/unit.hpp"
#include "dota/ability/registry.hpp"
#include "dota/ability/ability.hpp"
#include "dota/script/lua_state.hpp"
#include <iostream>

namespace dota::server {

SurvivorGameMode::SurvivorGameMode(GameSession* session, const std::string& data_dir)
    : session_(session), world_(session->world()), data_dir_(data_dir) {}

SurvivorGameMode::~SurvivorGameMode() = default;

void SurvivorGameMode::initialize() {
  if (initialized_) return;

  // 初始化 Lua 和 AbilityRegistry
  lua_state_ = std::make_unique<dota::LuaState>();
  registry_ = std::make_unique<dota::AbilityRegistry>();
  registry_->set_lua(lua_state_.get());

  // 加载所有 ability 定义
  std::string abilities_dir = data_dir_ + "/abilities";
  std::size_t loaded = registry_->load_dir(abilities_dir);
  std::cout << "[SurvivorMode] Loaded " << loaded << " ability definitions\n";

  // 创建子系统
  wave_spawner_ = std::make_unique<WaveSpawner>(world_);
  exp_system_ = std::make_unique<ExperienceSystem>();
  skill_pool_ = std::make_unique<SkillPool>();

  // 初始化技能池 (从 abilities 目录读取可选技能名, 过滤被动)
  skill_pool_->initialize(abilities_dir, registry_.get());

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

  // 立即发送初始选技请求
  request_skill_choices(player_id);
}

void SurvivorGameMode::on_player_left(uint32_t player_id) {
  std::cout << "[SurvivorMode] Player " << player_id << " left\n";
}

void SurvivorGameMode::on_unit_killed(uint32_t victim_id, uint32_t killer_id) {
  dota::Unit* victim = world_->find(victim_id);
  if (!victim) return;

  if (victim->team() == dota::Team::Dire) {
    wave_spawner_->on_enemy_killed(victim_id);

    uint32_t exp_value = 10 + current_wave_ * 2;

    if (killer_id != 0) {
      exp_system_->add_experience(killer_id, exp_value);
    }

    spawn_experience_orb(victim->position(), exp_value);

    if ((rand() % 100) < 30) {
      uint32_t gold_value = 5 + current_wave_;
      spawn_gold_coin(victim->position(), gold_value);
    }
  } else if (victim->team() == dota::Team::Radiant) {
    constexpr float kRespawnDelay = 5.0f;
    pending_respawns_.push_back({victim_id, game_time_ + kRespawnDelay});
  }
}

void SurvivorGameMode::on_unit_level_up(uint32_t unit_id, uint32_t new_level) {
  std::cout << "[SurvivorMode] Unit " << unit_id << " reached level " << new_level << "\n";
  request_skill_choices(unit_id);
}

void SurvivorGameMode::request_skill_choices(uint32_t player_id) {
  auto choices = skill_pool_->generate_skill_choices(player_id, 3);
  if (choices.empty()) return;

  pending_choices_[player_id] = choices;

  if (!send_to_player_) return;

  uint32_t current_level = exp_system_->get_level(player_id);

  dota::network::Packet packet;
  auto* level_up = packet.mutable_level_up();
  level_up->set_new_level(current_level);

  for (size_t i = 0; i < choices.size(); ++i) {
    const auto& skill_name = choices[i];
    auto* option = level_up->add_options();
    option->set_skill_id(static_cast<uint32_t>(i));
    option->set_skill_name(skill_name);
    option->set_current_level(skill_pool_->get_skill_level(player_id, skill_name));
    option->set_max_level(SkillPool::kMaxSkillLevel);

    // 从 AbilityDef 读取详细信息
    const auto* def = registry_->find(skill_name);
    if (def) {
      // 构建描述: 冷却 / 蓝耗 / 施法距离
      std::string desc;
      if (!def->cooldowns.empty()) {
        desc += "CD: ";
        for (size_t j = 0; j < def->cooldowns.size(); ++j) {
          if (j > 0) desc += "/";
          desc += std::to_string(static_cast<int>(def->cooldowns[j]));
        }
      }
      if (!def->mana_costs.empty()) {
        if (!desc.empty()) desc += "  ";
        desc += "Mana: ";
        for (size_t j = 0; j < def->mana_costs.size(); ++j) {
          if (j > 0) desc += "/";
          desc += std::to_string(static_cast<int>(def->mana_costs[j]));
        }
      }
      if (def->cast_range > 0) {
        if (!desc.empty()) desc += "  ";
        desc += "Range: " + std::to_string(static_cast<int>(def->cast_range));
      }
      option->set_description(desc);

      // 填充 ability_special 数值
      for (const auto& [key, val] : def->ability_special) {
        auto* sv = option->add_special_values();
        sv->set_key(key);
        if (val.is_int) {
          for (long v : val.ints) sv->add_values(static_cast<float>(v));
        } else {
          for (double v : val.floats) sv->add_values(static_cast<float>(v));
        }
      }
    }
  }

  send_to_player_(player_id, packet);

  std::cout << "[SurvivorMode] Sent skill choices to player " << player_id << ": ";
  for (const auto& s : choices) std::cout << s << " ";
  std::cout << "\n";
}

void SurvivorGameMode::choose_skill(uint32_t player_id, const std::string& skill_id) {
  if (!skill_pool_->choose_skill(player_id, skill_id)) {
    std::cout << "[SurvivorMode] Player " << player_id << " failed to choose " << skill_id << "\n";
    return;
  }

  uint32_t unit_id = session_->get_player_unit(player_id);
  dota::Unit* unit = world_->find(unit_id);
  if (!unit) return;

  uint32_t skill_level = skill_pool_->get_skill_level(player_id, skill_id);

  if (skill_level == 1) {
    // 新技能: 实例化并附加到 unit
    dota::Ability* ability = registry_->instantiate(skill_id, *unit);
    if (ability) {
      std::cout << "[SurvivorMode] Player " << player_id << " learned " << skill_id << "\n";
    } else {
      std::cout << "[SurvivorMode] Failed to instantiate " << skill_id << "\n";
    }
  } else {
    // 已有技能: 升级
    dota::Ability* ab = unit->abilities().find(skill_id);
    if (ab) {
      ab->set_level(static_cast<int>(skill_level));
      std::cout << "[SurvivorMode] Player " << player_id << " upgraded "
                << skill_id << " to level " << skill_level << "\n";
    }
  }

  // 发送确认
  if (send_to_player_) {
    dota::network::Packet packet;
    auto* learned = packet.mutable_skill_learned();
    learned->set_skill_id(0);
    learned->set_new_level(skill_level);
    send_to_player_(player_id, packet);
  }
}

float SurvivorGameMode::wave_time_remaining() const {
  return wave_spawner_->time_until_next_spawn();
}

bool SurvivorGameMode::is_wave_active() const {
  return wave_spawner_->is_wave_active();
}

void SurvivorGameMode::spawn_experience_orb(const Vec2& position, uint32_t exp_value) {
  (void)position;
  (void)exp_value;
}

void SurvivorGameMode::spawn_gold_coin(const Vec2& position, uint32_t gold_value) {
  (void)position;
  (void)gold_value;
}

void SurvivorGameMode::choose_skill_by_index(uint32_t player_id, uint32_t index) {
  auto it = pending_choices_.find(player_id);
  if (it == pending_choices_.end() || index >= it->second.size()) {
    std::cout << "[SurvivorMode] Player " << player_id
              << " invalid skill index " << index
              << " (pending_choices exists=" << (it != pending_choices_.end()) << ")\n";
    return;
  }

  std::string skill_id = it->second[index];
  std::cout << "[SurvivorMode] Player " << player_id
            << " choosing index " << index << " -> '" << skill_id << "'\n";
  pending_choices_.erase(it);
  choose_skill(player_id, skill_id);
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
        unit->set_position(Vec2{1600.0f, 1600.0f});
      }
      it = pending_respawns_.erase(it);
    } else {
      ++it;
    }
  }
}

} // namespace dota::server
