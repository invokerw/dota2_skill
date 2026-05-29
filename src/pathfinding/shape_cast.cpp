#include "dota/pathfinding/shape_cast.hpp"

#include "dota/pathfinding/nav_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dota::pathfinding {

namespace {

constexpr double kEps = 1e-12;

// 圆 vs 圆 swept-cast: 求最小 t∈[0, max_dist] 使 |p0 + t*dir - c| = r1+r2.
// 返回 -1 表示无碰撞. dir 假设已归一化 (a=1), 公式 |o + t*d|^2 = R^2 展开
// 二次方程 t^2 + 2(o·d)t + (|o|^2 - R^2) = 0, 取最小非负根.
double swept_circle_vs_circle(Vec2 p0, Vec2 dir, double max_dist,
                              double r1, Vec2 c, double r2) {
    const double R = r1 + r2;
    const double ox = p0.x - c.x;
    const double oy = p0.y - c.y;
    const double od = ox * dir.x + oy * dir.y;
    const double oo = ox * ox + oy * oy;
    const double cterm = oo - R * R;
    if (cterm <= 0.0) return 0.0;          // 起点已在内, 立即命中
    if (od >= 0.0) return -1.0;            // 远离目标, 不会撞
    const double disc = od * od - cterm;
    if (disc < 0.0) return -1.0;
    const double t = -od - std::sqrt(disc);
    if (t < 0.0 || t > max_dist) return -1.0;
    return t;
}

// 圆 vs AABB swept-cast (Minkowski 圆角矩形). dir 已归一化, max_dist >= 0.
// 算法: 4 条平行 face 的 slab test + 4 个 corner 圆盘 (限制在角落象限).
// 返回 -1 表示无碰撞.
double swept_circle_vs_aabb(Vec2 p0, Vec2 dir, double max_dist, double r,
                             double min_x, double min_y, double max_x, double max_y) {
    // 已重叠 (圆心到 AABB 距离 <= r) -> 立即命中
    {
        const double cx = std::clamp(p0.x, min_x, max_x);
        const double cy = std::clamp(p0.y, min_y, max_y);
        const double dx = p0.x - cx;
        const double dy = p0.y - cy;
        if (dx * dx + dy * dy <= r * r) return 0.0;
    }

    double best = std::numeric_limits<double>::infinity();

    // --- 4 face slab ---
    // y = min_y - r, x∈[min_x, max_x] (下边 face, dir.y > 0 才命中)
    auto check_h_face = [&](double y_face) {
        if (std::abs(dir.y) < kEps) return;
        const double t = (y_face - p0.y) / dir.y;
        if (t < 0.0 || t > max_dist || t >= best) return;
        const double hx = p0.x + t * dir.x;
        if (hx >= min_x && hx <= max_x) best = t;
    };
    auto check_v_face = [&](double x_face) {
        if (std::abs(dir.x) < kEps) return;
        const double t = (x_face - p0.x) / dir.x;
        if (t < 0.0 || t > max_dist || t >= best) return;
        const double hy = p0.y + t * dir.y;
        if (hy >= min_y && hy <= max_y) best = t;
    };
    check_h_face(min_y - r);
    check_h_face(max_y + r);
    check_v_face(min_x - r);
    check_v_face(max_x + r);

    // --- 4 corner disk, 命中点必须落在角落象限 (避免与 face 双重计) ---
    auto check_corner = [&](double cx, double cy, bool x_low, bool y_low) {
        const double ox = p0.x - cx;
        const double oy = p0.y - cy;
        const double od = ox * dir.x + oy * dir.y;
        const double oo = ox * ox + oy * oy;
        const double cterm = oo - r * r;
        if (cterm <= 0.0) return;          // 已在 disk 内, face slab 兜底处理
        if (od >= 0.0) return;
        const double disc = od * od - cterm;
        if (disc < 0.0) return;
        const double t = -od - std::sqrt(disc);
        if (t < 0.0 || t > max_dist || t >= best) return;
        const double hx = p0.x + t * dir.x;
        const double hy = p0.y + t * dir.y;
        const bool ok_x = x_low ? (hx <= min_x) : (hx >= max_x);
        const bool ok_y = y_low ? (hy <= min_y) : (hy >= max_y);
        if (ok_x && ok_y) best = t;
    };
    check_corner(min_x, min_y, true,  true);
    check_corner(max_x, min_y, false, true);
    check_corner(min_x, max_y, true,  false);
    check_corner(max_x, max_y, false, false);

    return std::isinf(best) ? -1.0 : best;
}

} // namespace

