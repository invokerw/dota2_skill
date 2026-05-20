#include "aim.hpp"
#include "app_state.hpp"

#include "dota/ability/ability.hpp"
#include "dota/core/unit.hpp"

#include "raylib.h"

#include <cmath>
#include <limits>

namespace dota::skill_tester {

namespace {

// 解析当前等级下的 special 字段值; 不存在返回 NaN.
double special_or_nan(const Ability& ab, const char* key) {
    const auto& sp = ab.ability_special();
    auto it = sp.find(key);
    if (it == sp.end()) return std::numeric_limits<double>::quiet_NaN();
    return it->second.get_float(ab.level());
}

double preview_aoe_radius(const Ability& ab, double fallback) {
    const double r = special_or_nan(ab, "radius");
    return std::isnan(r) ? fallback : r;
}

struct LinearPreview { double length; double width; };
bool preview_linear(const Ability& ab, LinearPreview& out) {
    const double w = special_or_nan(ab, "width");
    const double l = special_or_nan(ab, "length");
    if (std::isnan(w) || std::isnan(l)) return false;
    out.width = w;
    out.length = l;
    return true;
}

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

} // namespace

void draw_aim_preview(const AppState& app,
                      const visual::ViewCamera& cam,
                      Ability* ab,
                      Unit* caster,
                      Unit* hover_unit,
                      Vec2 mouse_world,
                      bool mouse_in_field) {
    if (app.aim == AimMode::None || !ab || !caster || !caster->alive()) return;

    const Vec2 caster_pos = caster->position();
    const double range = ab->cast_range();
    const Vector2 cs = cam.to_screen(caster_pos);

    if (range > 0.0) {
        DrawCircleLines(static_cast<int>(cs.x), static_cast<int>(cs.y),
                        cam.scalar(range), Color{200, 200, 80, 90});
    }

    if (app.aim == AimMode::AwaitUnitTarget) {
        if (!hover_unit) return;
        const Vector2 us = cam.to_screen(hover_unit->position());
        const double d = std::sqrt(dist2(caster_pos, hover_unit->position()));
        const double effective_range = range + hover_unit->hull_radius();
        const Color ring = (range <= 0.0 || d <= effective_range)
            ? Color{255, 220, 80, 255}
            : Color{220, 80, 80, 255};
        const float r_px = std::max(visual::kMinUnitRadiusPx,
                                    cam.scalar(hover_unit->hull_radius()));
        DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                        r_px + 4.0f, ring);
        DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                        r_px + 6.0f, ring);
        return;
    }

    if (app.aim == AimMode::AwaitPointTarget && mouse_in_field) {
        const Vector2 ts = cam.to_screen(mouse_world);
        const double d = std::sqrt(dist2(caster_pos, mouse_world));
        const Color line_c = (range <= 0.0 || d <= range)
            ? Color{255, 220, 80, 200}
            : Color{220, 80, 80, 200};

        LinearPreview lp;
        if (preview_linear(*ab, lp)) {
            // 线性投射物: 画从 caster 沿瞄准方向, 长度 lp.length, 宽 lp.width
            // 的胶囊 (= 中线 + 两端半圆 + 两侧平行线).
            const double dx = mouse_world.x - caster_pos.x;
            const double dy = mouse_world.y - caster_pos.y;
            const double len = std::sqrt(dx*dx + dy*dy);
            if (len > 1e-3) {
                const double ux = dx / len, uy = dy / len;
                const Vec2 end_w{caster_pos.x + ux * lp.length,
                                 caster_pos.y + uy * lp.length};
                const Vector2 es = cam.to_screen(end_w);
                const float hw = cam.scalar(lp.width * 0.5);
                const float nx = static_cast<float>(-uy) * hw;
                const float ny = static_cast<float>( ux) * hw;
                DrawLineEx({cs.x + nx, cs.y + ny},
                           {es.x + nx, es.y + ny}, 1.5f, line_c);
                DrawLineEx({cs.x - nx, cs.y - ny},
                           {es.x - nx, es.y - ny}, 1.5f, line_c);
                DrawCircleLines(static_cast<int>(cs.x), static_cast<int>(cs.y), hw, line_c);
                DrawCircleLines(static_cast<int>(es.x), static_cast<int>(es.y), hw, line_c);
                DrawLineEx(cs, es, 1.0f, Color{line_c.r, line_c.g, line_c.b, 120});
            }
        } else {
            // AoE 圆形预览
            DrawLineEx(cs, ts, 2.0f, line_c);
            const double size = preview_aoe_radius(*ab, 100.0);
            DrawCircleLines(static_cast<int>(ts.x), static_cast<int>(ts.y),
                            cam.scalar(size), line_c);
            DrawLineEx({ts.x - 8, ts.y}, {ts.x + 8, ts.y}, 2.0f, line_c);
            DrawLineEx({ts.x, ts.y - 8}, {ts.x, ts.y + 8}, 2.0f, line_c);
        }
    }
    // AwaitConfirmNoTarget: 预览只是 cast_range 圆, 已经画过.
}

} // namespace dota::skill_tester
