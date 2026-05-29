#pragma once

namespace dota::pathfinding {

// 全局可调参数. 运行时静态修改 (参考 PathfindingConfig.cs).
// 所有"距离"参数以 cell_size 为基准单位倍数, 便于不同 grid 尺度复用配置.
struct MovementConfig {
    // 网格 cell 大小 (世界单位)
    static inline double cell_size = 1.0;

    // GridNavigation: 起点 / 终点 blocked 时, 螺旋搜索最近通行 cell 的最大半径 (格子数)
    static inline int find_nearest_open_max_radius = 10;

    // WallTracer: Bug2 双向追踪共享的最大迭代次数
    static inline int wall_trace_max_iter = 500;
    // 最小追踪距离 = unit_radius * 此值. M-line crossing 提前退出的下限
    static inline double min_trace_dist_multiplier = 2.0;
    // 死循环检测阈值 (cell_size 倍数)
    static inline double max_trace_distance = 50.0;

    // UnitMovement: 阻挡 / 偏移连续超过 replan_threshold 时 full A* 重规划
    static inline int replan_threshold = 5;
    // 先尝试跳过当前 waypoint 的阈值 (低于 replan_threshold)
    static inline int waypoint_skip_threshold = 3;
    // waypoint 到达判定距离 (cell_size 倍数)
    static inline double arrival_epsilon = 0.001;
    // 最后一段被 unit 阻挡时, 距终点 < radius * 此值视为到达
    static inline double arrival_tolerance_multiplier = 2.5;
    // 最后一段当前位置已最近, 连续超过此次数才放弃
    static inline int closest_stall_threshold = 5;
    // 阻挡等待最大帧数. 实际等待 = (block_seed + id) % max_block_wait_frames + 1
    static inline int max_block_wait_frames = 5;
};

} // namespace dota::pathfinding
