#pragma once

#include <cmath>
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

// --- Vec2 数学（投射物、line/cone 查询、motion controller 都依赖这些）---

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(double s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { a.x -= b.x; a.y -= b.y; return a; }

inline double length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

inline Vec2 normalized(Vec2 v) {
    const double L = length(v);
    if (L <= 0.0) return {0.0, 0.0};
    return {v.x / L, v.y / L};
}

inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }

// 二维 z 分量"叉积"，用于侧距/方向判定
inline double cross_z(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

// 计算两点间距离的平方
inline double distance_sq(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline double distance(Vec2 a, Vec2 b) { return std::sqrt(distance_sq(a, b)); }

} // namespace dota
