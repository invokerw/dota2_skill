// 技能测试器: 选英雄 -> 选技能 -> 选目标 / 位置 -> 释放, 看效果.
//
// 控制:
//   左侧栏点击切换英雄 (= 重建 World).
//   底部技能槽点击, 或按 1/2/3/4 选中技能, 进入瞄准模式.
//   * UnitTarget: 鼠标 hover 一个 dummy, 左键释放; 距离过远显示红圈.
//   * PointTarget: 鼠标移动时画从 caster 到指针的线 + width / radius 提示, 左键释放.
//   * NoTarget: 进入待确认状态, 再次按数字键 / SPACE / 左键释放.
//   ESC 或右键取消瞄准.
//   R 重置当前英雄, SPACE 暂停 (非瞄准时), ESC 退出 (无瞄准时).

#include "dota/ability/ability.hpp"
#include "dota/ability/behavior.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/projectile/manager.hpp"
#include "dota/projectile/projectile.hpp"
#include "dota/script/lua_state.hpp"
#include "dota/tools/hero_catalog.hpp"

#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"
#include "visual_common.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace dota;
using dota::tools::HeroCatalog;
using dota::tools::HeroEntry;
using dota::visual::FloatingText;
using dota::visual::RenderProjectile;
using dota::visual::RenderUnit;
using dota::visual::ViewCamera;

namespace {

constexpr int kWindowW = 1280;
constexpr int kWindowH = 720;

// UI 布局参数. 战场区在 [kSidePanelW, kWindowW - kTunePanelW] x
// [0, kWindowH - kAbilityBarH].
constexpr int kSidePanelW = 220;   // 左侧英雄列表面板宽
constexpr int kTunePanelW = 240;   // 右侧调参面板宽
constexpr int kAbilityBarH = 96;   // 底部技能栏高度
constexpr int kAbilitySlotMax = 4; // 技能槽最大数量, 多余的不显示

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

// dummy 配置: 3 个 Dire, 不同 magic_resist, 高血不死.
struct DummySpec {
    const char* label;
    Vec2        pos;
    double      magic_resist;
    double      base_armor;
};
constexpr DummySpec kDummies[] = {
    {"Dummy MR0%",  { 600.0, -150.0}, 0.00,  0.0},
    {"Dummy MR25%", { 600.0,    0.0}, 0.25,  5.0},
    {"Dummy MR50%", { 600.0,  150.0}, 0.50, 10.0},
};

// S5 调参覆盖: 仅在 Apply / Reset 时由 main 写入, rebuild_with_hero 消费.
// 三个 dummy 共用同一份 override (各自的 magic_resist / base_armor 还是按
// kDummies 区分; 这里只统一 max_health / attack_damage / 以及"额外 MR / 装甲
// 偏移"). 第一版保持简单.
struct DummyOverride {
    bool   active        = false;
    double max_health    = 6000.0;
    double attack_damage = 0.0;
    double magic_resist_bonus = 0.0;   // 加到每个 dummy 的 base MR 上
    double base_armor_bonus   = 0.0;
};

// 主场景: 一个 caster + 3 dummy. 切英雄 / R 都走 rebuild_with_hero().
class Scene {
public:
    explicit Scene(const HeroCatalog& cat) : catalog_(cat) {
        rebuild_with_hero(0);
    }

