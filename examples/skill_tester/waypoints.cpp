#include "waypoints.hpp"

#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include "raylib.h"

#include <cmath>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

namespace dota::skill_tester {

void draw_move_marker(const visual::ViewCamera& cam, Unit* caster) {
    if (!caster) return;
    auto t = caster->move_target();
    if (!t) return;
    const Vector2 ms_pt = cam.to_screen(*t);
    const Color c{120, 230, 120, 200};
    DrawCircleLines(static_cast<int>(ms_pt.x),
                    static_cast<int>(ms_pt.y), 8.0f, c);
    DrawLineEx({ms_pt.x - 6, ms_pt.y}, {ms_pt.x + 6, ms_pt.y}, 1.5f, c);
    DrawLineEx({ms_pt.x, ms_pt.y - 6}, {ms_pt.x, ms_pt.y + 6}, 1.5f, c);
}

void draw_order_waypoints(const visual::ViewCamera& cam, Unit* caster, World* world) {
    if (!caster || !world) return;
    if (caster->orders().size() <= 1) return;

    const Vec2 caster_pos = caster->position();
    std::vector<Vec2> waypoints;
    waypoints.reserve(caster->orders().size());
    for (const Order& o : caster->orders()) {
        std::optional<Vec2> wp;
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, OrderMoveToPoint>) {
                wp = v.point;
            } else if constexpr (std::is_same_v<T, OrderCastPoint>) {
                wp = v.point;
            } else if constexpr (std::is_same_v<T, OrderCastTarget>) {
                if (Unit* u = world->find(v.target)) wp = u->position();
            } else if constexpr (std::is_same_v<T, OrderAttackTarget>) {
                if (Unit* u = world->find(v.target)) wp = u->position();
            } else if constexpr (std::is_same_v<T, OrderMoveToUnit>) {
                if (Unit* u = world->find(v.target)) wp = u->position();
            }
            // OrderCastNoTarget / OrderStop: 没有空间位置, 跳过.
        }, o);
        if (wp) waypoints.push_back(*wp);
    }
    if (waypoints.empty()) return;

    const Color line_c{200, 200, 120, 200};
    Vec2 prev = caster_pos;
    for (std::size_t i = 0; i < waypoints.size(); ++i) {
        const Vec2 cur = waypoints[i];
        const Vector2 a = cam.to_screen(prev);
        const Vector2 b = cam.to_screen(cur);
        // 虚线: 沿 a->b 切成 ~10px 段, 隔段画.
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len = std::sqrt(dx*dx + dy*dy);
        if (len > 1e-3f) {
            const float dash = 8.0f, gap = 6.0f, step = dash + gap;
            const float ux = dx / len, uy = dy / len;
            for (float s = 0.0f; s < len; s += step) {
                const float e = std::min(s + dash, len);
                DrawLineEx({a.x + ux * s, a.y + uy * s},
                           {a.x + ux * e, a.y + uy * e}, 1.5f, line_c);
            }
        }
        const Vector2 lbl = cam.to_screen(cur);
        DrawText(TextFormat("%zu", i + 1),
                 static_cast<int>(lbl.x) + 10,
                 static_cast<int>(lbl.y) - 10,
                 14, line_c);
        prev = cur;
    }
}

} // namespace dota::skill_tester
