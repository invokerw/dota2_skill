#include "panels.hpp"
#include "app_state.hpp"
#include "modifier_catalog.hpp"
#include "scene.hpp"
#include "ui_labels.hpp"

#include "dota/ability/ability.hpp"
#include "dota/ability/behavior.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/log/combat_log.hpp"
#include "dota/modifier/manager.hpp"
#include "dota/modifier/modifier.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <utility>

namespace dota::skill_tester {

namespace {

constexpr ImGuiWindowFlags kFixedFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse;

// Inspector "Unit -> Base" tab.
void draw_unit_base_tab(Scene& scene, AppState& app, Unit& selected) {
    const float half_w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    if (ImGui::Button("Caster", ImVec2(half_w, 26.0f))) {
        app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
    }
    ImGui::SameLine();
    if (ImGui::Button("Full", ImVec2(half_w, 26.0f))) {
        selected.set_health(selected.max_health());
        selected.set_mana(selected.max_mana());
    }
    if (ImGui::Button("Kill", ImVec2(half_w, 26.0f))) {
        selected.set_health(0.0);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revive", ImVec2(half_w, 26.0f))) {
        selected.set_health(selected.max_health());
        selected.set_mana(selected.max_mana());
    }

    ImGui::SeparatorText("Vitals");
    double hp = selected.health();
    if (drag_double("HP", hp, 5.0f, 0.0,
                    std::max(1.0, selected.max_health()), "%.0f")) {
        selected.set_health(hp);
    }
    double mana = selected.mana();
    if (drag_double("Mana", mana, 5.0f, 0.0,
                    std::max(1.0, selected.max_mana()), "%.0f")) {
        selected.set_mana(mana);
    }

    Vec2 pos = selected.position();
    bool pos_changed = false;
    pos_changed |= drag_double("Pos X", pos.x, 5.0f, -5000.0, 5000.0, "%.0f");
    pos_changed |= drag_double("Pos Y", pos.y, 5.0f, -5000.0, 5000.0, "%.0f");
    if (pos_changed) selected.set_position(pos);

    ImGui::SeparatorText("Base Stats");
    UnitStats stats = selected.stats();
    bool stats_changed = false;
    stats_changed |= drag_double("Max HP", stats.max_health, 10.0f, 1.0, 50000.0, "%.0f");
    stats_changed |= drag_double("Max Mana", stats.max_mana, 10.0f, 0.0, 10000.0, "%.0f");
    stats_changed |= drag_double("Attack", stats.attack_damage, 1.0f, 0.0, 1000.0, "%.0f");
    stats_changed |= drag_double("Armor", stats.base_armor, 0.2f, -50.0, 200.0, "%.1f");
    stats_changed |= drag_double("MR", stats.magic_resist, 0.01f, 0.0, 1.0, "%.2f");
    stats_changed |= drag_double("Move Speed", stats.move_speed, 5.0f, 0.0, 2000.0, "%.0f");
    stats_changed |= drag_double("Attack Range", stats.attack_range, 5.0f, 0.0, 2000.0, "%.0f");
    stats_changed |= drag_double("Hull", stats.hull_radius, 1.0f, 0.0, 200.0, "%.0f");
    if (stats_changed) selected.set_stats(stats);

    ImGui::SeparatorText("Effective");
    ImGui::Text("HP %.0f / %.0f", selected.health(), selected.max_health());
    ImGui::Text("Mana %.0f / %.0f", selected.mana(), selected.max_mana());
    ImGui::Text("Attack %.1f", selected.attack_damage());
    ImGui::Text("Armor %.1f", selected.armor());
    ImGui::Text("MR %.2f", selected.magic_resist());
    ImGui::Text("Move Speed %.0f", selected.move_speed());
    ImGui::Text("Regen %.1f / %.1f", selected.health_regen(), selected.mana_regen());
    ImGui::Text("Amp %.2f  Status Res %.2f",
                selected.spell_amp_pct(), selected.status_resist());
    ImGui::Text("Cast Range +%.0f", selected.cast_range_bonus());
    ImGui::TextUnformatted("States");
    draw_state_mask(selected.modifiers().aggregated_states());
}

// Inspector "Unit -> Modifiers" tab. 列出已挂修饰器 + 添加面板.
void draw_unit_modifiers_tab(Scene& scene, AppState& app, Unit& selected) {
    const auto& mods = selected.modifiers().all();
    std::size_t remove_index = mods.size();
    // 表格行只放 [Name(toggle) | Time | Stacks | Del], 详情放到表外按全 panel 宽渲染
    // 避免在窄 Name 列里被裁切.
    std::vector<bool> open_flags(mods.size(), false);
    if (ImGui::BeginTable(
            "##mod_table", 4,
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Stacks", ImGuiTableColumnFlags_WidthFixed, 58.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < mods.size(); ++i) {
            Modifier* mod = mods[i].get();
            if (!mod) continue;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableSetColumnIndex(0);
            open_flags[i] = ImGui::TreeNodeEx(
                "##mod_details",
                ImGuiTreeNodeFlags_NoTreePushOnOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth,
                "%s", mod->name().c_str());

            ImGui::TableSetColumnIndex(1);
            double duration = mod->permanent() ? -1.0 : mod->duration_remaining();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (drag_double("##duration", duration, 0.05f, -1.0, 600.0, "%.1f")) {
                mod->refresh(duration < 0.0 ? -1.0 : duration);
            }

            ImGui::TableSetColumnIndex(2);
            int stacks = mod->stack_count();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragInt("##stacks", &stacks, 0.1f, 1, 999)) {
                mod->set_stack_count(std::max(1, stacks));
            }

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("Del")) remove_index = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // 展开行的详情. 在表外渲染, 走整个 panel 宽度.
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (!open_flags[i]) continue;
        Modifier* mod = mods[i].get();
        if (!mod) continue;
        ImGui::PushID(static_cast<int>(i));
        ImGui::Indent();
        ImGui::SeparatorText(mod->name().c_str());
        ImGui::Text("Purgable %s  Dispellable %s  %s",
                    mod->is_purgable() ? "yes" : "no",
                    mod->is_dispellable() ? "yes" : "no",
                    mod->is_debuff() ? "debuff" : "buff");
        ImGui::TextUnformatted("States");
        draw_state_mask(mod->declared_states());
        const auto props = mod->declared_properties();
        ImGui::TextUnformatted("Properties");
        if (props.empty()) {
            ImGui::TextDisabled("(none)");
        } else {
            for (const auto& p : props) {
                ImGui::BulletText("%s %+0.2f",
                                  property_label(p.property), p.value);
            }
        }
        ImGui::Unindent();
        ImGui::PopID();
    }

