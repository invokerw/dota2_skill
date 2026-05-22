#pragma once

#include <cstdint>
#include <string>

namespace dota {

// 对应 Valve 的 DOTA_ABILITY_BEHAVIOR_* 枚举的位掩码. 并非 Dota 中的所有标志
// 在这里都有意义; 我们添加了阶段 3-6 中施法流程实际
// 查询的标志.
enum class BehaviorFlag : std::uint32_t {
    None            = 0,
    NoTarget        = 1u << 0,
    UnitTarget      = 1u << 1,
    PointTarget     = 1u << 2,
    Passive         = 1u << 3,
    Channelled      = 1u << 4,
    AoE             = 1u << 5,
    NotLearnable    = 1u << 6,
    IgnoreSilence   = 1u << 7,   // 物品等
    IgnoreMagicImmune = 1u << 8,
    IgnoreUntargetable = 1u << 9,
    // 法球类技能 (orb): 不主动施法, 通过 intrinsic modifier 在攻击时认领 record
    // 落副作用. 与 Dota DOTA_ABILITY_BEHAVIOR_ATTACK 对应; 与 Passive 共存.
    Attack          = 1u << 10,
    // 法球默认是否在挂载时启用自动施放 (玩家可关). 对应 Dota
    // DOTA_ABILITY_BEHAVIOR_AUTOCAST.
    AutoCast        = 1u << 11,
};

constexpr std::uint32_t to_mask(BehaviorFlag f) {
    return static_cast<std::uint32_t>(f);
}

constexpr std::uint32_t operator|(BehaviorFlag a, BehaviorFlag b) {
    return to_mask(a) | to_mask(b);
}

constexpr bool has_flag(std::uint32_t mask, BehaviorFlag f) {
    return (mask & to_mask(f)) != 0;
}

// 目标元数据. Dota 将其拆分到多个 KV 字段; 我们在这里将其
// 合并为阶段 3 所需的内容.
enum class TargetTeam : std::uint8_t {
    None    = 0,
    Enemy,
    Friendly,
    Both,
};

// YAML 加载器使用的解析辅助函数.
std::uint32_t parse_behavior_flags(const std::string& csv);
TargetTeam    parse_target_team(const std::string& s);

} // namespace dota
