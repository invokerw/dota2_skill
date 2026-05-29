// pathfinding_demo: 可视化 NavGrid + WallTracer + tick_movement 协作.
//
// 控制:
//   左键: 选中一个单位 (圆心 hull 范围内). 已有选中再左键 = 不切换, 仅生效在空地时.
//   右键: 给当前选中单位 issue_move 到鼠标点; Shift+右键 = 队尾追加.
//   B + 左键: 在网格上添加 8x8 cell 矩形障碍 (静态 blocked).
//   C + 左键: 在鼠标位置添加半径 60 的圆形障碍.
//   N: 切换是否绘制网格 cell 阻挡可视化.
//   P: 切换是否绘制 A* rough 路径 (绿色) / WallTracer smooth 路径 (白色).
//   T: 切换是否绘制每个单位最近 60 帧的运动轨迹 (橙色).
//   R: 重置场景 (重新生成两组单位, 清空所有障碍).
//   SPACE: 暂停 / 恢复.
//   ESC: 退出.

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace dota;
using namespace dota::pathfinding;

namespace {

constexpr int    kWindowW = 1280;
constexpr int    kWindowH = 720;
constexpr double kMapHalfW = 600.0;
constexpr double kMapHalfH = 400.0;
constexpr double kCellSize = 16.0;

struct ViewCamera {
    int   window_w{kWindowW};
    int   window_h{kWindowH};
    float origin_x{kWindowW * 0.5f};
    float origin_y{kWindowH * 0.5f};
    float zoom{0.9f};

