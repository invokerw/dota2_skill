#include "dota/pathfinding/nav_grid.hpp"

#include "dota/pathfinding/movement_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>

namespace dota::pathfinding {

namespace {

constexpr double kSqrt2 = 1.41421356237309504880;

// 8 方向邻居偏移. 索引顺序与 _costs 对齐 (cardinal=1, diagonal=sqrt(2)).
constexpr int kDx[8] = {-1,  0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1, -1,-1,  0, 0,  1, 1, 1};
constexpr double kCosts[8] = {kSqrt2, 1.0, kSqrt2, 1.0, 1.0, kSqrt2, 1.0, kSqrt2};

bool point_in_circle(double px, double py, double cx, double cy, double r2) {
    const double dx = px - cx;
    const double dy = py - cy;
    return dx * dx + dy * dy <= r2;
}

} // namespace

NavGrid::NavGrid(double origin_x, double origin_y, double map_width, double map_height,
                 double cell_size)
    : origin_x_(origin_x)
    , origin_y_(origin_y)
    , cell_size_(cell_size > 0.0 ? cell_size : 1.0)
    , width_(static_cast<int>(std::ceil(map_width / (cell_size > 0.0 ? cell_size : 1.0))))
    , height_(static_cast<int>(std::ceil(map_height / (cell_size > 0.0 ? cell_size : 1.0))))
    , blocked_(static_cast<std::size_t>(std::max(width_, 0)) *
               static_cast<std::size_t>(std::max(height_, 0)), 0) {}

bool NavGrid::is_blocked(int gx, int gy) const {
    if (gx < 0 || gx >= width_ || gy < 0 || gy >= height_) return true;
    return blocked_[cell_index(gx, gy)] > 0;
}

void NavGrid::add_blocked(int gx, int gy) {
    if (gx < 0 || gx >= width_ || gy < 0 || gy >= height_) return;
    ++blocked_[cell_index(gx, gy)];
}

void NavGrid::remove_blocked(int gx, int gy) {
    if (gx < 0 || gx >= width_ || gy < 0 || gy >= height_) return;
    int& v = blocked_[cell_index(gx, gy)];
    if (v > 0) --v;
}

std::vector<int> NavGrid::add_circle_obstacle(Vec2 center, double radius) {
    std::vector<int> cells;
    if (radius <= 0.0) return cells;
    circles_.push_back({center, radius});

    const double r2 = radius * radius;
    int min_gx, min_gy, max_gx, max_gy;
    world_to_grid(center.x - radius - cell_size_, center.y - radius - cell_size_, min_gx, min_gy);
    world_to_grid(center.x + radius + cell_size_, center.y + radius + cell_size_, max_gx, max_gy);
    min_gx = std::max(0, min_gx);
    min_gy = std::max(0, min_gy);
    max_gx = std::min(width_ - 1, max_gx);
    max_gy = std::min(height_ - 1, max_gy);

    const double half = cell_size_ * 0.5;
    for (int gy = min_gy; gy <= max_gy; ++gy) {
        for (int gx = min_gx; gx <= max_gx; ++gx) {
            double cx, cy;
            grid_to_world(gx, gy, cx, cy);
            // 四角全在圆内才算 blocked
            if (point_in_circle(cx - half, cy - half, center.x, center.y, r2) &&
                point_in_circle(cx + half, cy - half, center.x, center.y, r2) &&
                point_in_circle(cx - half, cy + half, center.x, center.y, r2) &&
                point_in_circle(cx + half, cy + half, center.x, center.y, r2)) {
                const int idx = cell_index(gx, gy);
                ++blocked_[static_cast<std::size_t>(idx)];
                cells.push_back(idx);
            }
        }
    }
    return cells;
}

void NavGrid::remove_blocked_cells(const std::vector<int>& cells) {
    for (int idx : cells) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= blocked_.size()) continue;
        int& v = blocked_[static_cast<std::size_t>(idx)];
        if (v > 0) --v;
    }
}

void NavGrid::world_to_grid(double wx, double wy, int& gx, int& gy) const {
    gx = static_cast<int>(std::floor((wx - origin_x_) / cell_size_));
    gy = static_cast<int>(std::floor((wy - origin_y_) / cell_size_));
}

void NavGrid::grid_to_world(int gx, int gy, double& wx, double& wy) const {
    wx = origin_x_ + (gx + 0.5) * cell_size_;
    wy = origin_y_ + (gy + 0.5) * cell_size_;
}

bool NavGrid::find_nearest_open(int sx, int sy, int& ox, int& oy) const {
    const int max_r = std::min(MovementConfig::find_nearest_open_max_radius,
                               std::max(width_, height_));
    for (int r = 1; r <= max_r; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                if (std::abs(dx) != r && std::abs(dy) != r) continue;
                const int nx = sx + dx;
                const int ny = sy + dy;
                if (nx < 0 || nx >= width_ || ny < 0 || ny >= height_) continue;
                if (!is_blocked(nx, ny)) {
                    ox = nx;
                    oy = ny;
                    return true;
                }
            }
        }
    }
    return false;
}

