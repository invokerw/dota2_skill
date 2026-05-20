#include "input.hpp"
#include "app_state.hpp"
#include "scene.hpp"
#include "ui_labels.hpp"

#include "dota/ability/ability.hpp"
#include "dota/core/order.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"

#include "imgui.h"
#include "raylib.h"

#include <algorithm>
#include <limits>
#include <variant>

namespace dota::skill_tester {

namespace {

constexpr int kAbilitySlotMax = 4;

double dist2(Vec2 a, Vec2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// 把 caster_abilities_ 里的 Ability* 反查为 caster->abilities().all() 中的下标.
// OrderCast* variant 用的是后者的 index (含 passive 槽).
int ability_index_of(Unit* caster, Ability* ab) {
    if (!caster || !ab) return -1;
    const auto& all = caster->abilities().all();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].get() == ab) return static_cast<int>(i);
    }
    return -1;
}

// 通过指令队列发起施法. 距离不够时单位会自动靠近, 而非弹 OutOfRange toast.
// 仍然先调 can_cast 一次, 把"魔不够 / 已死 / cooldown / silence"等本地可判
// 定的失败抛 toast -- 这些不该派生跟随移动. queue=true 时追加到队尾 (Shift).
void try_cast(Scene& scene, AppState& app, Ability* ab,
              const CastTarget& tgt, bool queue) {
    // Shift 追加时不做 can_cast 检查 -- 队尾命令将来才执行, mana / cooldown
    // 现在不达标不代表执行时也不达标.
    if (!queue) {
        const CastError pre = ab->can_cast(tgt);
        // OutOfRange / InvalidTarget (纯距离派生) 留给 OrderQueue 处理.
        if (pre != CastError::None && pre != CastError::OutOfRange) {
            app.show_toast(cast_error_text(pre), Color{220, 100, 100, 255},
                           scene.world()->time());
            app.reset_aim();
            return;
        }
    }
    const int idx = ability_index_of(scene.caster(), ab);
    if (idx < 0) { app.reset_aim(); return; }
    Unit* caster = scene.caster();
    if (!caster) { app.reset_aim(); return; }
    if (tgt.unit) {
        caster->issue_order(OrderCastTarget{idx, tgt.unit->id()}, queue);
    } else if (tgt.has_point) {
        caster->issue_order(OrderCastPoint{idx, tgt.point}, queue);
    } else {
        caster->issue_order(OrderCastNoTarget{idx}, queue);
    }
    app.show_toast(std::string(queue ? "Queue: " : "Cast: ") + ab->name(),
                   Color{120, 230, 120, 255}, scene.world()->time());
    app.reset_aim();
}

bool queue_modifier_held() {
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

} // namespace

InputContext compute_input_context(const visual::ViewCamera& cam,
                                   const FieldRect& field,
                                   Scene& scene) {
    InputContext ctx;
    const ImGuiIO& io = ImGui::GetIO();
    ctx.gui_wants_mouse    = io.WantCaptureMouse;
    ctx.gui_wants_keyboard = io.WantCaptureKeyboard;

    const Vector2 ms = GetMousePosition();
    ctx.mouse_in_field =
        !ctx.gui_wants_mouse &&
        ms.x >= field.x0 && ms.x < field.x1 &&
        ms.y >= field.y0 && ms.y < field.y1;
    ctx.mouse_world = cam.to_world(ms);

    // 拾取最近的活着的 dummy. 拾取半径 = 单位 hull_radius, 但保证最小屏幕半径
    // 不低于 kMinUnitRadiusPx 等价的世界距离, 避免 zoom 过大时点不到.
    if (ctx.mouse_in_field) {
        const double min_world_r = visual::kMinUnitRadiusPx / cam.zoom;
        for (Unit* u : scene.dummies()) {
            if (!u || !u->alive()) continue;
            const double pick_r = std::max(u->hull_radius(), min_world_r);
            if (dist2(u->position(), ctx.mouse_world) <= pick_r * pick_r) {
                ctx.hover_unit = u;
                break;
            }
        }
        double best_d2 = std::numeric_limits<double>::max();
        for (Unit* u : scene.units()) {
            if (!u) continue;
            const double pick_r = std::max(u->hull_radius(), min_world_r);
            const double d2 = dist2(u->position(), ctx.mouse_world);
            if (d2 <= pick_r * pick_r && d2 < best_d2) {
                best_d2 = d2;
                ctx.inspect_hover_unit = u;
            }
        }
    }
    return ctx;
}

