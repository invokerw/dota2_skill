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

    // TODO: 完整的玩家死亡处理
    // 可能的选项:
    //   1. 单人游戏: 游戏结束，发送 GameOver 消息
    //   2. 多人游戏: 玩家进入观战模式，波次结束后复活
    //   3. Roguelike 模式: 永久死亡，显示统计数据
    //   4. 复活机制: N 秒后在出生点复活

    // 简化实现: 打印死亡消息，游戏继续
    // 实际应该:
    //   - 发送 S2C_PlayerDeath 消息给所有客户端
    //   - 记录死亡统计
    //   - 根据游戏模式决定是否游戏结束
    //   - 如果允许复活，设置复活定时器

    std::cout << "[SurvivorMode] Player death handling not fully implemented\n";
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
  // 需要通过 GameSession 或 GameServerHandler 发送 S2C_LevelUp 消息
  // 当前架构中 SurvivorGameMode 无法直接访问网络层
  // 解决方案:
  //   1. 在 GameSession 中添加 send_to_player(player_id, message) 方法
  //   2. GameSession 持有 GameServer* 或消息发送回调
  //   3. 或者在 GameServerHandler 中轮询待发送消息队列

  std::cout << "[SurvivorMode] Sending skill choices to player " << player_id << ": ";
  for (const auto& skill_id : choices) {
    std::cout << skill_id << " ";
  }
  std::cout << "\n";

  // 占位: 打印技能选项信息
  uint32_t current_level = exp_system_->get_level(player_id);
  std::cout << "[SurvivorMode] Player " << player_id << " reached level "
            << current_level << ", choose from " << choices.size() << " skills\n";
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
  // TODO: 创建真正的拾取物实体
  // 当前简化实现: 直接给附近的玩家经验
  // 完整实现需要:
  //   1. 添加 Pickup 实体类型到 World
  //   2. 碰撞检测系统检测玩家与拾取物接触
  //   3. 触发拾取事件，应用效果并销毁拾取物
  //   4. 拾取物可能需要飞向玩家的动画

  // 简化版: 使用 find_enemies_in_radius 查找附近玩家 (但它查找敌人)
  // 更简化: 直接在敌人死亡时给击杀者经验，这里只记录日志
  std::cout << "[SurvivorMode] Spawned exp orb (" << exp_value
            << " exp) at (" << position.x << ", " << position.y
            << ") - simplified: exp given to killer directly\n";
}

void SurvivorGameMode::spawn_gold_coin(const Vec2& position, uint32_t gold_value) {
  // TODO: 创建金币拾取物
  // 金币系统需要:
  //   1. 玩家金币属性 (在 PlayerState 或 ExperienceSystem 中)
  //   2. 商店/升级系统来消费金币
  //   3. 与经验球类似的拾取机制

  // 当前占位实现: 仅记录日志
  std::cout << "[SurvivorMode] Spawned gold coin (" << gold_value
            << " gold) at (" << position.x << ", " << position.y
            << ") - gold system not yet implemented\n";
}

} // namespace dota::server
