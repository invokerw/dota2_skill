#pragma once

// 战斗日志 (combat log): 订阅 World 上的可视化事件, 把"谁对谁造成伤害",
// "谁释放/打断技能", "谁获得/失去 modifier", "死亡" 等等记录成结构化条目,
// 供 skill_tester / 录像 UI / 任何外部观察者使用.
//
// 设计要点:
//   - 完全旁观, 不影响游戏逻辑(与 Recorder 同级).
//   - 环形缓冲, 上限 capacity 后丢弃最旧 (避免长跑内存膨胀).
//   - 不直接持单位指针, 仅持 EntityId. 单位名称在事件发布时立刻 snapshot,
//     这样事后展示历史也不会因为单位被销毁而丢名字.
//   - 不存格式化字符串, 让 UI 端按需要再 format.

#include "dota/core/event_bus.hpp"
#include "dota/core/types.hpp"
#include "dota/modifier/modifier.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace dota {

class World;

enum class CombatLogKind : std::uint8_t {
    Damage,                // source 对 target 造成伤害
    Heal,                  // source 对 target 治疗
    ModifierAdded,         // target 获得 modifier
    ModifierRemoved,       // target 失去 modifier
    AbilityCastStarted,    // source 开始施法
    AbilityCastFinished,   // source 完成 / 被打断施法
    AttackLanded,          // 普攻命中 (含 missed)
    UnitDied,              // target 死亡 (source = killer)
};

struct CombatLogEntry {
    double         time{0.0};       // World::time() 在事件发布时的快照
    std::uint64_t  tick{0};         // 当前 tick 序号
    CombatLogKind  kind{CombatLogKind::Damage};

    EntityId    source{kInvalidEntityId};   // attacker / caster / healer / killer
    EntityId    target{kInvalidEntityId};   // victim / target / unit
    std::string source_name;                // 发布时 snapshot, 防止单位被销毁后丢名字
    std::string target_name;

    std::string name;          // ability name / modifier name
    double      amount{0.0};   // damage applied / heal amount / modifier duration
    double      amount_pre{0.0}; // damage 进入管线时(放大后, resistance 前)的值
    DamageType  dtype{DamageType::Physical};
    bool        flag{false};   // missed (Attack) / interrupted (Cast) / permanent (ModifierAdded)
    int         stacks{0};
};

class CombatLog {
public:
    using Filter = std::function<bool(const CombatLogEntry&)>;

    // 订阅 world.events() 上的所有可视化事件. capacity = 缓冲条目上限.
    explicit CombatLog(World& world, std::size_t capacity = 2000);
    ~CombatLog();

    CombatLog(const CombatLog&) = delete;
    CombatLog& operator=(const CombatLog&) = delete;

    const std::deque<CombatLogEntry>& entries() const { return ring_; }
    std::size_t size() const { return ring_.size(); }
    std::size_t capacity() const { return capacity_; }
    void clear() { ring_.clear(); }

    // 可选过滤器. 返回 false 的条目会被丢弃, 不写入环形缓冲.
    void set_filter(Filter f) { filter_ = std::move(f); }

    // 把一条 entry 渲染成人类可读的一行文本 (dota 客户端风格).
    static std::string format(const CombatLogEntry& e);

private:
    void push(CombatLogEntry e);

    World&                       world_;
    std::deque<CombatLogEntry>   ring_;
    std::size_t                  capacity_;
    Filter                       filter_;
    std::vector<EventBus::Token> tokens_;
};

} // namespace dota