    if (remove_index < mods.size()) {
        selected.modifiers().remove_at(remove_index);
    }

    ImGui::SeparatorText("Add");
    static int add_mod_idx = 0;
    static int last_add_mod_idx = -1;
    static ModifierParamBag add_mod_params;
    const auto modifier_catalog = build_modifier_catalog(scene);
    if (add_mod_idx >= static_cast<int>(modifier_catalog.size())) {
        add_mod_idx = 0;
        last_add_mod_idx = -1;
    }

    if (modifier_catalog.empty()) {
        ImGui::TextDisabled("(no registered modifiers)");
        return;
    }
    if (ImGui::BeginCombo("Name", modifier_catalog[add_mod_idx].label.c_str())) {
        for (std::size_t i = 0; i < modifier_catalog.size(); ++i) {
            const bool is_selected = add_mod_idx == static_cast<int>(i);
            if (ImGui::Selectable(modifier_catalog[i].label.c_str(), is_selected)) {
                add_mod_idx = static_cast<int>(i);
            }
            if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const ModifierAddSpec& spec = modifier_catalog[add_mod_idx];
    if (last_add_mod_idx != add_mod_idx || add_mod_params.empty()) {
        reset_modifier_param_values(spec, add_mod_params);
        last_add_mod_idx = add_mod_idx;
    }
    draw_modifier_param_controls(spec, add_mod_params);

    if (ImGui::Button("Add Modifier", ImVec2(-FLT_MIN, 28.0f))) {
        std::unique_ptr<Modifier> mod = spec.create(selected, add_mod_params);
        if (mod) {
            selected.modifiers().attach(std::move(mod));
            app.show_toast("Modifier added", Color{120, 230, 120, 255},
                           scene.world()->time());
        } else {
            app.show_toast("Modifier unavailable", Color{220, 100, 100, 255},
                           scene.world()->time());
        }
    }
}

// Inspector "Unit -> Ability" tab.
void draw_unit_ability_tab(Scene& scene, AppState& app, Unit& selected) {
    const auto& abilities = selected.abilities().all();
    std::size_t remove_ability_index = abilities.size();
    if (abilities.empty()) {
        ImGui::TextDisabled("(no abilities)");
    }
    for (std::size_t i = 0; i < abilities.size(); ++i) {
        Ability* ab = abilities[i].get();
        if (!ab) continue;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode("##ability_details", "%s", ab->name().c_str())) {
            ImGui::Text("Behavior %s  Phase %s",
                        behavior_label(ab->behavior()),
                        phase_label(ab->phase()));
            ImGui::Text("CD remaining %.1f", ab->cooldown_remaining());

            int level = ab->level();
            if (ImGui::DragInt("Level", &level, 0.1f, 1, 30)) {
                ab->set_level(level);
            }
            double cast_range = ab->cast_range();
            if (drag_double("Cast Range", cast_range, 5.0f, 0.0, 5000.0, "%.0f")) {
                ab->set_cast_range(cast_range);
            }
            double cast_point = ab->cast_point();
            if (drag_double("Cast Point", cast_point, 0.01f, 0.0, 10.0, "%.2f")) {
                ab->set_cast_point(cast_point);
            }
            double backswing = ab->backswing();
            if (drag_double("Backswing", backswing, 0.01f, 0.0, 10.0, "%.2f")) {
                ab->set_backswing(backswing);
            }
            double channel_time = ab->channel_time();
            if (drag_double("Channel Time", channel_time, 0.01f, 0.0, 30.0, "%.2f")) {
                ab->set_channel_time(channel_time);
            }
            double cooldown = ab->cooldown_for_level();
            if (drag_double("Cooldown", cooldown, 0.1f, 0.0, 300.0, "%.1f")) {
                ab->set_cooldown_levels({cooldown});
            }
            double mana_cost = ab->mana_cost_for_level();
            if (drag_double("Mana Cost", mana_cost, 1.0f, 0.0, 2000.0, "%.0f")) {
                ab->set_mana_cost_levels({mana_cost});
            }

            AbilitySpecial special = ab->ability_special();
            bool special_changed = false;
            if (!special.empty() && ImGui::TreeNode("Specials")) {
                for (auto& [key, value] : special) {
                    const std::size_t value_count = value.is_int
                        ? value.ints.size() : value.floats.size();
                    if (value_count == 0) continue;
                    const std::size_t idx = static_cast<std::size_t>(
                        std::clamp(ab->level() - 1, 0,
                                   static_cast<int>(value_count) - 1));
                    double v = value.get_float(ab->level());
                    if (drag_double(key.c_str(), v,
                                    value.is_int ? 1.0f : 0.05f,
                                    -10000.0, 10000.0,
                                    value.is_int ? "%.0f" : "%.2f")) {
                        if (value.is_int) {
                            const long iv = static_cast<long>(std::llround(v));
                            value.ints[idx] = iv;
                            if (idx < value.floats.size()) {
                                value.floats[idx] = static_cast<double>(iv);
                            }
                        } else {
                            value.floats[idx] = v;
                        }
                        special_changed = true;
                    }
                }
                ImGui::TreePop();
            }
            if (special_changed) {
                ab->set_ability_special(std::move(special));
            }

            if (ImGui::Button("Remove Ability", ImVec2(-FLT_MIN, 26.0f))) {
                remove_ability_index = i;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove_ability_index < abilities.size()) {
        scene.remove_ability_at(selected, remove_ability_index);
        app.selected_ability = -1;
        app.show_toast("Ability removed", Color{200, 200, 80, 255},
                       scene.world()->time());
    }

    ImGui::SeparatorText("Add");
    const auto& choices = scene.ability_choices();
    static int add_ability_idx = 0;
    if (add_ability_idx >= static_cast<int>(choices.size())) {
        add_ability_idx = 0;
    }
    if (choices.empty()) {
        ImGui::TextDisabled("(no registered abilities)");
        return;
    }
    if (ImGui::BeginCombo("Name", choices[add_ability_idx].label.c_str())) {
        for (std::size_t i = 0; i < choices.size(); ++i) {
            const bool selected_item = add_ability_idx == static_cast<int>(i);
            if (ImGui::Selectable(choices[i].label.c_str(), selected_item)) {
                add_ability_idx = static_cast<int>(i);
            }
            if (selected_item) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Add Ability", ImVec2(-FLT_MIN, 28.0f))) {
        Ability* added = scene.add_ability_to(selected, choices[add_ability_idx].name);
        if (added) {
            app.selected_ability = -1;
            app.show_toast("Ability added", Color{120, 230, 120, 255},
                           scene.world()->time());
        } else {
            app.show_toast("Ability add failed", Color{220, 100, 100, 255},
                           scene.world()->time());
        }
    }
}

// Inspector "Scenario" tab: dummy 调参 + AI 模式选择.
void draw_scenario_tab(Scene& scene, AppState& app) {
    ImGui::SeparatorText("Dummy Stats");
    ImGui::SliderFloat("HP",   &app.tune_max_health,  100.0f, 10000.0f, "%.0f");
    ImGui::SliderFloat("MR+",  &app.tune_mr_bonus,     -1.0f,    1.0f,  "%+.2f");
    ImGui::SliderFloat("Arm+", &app.tune_armor_bonus, -10.0f,   30.0f,  "%+.1f");
    ImGui::SliderFloat("AD",   &app.tune_attack_dmg,    0.0f,  200.0f,  "%.0f");
    ImGui::Spacing();
    const float btn_w = (ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f;
    if (ImGui::Button("Apply", ImVec2(btn_w, 28.0f))) {
        DummyOverride o;
        o.active             = true;
        o.max_health         = std::max(1.0f, app.tune_max_health);
        o.attack_damage      = std::max(0.0f, app.tune_attack_dmg);
        o.magic_resist_bonus = app.tune_mr_bonus;
        o.base_armor_bonus   = app.tune_armor_bonus;
        scene.set_dummy_override(o);
        scene.rebuild_with_hero(scene.hero_index());
        app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
        app.selected_ability = -1;
        app.aim = AimMode::None;
        app.show_toast("Dummy stats applied", Color{120, 230, 120, 255},
                       scene.world()->time());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset", ImVec2(btn_w, 28.0f))) {
        app.tune_max_health  = 6000.0f;
        app.tune_attack_dmg  = 0.0f;
        app.tune_mr_bonus    = 0.0f;
        app.tune_armor_bonus = 0.0f;
        scene.set_dummy_override({});
        scene.rebuild_with_hero(scene.hero_index());
        app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
        app.selected_ability = -1;
        app.aim = AimMode::None;
        app.show_toast("Dummies reset", Color{200, 200, 80, 255},
                       scene.world()->time());
    }

    ImGui::SeparatorText("Dummy AI");
    const char* ai_items[] = {"Idle", "Strafe", "Charge"};
    const int prev_ai = app.dummy_ai_idx;
    ImGui::Combo("Mode", &app.dummy_ai_idx, ai_items, IM_ARRAYSIZE(ai_items));
    if (prev_ai != app.dummy_ai_idx && app.dummy_ai_idx == 0) {
        // Idle: 清掉所有 dummy 的 move 指令
        for (Unit* d : scene.dummies()) {
            if (d) d->stop_move();
        }
    }
}

} // namespace

float draw_main_menu_bar(AppState& app) {
    float h = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Window")) {
            ImGui::MenuItem("Combat Log", "L", &app.show_combat_log);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("dota2_skill -- skill tester");
            ImGui::TextDisabled("1-4 / click 选技能, A 普攻, LMB 释放, RMB 走 / 取消.");
            ImGui::TextDisabled("Shift 队尾追加, S 全停, R 重置, SPACE 暂停, L 日志.");
            ImGui::EndMenu();
        }
        h = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }
    return h;
}

void draw_heroes_panel(const tools::HeroCatalog& catalog, Scene& scene,
                       AppState& app, float menu_h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, menu_h));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kSidePanelW),
                                    static_cast<float>(kWindowH - kAbilityBarH) - menu_h));
    if (ImGui::Begin("Heroes", nullptr, kFixedFlags)) {
        const int prev_active = app.hero_active;
        for (std::size_t i = 0; i < catalog.heroes().size(); ++i) {
            const bool sel = (static_cast<int>(i) == app.hero_active);
            if (ImGui::Selectable(catalog.heroes()[i].yaml_name.c_str(), sel)) {
                app.hero_active = static_cast<int>(i);
            }
        }
        if (app.hero_active >= 0 && app.hero_active != prev_active &&
            static_cast<std::size_t>(app.hero_active) != scene.hero_index()) {
            scene.rebuild_with_hero(static_cast<std::size_t>(app.hero_active));
            app.selected_unit_id = scene.caster() ? scene.caster()->id() : kInvalidEntityId;
            app.selected_ability = -1;
            app.paused = false;
        }
    }
    ImGui::End();
}

