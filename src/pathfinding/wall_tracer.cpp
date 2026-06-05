#include "dota/pathfinding/wall_tracer.hpp"

#include "dota/pathfinding/movement_config.hpp"
#include "dota/pathfinding/nav_grid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace dota::pathfinding {

namespace {

constexpr double kPi = 3.14159265358979323846;

double path_length(const std::vector<Vec2>& path) {
    double len = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const double dx = path[i].x - path[i - 1].x;
        const double dy = path[i].y - path[i - 1].y;
        len += std::sqrt(dx * dx + dy * dy);
    }
    return len;
}

// 两条路径优劣: result 越小越好 (Reached=0 < Partial=1 < Blocked=2), 同 result 看长度
bool is_better(PathResult ra, const std::vector<Vec2>& pa,
               PathResult rb, const std::vector<Vec2>& pb) {
    if (static_cast<int>(ra) != static_cast<int>(rb))
        return static_cast<int>(ra) < static_cast<int>(rb);
    return path_length(pa) < path_length(pb);
}

struct TraceState {
    Vec2 pos;
    bool tracing{false};
    double blocked_angle{0.0};
    double trace_dir{1.0};         // +1=CCW, -1=CW
    double traced_dist{0.0};
    double min_trace_dist{0.0};
    Vec2   m_line_start{};
    double m_line_entry_dist{0.0};
    std::vector<Vec2> path;        // 含起点
    std::optional<PathResult> result;
    double cost{0.0};

    TraceState(Vec2 start, double dir, double min_trace) {
        pos = start;
        trace_dir = dir;
        min_trace_dist = min_trace;
        path.push_back(start);
    }
};

// M-line crossing: 距离 m-line < threshold 且距目标比进入时近
bool m_line_crossing(Vec2 p, Vec2 m_start, Vec2 m_end, double m_entry_dist,
                     double waypoint_threshold) {
    double mx = m_end.x - m_start.x;
    double my = m_end.y - m_start.y;
    const double m_len = std::sqrt(mx * mx + my * my);
    if (m_len < 1e-9) return true;
    mx /= m_len;
    my /= m_len;
    const double dx = p.x - m_start.x;
    const double dy = p.y - m_start.y;
    const double cross = dx * my - dy * mx;
    const double dist_to_line = std::abs(cross);
    const double dist_to_target = std::sqrt(
        (p.x - m_end.x) * (p.x - m_end.x) +
        (p.y - m_end.y) * (p.y - m_end.y));
    return dist_to_line < waypoint_threshold &&
           dist_to_target < m_entry_dist - waypoint_threshold;
}

} // namespace

WallTracer::WallTracer(double unit_radius, double step_angle_deg, double waypoint_threshold)
    : unit_radius_(unit_radius)
    , step_angle_(step_angle_deg * kPi / 180.0)
    , waypoint_threshold_(waypoint_threshold)
    , step_dist_(std::max(unit_radius * 0.5, 1e-3)) {}

