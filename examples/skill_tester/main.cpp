// 技能测试器: 选英雄 -> 选技能 -> 选目标 / 位置 -> 释放, 看效果.
//
// 控制:
//   左侧栏点击切换英雄 (= 重建 World).
//   底部技能槽点击, 或按 1/2/3/4 选中技能, 进入瞄准模式.
//   * UnitTarget: 鼠标 hover 一个 dummy, 左键释放; 距离过远显示红圈.
//   * PointTarget: 鼠标移动时画从 caster 到指针的线 + width / radius 提示, 左键释放.
//   * NoTarget: 进入待确认状态, 再次按数字键 / SPACE / 左键释放.
//   * 法球 (orb, is_orb): 点击 / 按数字键 = toggle autocast, 不进入瞄准.
//   A: 普攻待选目标, 左键点中敌方单位派 OrderAttackTarget.
//   ESC 或右键取消瞄准.
//   R 重置当前英雄, SPACE 暂停 (非瞄准时), ESC 退出 (无瞄准时).
//   Shift 修饰键 = 队尾追加 (Shift+RMB 走点 / Shift+LMB 释放都会进队列).
//   S 全停: 清空 caster 指令队列.

#include "aim.hpp"
#include "app_state.hpp"
#include "input.hpp"
#include "panels.hpp"
#include "render_helpers.hpp"
#include "scene.hpp"
#include "waypoints.hpp"

#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/tools/hero_catalog.hpp"

#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace dota;
using namespace dota::skill_tester;

namespace {

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

// 加载 CJK 字体. 路径解析: 运行期 env DOTA_CJK_FONT_PATH 优先, 否则用 CMake
// 烤进去的 DOTA_CJK_FONT_PATH 宏 (DOWNLOAD_CJK_FONT=ON 时拉的). 找不到就保持
// imgui 默认字体, 中文显示成方块但不影响功能.
void load_cjk_font() {
    namespace fs = std::filesystem;
    std::string path;
    if (const char* env = std::getenv("DOTA_CJK_FONT_PATH"); env && *env) {
        path = env;
    } else {
#ifdef DOTA_CJK_FONT_PATH
        path = DOTA_CJK_FONT_PATH;
#endif
    }
    if (path.empty() || !fs::exists(path)) return;
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        path.c_str(), 16.0f, nullptr, ranges);
    if (!font) io.Fonts->AddFontDefault();
    rlImGuiReloadFonts();
}

visual::ViewCamera make_camera(const FieldRect& field) {
    visual::ViewCamera cam;
    cam.window_w = field.w();
    cam.window_h = field.h();
    cam.origin_x = static_cast<float>(field.x0) + cam.window_w * 0.5f;
    cam.origin_y = static_cast<float>(field.y0) + cam.window_h * 0.5f;
    return cam;
}

// 战场区裁剪渲染: 网格 + 投射物 + 单位 + selected 高亮 + 飘字 + 瞄准预览 +
// 移动 marker + 多步 OrderQueue 航点虚线.
void draw_battlefield(Scene& scene,
                      const AppState& app,
                      const visual::ViewCamera& cam,
                      const FieldRect& field,
                      const InputContext& ctx) {
    BeginScissorMode(field.x0, field.y0, cam.window_w, cam.window_h);
    visual::draw_grid(cam, -1200, 1200, -800, 800, 200);
    for (const auto& p : scene.render_projectiles()) visual::draw_projectile(cam, p);
    for (const auto& u : scene.render_units())       visual::draw_unit(cam, u);

    if (Unit* selected = scene.find_unit(app.selected_unit_id)) {
        const Vector2 ss = cam.to_screen(selected->position());
        const float sr = std::max(visual::kMinUnitRadiusPx,
                                  cam.scalar(selected->hull_radius()));
        DrawCircleLines(static_cast<int>(ss.x), static_cast<int>(ss.y),
                        sr + 5.0f, Color{120, 210, 255, 255});
        DrawCircleLines(static_cast<int>(ss.x), static_cast<int>(ss.y),
                        sr + 7.0f, Color{120, 210, 255, 180});
    }
    for (auto& f : scene.texts()) {
        visual::draw_floating_text(cam, f, scene.world()->time());
    }

    if (app.selected_ability >= 0 &&
        app.selected_ability < static_cast<int>(scene.caster_abilities().size())) {
        Ability* ab = scene.caster_abilities()[app.selected_ability];
        draw_aim_preview(app, cam, ab, scene.caster(), ctx.hover_unit,
                         ctx.mouse_world, ctx.mouse_in_field);
    }

    // 普攻瞄准: 高亮 hover 敌方单位.
    if (app.aim == AimMode::AwaitAttackTarget && ctx.hover_unit && scene.caster()) {
        Unit* h = ctx.hover_unit;
        const bool enemy = h->team() != scene.caster()->team();
        const Vector2 us = cam.to_screen(h->position());
        const float r_px = std::max(visual::kMinUnitRadiusPx,
                                    cam.scalar(h->hull_radius()));
        const Color c = enemy ? Color{255, 90, 90, 255} : Color{160, 160, 160, 200};
        DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                        r_px + 4.0f, c);
        DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                        r_px + 6.0f, c);
    }

    draw_move_marker(cam, scene.caster());
    draw_order_waypoints(cam, scene.caster(), scene.world());
    EndScissorMode();
}

