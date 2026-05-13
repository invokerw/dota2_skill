#pragma once

#include <cstdint>
#include <compare>

namespace dota {

using EntityId = std::uint32_t;
inline constexpr EntityId kInvalidEntityId = 0;

enum class Team : std::uint8_t {
    Neutral = 0,
    Radiant = 1,
    Dire    = 2,
};

struct Vec2 {
    double x{0.0};
    double y{0.0};

    friend auto operator<=>(const Vec2&, const Vec2&) = default;
};

inline double distance_sq(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace dota