void process_keyboard(Scene& scene, AppState& app, InputContext& ctx) {
    // ESC: 优先取消瞄准, 没在瞄准时退出窗口.
    if (!ctx.gui_wants_keyboard && IsKeyPressed(KEY_ESCAPE)) {
        if (app.aim != AimMode::None) {
            app.reset_aim();
        } else {
            ctx.quit_requested = true;
        }
    }

    if (!ctx.gui_wants_keyboard && app.aim == AimMode::None && IsKeyPressed(KEY_SPACE)) {
        app.paused = !app.paused;
    }
    if (!ctx.gui_wants_keyboard && IsKeyPressed(KEY_R)) {
        scene.rebuild_with_hero(scene.hero_index());
        app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
        app.selected_ability = -1;
        app.reset_aim();
        app.paused = false;
    }
    // S: 全停 -- 清空 caster 的指令队列(Dota 风格 stop). 不打断当前已经
    // 进入 cast point 的 ability; 仅清掉队列里待派发项.
    if (!ctx.gui_wants_keyboard && IsKeyPressed(KEY_S) &&
        scene.caster() && scene.caster()->alive()) {
        scene.caster()->issue_order(OrderStop{});
        app.reset_aim();
    }

    const bool queue_mod = queue_modifier_held();

    // 数字键 1-4 选中技能槽: 第一次按 = 选并进入瞄准; 已在该槽瞄准时, 对
    // NoTarget 触发释放, 其他类型保持选中 (无变化).
    const int key_slots[] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR};
    const int slot_count = std::min<int>(
        kAbilitySlotMax, static_cast<int>(scene.caster_abilities().size()));
    for (int i = 0; i < slot_count && !ctx.gui_wants_keyboard; ++i) {
        if (!IsKeyPressed(key_slots[i])) continue;
        Ability* ab = scene.caster_abilities()[i];
        const AimMode want = aim_for_behavior(ab->behavior());
        if (app.selected_ability == i && app.aim == AimMode::AwaitConfirmNoTarget) {
            CastTarget tgt;
            try_cast(scene, app, ab, tgt, queue_mod);
            app.selected_ability = -1;
        } else {
            app.selected_ability = i;
            app.aim = want;
        }
    }

    // SPACE 在 NoTarget 待确认时也可释放
    if (!ctx.gui_wants_keyboard &&
        app.aim == AimMode::AwaitConfirmNoTarget && IsKeyPressed(KEY_SPACE) &&
        app.selected_ability >= 0 && app.selected_ability < slot_count) {
        CastTarget tgt;
        try_cast(scene, app, scene.caster_abilities()[app.selected_ability],
                 tgt, queue_mod);
        app.selected_ability = -1;
    }
}

void process_mouse(Scene& scene, AppState& app, const InputContext& ctx) {
    const bool queue_mod = queue_modifier_held();
    const int slot_count = std::min<int>(
        kAbilitySlotMax, static_cast<int>(scene.caster_abilities().size()));

    // 右键取消瞄准
    if (!ctx.gui_wants_mouse && app.aim != AimMode::None &&
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        app.reset_aim();
    }

    // 非 aim 模式下 RMB 命令 caster 走位 (Shift 追加).
    if (ctx.mouse_in_field && app.aim == AimMode::None &&
        IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) &&
        scene.caster() && scene.caster()->alive()) {
        scene.caster()->issue_order(OrderMoveToPoint{ctx.mouse_world}, queue_mod);
    }

    // 非 aim 模式下 LMB 选中任意单位, 右侧 Inspector 显示其状态.
    if (ctx.mouse_in_field && app.aim == AimMode::None &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && ctx.inspect_hover_unit) {
        app.selected_unit_id = ctx.inspect_hover_unit->id();
    }

    // 左键 -- 仅在战场内有效, 且当前选了技能槽
    if (ctx.mouse_in_field && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        app.selected_ability >= 0 && app.selected_ability < slot_count) {
        Ability* ab = scene.caster_abilities()[app.selected_ability];
        CastTarget tgt;
        bool fire = false;
        if (app.aim == AimMode::AwaitUnitTarget && ctx.hover_unit) {
            tgt.unit = ctx.hover_unit;
            fire = true;
        } else if (app.aim == AimMode::AwaitPointTarget) {
            tgt.point = ctx.mouse_world;
            tgt.has_point = true;
            fire = true;
        } else if (app.aim == AimMode::AwaitConfirmNoTarget) {
            fire = true;
        }
        if (fire) {
            try_cast(scene, app, ab, tgt, queue_mod);
            app.selected_ability = -1;
        }
    }
}

void tick_dummy_ai(Scene& scene, AppState& app, double dt) {
    if (dt <= 0.0 || app.dummy_ai_idx == 0) return;
    enum class DummyAI { Idle = 0, Strafe = 1, Charge = 2 };
    const auto& ds = scene.dummies();
    if (app.strafe_dir.size() < ds.size()) app.strafe_dir.resize(ds.size(), 1);
    const auto mode = static_cast<DummyAI>(app.dummy_ai_idx);
    for (std::size_t i = 0; i < ds.size(); ++i) {
        Unit* d = ds[i];
        if (!d || !d->alive()) continue;
        if (mode == DummyAI::Charge) {
            if (!scene.caster() || !scene.caster()->alive()) continue;
            // 已经在攻击同一目标 -> 不重复入队, 避免 issue_order 反复清队.
            const auto* cur = d->current_order();
            bool already = false;
            if (cur) {
                if (auto* at = std::get_if<OrderAttackTarget>(cur)) {
                    already = (at->target == scene.caster()->id());
                }
            }
            if (!already) {
                d->issue_order(OrderAttackTarget{scene.caster()->id()});
            }
        } else if (mode == DummyAI::Strafe) {
            // 在 (pos.x, pos.y +- 200) 之间往返. 到达后 (无 target) 切方向.
            if (!d->move_target().has_value()) {
                const Vec2 p = d->position();
                d->issue_move({p.x, p.y + 200.0 * app.strafe_dir[i]});
                app.strafe_dir[i] = -app.strafe_dir[i];
            }
        }
    }
}

} // namespace dota::skill_tester
