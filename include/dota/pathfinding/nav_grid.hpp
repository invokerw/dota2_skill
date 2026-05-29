#pragma once

#include "dota/core/types.hpp"

#include <cstdint>
#include <vector>

namespace dota::pathfinding {

// 静态地形网格. 持有:
//   1. 矩形阵列 cell, 每个 cell 一个 blocked 计数 (引用计数, 多源叠加)
//   2. 圆形障碍列表 — add_circle_obstacle 时四角全在圆内的 cell 自动 blocked,
//      圆本身另存一份给 ShapeCast / WallTracer 看到
//
// A* 走 8 方向, 防对角穿角, 以 cell 中心为节点. 失败时 find_nearest_open
// 螺旋搜索最近通行 cell 兜底. 路径用 line-of-sight bresenham 简化.
class NavGrid {
public:
    NavGrid(double origin_x, double origin_y, double map_width, double map_height,
            double cell_size);

    int    width()    const { return width_; }
    int    height()   const { return height_; }
    double cell_size() const { return cell_size_; }
    double origin_x() const { return origin_x_; }
    double origin_y() const { return origin_y_; }

    // --- 静态格子 ---
    bool is_blocked(int gx, int gy) const;
    void add_blocked(int gx, int gy);
    void remove_blocked(int gx, int gy);

    // --- 圆形障碍 ---
    struct CircleObstacle {
        Vec2   center;
        double radius;
    };

    // 注册圆形障碍. 返回此次因该圆而 blocked 的 cell 索引列表 (idx = gy*width+gx).
    // 调用方应保存返回值, 移除时传入 remove_blocked_cells 保证对称.
    // 仅四角全部在圆内的 cell 会标 blocked.
    std::vector<int> add_circle_obstacle(Vec2 center, double radius);

    // 反向: 移除一组 cell 的 blocked 计数. 不删除圆障碍记录 (单独 remove_circle_obstacle).
    void remove_blocked_cells(const std::vector<int>& cells);

    // 访问当前所有圆形障碍 (ShapeCast / WallTracer 直接读)
    const std::vector<CircleObstacle>& circles() const { return circles_; }

    // 删除一个圆形障碍记录. blocked cell 不会自动清除, 调用方应先 remove_blocked_cells
    void clear_circles() { circles_.clear(); }

    // --- 坐标转换 ---
    void world_to_grid(double wx, double wy, int& gx, int& gy) const;
    void grid_to_world(int gx, int gy, double& wx, double& wy) const;

    // --- A* ---
    // 失败返回空 vector. 成功路径包含 start / end 实际坐标 (已替换), 中间点
    // 是 cell 中心. 已做 line-of-sight 简化.
    std::vector<Vec2> find_path(Vec2 start, Vec2 end);

    // 起点 / 终点在 blocked cell 时的螺旋搜索. 找不到返回 false (out 不变).
    bool find_nearest_open(int sx, int sy, int& ox, int& oy) const;

    // bresenham 视线检测 (cell 中心粒度). 对角穿角时同样需要两条 cardinal 邻居都通行.
    bool has_line_of_sight(Vec2 a, Vec2 b) const;

private:
    int    cell_index(int gx, int gy) const { return gy * width_ + gx; }

    double origin_x_;
    double origin_y_;
    double cell_size_;
    int    width_;
    int    height_;
    std::vector<int> blocked_;  // 引用计数, 0 = 通行
    std::vector<CircleObstacle> circles_;
};

} // namespace dota::pathfinding