namespace {

// 推进一步 Bug2 模拟. 完成时设置 s.result.
void simulate_step(const NavGrid& grid,
                   const std::vector<DynamicCircle>& dynamics,
                   EntityId ignore_id,
                   std::uint32_t query_group,
                   double unit_radius, double step_angle, double step_dist,
                   double waypoint_threshold, double cell_size,
                   Vec2 end, TraceState& s) {
    if (s.result.has_value()) return;

    if (!s.tracing) {
        // --- 直线向目标推进 ---
        const double dx = end.x - s.pos.x;
        const double dy = end.y - s.pos.y;
        const double dist = std::sqrt(dx * dx + dy * dy);
        if (dist < waypoint_threshold) {
            s.path.push_back(end);
            s.cost += dist;
            s.result = PathResult::Reached;
            return;
        }
        Vec2 dir{dx / dist, dy / dist};
        const auto hit = shape_cast_circle(grid, dynamics, ignore_id,
                                           s.pos, dir, dist, unit_radius, query_group);
        if (!hit.hit) {
            s.path.push_back(end);
            s.cost += dist;
            s.result = PathResult::Reached;
            return;
        }

        // 碰撞前进到接触点稍前
        const double safe = std::max(0.0, hit.toi - 0.01 * cell_size);
        s.pos = {s.pos.x + dir.x * safe, s.pos.y + dir.y * safe};
        s.cost += safe;
        s.path.push_back(s.pos);

        // 进入墙壁追踪
        s.blocked_angle = std::atan2(dir.y, dir.x);
        s.traced_dist = 0.0;
        s.min_trace_dist = unit_radius * MovementConfig::min_trace_dist_multiplier;
        s.m_line_start = s.pos;
        s.m_line_entry_dist = std::sqrt((end.x - s.pos.x) * (end.x - s.pos.x) +
                                        (end.y - s.pos.y) * (end.y - s.pos.y));
        s.tracing = true;
        return;
    }

    // --- 沿墙壁旋转一步 ---
    const int max_steps = static_cast<int>(std::ceil(2.0 * kPi / step_angle));
    int best_step = -1;
    double best_dx = 0.0, best_dy = 0.0;
    for (int i = 0; i <= max_steps; ++i) {
        const double angle = s.blocked_angle + s.trace_dir * i * step_angle;
        const double cdx = std::cos(angle);
        const double cdy = std::sin(angle);
        const auto hit = shape_cast_circle(grid, dynamics, ignore_id,
                                           s.pos, {cdx, cdy}, step_dist,
                                           unit_radius, query_group);
        if (!hit.hit) {
            best_dx = cdx;
            best_dy = cdy;
            best_step = i;
            break;
        }
    }
    if (best_step < 0) {
        s.result = PathResult::Blocked;
        return;
    }

    s.pos = {s.pos.x + best_dx * step_dist, s.pos.y + best_dy * step_dist};
    s.traced_dist += step_dist;
    s.cost += step_dist;

    const double clear_angle = s.blocked_angle + s.trace_dir * best_step * step_angle;
    s.blocked_angle = clear_angle - s.trace_dir * step_angle;

    // 从上一 waypoint 直射当前位置不通 -> 必须记录拐点
    {
        const Vec2 last_wp = s.path.back();
        const double wp_dx = s.pos.x - last_wp.x;
        const double wp_dy = s.pos.y - last_wp.y;
        const double wp_dist = std::sqrt(wp_dx * wp_dx + wp_dy * wp_dy);
        if (wp_dist >= 1e-3 * cell_size) {
            const double wp_dir_x = wp_dx / wp_dist;
            const double wp_dir_y = wp_dy / wp_dist;
            const auto wp_hit = shape_cast_circle(grid, dynamics, ignore_id,
                                                  last_wp, {wp_dir_x, wp_dir_y},
                                                  wp_dist, unit_radius, query_group);
            if (wp_hit.hit) {
                // 记录上一步的位置作为拐点
                Vec2 prev{s.pos.x - best_dx * step_dist, s.pos.y - best_dy * step_dist};
                s.path.push_back(prev);
            }
        }
    }

    // M-line crossing 检测
    if (s.traced_dist > s.min_trace_dist) {
        if (m_line_crossing(s.pos, s.m_line_start, end, s.m_line_entry_dist,
                            waypoint_threshold)) {
            const double twx = end.x - s.pos.x;
            const double twy = end.y - s.pos.y;
            const double tw_dist = std::sqrt(twx * twx + twy * twy);
            if (tw_dist > 1e-3 * cell_size) {
                const Vec2 tw_dir{twx / tw_dist, twy / tw_dist};
                const double check = std::min(unit_radius * 4.0, tw_dist);
                const auto tw_hit = shape_cast_circle(grid, dynamics, ignore_id,
                                                     s.pos, tw_dir, check,
                                                     unit_radius, query_group);
                if (tw_hit.hit && tw_hit.toi < 0.02 * cell_size) {
                    // 距离虽近但前方仍被堵, 推迟退出, 抬高 min_trace_dist
                    s.min_trace_dist = s.traced_dist +
                                       unit_radius * MovementConfig::min_trace_dist_multiplier;
                    return;
                }
            }
            s.path.push_back(s.pos);
            s.tracing = false;
        }
    }

    // 死循环检测
    if (s.traced_dist > MovementConfig::max_trace_distance * cell_size) {
        s.result = PathResult::Partial;
    }
}

} // namespace

