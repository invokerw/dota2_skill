## 总目标

为单位增加 `hull_radius` (碰撞半径), 让三个空间查询从"点判定"升级为"圆判定", 与 Dota 2 的 hull_radius 机制对齐. 投射物 / AoE / cone 全部受益.

参考 Dota 2: 默认英雄 24, 大型/肥肉单位 27. AoE 命中按 `aoe_radius + target.hull_radius` 判定, 投射物按 `width/2 + target.hull_radius` 判定.

---

## Stage 1: 引入 hull_radius 字段

**Goal**: `UnitStats` 增加 `hull_radius`, 暴露 getter, 接入 YAML loader + Lua 绑定.

**Success Criteria**:
- `Unit::hull_radius()` 返回 stats 中的值, 默认 24.0.
- YAML hero 文件可选字段 `hull_radius` 覆盖默认值.
- Lua 可调 `unit:hull_radius()`.
- 已有 125+ 测试全部通过 (此阶段不改空间查询行为, 默认值通过测试 fixture 显式置 0 屏蔽).

**Status**: Not Started

### 改动

- [include/dota/core/unit.hpp](include/dota/core/unit.hpp) `UnitStats` 加 `double hull_radius = 24.0;`
- 同文件 `Unit` 加 `double hull_radius() const { return stats_.hull_radius; }` (内联即可, 不走 modifier 聚合)
- [include/dota/tools/hero_catalog.hpp](include/dota/tools/hero_catalog.hpp) `HeroEntry` 加 `double hull_radius = 24.0;`
- [src/tools/hero_catalog.cpp](src/tools/hero_catalog.cpp) 解析 `hero["hull_radius"]`
- [src/script/bindings.cpp](src/script/bindings.cpp) Unit 用户类型表添加 `"hull_radius", &Unit::hull_radius`

### 测试

- 在 [tests/test_unit_basic.cpp](tests/test_unit_basic.cpp) 增加 1 个测试: 默认 24, stats 显式赋值后通过 getter 读出.

---

## Stage 2: 把 hull_radius 接入三个空间查询

**Goal**: 三个查询从"点判定"改为"圆判定". `LinearProjectile` 自动继承.

**Success Criteria**:
- `find_enemies_in_radius`: 命中条件 `distance(origin, u.pos) <= radius + u.hull_radius`.
- `find_enemies_in_line`: 命中条件 `distance(proj_clamped, u.pos) <= width/2 + u.hull_radius`. 端点也按胶囊处理 (clamp t∈[0,1] 后逐目标比距离, 现行结构正好支持).
- `find_enemies_in_cone`: 精确实现. 命中条件: 单位圆与扇形相交.
  - 公式: 扇形 = 圆心 origin + 半径 length 的圆盘 ∩ 角度 ≤ half_angle 的楔形.
  - 精确判据 (单位圆心 P, 半径 r):
    1. `dist(P, origin) <= r` -> 命中 (origin 在单位内)
    2. `dist(P, origin) - r > length` -> 不命中 (太远)
    3. 角度判据: 设 `angle = acos(dot(dir, normalize(P - origin)))`. 若 `angle <= half_angle` -> 命中 (在锥内).
       否则比较单位圆到锥两条边界射线的最短距离 -- 若任一射线到 P 的最短距离 ≤ r, 仍命中.
  - 边界射线: 从 origin 沿 `rotate(dir, ±half_angle)`, 长度 `length`. "圆到射线段最短距离" 用线段距离公式 (clamp 投影 t∈[0, length]).
- 已有 125+ 测试通过 (spatial_queries 在 Stage 3 调整).

**Status**: Not Started

### 改动 (全部在 [src/core/world.cpp](src/core/world.cpp))

```cpp
std::vector<Unit*> World::find_enemies_in_radius(Vec2 origin, double radius, Team source_team) {
    std::vector<Unit*> out;
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const double r = radius + u->hull_radius();
        if (distance_sq(origin, u->position()) <= r * r) out.push_back(u.get());
    }
    return out;
}
```

```cpp
std::vector<Unit*> World::find_enemies_in_line(Vec2 start, Vec2 end, double width, Team source_team) {
    std::vector<Unit*> out;
    const Vec2   seg = end - start;
    const double seg_len2 = seg.x * seg.x + seg.y * seg.y;
    if (seg_len2 <= 0.0) return out;
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const Vec2 d = u->position() - start;
        double t = (d.x * seg.x + d.y * seg.y) / seg_len2;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        const Vec2 proj{start.x + seg.x * t, start.y + seg.y * t};
        const double thresh = width * 0.5 + u->hull_radius();
        if (distance_sq(proj, u->position()) <= thresh * thresh) out.push_back(u.get());
    }
    return out;
}
```

精确 cone (新增静态 helper `point_to_segment_dist2`):

