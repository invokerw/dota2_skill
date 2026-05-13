#pragma once

#include <cstdint>
#include <string>

namespace dota {

// Bitmask mirroring Valve's DOTA_ABILITY_BEHAVIOR_* enum. Not all flags from
// Dota are meaningful here yet; we add the ones the cast pipeline actually
// consults in Stages 3–6.
enum class BehaviorFlag : std::uint32_t {
    None            = 0,
    NoTarget        = 1u << 0,
    UnitTarget      = 1u << 1,
    PointTarget     = 1u << 2,
    Passive         = 1u << 3,
    Channelled      = 1u << 4,
    AoE             = 1u << 5,
    NotLearnable    = 1u << 6,
    IgnoreSilence   = 1u << 7,   // items etc.
    IgnoreMagicImmune = 1u << 8,
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

// Targeting metadata. Dota splits this across multiple KV fields; we collapse
// them here to just what Stage 3 needs.
enum class TargetTeam : std::uint8_t {
    None    = 0,
    Enemy,
    Friendly,
    Both,
};

// Parse helpers used by the YAML loader.
std::uint32_t parse_behavior_flags(const std::string& csv);
TargetTeam    parse_target_team(const std::string& s);

} // namespace dota
