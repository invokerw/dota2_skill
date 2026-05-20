#pragma once

// 三块固定 imgui 面板 (Heroes / Abilities / Inspector). 所有面板都接受
// (Scene&, AppState&) 引用 + layout 常量, 内部各自处理布局 + 状态读写.

#include "dota/tools/hero_catalog.hpp"

namespace dota::skill_tester {

class Scene;
struct AppState;

// 布局常量 (像素).
inline constexpr int kWindowW = 1280;
inline constexpr int kWindowH = 720;
inline constexpr int kSidePanelW = 220;   // 左侧 Heroes 面板宽
inline constexpr int kTunePanelW = 340;   // 右侧 Inspector 面板宽
inline constexpr int kAbilityBarH = 96;   // 底部技能栏高度
inline constexpr int kAbilitySlotMax = 4;

void draw_heroes_panel(const tools::HeroCatalog& catalog, Scene& scene, AppState& app);
void draw_abilities_panel(Scene& scene, AppState& app);
void draw_inspector_panel(Scene& scene, AppState& app);

} // namespace dota::skill_tester
