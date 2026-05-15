#pragma once

#include <cstdint>
#include <compare>

namespace dota {

using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidEntityId = 0;

// 队伍枚举
enum class Team : std::uint8_t {
    Neutral = 0,  // 中立
    Radiant = 1,  // 天辉
    Dire    = 2,  // 夜魇
};

// 二维向量
struct Vec2 {
    double x{0.0};
    double y{0.0};

    friend auto operator<=>(const Vec2&, const Vec2&) = default;
};

// 计算两点间距离的平方
inline double distance_sq(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace dota
