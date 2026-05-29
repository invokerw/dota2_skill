#pragma once

#include "dota/core/types.hpp"
#include "dota/pathfinding/collision_groups.hpp"
#include "dota/pathfinding/shape_cast.hpp"

#include <cstdint>
#include <vector>

namespace dota::pathfinding {

class NavGrid;

enum class PathResult {
    Reached,  // 成功到达终点
    Partial,  // 死循环 / 超过最大迭代, 返回已探索的部分路径
    Blocked,  // 360° 全堵, 起点出不去
};

// Bug2 双向墙壁追踪.
//
// 给定起点 / 终点 + ShapeCast 上下文 (NavGrid + dynamics + ignore + group),
// 同时启动 CCW (+1) 和 CW (-1) 两个方向的模拟, 交替推进 cost 较小的方向, 命中
// M-line 后退出追踪. 完成时返回较优结果 (Reached < Partial < Blocked, 同 result
// 比路径长度).
//
// 不直接控制 unit, 仅返回路径点列表 — Stage 3 由 World::tick_movement 驱动.
class WallTracer {
public:
    WallTracer(double unit_radius, double step_angle_deg = 15.0,
               double waypoint_threshold = 0.3);

    // 输出 path 不包含起点, 包含终点 (Reached 时). dir 内部归一化.
    PathResult find_path(const NavGrid& grid,
                         const std::vector<DynamicCircle>& dynamics,
                         EntityId ignore_id,
                         Vec2 start, Vec2 end,
                         std::vector<Vec2>& out_path,
                         std::uint32_t query_group = CollisionGroups::Terrain);

    // 路径简化: 贪心跳跃, 从当前点找最远可直达的点.
    // path[0] 视为起点 (保留), 后续点中能直达的会被压缩.
    void simplify(const NavGrid& grid,
                  const std::vector<DynamicCircle>& dynamics,
                  EntityId ignore_id,
                  std::vector<Vec2>& path,
                  std::uint32_t query_group = CollisionGroups::Terrain);

    // 调试访问最近一次 find_path 选中方向 (+1 / -1 / 0=未追踪)
    double last_trace_dir() const { return last_trace_dir_; }
    // 最近一次选中方向的原始路径 (含起点)
    const std::vector<Vec2>& debug_raw_path() const { return debug_raw_path_; }

private:
    double unit_radius_;
    double step_angle_;            // 弧度
    double waypoint_threshold_;
    double step_dist_;
    double last_trace_dir_{0.0};
    std::vector<Vec2> debug_raw_path_;
};

} // namespace dota::pathfinding
