#pragma once

// Caster 的移动 marker (单点) + OrderQueue 多步航点虚线.
// 仅依赖 raylib 绘制, 不动 ImGui.

#include "render_helpers.hpp"

namespace dota {
class Unit;
class World;
}

namespace dota::skill_tester {

// 画当前 caster 的 issue_move target (绿色十字 + 圈).
void draw_move_marker(const visual::ViewCamera& cam, Unit* caster);

// 画 caster->orders() 的航点串成的虚线, 末尾标 1..N. 仅 size>1 时绘制.
// (size==1 是已经派发的当前 order, 单独画 move_marker 已经够了.)
void draw_order_waypoints(const visual::ViewCamera& cam, Unit* caster, World* world);

} // namespace dota::skill_tester