    Vector2 to_screen(Vec2 w) const {
        return {origin_x + static_cast<float>(w.x) * zoom,
                origin_y + static_cast<float>(w.y) * zoom};
    }
    Vec2 to_world(Vector2 s) const {
        return {(s.x - origin_x) / zoom, (s.y - origin_y) / zoom};
    }
    float scalar(double v) const { return static_cast<float>(v) * zoom; }
};

UnitStats make_stats(double speed = 280.0) {
    UnitStats s;
    s.max_health = 1000.0;
    s.move_speed = speed;
    s.hull_radius = 22.0;
    return s;
}

Color team_color(Team t, bool selected) {
    if (selected) return Color{255, 230, 80, 255};
    switch (t) {
        case Team::Radiant: return Color{86, 171, 47, 255};
        case Team::Dire:    return Color{220, 80, 80, 255};
        default:            return Color{170, 170, 170, 255};
    }
}

struct Trail {
    std::deque<Vec2> points;
    void push(Vec2 p, std::size_t cap = 60) {
        if (!points.empty()) {
            const Vec2 last = points.back();
            const double dx = p.x - last.x, dy = p.y - last.y;
            if (dx * dx + dy * dy < 1.0) return;
        }
        points.push_back(p);
        while (points.size() > cap) points.pop_front();
    }
};

void rebuild_scene(World& w,
                   std::shared_ptr<NavGrid>& grid,
                   std::vector<Unit*>& units) {
    units.clear();

    grid = std::make_shared<NavGrid>(-kMapHalfW, -kMapHalfH,
                                      kMapHalfW * 2.0, kMapHalfH * 2.0,
                                      kCellSize);
    // 中央一个圆障碍 + 一个矩形 blocked 区域, 让默认场景就能演示绕行.
    grid->add_circle_obstacle({0.0, 0.0}, 70.0);
    grid->add_circle_obstacle({-200.0, 120.0}, 50.0);
    grid->add_circle_obstacle({ 220.0, -140.0}, 60.0);
    w.set_nav_grid(grid);

    // 左侧 4 个 Radiant, 右侧 4 个 Dire, 命令各自交叉到对面.
    const std::array<Vec2, 4> left  = {Vec2{-500.0, -150.0}, {-500.0, -50.0},
                                        {-500.0,   50.0}, {-500.0, 150.0}};
    const std::array<Vec2, 4> right = {Vec2{ 500.0, -150.0}, { 500.0, -50.0},
                                        { 500.0,   50.0}, { 500.0, 150.0}};
    for (const auto& p : left) {
        Unit* u = w.spawn("R", Team::Radiant, make_stats(), p);
        units.push_back(u);
    }
    for (const auto& p : right) {
        Unit* u = w.spawn("D", Team::Dire, make_stats(), p);
        units.push_back(u);
    }
    // 默认让所有单位走对面起点, 用来观察相向避让.
    for (std::size_t i = 0; i < 4; ++i) {
        units[i]->issue_move(right[i]);
        units[i + 4]->issue_move(left[i]);
    }
}

void draw_grid_overlay(const ViewCamera& cam, const NavGrid& g, bool show_cells) {
    // 边界
    const Vector2 a = cam.to_screen({g.origin_x(), g.origin_y()});
    const Vector2 b = cam.to_screen({g.origin_x() + g.width()  * g.cell_size(),
                                     g.origin_y() + g.height() * g.cell_size()});
    DrawRectangleLines(static_cast<int>(a.x), static_cast<int>(a.y),
                       static_cast<int>(b.x - a.x),
                       static_cast<int>(b.y - a.y),
                       Color{60, 80, 100, 220});

    if (!show_cells) return;
    const Color blocked{200, 80, 80, 90};
    for (int gy = 0; gy < g.height(); ++gy) {
        for (int gx = 0; gx < g.width(); ++gx) {
            if (!g.is_blocked(gx, gy)) continue;
            double wx, wy;
            g.grid_to_world(gx, gy, wx, wy);
            const double half = g.cell_size() * 0.5;
            const Vector2 p0 = cam.to_screen({wx - half, wy - half});
            const Vector2 p1 = cam.to_screen({wx + half, wy + half});
            DrawRectangle(static_cast<int>(p0.x), static_cast<int>(p0.y),
                          static_cast<int>(p1.x - p0.x),
                          static_cast<int>(p1.y - p0.y),
                          blocked);
        }
    }
}

void draw_circle_obstacles(const ViewCamera& cam, const NavGrid& g) {
    for (const auto& c : g.circles()) {
        const Vector2 sc = cam.to_screen(c.center);
        DrawCircleV(sc, cam.scalar(c.radius), Color{150, 60, 60, 130});
        DrawCircleLines(static_cast<int>(sc.x), static_cast<int>(sc.y),
                        cam.scalar(c.radius), Color{255, 120, 120, 230});
    }
}

void draw_paths(const ViewCamera& cam, const Unit* u) {
    const auto& ms = u->move_state();
    if (!ms.active) return;

    // rough = A* 输出 (绿色虚线)
    if (ms.rough.size() >= 2) {
        for (std::size_t i = 0; i + 1 < ms.rough.size(); ++i) {
            const Vector2 a = cam.to_screen(ms.rough[i]);
            const Vector2 b = cam.to_screen(ms.rough[i + 1]);
            DrawLineEx(a, b, 1.5f, Color{120, 220, 120, 200});
        }
        for (const auto& p : ms.rough) {
            const Vector2 s = cam.to_screen(p);
            DrawCircleV(s, 3.0f, Color{120, 220, 120, 230});
        }
    }
    // smooth = WallTracer 当前段输出 (白色)
    if (ms.smooth.size() >= 2) {
        for (std::size_t i = 0; i + 1 < ms.smooth.size(); ++i) {
            const Vector2 a = cam.to_screen(ms.smooth[i]);
            const Vector2 b = cam.to_screen(ms.smooth[i + 1]);
            DrawLineEx(a, b, 2.0f, Color{240, 240, 240, 230});
        }
    }
    // 终点 marker
    const Vector2 dst = cam.to_screen(ms.destination);
    DrawCircleLines(static_cast<int>(dst.x), static_cast<int>(dst.y),
                    8.0f, Color{255, 220, 80, 255});
    DrawCircleLines(static_cast<int>(dst.x), static_cast<int>(dst.y),
                    11.0f, Color{255, 220, 80, 160});
}

void draw_trail(const ViewCamera& cam, const Trail& tr) {
    if (tr.points.size() < 2) return;
    for (std::size_t i = 0; i + 1 < tr.points.size(); ++i) {
        const Vector2 a = cam.to_screen(tr.points[i]);
        const Vector2 b = cam.to_screen(tr.points[i + 1]);
        const float t = static_cast<float>(i) /
                        static_cast<float>(tr.points.size() - 1);
        const unsigned char alpha = static_cast<unsigned char>(80 + t * 140);
        DrawLineEx(a, b, 1.5f, Color{255, 160, 60, alpha});
    }
}

void draw_unit(const ViewCamera& cam, const Unit* u, bool selected) {
    const Vector2 c = cam.to_screen(u->position());
    const float r_px = std::max(8.0f, cam.scalar(u->hull_radius()));
    const Color body = team_color(u->team(), selected);
    DrawCircleV(c, r_px, body);
    DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                    r_px, BLACK);
    if (selected) {
        DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y),
                        r_px + 5.0f, Color{255, 230, 80, 220});
    }
}

Unit* pick_unit(const std::vector<Unit*>& units, Vec2 wp) {
    for (Unit* u : units) {
        if (!u || !u->alive()) continue;
        const double dx = u->position().x - wp.x;
        const double dy = u->position().y - wp.y;
        const double r = u->hull_radius();
        if (dx * dx + dy * dy <= r * r) return u;
    }
    return nullptr;
}

void add_rect_block(NavGrid& g, Vec2 wp, int half_cells = 4) {
    int cx, cy;
    g.world_to_grid(wp.x, wp.y, cx, cy);
    for (int dy = -half_cells; dy <= half_cells; ++dy) {
        for (int dx = -half_cells; dx <= half_cells; ++dx) {
            const int gx = cx + dx, gy = cy + dy;
            if (gx < 0 || gy < 0 || gx >= g.width() || gy >= g.height()) continue;
            g.add_blocked(gx, gy);
        }
    }
}

} // namespace