PathResult WallTracer::find_path(const NavGrid& grid,
                                  const std::vector<DynamicCircle>& dynamics,
                                  EntityId ignore_id,
                                  Vec2 start, Vec2 end,
                                  std::vector<Vec2>& out_path,
                                  std::uint32_t query_group) {
    last_trace_dir_ = 0.0;
    debug_raw_path_.clear();
    out_path.clear();

    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double total = std::sqrt(dx * dx + dy * dy);
    if (total < waypoint_threshold_) {
        out_path.push_back(end);
        return PathResult::Reached;
    }

    TraceState states[2] = {
        TraceState(start,  1.0, unit_radius_ * MovementConfig::min_trace_dist_multiplier),
        TraceState(start, -1.0, unit_radius_ * MovementConfig::min_trace_dist_multiplier),
    };

    int best_idx = -1;
    PathResult best_result = PathResult::Blocked;
    double best_cost = std::numeric_limits<double>::infinity();
    const int max_iter = MovementConfig::wall_trace_max_iter;

    for (int iter = 0; iter < max_iter; ++iter) {
        // 选 cost 较小的未完成方向
        int pick = -1;
        for (int i = 0; i < 2; ++i) {
            if (states[i].result.has_value()) continue;
            if (states[i].cost >= best_cost) {
                states[i].result = PathResult::Partial;
                continue;
            }
            if (pick < 0 || states[i].cost < states[pick].cost) pick = i;
        }
        if (pick < 0) break;

        simulate_step(grid, dynamics, ignore_id, query_group,
                      unit_radius_, step_angle_, step_dist_, waypoint_threshold_,
                      grid.cell_size(), end, states[pick]);

        if (!states[pick].result.has_value()) continue;

        // 完成: 终点保证 + 长度比较
        TraceState& st = states[pick];
        if (st.result.value() == PathResult::Reached) {
            const double dxe = end.x - st.path.back().x;
            const double dye = end.y - st.path.back().y;
            if (st.path.size() <= 1 ||
                std::sqrt(dxe * dxe + dye * dye) > waypoint_threshold_) {
                st.path.push_back(end);
            }
        }
        const double cost = path_length(st.path);
        if (best_idx < 0 || is_better(st.result.value(), st.path, best_result, states[best_idx].path)) {
            best_idx = pick;
            best_result = st.result.value();
            best_cost = cost;
        }
    }

    if (best_idx < 0) {
        // 双方都没完成 -> 选 cost 较小的作 Partial
        best_idx = (states[0].cost <= states[1].cost) ? 0 : 1;
        best_result = PathResult::Partial;
    }

    last_trace_dir_ = states[best_idx].trace_dir;
    debug_raw_path_ = states[best_idx].path;

    // 输出 path 跳过起点
    const auto& src = states[best_idx].path;
    out_path.reserve(src.size() > 0 ? src.size() - 1 : 0);
    for (std::size_t i = 1; i < src.size(); ++i) out_path.push_back(src[i]);

    return best_result;
}

void WallTracer::simplify(const NavGrid& grid,
                           const std::vector<DynamicCircle>& dynamics,
                           EntityId ignore_id,
                           std::vector<Vec2>& path,
                           std::uint32_t query_group) {
    if (path.size() < 3) return;
    std::vector<Vec2> out;
    out.push_back(path.front());
    std::size_t cur = 0;
    while (cur < path.size() - 1) {
        std::size_t furthest = cur + 1;
        for (std::size_t i = path.size() - 1; i > cur + 1; --i) {
            const double dx = path[i].x - path[cur].x;
            const double dy = path[i].y - path[cur].y;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < MovementConfig::arrival_epsilon * grid.cell_size()) continue;
            const Vec2 dir{dx / dist, dy / dist};
            const auto hit = shape_cast_circle(grid, dynamics, ignore_id,
                                               path[cur], dir, dist,
                                               unit_radius_, query_group);
            if (!hit.hit) {
                furthest = i;
                break;
            }
        }
        out.push_back(path[furthest]);
        cur = furthest;
    }
    path = std::move(out);
}

} // namespace dota::pathfinding