ShapeCastHit shape_cast_circle(
    const NavGrid& grid,
    const std::vector<DynamicCircle>& dynamics,
    EntityId ignore_id,
    Vec2 start, Vec2 dir, double max_dist, double radius,
    std::uint32_t query_group) {
    ShapeCastHit best;
    best.toi = std::numeric_limits<double>::infinity();
    if (max_dist <= 0.0) return ShapeCastHit{};

    // dir 假设已归一化; 仍兜底一次, 避免上层忘了
    double dlen2 = dir.x * dir.x + dir.y * dir.y;
    if (dlen2 < kEps) return ShapeCastHit{};
    if (std::abs(dlen2 - 1.0) > 1e-6) {
        const double inv = 1.0 / std::sqrt(dlen2);
        dir.x *= inv;
        dir.y *= inv;
    }

    // --- 静态: blocked cell ---
    if (query_group & CollisionGroups::Terrain) {
        // 扫掠 bbox + radius padding
        const double end_x = start.x + dir.x * max_dist;
        const double end_y = start.y + dir.y * max_dist;
        const double pad = radius + grid.cell_size();
        const double bbx_min = std::min(start.x, end_x) - pad;
        const double bbx_max = std::max(start.x, end_x) + pad;
        const double bby_min = std::min(start.y, end_y) - pad;
        const double bby_max = std::max(start.y, end_y) + pad;
        int gx_min, gy_min, gx_max, gy_max;
        grid.world_to_grid(bbx_min, bby_min, gx_min, gy_min);
        grid.world_to_grid(bbx_max, bby_max, gx_max, gy_max);
        gx_min = std::max(0, gx_min);
        gy_min = std::max(0, gy_min);
        gx_max = std::min(grid.width() - 1, gx_max);
        gy_max = std::min(grid.height() - 1, gy_max);

        for (int gy = gy_min; gy <= gy_max; ++gy) {
            for (int gx = gx_min; gx <= gx_max; ++gx) {
                if (!grid.is_blocked(gx, gy)) continue;
                const double cell_min_x = grid.origin_x() + gx * grid.cell_size();
                const double cell_min_y = grid.origin_y() + gy * grid.cell_size();
                const double cell_max_x = cell_min_x + grid.cell_size();
                const double cell_max_y = cell_min_y + grid.cell_size();
                const double t = swept_circle_vs_aabb(
                    start, dir, max_dist, radius,
                    cell_min_x, cell_min_y, cell_max_x, cell_max_y);
                if (t >= 0.0 && t < best.toi) {
                    best.toi = t;
                    best.hit = true;
                    best.kind = ShapeCastHit::Kind::Terrain;
                    best.unit = kInvalidEntityId;
                    best.group = CollisionGroups::Terrain;
                }
            }
        }

        // --- 静态: 圆形障碍 ---
        for (const auto& c : grid.circles()) {
            const double t = swept_circle_vs_circle(
                start, dir, max_dist, radius, c.center, c.radius);
            if (t >= 0.0 && t < best.toi) {
                best.toi = t;
                best.hit = true;
                best.kind = ShapeCastHit::Kind::Terrain;
                best.unit = kInvalidEntityId;
                best.group = CollisionGroups::Terrain;
            }
        }
    }

    // --- 动态: dynamics, 按 group 过滤 + ignore_id ---
    for (const auto& d : dynamics) {
        if (d.id == ignore_id && ignore_id != kInvalidEntityId) continue;
        if ((d.group & query_group) == 0) continue;
        const double t = swept_circle_vs_circle(
            start, dir, max_dist, radius, d.center, d.radius);
        if (t >= 0.0 && t < best.toi) {
            best.toi = t;
            best.hit = true;
            best.kind = (d.group & CollisionGroups::Unit) ? ShapeCastHit::Kind::Unit
                                                          : ShapeCastHit::Kind::Terrain;
            best.unit = d.id;
            best.group = d.group;
        }
    }

    if (!best.hit) return ShapeCastHit{};
    return best;
}

} // namespace dota::pathfinding