    void rebuild_with_hero(std::size_t idx) {
        if (idx >= catalog_.heroes().size()) return;
        hero_index_ = idx;

        // 销毁顺序: World 持有的 ScriptedAbility 内部 sol::table / sol::function
        // 引用了 lua_ 拥有的 lua_State. 必须先销毁 world_ (连带 reg_), 再销毁
        // lua_, 否则 sol 析构时会对已销毁的 state 调 luaL_unref 触发 segfault.
        world_.reset();
        reg_.reset();
        lua_.reset();

        lua_   = std::make_unique<LuaState>();
        reg_   = std::make_unique<AbilityRegistry>();
        reg_->set_lua(lua_.get());
        // 加载所选英雄的 yaml -- 否则技能 def 找不到
        reg_->load_file(catalog_.heroes()[hero_index_].yaml_path);

        world_  = std::make_unique<World>();
        texts_.clear();

        const HeroEntry& h = catalog_.heroes()[hero_index_];

        UnitStats cs;
        cs.max_health       = h.base_health > 0 ? h.base_health : 800.0;
        cs.max_mana         = h.base_mana   > 0 ? h.base_mana   : 600.0;
        cs.attack_damage    = 55.0;
        cs.base_armor       = h.base_armor;
        cs.magic_resist     = h.magic_resist;
        cs.base_attack_time = 1.7;
        caster_ = world_->spawn(h.yaml_name, Team::Radiant, cs, {-600.0, 0.0});

        // 给 caster 实例化所有非 passive 技能, 用于后续选择 / 施法
        caster_abilities_.clear();
        for (const auto& a : h.abilities) {
            if (a.is_passive) continue;
            Ability* inst = reg_->instantiate(a.name, *caster_);
            if (inst) caster_abilities_.push_back(inst);
        }

        // 3 个 dummy. 若 dummy_override_.active, 用调参面板的 stats.
        dummies_.clear();
        for (const auto& d : kDummies) {
            UnitStats ds;
            ds.max_health    = dummy_override_.active
                ? dummy_override_.max_health : 6000.0;
            ds.max_mana      = 0.0;
            ds.attack_damage = dummy_override_.active
                ? dummy_override_.attack_damage : 0.0;
            ds.base_armor    = d.base_armor +
                (dummy_override_.active ? dummy_override_.base_armor_bonus : 0.0);
            ds.magic_resist  = d.magic_resist +
                (dummy_override_.active ? dummy_override_.magic_resist_bonus : 0.0);
            ds.base_attack_time = 1.7;
            dummies_.push_back(world_->spawn(d.label, Team::Dire, ds, d.pos));
        }

        // 飘字订阅
        world_->events().subscribe<DamageAppliedEvent>(
            [this](DamageAppliedEvent& e) {
                if (e.amount_applied <= 0.0) return;
                Unit* v = world_->find(e.victim);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "-%.0f", e.amount_applied);
                Color c = (e.type == DamageType::Magical) ? Color{120, 180, 255, 255}
                        : (e.type == DamageType::Pure)    ? Color{255, 220, 120, 255}
                                                          : Color{255, 100, 100, 255};
                texts_.push_back({v->position(), buf, c, world_->time()});
            });
        world_->events().subscribe<HealAppliedEvent>(
            [this](HealAppliedEvent& e) {
                if (e.amount <= 0.0) return;
                Unit* v = world_->find(e.target);
                if (!v) return;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "+%.0f", e.amount);
                texts_.push_back({v->position(), buf, Color{120, 230, 120, 255},
                                 world_->time()});
            });
    }

    void update(double dt) {
        if (!world_) return;
        world_->advance(dt);
        const double now = world_->time();
        texts_.erase(
            std::remove_if(texts_.begin(), texts_.end(),
                [now](const FloatingText& f) { return now - f.spawn_time > 1.2; }),
            texts_.end());
    }

    World*                            world()        { return world_.get(); }
    Unit*                             caster() const { return caster_; }
    const std::vector<Unit*>&         dummies() const { return dummies_; }
    const std::vector<Ability*>&      caster_abilities() const { return caster_abilities_; }
    std::size_t                       hero_index() const { return hero_index_; }
    std::vector<FloatingText>&        texts()        { return texts_; }

    void set_dummy_override(const DummyOverride& o) { dummy_override_ = o; }
    const DummyOverride& dummy_override() const     { return dummy_override_; }

    // 收集当前所有要绘制的 RenderUnit / RenderProjectile (复用 visual_common)
    std::vector<RenderUnit> render_units() const {
        std::vector<RenderUnit> out;
        if (!world_) return out;
        for (Team t : {Team::Radiant, Team::Dire, Team::Neutral}) {
            for (Unit* u : world_->units_on_team(t)) {
                if (!u) continue;
                RenderUnit ru;
                ru.id      = u->id();
                ru.name    = u->name();
                ru.team    = u->team();
                ru.alive   = u->alive();
                ru.hp      = u->health();
                ru.max_hp  = u->max_health();
                ru.position= u->position();
                for (auto& m : u->modifiers().all()) ru.modifiers.push_back(m->name());
                for (const auto& a : u->abilities().all()) {
                    if (a->phase() != CastPhase::Casting) continue;
                    const float total = static_cast<float>(a->cast_point());
                    if (total <= 0.0f) continue;
                    ru.casting_ability = a->name();
                    ru.cast_progress = 1.0f - std::clamp(
                        static_cast<float>(a->phase_timer()) / total, 0.0f, 1.0f);
                    break;
                }
                out.push_back(std::move(ru));
            }
        }
        return out;
    }
    std::vector<RenderProjectile> render_projectiles() const {
        std::vector<RenderProjectile> out;
        if (!world_) return out;
        for (const auto& p : world_->projectiles().live()) {
            if (!p) continue;
            RenderProjectile rp;
            rp.pid    = p->pid();
            rp.pos    = p->position();
            rp.linear = p->is_linear();
            rp.dir    = p->direction();
            rp.width  = p->width();
            out.push_back(rp);
        }
        return out;
    }