```cpp
static double point_to_segment_dist2(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const double len2 = ab.x*ab.x + ab.y*ab.y;
    if (len2 <= 0.0) return distance_sq(p, a);
    double t = ((p.x-a.x)*ab.x + (p.y-a.y)*ab.y) / len2;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    const Vec2 proj{a.x + ab.x*t, a.y + ab.y*t};
    return distance_sq(p, proj);
}

std::vector<Unit*> World::find_enemies_in_cone(Vec2 origin, Vec2 direction, double length,
                                                double half_angle_rad, Team source_team) {
    std::vector<Unit*> out;
    const Vec2 dir = normalized(direction);
    if (dir.x == 0.0 && dir.y == 0.0) return out;
    const double cos_half = std::cos(half_angle_rad);
    const double sin_half = std::sin(half_angle_rad);
    // 边界射线终点
    const Vec2 left_end {origin.x + (dir.x*cos_half - dir.y*sin_half) * length,
                         origin.y + (dir.x*sin_half + dir.y*cos_half) * length};
    const Vec2 right_end{origin.x + (dir.x*cos_half + dir.y*sin_half) * length,
                         origin.y + (-dir.x*sin_half + dir.y*cos_half) * length};
    for (auto& u : units_) {
        if (!u->alive()) continue;
        if (u->team() == source_team || u->team() == Team::Neutral) continue;
        const double r  = u->hull_radius();
        const double r2 = r * r;
        const Vec2 P = u->position();
        const Vec2 d = P - origin;
        const double dist2 = d.x*d.x + d.y*d.y;
        // (a) 圆心包含 origin
        if (dist2 <= r2) { out.push_back(u.get()); continue; }
        // (b) 距离超过 length + r 直接淘汰
        const double max_reach = length + r;
        if (dist2 > max_reach * max_reach) continue;
        // (c) 锥内: 圆心在角度楔形内 + 距离 <= length + r
        const double dist = std::sqrt(dist2);
        const double cos_to = (dir.x * d.x + dir.y * d.y) / dist;
        if (cos_to >= cos_half && dist - r <= length) { out.push_back(u.get()); continue; }
        // (d) 圆与边界射线段相交
        if (point_to_segment_dist2(P, origin, left_end)  <= r2) { out.push_back(u.get()); continue; }
        if (point_to_segment_dist2(P, origin, right_end) <= r2) { out.push_back(u.get()); continue; }
    }
    return out;
}
```

注意:
- 楔形角度判据用 `cos_to >= cos_half` 仍然是"圆心在锥内"的近似, 但配合 (a)(d) 两步覆盖了所有"圆心在锥外但圆切入"的场景 -- 整体判据变成精确解.
- (c) 步加了 `dist - r <= length` 防止单位圆心在锥内但远超长度仍被命中 (例如 origin 后方向上方).

---

## Stage 3: 数据 + 测试调整

**Goal**: hero YAML 标注真实 hull_radius. 老 spatial_queries 用例显式置 0 保留几何精确语义, 新增 hull_radius>0 的覆盖测试.

**Success Criteria**:
- 全部 ctest 通过.
- 新增 4 个 spatial_queries 测试 (radius/line/cone 各 1, 加 1 个 cone 边界射线相切).

**Status**: Not Started

### YAML 数据

按 Dota 2 wiki 真实值:

| 英雄 | hull_radius |
|---|---|
| Pudge | 27 |
| Sven | 27 |
| Earthshaker | 24 |
| Juggernaut | 24 |
| Lina | 24 |
| Lion | 24 |

只需改非默认值 (Pudge / Sven). 其余英雄留空走默认.

### test_spatial_queries.cpp 调整

`stats()` fixture 加 `s.hull_radius = 0.0;` -- 现有 7 个用例保持原"点判定"语义, 不破.

### 新增 4 个测试

```cpp
TEST(SpatialQuery, RadiusUsesTargetHullRadius) {
    // hull=30 的目标圆心在半径外 (520), 但边缘切入 500 半径 -> 命中
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    UnitStats fat = stats(); fat.hull_radius = 30.0;
    auto* e = w.spawn("e", Team::Dire, fat, {520.0, 0.0});
    auto hit = w.find_enemies_in_radius({0.0,0.0}, 500.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), e->id());
}

TEST(SpatialQuery, LineUsesTargetHullRadius) {
    // line width=100 (半宽 50), 单位圆心 y=70 但 hull=30 -> 距线 70-30=40 ≤ 50, 命中
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    UnitStats fat = stats(); fat.hull_radius = 30.0;
    auto* e = w.spawn("e", Team::Dire, fat, {500.0, 70.0});
    auto hit = w.find_enemies_in_line({0,0}, {1000,0}, 100.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), e->id());
}

TEST(SpatialQuery, ConeIncludesUnitOutsideAngleButTangent) {
    // 圆心在锥外但圆切入边界射线
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    UnitStats fat = stats(); fat.hull_radius = 50.0;
    // half=45度, 边界射线方向 (cos45, sin45). 圆心垂直于该射线偏离 40 单位 (< hull 50)
    auto* e = w.spawn("e", Team::Dire, fat, {200.0, 250.0}); // 垂线距离约 35
    auto hit = w.find_enemies_in_cone({0,0}, {1.0,0.0}, 500.0, M_PI/4.0, Team::Radiant);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(hit[0]->id(), e->id());
}

TEST(SpatialQuery, ConeRejectsBehindEvenWithLargeHull) {
    // 单位在身后, 即使 hull 大也不命中 (origin 在单位外)
    World w;
    w.spawn("hero", Team::Radiant, stats(), {0.0, 0.0});
    UnitStats fat = stats(); fat.hull_radius = 80.0;
    w.spawn("e", Team::Dire, fat, {-200.0, 0.0});
    auto hit = w.find_enemies_in_cone({0,0}, {1.0,0.0}, 500.0, M_PI/4.0, Team::Radiant);
    EXPECT_TRUE(hit.empty());
}
```

### 命令

```sh
cmake --build build -j
ctest --test-dir build --output-on-failure
```

---

## 提交节奏

按 Stage 1 → 2 → 3, 每个 stage 一个 commit. 每次都跑全量测试, 不能回归.

1. `feat: 单位增加 hull_radius 字段 (Stage 1)`
2. `feat: 空间查询接入 hull_radius (Stage 2)`
3. `feat: 调整 spatial_queries 测试 + 标注 hero hull_radius (Stage 3)`