int main() {
    InitWindow(kWindowW, kWindowH, "dota2_skill -- pathfinding demo");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    World world;
    std::shared_ptr<NavGrid> grid;
    std::vector<Unit*> units;
    rebuild_scene(world, grid, units);

    std::unordered_map<EntityId, Trail> trails;
    ViewCamera cam;

    Unit* selected = units.empty() ? nullptr : units.front();
    EntityId selected_id = selected ? selected->id() : kInvalidEntityId;

    bool paused      = false;
    bool show_cells  = true;
    bool show_paths  = true;
    bool show_trail  = true;
    bool quit        = false;

    while (!quit && !WindowShouldClose()) {
        // --- 输入 ---
        if (IsKeyPressed(KEY_ESCAPE)) quit = true;
        if (IsKeyPressed(KEY_SPACE))  paused = !paused;
        if (IsKeyPressed(KEY_N))      show_cells = !show_cells;
        if (IsKeyPressed(KEY_P))      show_paths = !show_paths;
        if (IsKeyPressed(KEY_T))      show_trail = !show_trail;
        if (IsKeyPressed(KEY_R)) {
            trails.clear();
            // 重新构造 World 太重 -- 这里直接销毁所有 unit + 重建 scene.
            // 简单起见, 复用同一个 World, 让 spawn 继续累加. 主要演示场景不需要
            // 严格回到初始 EntityId.
            for (Unit* u : units) {
                if (u) u->set_health(0.0);
            }
            rebuild_scene(world, grid, units);
            selected = units.empty() ? nullptr : units.front();
            selected_id = selected ? selected->id() : kInvalidEntityId;
        }

        const Vector2 mouse = GetMousePosition();
        const Vec2 mouse_w = cam.to_world(mouse);
        const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        const bool b_held = IsKeyDown(KEY_B);
        const bool c_held = IsKeyDown(KEY_C);

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (b_held) {
                add_rect_block(*grid, mouse_w);
            } else if (c_held) {
                grid->add_circle_obstacle(mouse_w, 60.0);
            } else {
                if (Unit* hit = pick_unit(units, mouse_w)) {
                    selected = hit;
                    selected_id = hit->id();
                }
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && selected && selected->alive()) {
            selected->issue_order(OrderMoveToPoint{mouse_w}, shift);
        }

        // --- 模拟 ---
        const double dt = paused ? 0.0 : std::min(0.05, static_cast<double>(GetFrameTime()));
        if (dt > 0.0) {
            world.advance(dt);
            for (Unit* u : units) {
                if (u && u->alive()) trails[u->id()].push(u->position());
            }
        }

        // 选中可能死亡 / 被回收: 保险刷新.
        if (selected && !selected->alive()) {
            selected = nullptr;
            for (Unit* u : units) {
                if (u && u->id() == selected_id && u->alive()) { selected = u; break; }
            }
        }

        // --- 渲染 ---
        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        draw_grid_overlay(cam, *grid, show_cells);
        draw_circle_obstacles(cam, *grid);

        if (show_trail) {
            for (Unit* u : units) {
                if (!u) continue;
                auto it = trails.find(u->id());
                if (it != trails.end()) draw_trail(cam, it->second);
            }
        }
        if (show_paths) {
            for (Unit* u : units) {
                if (u && u->alive()) draw_paths(cam, u);
            }
        }
        for (Unit* u : units) {
            if (!u) continue;
            draw_unit(cam, u, u == selected);
        }

        // HUD
        const char* status = paused ? "PAUSED" : "RUN";
        DrawText(TextFormat(
            "pathfinding_demo  [%s]  t=%.2fs  units=%d  cells=%dx%d  cs=%.1f",
            status, world.time(), static_cast<int>(units.size()),
            grid->width(), grid->height(), grid->cell_size()),
            12, 10, 18, RAYWHITE);
        DrawText("LMB pick / RMB move (Shift queue) / B+LMB block / C+LMB circle / "
                 "N cells / P paths / T trail / R reset / SPACE pause / ESC quit",
                 12, kWindowH - 26, 14, Color{180, 200, 220, 230});
        if (selected) {
            const auto& ms = selected->move_state();
            DrawText(TextFormat(
                "selected #%llu pos=(%.0f, %.0f) active=%d rough=%zu/%zu "
                "smooth=%zu/%zu seg_block=%d seg_miss=%d block_wait=%d",
                static_cast<unsigned long long>(selected->id()),
                selected->position().x, selected->position().y,
                ms.active ? 1 : 0,
                ms.rough_index, ms.rough.size(),
                ms.smooth_index, ms.smooth.size(),
                ms.seg_block, ms.seg_miss, ms.block_wait),
                12, 32, 14, Color{255, 230, 80, 230});
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
