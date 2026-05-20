#pragma once

// 主循环 update 阶段: 处理键盘 / 鼠标输入路由 + dummy AI 推进.
// 把所有"读 raylib 输入 -> 修改 AppState / 派发 Order"的逻辑集中到一处.

#include "render_helpers.hpp"

namespace dota {
class Unit;
}

namespace dota::skill_tester {

class Scene;
struct AppState;
struct FieldRect;

struct InputContext {
    bool gui_wants_mouse{false};
    bool gui_wants_keyboard{false};
    bool mouse_in_field{false};
    Vec2 mouse_world{};
    Unit* hover_unit{nullptr};         // 鼠标悬停的活着的 dummy (用于瞄准)
    Unit* inspect_hover_unit{nullptr}; // 鼠标悬停的任意单位 (用于 Inspector 选中)
    bool quit_requested{false};
};

// 计算鼠标 / hover 状态.
InputContext compute_input_context(const visual::ViewCamera& cam,
                                   const FieldRect& field,
                                   Scene& scene);

// 处理键盘输入: ESC, SPACE, R, S, 数字键, Shift modifier.
void process_keyboard(Scene& scene, AppState& app, InputContext& ctx);

// 处理鼠标输入: RMB 取消瞄准 / 移动指令; LMB 选中单位 / 释放技能.
void process_mouse(Scene& scene, AppState& app, const InputContext& ctx);

// 推进 dummy AI (Idle / Strafe / Charge).
void tick_dummy_ai(Scene& scene, AppState& app, double dt);

} // namespace dota::skill_tester
