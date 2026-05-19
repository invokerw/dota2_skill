#pragma once

#include "dota/core/types.hpp"
#include "dota/modifier/modifier.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace dota::replay {

// 录像中的一个单位视图. 由 unit_spawn 创建, damage / heal / unit_died 维护其 HP / 存活;
// 每帧 positions 数组刷新 position.
struct UnitView {
    EntityId    id{kInvalidEntityId};
    std::string name;
    Team        team{Team::Neutral};
    double      max_hp{0.0};
    double      max_mana{0.0};
    double      hp{0.0};
    Vec2        position{};
    bool        alive{true};
    std::vector<std::string> modifiers;
    std::string casting_ability;       // 当前正在 cast 的技能名 (空 = 没有)
    double      cast_started_t{0.0};   // 开始 cast 的世界时间
};

// 录像中的一个投射物视图. 录像不存每 tick 投射物位置, 因此 Player 用
// origin + dir * speed * elapsed 外推 (linear), 或 snap 到目标 (tracking).
struct ProjectileView {
    EntityId pid{kInvalidEntityId};
    EntityId source{kInvalidEntityId};
    Vec2     origin{};
    Vec2     dir{};
    double   speed{0.0};
    double   length{0.0};
    double   width{0.0};
    bool     tracking{false};
    EntityId target{kInvalidEntityId};
    double   spawn_t{0.0};
    Vec2     last_pos{};   // 最近一次解算的位置
};

// 一次性的事件回放, 供 UI 推飘字用. 调用方在每帧绘制后清空.
struct DamageRecord {
    EntityId   src;
    EntityId   dst;
    DamageType type;
    double     applied;
    double     t;
};
struct HealRecord {
    EntityId src;
    EntityId dst;
    double   amount;
    double   t;
};
struct DeathRecord {
    EntityId id;
    EntityId killer;
    double   t;
};

// 把 JSONL 录像解析后逐帧驱动出 UnitView / ProjectileView 状态. 不依赖引擎,
// duel_visual 的 --replay 模式直接读这些视图绘制.
class Player {
public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // 解析整个 JSONL 流到内存 (文件通常 <1 MB, 不需要流式).
    // 返回 false 表示 header 缺失或版本不支持.
    bool load(std::istream& in);
    bool load_file(const std::string& path);

    int tick_rate() const { return tick_rate_; }
    const std::string& scenario() const { return scenario_; }
    double duration() const;       // 最后一帧的 t (秒)
    std::size_t frame_count() const { return frames_.size(); }

    // 推进 dt 秒, 应用本段时间内跨过的所有 frame.
    void advance(double dt);
    // 重置回 t=0 状态后再 advance(t).
    void seek(double t);
    double time() const { return time_; }
    bool   finished() const { return current_frame_ >= frames_.size(); }

    const std::unordered_map<EntityId, UnitView>&       units() const { return units_; }
    const std::unordered_map<EntityId, ProjectileView>& projectiles() const { return projectiles_; }

    // 飘字事件缓冲. 调用方读完应自行 clear.
    std::vector<DamageRecord>& damages() { return damage_buffer_; }
    std::vector<HealRecord>&   heals()   { return heal_buffer_;   }
    std::vector<DeathRecord>&  deaths()  { return death_buffer_;  }

private:
    struct Frame {
        double                                       t{0.0};
        std::uint64_t                                tick{0};
        std::vector<std::tuple<EntityId, double, double>> positions;
        std::vector<std::string>                     events;  // 每条事件原始 JSON
    };

    void reset_state();
    void apply_frame(const Frame& f);
    void apply_event(const std::string& json, double t);
    void recompute_projectile_positions();

    int                                          tick_rate_{30};
    std::string                                  scenario_;
    std::vector<Frame>                           frames_;
    std::size_t                                  current_frame_{0};
    double                                       time_{0.0};

    std::unordered_map<EntityId, UnitView>       units_;
    std::unordered_map<EntityId, ProjectileView> projectiles_;

    std::vector<DamageRecord>                    damage_buffer_;
    std::vector<HealRecord>                      heal_buffer_;
    std::vector<DeathRecord>                     death_buffer_;
};

} // namespace dota::replay
