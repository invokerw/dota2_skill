#pragma once

// 跨模块共享的应用层状态. 替代之前 main 里散落的 50 行局部变量 + 多个 lambda
// 闭包. 输入路由 / 面板 / 瞄准预览都通过这个结构通信.

#include "ui_labels.hpp"

#include "raylib.h"

#include "dota/core/types.hpp"

#include <string>
#include <vector>

namespace dota::skill_tester {

struct AppState {
    EntityId selected_unit_id{kInvalidEntityId};
    int      hero_active{0};
    int      selected_ability{-1};
    AimMode  aim{AimMode::None};
    bool     paused{false};

    // dummy AI 模式: 0=Idle, 1=Strafe, 2=Charge. strafe_dir 与 dummies 索引对齐
    // (+1 = 朝 +Y, -1 = 朝 -Y).
    int              dummy_ai_idx{0};
    std::vector<int> strafe_dir{1, 1, 1};

    // Dummy 调参面板 (Inspector "Scenario" tab) 的当前值. Apply 时写回 Scene.
    float tune_max_health {6000.0f};
    float tune_attack_dmg {0.0f};
    float tune_mr_bonus   {0.0f};
    float tune_armor_bonus{0.0f};

    // 顶部 toast (1.5s 淡出).
    std::string toast_text;
    double      toast_t0{-10.0};
    Color       toast_color{255, 200, 80, 255};

    void show_toast(std::string text, Color c, double now) {
        toast_text = std::move(text);
        toast_t0 = now;
        toast_color = c;
    }

    void reset_aim() { aim = AimMode::None; }
};

// 战场区域 (像素). 由 main 计算一次, 后续给 input / aim / waypoints 复用.
struct FieldRect {
    int x0, y0, x1, y1;
    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
};

} // namespace dota::skill_tester
