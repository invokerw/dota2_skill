#pragma once

// skill_tester 的渲染辅助 (header-only). 提供 RenderUnit / RenderProjectile /
// FloatingText 中间表示, 以及对应的 raylib 绘制函数和 ViewCamera 视图变换.

#include "dota/core/types.hpp"
#include "dota/core/world.hpp"

#include "raylib.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace dota::visual {

// 渲染端单位圆的最小像素半径. 真实尺寸 = max(hull_radius * zoom, kMinUnitRadiusPx).
inline constexpr float kMinUnitRadiusPx = 10.0f;

struct RenderUnit {
    EntityId    id{kInvalidEntityId};
    std::string name;
    Team        team{Team::Neutral};
    bool        alive{true};
    double      hp{0.0};
    double      max_hp{0.0};
    Vec2        position{};
    double      hull_radius{24.0};
    std::vector<std::string> modifiers;
    std::string casting_ability;
    float       cast_progress{-1.0f};   // 0..1; <0 = 未知
};

struct RenderProjectile {
    EntityId pid{kInvalidEntityId};
    Vec2     pos{};
    Vec2     dir{};
    double   width{0.0};
    bool     linear{true};
};

struct FloatingText {
    Vec2        world_pos{};
    std::string text;
    Color       color{255, 255, 255, 255};
    double      spawn_time{0.0};
};

// 视图: 世界坐标 -> 屏幕像素. 默认按窗口 1280x720 居中, 调用方可改 zoom / center.
struct ViewCamera {
    int   window_w{1280};
    int   window_h{720};
    float origin_x{640.0f};
    float origin_y{360.0f};
    float zoom{0.45f};
    Vec2  center{0.0, 0.0};

    Vector2 to_screen(Vec2 w) const {
        return {
            origin_x + static_cast<float>((w.x - center.x) * zoom),
            origin_y + static_cast<float>((w.y - center.y) * zoom),
        };
    }
    Vec2 to_world(Vector2 s) const {
        return {
            center.x + (s.x - origin_x) / zoom,
            center.y + (s.y - origin_y) / zoom,
        };
    }
    float scalar(double s) const { return static_cast<float>(s * zoom); }
};

inline Color team_color(Team t, bool alive) {
    if (!alive) return Color{90, 90, 90, 255};
    switch (t) {
        case Team::Radiant: return Color{86, 171, 47, 255};
        case Team::Dire:    return Color{200, 60, 60, 255};
        default:            return Color{160, 160, 160, 255};
    }
}

inline void draw_unit(const ViewCamera& cam, const RenderUnit& u) {
    const Vector2 c = cam.to_screen(u.position);
    const Color body = team_color(u.team, u.alive);
    const float r_px = std::max(kMinUnitRadiusPx, cam.scalar(u.hull_radius));
    DrawCircleV(c, r_px, body);
    DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                    r_px, BLACK);

    const float bar_w = 60.0f;
    const float bar_h = 6.0f;
    const float bar_y = c.y - r_px - 14.0f;
    const float bar_x = c.x - bar_w * 0.5f;
    const float frac  = u.alive ? static_cast<float>(u.hp / u.max_hp) : 0.0f;
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w), static_cast<int>(bar_h),
                  Color{40, 40, 40, 230});
    DrawRectangle(static_cast<int>(bar_x), static_cast<int>(bar_y),
                  static_cast<int>(bar_w * std::clamp(frac, 0.0f, 1.0f)),
                  static_cast<int>(bar_h), Color{80, 220, 80, 255});

    const int name_w = MeasureText(u.name.c_str(), 14);
    DrawText(u.name.c_str(),
             static_cast<int>(c.x) - name_w / 2,
             static_cast<int>(bar_y) - 16, 14, RAYWHITE);

    const float mod_y = c.y + r_px + 6.0f;
    float mod_x = c.x - (u.modifiers.size() * 12.0f) * 0.5f;
    for (auto& name : u.modifiers) {
        const bool dbg =
            name.find("_slow")    != std::string::npos ||
            name.find("_stun")    != std::string::npos ||
            name.find("_hex")     != std::string::npos ||
            name.find("_silence") != std::string::npos ||
            name.find("_drag")    != std::string::npos ||
            name.find("_dot")     != std::string::npos ||
            name.find("burning")  != std::string::npos;
        Color mc = dbg ? Color{220, 80, 80, 255} : Color{80, 180, 220, 255};
        DrawRectangle(static_cast<int>(mod_x), static_cast<int>(mod_y),
                      10, 10, mc);
        DrawRectangleLines(static_cast<int>(mod_x), static_cast<int>(mod_y),
                           10, 10, BLACK);
        mod_x += 12.0f;
    }

    if (!u.casting_ability.empty()) {
        const float cb_w = 70.0f;
        const float cb_h = 5.0f;
        const float cb_y = c.y + r_px + 22.0f;
        const float cb_x = c.x - cb_w * 0.5f;
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w), static_cast<int>(cb_h),
                      Color{20, 20, 20, 220});
        const float p = u.cast_progress >= 0.0f ? u.cast_progress : 0.5f;
        DrawRectangle(static_cast<int>(cb_x), static_cast<int>(cb_y),
                      static_cast<int>(cb_w * p), static_cast<int>(cb_h),
                      Color{255, 220, 80, 255});
    }
}

inline void draw_projectile(const ViewCamera& cam, const RenderProjectile& p) {
    const Vector2 c = cam.to_screen(p.pos);
    if (p.linear) {
        const double tail_world = 60.0;
        const Vec2 tail{p.pos.x - p.dir.x * tail_world,
                        p.pos.y - p.dir.y * tail_world};
        const Vector2 t = cam.to_screen(tail);
        const float thickness = std::max(2.0f, cam.scalar(p.width * 0.3));
        DrawLineEx(t, c, thickness, Color{255, 200, 60, 220});
        DrawCircleV(c, std::max(4.0f, thickness * 0.7f),
                    Color{255, 240, 120, 255});
    } else {
        DrawCircleV(c, 8.0f, Color{255, 160, 60, 255});
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                        8.0f, BLACK);
    }
}

inline void draw_floating_text(const ViewCamera& cam, const FloatingText& f, double now) {
    const double age = now - f.spawn_time;
    const float life = 1.2f;
    const float t = static_cast<float>(age / life);
    const Vector2 base = cam.to_screen(f.world_pos);
    const float dy = -50.0f * t;
    const unsigned char alpha = static_cast<unsigned char>(
        std::max(0.0f, 255.0f * (1.0f - t)));
    Color c = f.color; c.a = alpha;
    const int w = MeasureText(f.text.c_str(), 18);
    DrawText(f.text.c_str(),
             static_cast<int>(base.x) - w / 2,
             static_cast<int>(base.y) - kMinUnitRadiusPx - 30 + static_cast<int>(dy),
             18, c);
}

// 在 view 范围内画浅网格 (背景).
inline void draw_grid(const ViewCamera& cam,
                      double world_x_min, double world_x_max,
                      double world_y_min, double world_y_max,
                      double step = 200.0) {
    const Color line{32, 38, 46, 255};
    for (double x = world_x_min; x <= world_x_max; x += step) {
        const Vector2 a = cam.to_screen({x, world_y_min});
        const Vector2 b = cam.to_screen({x, world_y_max});
        DrawLineV(a, b, line);
    }
    for (double y = world_y_min; y <= world_y_max; y += step) {
        const Vector2 a = cam.to_screen({world_x_min, y});
        const Vector2 b = cam.to_screen({world_x_max, y});
        DrawLineV(a, b, line);
    }
}

} // namespace dota::visual
