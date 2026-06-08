// include/server/server/game_session.hpp
// 游戏会话 - Stage 3

#pragma once

#include "dota/core/world.hpp"
#include "messages.pb.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <vector>

namespace dota::server {

class SurvivorGameMode;

/**
 * 游戏会话 (房间)
 *
 * 持有 World 实例和游戏逻辑
 * 管理一个游戏实例中的所有玩家
 */
class GameSession {
 public:
  GameSession(uint32_t session_id, const std::string& map_name);
  ~GameSession();  // 需要在 .cpp 中定义，因为包含不完整类型的 unique_ptr

  // 会话 ID
  uint32_t session_id() const { return session_id_; }

  // 玩家管理
  bool add_player(uint32_t player_id, const std::string& player_name);
  void remove_player(uint32_t player_id);
  bool has_player(uint32_t player_id) const;

  // 获取玩家的单位 ID
  uint32_t get_player_unit(uint32_t player_id) const;

  // 游戏逻辑
  void tick(float dt);

  // 输入处理
  void handle_move_command(uint32_t player_id, const dota::network::MoveCommand& cmd);
  void handle_use_ability_command(uint32_t player_id, const dota::network::UseAbilityCommand& cmd);
  void handle_stop_command(uint32_t player_id);

  // 状态快照
  void generate_snapshot(dota::network::S2C_Snapshot* snapshot);
  void generate_delta_snapshot(dota::network::S2C_DeltaSnapshot* delta, uint32_t base_tick);

  // World 访问
  dota::World* world() { return world_.get(); }
  const dota::World* world() const { return world_.get(); }

  // 游戏模式访问
  SurvivorGameMode* game_mode() { return game_mode_.get(); }

  // 消息发送回调 (由 GameServer 设置)
  using SendToPlayerFn = std::function<void(uint32_t player_id, const dota::network::Packet& packet)>;
  void set_send_callback(SendToPlayerFn fn);

  // 会话状态
  uint32_t tick_count() const { return tick_count_; }
  size_t player_count() const { return player_units_.size(); }

 private:
  // 创建玩家单位
  uint32_t create_player_unit(const std::string& player_name, const Vec2& spawn_pos);

  // 序列化实体到快照
  void serialize_entity(const dota::Unit* unit, dota::network::EntityState* state);

  uint32_t session_id_;
  std::unique_ptr<dota::World> world_;
  std::unique_ptr<SurvivorGameMode> game_mode_;

  // 玩家 ID -> 单位 ID 映射
  std::map<uint32_t, uint32_t> player_units_;

  // Tick 计数
  uint32_t tick_count_ = 0;

  // 上次快照状态（用于增量同步）
  std::map<uint32_t, dota::network::EntityState> last_snapshot_;
};

} // namespace dota::server
