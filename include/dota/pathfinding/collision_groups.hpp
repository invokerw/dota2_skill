#pragma once

#include <cstdint>

namespace dota::pathfinding {

// 碰撞分组位掩码. ShapeCast 时 (query_group & body_group) != 0 才命中.
//
// 典型用法:
//   - 静态地形 (NavGrid blocked cell, 圆形障碍): 始终 = Terrain
//   - 移动中 unit: Unit. 其他移动者的 wall tracer (查 Terrain) 看不到他, 不绕行
//   - 静止 / 阻挡等待中 unit: Terrain | Unit. 他成了"临时圆形障碍", 别人会绕开
//   - 移动期 ShapeCast (FollowPath): 查 All, 既看地形也看其他 unit
struct CollisionGroups {
    static constexpr std::uint32_t Terrain = 1u;
    static constexpr std::uint32_t Unit    = 2u;
    static constexpr std::uint32_t All     = Terrain | Unit;
};

} // namespace dota::pathfinding