void draw_help_line(AimMode aim, const FieldRect& field) {
    const char* aim_hint = "";
    switch (aim) {
        case AimMode::AwaitUnitTarget:      aim_hint = "  [aim: click a target]"; break;
        case AimMode::AwaitPointTarget:     aim_hint = "  [aim: click a point]"; break;
        case AimMode::AwaitConfirmNoTarget: aim_hint = "  [confirm: SPACE / number / left-click]"; break;
        case AimMode::AwaitAttackTarget:    aim_hint = "  [attack: click an enemy]"; break;
        default: break;
    }
    DrawText(TextFormat(
                 "1-4 / click to select   A attack   LMB cast   RMB move / cancel   "
                 "Shift queue   S stop   L log   ESC cancel   R reset   SPACE pause%s",
                 aim_hint),
             kSidePanelW + 12, kWindowH - kAbilityBarH - 22,
             14, Color{160, 160, 160, 255});
    (void)field;
}

void draw_toast(const AppState& app, const FieldRect& field, double now) {
    const double age = now - app.toast_t0;
    if (age < 0.0 || age >= 1.5) return;
    const float alpha = static_cast<float>(1.0 - age / 1.5);
    Color tc = app.toast_color;
    tc.a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    const int tw = MeasureText(app.toast_text.c_str(), 22);
    const int tx = field.x0 + (field.w() - tw) / 2;
    const int ty = field.y0 + 38;
    DrawText(app.toast_text.c_str(), tx, ty, 22, tc);
}

} // namespace

int main() {
    tools::HeroCatalog catalog;
    try {
        catalog.scan(data_dir() + "/heroes");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "扫描英雄目录失败: %s\n", e.what());
        return 1;
    }
    if (catalog.heroes().empty()) {
        std::fprintf(stderr, "data/heroes/ 下没有可用英雄\n");
        return 1;
    }

    InitWindow(kWindowW, kWindowH, "dota2_skill -- skill tester");
    SetTargetFPS(60);
    // 我们自己处理 ESC: 优先取消瞄准, 其次退出. 不让 raylib 直接 set close.
    SetExitKey(KEY_NULL);
    rlImGuiSetup(true);
    load_cjk_font();

    Scene scene(catalog);

    AppState app;
    app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;

    // 战场矩形的 y0 与 camera 在主循环里随 menu_h 重算; 初值用 0 占位.
    FieldRect field{
        kSidePanelW, 0,
        kWindowW - kTunePanelW, kWindowH - kAbilityBarH,
    };
    visual::ViewCamera cam = make_camera(field);
    int last_menu_h = -1;

    bool quit = false;
    while (!quit && !WindowShouldClose()) {
        // imgui 在每帧开始时消费 raylib 事件, 之后通过 io.WantCapture* 告诉我们
        // 输入是否被 GUI 截获. 必须在 rlImGuiBegin 之后再 query.
        rlImGuiBegin();

        // 顶部主菜单栏先画, 拿到实际高度再算其余面板的 y0.
        const int menu_h = static_cast<int>(draw_main_menu_bar(app));
        if (menu_h != last_menu_h) {
            field.y0 = menu_h;
            cam = make_camera(field);
            last_menu_h = menu_h;
        }

        InputContext ctx = compute_input_context(cam, field, scene);
        process_keyboard(scene, app, ctx);
        process_mouse(scene, app, ctx);
        if (ctx.quit_requested) quit = true;

        const float dt_raw = GetFrameTime();
        const double dt = app.paused ? 0.0 : std::min(static_cast<double>(dt_raw), 0.05);

        // Dummy AI 在 scene.update 之前推进 -- 命令在本 tick 即生效.
        tick_dummy_ai(scene, app, dt);
        if (dt > 0.0) scene.update(dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        draw_battlefield(scene, app, cam, field, ctx);

        DrawText(TextFormat("hero: %s   t = %.2fs%s",
                            catalog.heroes()[scene.hero_index()].yaml_name.c_str(),
                            scene.world()->time(),
                            app.paused ? "  [PAUSED]" : ""),
                 field.x0 + 12, field.y0 + 10, 20, RAYWHITE);

        draw_heroes_panel(catalog, scene, app, static_cast<float>(menu_h));
        draw_abilities_panel(scene, app);
        draw_inspector_panel(scene, app, static_cast<float>(menu_h));
        draw_combat_log_window(scene, app);

        draw_help_line(app.aim, field);
        draw_toast(app, field, scene.world()->time());

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
