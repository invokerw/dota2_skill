#pragma once

#include "dota/core/types.hpp"
#include "dota/pathfinding/collision_groups.hpp"

#include <cstdint>
#include <vector>

namespace dota::pathfinding {

class NavGrid;

// 一个参与碰撞的动态圆形 body. ShapeCast 用 group 过滤.
//
// 把 Unit 解耦成 DynamicCircle 是为了让 ShapeCast 不依赖 Unit/World 头, 测试
// 也可以构造任意 body 列表. Stage 3 在 World::tick_movement 中按需收集所有
// 存活 unit 的 DynamicCircle.
struct DynamicCircle {
    Vec2          center;
    double        radius{0.0};
    std::uint32_t group{CollisionGroups::All};
    EntityId      id{kInvalidEntityId};
};

struct ShapeCastHit {
    enum class Kind { None, Terrain, Unit };

    bool          hit{false};
    double        toi{0.0};        // [0, max_dist], hit=true 时有效
    Kind          kind{Kind::None};
    EntityId      unit{kInvalidEntityId};   // kind=Unit 时为命中 unit id
    std::uint32_t group{0};        // 命中 body 所在 group, kind=Terrain 时 = Terrain
};

// 圆形 swept-cast: 从 start 沿 dir (需归一化) 走 max_dist, 圆半径 = radius.
// 命中目标:
//   - NavGrid 中 blocked cell (group = Terrain)
//   - NavGrid 中圆形障碍 (group = Terrain)
//   - dynamics 列表中 group & query_group 非零的圆 (group = Unit)
//
// ignore_id != kInvalidEntityId 时跳过该 id 的 dynamic circle (查询前临时
// 排除自身). 命中多个目标时返回 toi 最小者.
//
// 算法全闭式解析:
//   - 圆 vs 圆: |p0 + t*dir - c|^2 = (r1+r2)^2 二次方程取最小正根
//   - 圆 vs AABB: 把 AABB 向外膨胀 radius (Minkowski 和), ray vs 扩展 AABB
//     做 slab; 命中点位于角区域则用对应 AABB 角点做圆-圆 swept 修正
ShapeCastHit shape_cast_circle(
    const NavGrid& grid,
    const std::vector<DynamicCircle>& dynamics,
    EntityId ignore_id,
    Vec2 start, Vec2 dir, double max_dist, double radius,
    std::uint32_t query_group);

} // namespace dota::pathfinding