private:
    const HeroCatalog&              catalog_;
    std::size_t                     hero_index_{0};
    std::unique_ptr<LuaState>       lua_;
    std::unique_ptr<AbilityRegistry> reg_;
    std::unique_ptr<World>          world_;
    Unit*                           caster_{nullptr};
    std::vector<Unit*>              dummies_;
    std::vector<Ability*>           caster_abilities_;
    std::vector<FloatingText>       texts_;
    DummyOverride                   dummy_override_{};
};

} // namespace

// 把 ability behavior 翻成短标签, 显示在按钮上.
const char* behavior_label(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::Channelled))   return "CHN";
    if (has_flag(b, BehaviorFlag::PointTarget))  return "PT";
    if (has_flag(b, BehaviorFlag::UnitTarget))   return "UT";
    if (has_flag(b, BehaviorFlag::NoTarget))     return "NT";
    return "?";
}

// --- S4: 瞄准状态 ---
enum class AimMode {
    None,
    AwaitUnitTarget,
    AwaitPointTarget,
    AwaitConfirmNoTarget,
};

AimMode aim_for_behavior(std::uint32_t b) {
    if (has_flag(b, BehaviorFlag::PointTarget)) return AimMode::AwaitPointTarget;
    if (has_flag(b, BehaviorFlag::UnitTarget))  return AimMode::AwaitUnitTarget;
    if (has_flag(b, BehaviorFlag::NoTarget))    return AimMode::AwaitConfirmNoTarget;
    return AimMode::None;
}

const char* cast_error_text(CastError e) {
    switch (e) {
        case CastError::None:              return "OK";
        case CastError::NotReady:          return "Not ready";
        case CastError::OnCooldown:        return "On cooldown";
        case CastError::NotEnoughMana:     return "Not enough mana";
        case CastError::Silenced:          return "Silenced";
        case CastError::Stunned:           return "Stunned";
        case CastError::Hexed:             return "Hexed";
        case CastError::CasterDead:        return "Caster dead";
        case CastError::InvalidTarget:     return "Invalid target";
        case CastError::TargetMagicImmune: return "Target magic immune";
        case CastError::OutOfRange:        return "Out of range";
        case CastError::NotLearned:        return "Not learned";
    }
    return "?";
}

// 找 ability_special 里第一个能用的尺寸提示 (radius / width). 找不到给默认.
double preview_size(const Ability& ab, std::uint32_t behavior, double fallback) {
    const auto& sp = ab.ability_special();
    static const char* keys[] = {"radius", "width", "hook_width"};
    for (const char* k : keys) {
        auto it = sp.find(k);
        if (it != sp.end()) return it->second.get_float(ab.level());
    }
    (void)behavior;
    return fallback;
}

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

