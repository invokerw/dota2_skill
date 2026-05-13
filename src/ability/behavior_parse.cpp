// Behavior / target-team string parsing used by the YAML loader.
// Keeping it separate from behavior.hpp so the header stays header-only-ish
// and pulls in no <string> ops beyond the declarations.

#include "dota/ability/behavior.hpp"

#include <algorithm>
#include <string_view>

namespace dota {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

std::uint32_t parse_one(std::string_view tok) {
    // Normalise: Uppercase, strip any "BEHAVIOR_" / "DOTA_ABILITY_BEHAVIOR_" prefix.
    std::string norm(tok);
    std::transform(norm.begin(), norm.end(), norm.begin(),
                   [](char c) { return static_cast<char>(std::toupper(c)); });
    constexpr std::string_view prefixes[] = {"DOTA_ABILITY_BEHAVIOR_", "BEHAVIOR_"};
    for (auto p : prefixes) {
        if (norm.starts_with(p)) {
            norm.erase(0, p.size());
            break;
        }
    }

    if (norm == "NO_TARGET" || norm == "NOTARGET")           return to_mask(BehaviorFlag::NoTarget);
    if (norm == "UNIT_TARGET" || norm == "UNITTARGET")       return to_mask(BehaviorFlag::UnitTarget);
    if (norm == "POINT" || norm == "POINT_TARGET")           return to_mask(BehaviorFlag::PointTarget);
    if (norm == "PASSIVE")                                   return to_mask(BehaviorFlag::Passive);
    if (norm == "CHANNELLED" || norm == "CHANNELED")         return to_mask(BehaviorFlag::Channelled);
    if (norm == "AOE")                                       return to_mask(BehaviorFlag::AoE);
    if (norm == "NOT_LEARNABLE")                             return to_mask(BehaviorFlag::NotLearnable);
    if (norm == "IGNORE_SILENCE")                            return to_mask(BehaviorFlag::IgnoreSilence);
    if (norm == "IGNORE_MAGIC_IMMUNE")                       return to_mask(BehaviorFlag::IgnoreMagicImmune);
    return 0;
}

} // namespace

std::uint32_t parse_behavior_flags(const std::string& csv) {
    std::uint32_t mask = 0;
    std::string_view v{csv};
    while (!v.empty()) {
        auto pos = v.find_first_of(",|");
        auto tok = trim(pos == std::string_view::npos ? v : v.substr(0, pos));
        if (!tok.empty()) mask |= parse_one(tok);
        if (pos == std::string_view::npos) break;
        v.remove_prefix(pos + 1);
    }
    return mask;
}

TargetTeam parse_target_team(const std::string& s) {
    std::string norm = s;
    std::transform(norm.begin(), norm.end(), norm.begin(),
                   [](char c) { return static_cast<char>(std::toupper(c)); });
    constexpr std::string_view prefix = "DOTA_UNIT_TARGET_TEAM_";
    if (norm.starts_with(prefix)) norm.erase(0, prefix.size());
    if (norm == "ENEMY")    return TargetTeam::Enemy;
    if (norm == "FRIENDLY") return TargetTeam::Friendly;
    if (norm == "BOTH")     return TargetTeam::Both;
    return TargetTeam::None;
}

} // namespace dota
