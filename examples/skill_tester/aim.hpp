#pragma once

// 瞄准预览绘制: 根据 ability 行为类型 + 当前 hover_unit / mouse_world 在战场上
// 画 cast_range 圆 / 单位高亮环 / 胶囊 (线性投射物) / AoE 圆形预览.

#include "render_helpers.hpp"

namespace dota {
class Ability;
class Unit;
}

namespace dota::skill_tester {

struct AppState;

void draw_aim_preview(const AppState& app,
                      const visual::ViewCamera& cam,
                      Ability* ab,
                      Unit* caster,
                      Unit* hover_unit,
                      Vec2 mouse_world,
                      bool mouse_in_field);

} // namespace dota::skill_tester