void draw_abilities_panel(Scene& scene, AppState& app) {
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
        const float slot_gap = ImGui::GetStyle().ItemSpacing.x;
        const float slot_w = slots > 0
            ? (ImGui::GetContentRegionAvail().x - slot_gap * (slots - 1)) / slots
            : 200.0f;
        const ImVec2 slot_sz(std::max(120.0f, slot_w), 64.0f);
        for (int i = 0; i < slots; ++i) {
            if (i > 0) ImGui::SameLine();
            Ability* ab = scene.caster_abilities()[i];
            const bool is_orb = ab->is_orb();
            // 纯被动 (Passive 且非 orb): 只在槽里展示, 不响应点击.
            const bool is_pure_passive = ab->is_passive() && !is_orb;
            // 法球槽 = autocast 状态; 主动槽 = 选中态; 纯被动无高亮.
            const bool highlight = is_orb ? ab->autocast_on()
                                 : is_pure_passive ? false
                                                   : (app.selected_ability == i);
            ImGui::PushID(i);
            int style_pops = 0;
            if (highlight) {
                if (is_orb) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.30f, 0.60f, 0.95f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.40f, 0.70f, 1.00f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.20f, 0.50f, 0.85f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.95f, 0.78f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(1.0f, 0.85f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.85f, 0.7f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,0,0,1));
                }
                style_pops = 4;
            } else if (is_pure_passive) {
                // 灰色 + 暗化文字, 视觉上明确"不可点".
                ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
                style_pops = 4;
            }
            const double cd = ab->cooldown_remaining();
            const double mp = ab->mana_cost_for_level();
            const char* tag = is_orb ? "ORB"
                            : is_pure_passive ? "PAS"
                                              : behavior_label(ab->behavior());
            char label[128];
            if (is_orb) {
                std::snprintf(label, sizeof(label),
                              "[%d] %s\n%s  AC %s  MP %d",
                              i + 1, ab->name().c_str(), tag,
                              ab->autocast_on() ? "ON" : "OFF",
                              static_cast<int>(mp));
            } else if (is_pure_passive) {
                std::snprintf(label, sizeof(label),
                              "%s\n%s  Lv %d",
                              ab->name().c_str(), tag, ab->level());
            } else {
                std::snprintf(label, sizeof(label),
                              "[%d] %s\n%s  CD %.1fs  MP %d",
                              i + 1, ab->name().c_str(), tag, cd,
                              static_cast<int>(mp));
            }
            if (is_pure_passive) {
                // 用 Button 保持视觉一致, 但忽略点击.
                ImGui::Button(label, slot_sz);
            } else if (ImGui::Button(label, slot_sz)) {
                if (is_orb) {
                    ab->set_autocast_on(!ab->autocast_on());
                } else {
                    app.selected_ability = i;
                }
            }
            if (style_pops > 0) ImGui::PopStyleColor(style_pops);
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void draw_inspector_panel(Scene& scene, AppState& app, float menu_h) {
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowW - kTunePanelW), menu_h));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(kTunePanelW),
                                    static_cast<float>(kWindowH) - menu_h));
    if (ImGui::Begin("Inspector", nullptr, kFixedFlags)) {
        Unit* selected = scene.find_unit(app.selected_unit_id);
        if (!selected && scene.caster()) {
            app.selected_unit_id = scene.caster()->id();
            selected = scene.caster();
        }

        if (ImGui::BeginTabBar("##inspector_tabs")) {
            if (ImGui::BeginTabItem("Unit")) {
                if (!selected) {
                    ImGui::TextDisabled("(no unit selected)");
                } else {
                    ImGui::TextUnformatted(selected->name().c_str());
                    ImGui::Text("id %u  %s  %s",
                                selected->id(), team_label(selected->team()),
                                selected->alive() ? "alive" : "dead");
                    ImGui::Spacing();
                    if (ImGui::BeginTabBar("##unit_modules")) {
                        if (ImGui::BeginTabItem("Base")) {
                            draw_unit_base_tab(scene, app, *selected);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Modifiers")) {
                            draw_unit_modifiers_tab(scene, app, *selected);
                            ImGui::EndTabItem();
                        }
                        if (ImGui::BeginTabItem("Ability")) {
                            draw_unit_ability_tab(scene, app, *selected);
                            ImGui::EndTabItem();
                        }
                        ImGui::EndTabBar();
                    }
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Scenario")) {
                draw_scenario_tab(scene, app);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

namespace {

// 类别 -> AppState 复选框的映射 + 行颜色. 颜色仿 dota 客户端 combat log:
//   红=伤害, 绿=治疗, 黄=modifier+/橙=modifier-, 蓝=施法, 灰=普攻, 暗红=死亡.
//   类型 tag 与该行内的 [名字] 共用同一颜色, 其他文字保持默认色.
struct LogKindStyle {
    bool       AppState::*toggle;
    ImVec4     color;
    const char* label;
};

const LogKindStyle& style_for(CombatLogKind k) {
    static const LogKindStyle damage   {&AppState::log_show_damage,
        ImVec4(1.00f, 0.55f, 0.55f, 1.0f), "DMG"};
    static const LogKindStyle heal     {&AppState::log_show_heal,
        ImVec4(0.55f, 0.95f, 0.55f, 1.0f), "HEAL"};
    static const LogKindStyle mod_add  {&AppState::log_show_modifier,
        ImVec4(0.95f, 0.85f, 0.40f, 1.0f), "MOD+"};
    static const LogKindStyle mod_rem  {&AppState::log_show_modifier,
        ImVec4(0.95f, 0.65f, 0.30f, 1.0f), "MOD-"};
    static const LogKindStyle cast     {&AppState::log_show_cast,
        ImVec4(0.55f, 0.80f, 1.00f, 1.0f), "CAST"};
    static const LogKindStyle attack   {&AppState::log_show_attack,
        ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "ATK"};
    static const LogKindStyle death    {&AppState::log_show_death,
        ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "DIE"};
    switch (k) {
        case CombatLogKind::Damage:              return damage;
        case CombatLogKind::Heal:                return heal;
        case CombatLogKind::ModifierAdded:       return mod_add;
        case CombatLogKind::ModifierRemoved:     return mod_rem;
        case CombatLogKind::AbilityCastStarted:
        case CombatLogKind::AbilityCastFinished: return cast;
        case CombatLogKind::AttackLanded:        return attack;
        case CombatLogKind::UnitDied:            return death;
    }
    return damage;
}

bool kind_visible(const AppState& app, CombatLogKind k) {
    return app.*(style_for(k).toggle);
}

// 名字回退: 名字为空时显示 #id, 全空时显示 ?. 跟 CombatLog::format 内部规则一致.
std::string entity_label(const std::string& name, EntityId id) {
    if (!name.empty()) return name;
    if (id == kInvalidEntityId) return "?";
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%u", id);
    return buf;
}

const char* dtype_text(DamageType t) {
    switch (t) {
        case DamageType::Physical: return "physical";
        case DamageType::Magical:  return "magical";
        case DamageType::Pure:     return "pure";
    }
    return "?";
}

// SameLine 紧贴 (无额外间距). 中间靠手动空格控制可读性.
void same_line_tight() { ImGui::SameLine(0.0f, 0.0f); }

// 输出 "[text]" 用 col 染色.
void draw_bracketed(const ImVec4& col, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::Text("[%s]", text);
    ImGui::PopStyleColor();
}

void draw_plain(const char* text) {
    ImGui::TextUnformatted(text);
}

// 渲染一行 entry 的"内容段" (时间和类型 tag 由调用方画). col 是当前 kind 颜色,
// 用来给 [单位名] / [modifier 名] / [ability 名] 染色.
void draw_entry_content(const CombatLogEntry& e, const ImVec4& col) {
    const std::string s = entity_label(e.source_name, e.source);
    const std::string t = entity_label(e.target_name, e.target);
    char num[64];
    switch (e.kind) {
        case CombatLogKind::Damage:
            draw_bracketed(col, s.c_str()); same_line_tight();
            draw_plain(" hits ");           same_line_tight();
            draw_bracketed(col, t.c_str()); same_line_tight();
            std::snprintf(num, sizeof(num), " for %.0f %s (pre %.0f)",
                          e.amount, dtype_text(e.dtype), e.amount_pre);
            draw_plain(num);
            break;
        case CombatLogKind::Heal:
            draw_bracketed(col, s.c_str()); same_line_tight();
            draw_plain(" heals ");          same_line_tight();
            draw_bracketed(col, t.c_str()); same_line_tight();
            std::snprintf(num, sizeof(num), " for %.0f", e.amount);
            draw_plain(num);
            break;
        case CombatLogKind::ModifierAdded:
            draw_bracketed(col, t.c_str());          same_line_tight();
            draw_plain(" gains ");                   same_line_tight();
            draw_bracketed(col, e.name.c_str());     same_line_tight();
            if (e.flag) {
                std::snprintf(num, sizeof(num), " (permanent, %d stack%s)",
                              e.stacks, e.stacks == 1 ? "" : "s");
            } else {
                std::snprintf(num, sizeof(num), " (%.1fs, %d stack%s)",
                              e.amount, e.stacks, e.stacks == 1 ? "" : "s");
            }
            draw_plain(num);
            break;
        case CombatLogKind::ModifierRemoved:
            draw_bracketed(col, t.c_str());      same_line_tight();
            draw_plain(" loses ");               same_line_tight();
            draw_bracketed(col, e.name.c_str());
            break;
        case CombatLogKind::AbilityCastStarted:
            draw_bracketed(col, s.c_str());      same_line_tight();
            draw_plain(" casts ");               same_line_tight();
            draw_bracketed(col, e.name.c_str());
            if (e.target != kInvalidEntityId) {
                same_line_tight();
                draw_plain(" on ");              same_line_tight();
                draw_bracketed(col, t.c_str());
            }
            break;
        case CombatLogKind::AbilityCastFinished:
            draw_bracketed(col, s.c_str());      same_line_tight();
            draw_plain(e.flag ? " interrupted " : " finished ");
            same_line_tight();
            draw_bracketed(col, e.name.c_str());
            break;
        case CombatLogKind::AttackLanded:
            draw_bracketed(col, s.c_str());      same_line_tight();
            draw_plain(" attacks ");             same_line_tight();
            draw_bracketed(col, t.c_str());      same_line_tight();
            if (e.flag) {
                draw_plain(" -- missed");
            } else {
                std::snprintf(num, sizeof(num), " for %.0f", e.amount);
                draw_plain(num);
            }
            break;
        case CombatLogKind::UnitDied:
            draw_bracketed(col, t.c_str());      same_line_tight();
            draw_plain(" died");
            if (e.source != kInvalidEntityId) {
                same_line_tight();
                draw_plain(" (killer: ");        same_line_tight();
                draw_bracketed(col, s.c_str());  same_line_tight();
                draw_plain(")");
            }
            break;
    }
}

} // namespace

void draw_combat_log_window(Scene& scene, AppState& app) {
    if (!app.show_combat_log) return;
    CombatLog* log = scene.combat_log();
    if (!log) return;

    // 默认右下角弹出, 之后由用户拖拽 + imgui.ini 记位.
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kWindowW - kTunePanelW - 460),
                                   static_cast<float>(kWindowH - kAbilityBarH - 320)),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 300.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Combat Log", &app.show_combat_log)) {
        ImGui::End();
        return;
    }

    // 顶部工具行: 类别 toggle + 自动滚动 + 清空.
    ImGui::Checkbox("DMG",  &app.log_show_damage);   ImGui::SameLine();
    ImGui::Checkbox("HEAL", &app.log_show_heal);     ImGui::SameLine();
    ImGui::Checkbox("MOD",  &app.log_show_modifier); ImGui::SameLine();
    ImGui::Checkbox("CAST", &app.log_show_cast);     ImGui::SameLine();
    ImGui::Checkbox("ATK",  &app.log_show_attack);   ImGui::SameLine();
    ImGui::Checkbox("DIE",  &app.log_show_death);
    ImGui::Checkbox("Autoscroll", &app.log_autoscroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) log->clear();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu / %zu)", log->size(), log->capacity());
    ImGui::Separator();

    // 滚动区. 行格式: [时间] [类型 tag] [内容]. 类型 tag 与内容里的 [name]
    // 共用 kind 颜色, 时间用暗灰色.
    if (ImGui::BeginChild("##log_scroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto& entries = log->entries();
        std::vector<const CombatLogEntry*> visible;
        visible.reserve(entries.size());
        for (const auto& e : entries) {
            if (kind_visible(app, e.kind)) visible.push_back(&e);
        }
        const ImVec4 time_col(0.55f, 0.55f, 0.55f, 1.0f);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const CombatLogEntry& e = *visible[i];
                const LogKindStyle& st = style_for(e.kind);
                ImGui::PushID(i);

                // 让整行点击可选中单位: 用一个跨行的 InvisibleButton 占位,
                // 然后 SameLine 退回起点叠加文字.
                const ImVec2 row_min = ImGui::GetCursorScreenPos();
                const float  row_w   = ImGui::GetContentRegionAvail().x;
                const float  row_h   = ImGui::GetTextLineHeightWithSpacing();
                ImGui::InvisibleButton("##row", ImVec2(row_w, row_h));
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    EntityId pick = e.source != kInvalidEntityId ? e.source : e.target;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        pick = e.target != kInvalidEntityId ? e.target : e.source;
                    }
                    if (pick != kInvalidEntityId) app.selected_unit_id = pick;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        row_min,
                        ImVec2(row_min.x + row_w, row_min.y + row_h),
                        ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                }

                // 在 InvisibleButton 顶上绘制文字: 时间 -> 类型 -> 内容.
                ImGui::SetCursorScreenPos(row_min);
                char tbuf[16];
                std::snprintf(tbuf, sizeof(tbuf), "%6.2fs", e.time);
                ImGui::PushStyleColor(ImGuiCol_Text, time_col);
                ImGui::TextUnformatted(tbuf);
                ImGui::PopStyleColor();

                ImGui::SameLine(80.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, st.color);
                ImGui::TextUnformatted(st.label);
                ImGui::PopStyleColor();

                ImGui::SameLine(130.0f);
                draw_entry_content(e, st.color);

                ImGui::PopID();
            }
        }
        clipper.End();

        // 自动滚到底, 但用户手动上滚后停止.
        if (app.log_autoscroll &&
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace dota::skill_tester