bool NavGrid::has_line_of_sight(Vec2 a, Vec2 b) const {
    int x0, y0, x1, y1;
    world_to_grid(a.x, a.y, x0, y0);
    world_to_grid(b.x, b.y, x1, y1);
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        if (is_blocked(x0, y0)) return false;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        const bool move_x = e2 > -dy;
        const bool move_y = e2 < dx;
        if (move_x && move_y) {
            // 对角穿角防穿透
            if (is_blocked(x0 + sx, y0) || is_blocked(x0, y0 + sy)) return false;
        }
        if (move_x) { err -= dy; x0 += sx; }
        if (move_y) { err += dx; y0 += sy; }
    }
    return true;
}

namespace {

double octile_heuristic(int ax, int ay, int bx, int by, double cell) {
    const double dx = std::abs(ax - bx);
    const double dy = std::abs(ay - by);
    return (dx + dy + (kSqrt2 - 2.0) * std::min(dx, dy)) * cell;
}

} // namespace

std::vector<Vec2> NavGrid::find_path(Vec2 start, Vec2 end) {
    int sx, sy, ex, ey;
    world_to_grid(start.x, start.y, sx, sy);
    world_to_grid(end.x, end.y, ex, ey);
    sx = std::clamp(sx, 0, width_ - 1);
    sy = std::clamp(sy, 0, height_ - 1);
    ex = std::clamp(ex, 0, width_ - 1);
    ey = std::clamp(ey, 0, height_ - 1);

    if (is_blocked(sx, sy)) {
        if (!find_nearest_open(sx, sy, sx, sy)) return {};
    }
    if (is_blocked(ex, ey)) {
        if (!find_nearest_open(ex, ey, ex, ey)) return {};
    }

    struct PQNode {
        double f;
        int    key;
        int    x;
        int    y;
        double g;
    };
    struct PQCmp {
        bool operator()(const PQNode& a, const PQNode& b) const {
            if (a.f != b.f) return a.f > b.f;  // min-heap
            return a.key > b.key;
        }
    };
    std::priority_queue<PQNode, std::vector<PQNode>, PQCmp> open;
    std::unordered_map<int, double> g_score;
    std::unordered_map<int, int>    came_from;

    const int start_key = cell_index(sx, sy);
    const int end_key   = cell_index(ex, ey);
    g_score[start_key] = 0.0;
    open.push({octile_heuristic(sx, sy, ex, ey, cell_size_), start_key, sx, sy, 0.0});

    bool found = false;
    while (!open.empty()) {
        const PQNode current = open.top();
        open.pop();
        // lazy deletion
        auto it = g_score.find(current.key);
        if (it == g_score.end() || current.g > it->second) continue;
        if (current.key == end_key) { found = true; break; }

        for (int i = 0; i < 8; ++i) {
            const int nx = current.x + kDx[i];
            const int ny = current.y + kDy[i];
            if (is_blocked(nx, ny)) continue;
            // 防对角穿角
            if (kDx[i] != 0 && kDy[i] != 0) {
                if (is_blocked(current.x + kDx[i], current.y) ||
                    is_blocked(current.x, current.y + kDy[i])) continue;
            }
            const int    nkey = cell_index(nx, ny);
            const double tent = current.g + kCosts[i] * cell_size_;
            auto nit = g_score.find(nkey);
            if (nit == g_score.end() || tent < nit->second) {
                g_score[nkey] = tent;
                came_from[nkey] = current.key;
                open.push({tent + octile_heuristic(nx, ny, ex, ey, cell_size_),
                           nkey, nx, ny, tent});
            }
        }
    }

    if (!found) return {};

    // 重建路径 (cell 中心), reverse 后替换首尾为实际坐标, 最后做 LOS 简化.
    std::vector<Vec2> path;
    int cur = end_key;
    while (cur != start_key) {
        const int gx = cur % width_;
        const int gy = cur / width_;
        double wx, wy;
        grid_to_world(gx, gy, wx, wy);
        path.push_back({wx, wy});
        auto pit = came_from.find(cur);
        if (pit == came_from.end()) break;
        cur = pit->second;
    }
    double swx, swy;
    grid_to_world(sx, sy, swx, swy);
    path.push_back({swx, swy});
    std::reverse(path.begin(), path.end());

    if (!path.empty()) {
        path.front() = start;
        path.back()  = end;
    }

    // line-of-sight 贪心简化
    if (path.size() > 2) {
        std::vector<Vec2> simplified;
        simplified.push_back(path.front());
        std::size_t cur_i = 0;
        while (cur_i < path.size() - 1) {
            std::size_t furthest = cur_i + 1;
            for (std::size_t i = path.size() - 1; i > cur_i + 1; --i) {
                if (has_line_of_sight(path[cur_i], path[i])) {
                    furthest = i;
                    break;
                }
            }
            simplified.push_back(path[furthest]);
            cur_i = furthest;
        }
        path = std::move(simplified);
    }
    return path;
}

} // namespace dota::pathfinding