int main() {
    HeroCatalog catalog;
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

    rlImGuiSetup(/*dark theme=*/true);

    Scene scene(catalog);

    // 战场视图: 居中到 [kSidePanelW, kWindowW - kTunePanelW] x
    // [0, kWindowH - kAbilityBarH].
    ViewCamera cam;
    const int field_x0 = kSidePanelW;
    const int field_x1 = kWindowW - kTunePanelW;
    const int field_y0 = 0;
    const int field_y1 = kWindowH - kAbilityBarH;
    cam.window_w = field_x1 - field_x0;
    cam.window_h = field_y1 - field_y0;
    cam.origin_x = static_cast<float>(field_x0) + cam.window_w * 0.5f;
    cam.origin_y = static_cast<float>(field_y0) + cam.window_h * 0.5f;

    int  hero_active  = 0;
    int  selected_ability = -1;
    AimMode aim = AimMode::None;
    bool paused = false;

    // S5: 调参面板状态. 用 float 跟 imgui 滑动条绑定.
    float tune_max_health  = 6000.0f;
    float tune_attack_dmg  = 0.0f;
    float tune_mr_bonus    = 0.0f;     // -1.0..+1.0
    float tune_armor_bonus = 0.0f;     // -10..+30
    auto apply_dummy_tune = [&](bool also_rebuild) {
        DummyOverride o;
        o.active             = true;
        o.max_health         = std::max(1.0f, tune_max_health);
        o.attack_damage      = std::max(0.0f, tune_attack_dmg);
        o.magic_resist_bonus = tune_mr_bonus;
        o.base_armor_bonus   = tune_armor_bonus;
        scene.set_dummy_override(o);
        if (also_rebuild) {
            scene.rebuild_with_hero(scene.hero_index());
            selected_ability = -1;
            aim = AimMode::None;
        }
    };
    std::string toast_text;     // 顶部 toast (1.5s 淡出)
    double      toast_t0 = -10.0;
    Color       toast_color = Color{255, 200, 80, 255};
    auto show_toast = [&](const std::string& s, Color c) {
        toast_text = s;
        toast_t0   = scene.world()->time();
        toast_color = c;
    };

    auto reset_aim = [&] { aim = AimMode::None; };

    auto try_cast = [&](Ability* ab, const CastTarget& tgt) {
        const CastError e = ab->order_cast(tgt, *scene.world());
        if (e != CastError::None) {
            show_toast(std::string(cast_error_text(e)),
                       Color{220, 100, 100, 255});
        } else {
            show_toast("Cast: " + ab->name(),
                       Color{120, 230, 120, 255});
        }
        reset_aim();
    };

    bool quit = false;
    while (!quit && !WindowShouldClose()) {
        // imgui 在每帧开始时消费 raylib 事件, 之后通过 io.WantCapture* 告诉我们
        // 输入是否被 GUI 截获. 必须在 rlImGuiBegin 之后再 query.
        rlImGuiBegin();
        const ImGuiIO& io = ImGui::GetIO();
        const bool gui_wants_mouse    = io.WantCaptureMouse;
        const bool gui_wants_keyboard = io.WantCaptureKeyboard;

        // ESC: 优先取消瞄准, 没在瞄准时退出窗口.
        if (!gui_wants_keyboard && IsKeyPressed(KEY_ESCAPE)) {
            if (aim != AimMode::None) {
                reset_aim();
            } else {
                quit = true;
            }
        }

        if (!gui_wants_keyboard && aim == AimMode::None && IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (!gui_wants_keyboard && IsKeyPressed(KEY_R)) {
            scene.rebuild_with_hero(scene.hero_index());
            selected_ability = -1;
            reset_aim();
            paused = false;
        }

        // 数字键 1-4 选中技能槽: 第一次按 = 选并进入瞄准; 已在该槽瞄准时, 对
        // NoTarget 触发释放, 其他类型切回选择 (无变化).
        const int key_slots[] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
        const int slot_count =
            std::min<int>(kAbilitySlotMax,
                          static_cast<int>(scene.caster_abilities().size()));
        for (int i = 0; i < slot_count && !gui_wants_keyboard; ++i) {
            if (!IsKeyPressed(key_slots[i])) continue;
            Ability* ab = scene.caster_abilities()[i];
            const AimMode want = aim_for_behavior(ab->behavior());
            if (selected_ability == i && aim == AimMode::AwaitConfirmNoTarget) {
                // 已选 NoTarget 槽再按一次 = 释放.
                CastTarget tgt;
                try_cast(ab, tgt);
                selected_ability = -1;
            } else {
                selected_ability = i;
                aim = want;
            }
        }

        // SPACE 在 NoTarget 待确认时也可释放
        if (!gui_wants_keyboard &&
            aim == AimMode::AwaitConfirmNoTarget && IsKeyPressed(KEY_SPACE) &&
            selected_ability >= 0 && selected_ability < slot_count) {
            CastTarget tgt;
            try_cast(scene.caster_abilities()[selected_ability], tgt);
            selected_ability = -1;
        }

        // 鼠标位置 -> 世界坐标; 仅当鼠标在战场区且未被 GUI 截获时有效
        const Vector2 ms = GetMousePosition();
        const bool mouse_in_field =
            !gui_wants_mouse &&
            ms.x >= field_x0 && ms.x < field_x1 &&
            ms.y >= field_y0 && ms.y < field_y1;
        const Vec2 mouse_world = cam.to_world(ms);

        // 拾取最近的活着的 dummy (圆形碰撞, 半径 = kUnitRadiusPx / zoom)
        Unit* hover_unit = nullptr;
        if (mouse_in_field) {
            const double pick_r = dota::visual::kUnitRadiusPx / cam.zoom;
            const double pick_r2 = pick_r * pick_r;
            for (Unit* u : scene.dummies()) {
                if (!u || !u->alive()) continue;
                if (dist2(u->position(), mouse_world) <= pick_r2) {
                    hover_unit = u;
                    break;
                }
            }
        }

        // 右键取消瞄准
        if (!gui_wants_mouse && aim != AimMode::None &&
            IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            reset_aim();
        }

        // 左键 -- 仅在战场内有效
        if (mouse_in_field && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            selected_ability >= 0 && selected_ability < slot_count) {
            Ability* ab = scene.caster_abilities()[selected_ability];
            CastTarget tgt;
            bool fire = false;
            if (aim == AimMode::AwaitUnitTarget && hover_unit) {
                tgt.unit = hover_unit;
                fire = true;
            } else if (aim == AimMode::AwaitPointTarget) {
                tgt.point = mouse_world;
                tgt.has_point = true;
                fire = true;
            } else if (aim == AimMode::AwaitConfirmNoTarget) {
                fire = true;
            }
            if (fire) {
                try_cast(ab, tgt);
                selected_ability = -1;
            }
        }

        const float dt_raw = GetFrameTime();
        const double dt = paused ? 0.0 : std::min(static_cast<double>(dt_raw), 0.05);
        if (dt > 0.0) scene.update(dt);

        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        // --- 战场区域裁剪 + 网格 + 单位 + 投射物 + 飘字 ---
        BeginScissorMode(field_x0, field_y0, cam.window_w, cam.window_h);
        dota::visual::draw_grid(cam, -1200, 1200, -800, 800, 200);
        for (const auto& p : scene.render_projectiles()) dota::visual::draw_projectile(cam, p);
        for (const auto& u : scene.render_units())       dota::visual::draw_unit(cam, u);
        for (auto& f : scene.texts())
            dota::visual::draw_floating_text(cam, f, scene.world()->time());

        // --- 瞄准预览 ---
        if (aim != AimMode::None && selected_ability >= 0 &&
            selected_ability < slot_count && scene.caster() && scene.caster()->alive()) {
            Ability* ab = scene.caster_abilities()[selected_ability];
            const Vec2 caster_pos = scene.caster()->position();
            const double range = ab->cast_range();
            const Vector2 cs = cam.to_screen(caster_pos);

            // cast_range 圆 (浅色)
            if (range > 0.0) {
                DrawCircleLines(static_cast<int>(cs.x), static_cast<int>(cs.y),
                                cam.scalar(range), Color{200, 200, 80, 90});
            }

            if (aim == AimMode::AwaitUnitTarget) {
                if (hover_unit) {
                    const Vector2 us = cam.to_screen(hover_unit->position());
                    const double d = std::sqrt(dist2(caster_pos, hover_unit->position()));
                    const Color ring = (range <= 0.0 || d <= range)
                        ? Color{255, 220, 80, 255}
                        : Color{220, 80, 80, 255};
                    DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                                    dota::visual::kUnitRadiusPx + 4.0f, ring);
                    DrawCircleLines(static_cast<int>(us.x), static_cast<int>(us.y),
                                    dota::visual::kUnitRadiusPx + 6.0f, ring);
                }
            } else if (aim == AimMode::AwaitPointTarget && mouse_in_field) {
                const Vector2 ts = cam.to_screen(mouse_world);
                const double d = std::sqrt(dist2(caster_pos, mouse_world));
                const Color line_c = (range <= 0.0 || d <= range)
                    ? Color{255, 220, 80, 200}
                    : Color{220, 80, 80, 200};
                DrawLineEx(cs, ts, 2.0f, line_c);
                const double size = preview_size(*ab, ab->behavior(), 100.0);
                DrawCircleLines(static_cast<int>(ts.x), static_cast<int>(ts.y),
                                cam.scalar(size), line_c);
                DrawLineEx({ts.x - 8, ts.y}, {ts.x + 8, ts.y}, 2.0f, line_c);
                DrawLineEx({ts.x, ts.y - 8}, {ts.x, ts.y + 8}, 2.0f, line_c);
            }
            // AwaitConfirmNoTarget: 预览只是 cast_range 圆, 已经画过.
        }
        EndScissorMode();

        // --- HUD 顶部状态行 ---
        DrawText(TextFormat("hero: %s   t = %.2fs%s",
                            catalog.heroes()[scene.hero_index()].yaml_name.c_str(),
                            scene.world()->time(),
                            paused ? "  [PAUSED]" : ""),
                 field_x0 + 12, 10, 20, RAYWHITE);

        // --- imgui 面板: Heroes (左) / Ability Bar (底) / Dummy Tuning (右) ---
        // 三块都钉死位置 + 关掉 move/resize/collapse, 当成固定 dock.
        constexpr ImGuiWindowFlags kFixedFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;

        // Heroes 面板.
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kSidePanelW),
                                        static_cast<float>(kWindowH - kAbilityBarH)));
        if (ImGui::Begin("Heroes", nullptr, kFixedFlags)) {
            const int prev_active = hero_active;
            for (std::size_t i = 0; i < catalog.heroes().size(); ++i) {
                const bool sel = (static_cast<int>(i) == hero_active);
                if (ImGui::Selectable(catalog.heroes()[i].yaml_name.c_str(), sel)) {
                    hero_active = static_cast<int>(i);
                }
            }
            if (hero_active >= 0 && hero_active != prev_active &&
                static_cast<std::size_t>(hero_active) != scene.hero_index()) {
                scene.rebuild_with_hero(static_cast<std::size_t>(hero_active));
                selected_ability = -1;
                paused = false;
            }
        }
        ImGui::End();

        // Ability Bar 面板 (底部, 横跨战场宽度).
        const int bar_y = kWindowH - kAbilityBarH;
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kSidePanelW),
                                        static_cast<float>(bar_y)));
        ImGui::SetNextWindowSize(ImVec2(
            static_cast<float>(kWindowW - kSidePanelW - kTunePanelW),
            static_cast<float>(kAbilityBarH)));
        if (ImGui::Begin("Abilities", nullptr,
                         kFixedFlags | ImGuiWindowFlags_NoTitleBar)) {
            const int slots = std::min<int>(
                kAbilitySlotMax,
                static_cast<int>(scene.caster_abilities().size()));
            if (slots == 0) {
                ImGui::TextDisabled("(no active abilities)");
            }
            const ImVec2 slot_sz(200.0f, 64.0f);
            for (int i = 0; i < slots; ++i) {
                if (i > 0) ImGui::SameLine();
                Ability* ab = scene.caster_abilities()[i];
                const bool selected = (selected_ability == i);
                ImGui::PushID(i);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.95f, 0.78f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.85f, 0.7f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                }
                const double cd = ab->cooldown_remaining();
                const double mp = ab->mana_cost_for_level();
                const char* tag = behavior_label(ab->behavior());
                char label[128];
                std::snprintf(label, sizeof(label),
                              "[%d] %s\n%s  CD %.1fs  MP %d",
                              i + 1, ab->name().c_str(), tag, cd,
                              static_cast<int>(mp));
                if (ImGui::Button(label, slot_sz)) selected_ability = i;
                if (selected) ImGui::PopStyleColor(4);
                ImGui::PopID();
            }
        }
        ImGui::End();

        // Dummy Tuning 面板 (右侧).
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowW - kTunePanelW), 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kTunePanelW),
                                        static_cast<float>(kWindowH)));
        if (ImGui::Begin("Dummy Tuning", nullptr, kFixedFlags)) {
            ImGui::SliderFloat("HP",   &tune_max_health,  100.0f, 10000.0f, "%.0f");
            ImGui::SliderFloat("MR+",  &tune_mr_bonus,     -1.0f,    1.0f,  "%+.2f");
            ImGui::SliderFloat("Arm+", &tune_armor_bonus, -10.0f,   30.0f,  "%+.1f");
            ImGui::SliderFloat("AD",   &tune_attack_dmg,    0.0f,  200.0f,  "%.0f");
            ImGui::Spacing();
            const float btn_w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
            if (ImGui::Button("Apply", ImVec2(btn_w, 28.0f))) {
                apply_dummy_tune(true);
                show_toast("Dummy stats applied", Color{120, 230, 120, 255});
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(btn_w, 28.0f))) {
                tune_max_health  = 6000.0f;
                tune_attack_dmg  = 0.0f;
                tune_mr_bonus    = 0.0f;
                tune_armor_bonus = 0.0f;
                scene.set_dummy_override({});
                scene.rebuild_with_hero(scene.hero_index());
                selected_ability = -1;
                aim = AimMode::None;
                show_toast("Dummies reset", Color{200, 200, 80, 255});
            }
            ImGui::Spacing();
            ImGui::TextWrapped("Apply rebuilds dummies (caster persists, mid-cast aborts).");
        }
        ImGui::End();

        // --- 帮助文字 (技能栏上方一行) ---
        const char* aim_hint = "";
        switch (aim) {
            case AimMode::AwaitUnitTarget:    aim_hint = "  [aim: click a target]"; break;
            case AimMode::AwaitPointTarget:   aim_hint = "  [aim: click a point]"; break;
            case AimMode::AwaitConfirmNoTarget: aim_hint = "  [confirm: SPACE / number / left-click]"; break;
            default: break;
        }
        DrawText(TextFormat(
                     "1-4 / click to select   LMB cast   RMB / ESC cancel   "
                     "R reset   SPACE pause%s",
                     aim_hint),
                 kSidePanelW + 12, kWindowH - kAbilityBarH - 22,
                 14, Color{160, 160, 160, 255});

        // --- Toast (战场顶部居中) ---
        const double age = scene.world()->time() - toast_t0;
        if (age >= 0.0 && age < 1.5) {
            const float alpha = static_cast<float>(1.0 - age / 1.5);
            Color tc = toast_color;
            tc.a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
            const int tw = MeasureText(toast_text.c_str(), 22);
            const int tx = field_x0 + (cam.window_w - tw) / 2;
            const int ty = 38;
            DrawText(toast_text.c_str(), tx, ty, 22, tc);
        }

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
