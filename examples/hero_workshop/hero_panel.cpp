#include "hero_panel.hpp"

#include "dota/tools/ability_ops.hpp"

#include "imgui.h"

#include <cfloat>
#include <string>
#include <vector>

namespace dota::hero_workshop {

namespace {

bool drag_double(const char* key, YAML::Node node,
                 float speed, double min_v, double max_v, const char* fmt) {
    double v = node[key] ? node[key].as<double>() : 0.0;
    float fv = static_cast<float>(v);
    if (ImGui::DragFloat(key, &fv, speed,
                          static_cast<float>(min_v),
                          static_cast<float>(max_v), fmt)) {
        node[key] = static_cast<double>(fv);
        return true;
    }
    return false;
}

bool draw_hero_meta(YAML::Node hero_node) {
    bool changed = false;

    // hero.name 文本 -- 用栈缓冲 ImGui::InputText, 失焦后写回.
    static char name_buf[128];
    const std::string cur_name =
        hero_node["name"] ? hero_node["name"].as<std::string>() : "";

    // 选中切换时 main.cpp 重置 panel state, 但 imgui static buf 不会自动跟着
    // 切; 用 ImGui::SetNextItemWidth + InputText, 实时写回.
    std::snprintf(name_buf, sizeof(name_buf), "%s", cur_name.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("name", name_buf, sizeof(name_buf))) {
        hero_node["name"] = std::string(name_buf);
        changed = true;
    }

    changed |= drag_double("base_health",       hero_node, 5.0f,  0.0,  10000.0, "%.0f");
    changed |= drag_double("base_mana",         hero_node, 5.0f,  0.0,  10000.0, "%.0f");
    changed |= drag_double("base_armor",        hero_node, 0.5f, -50.0,  100.0,  "%.1f");
    changed |= drag_double("base_magic_resist", hero_node, 0.01f, 0.0,    1.0,   "%.2f");
    changed |= drag_double("hull_radius",       hero_node, 1.0f,  0.0,  200.0,  "%.0f");

    // attack_type: melee / ranged 二选一. yaml 里写枚举字符串, 内部映射到
    // UnitStats.ranged. 缺省视为 melee.
    const std::string cur_at =
        hero_node["attack_type"] ? hero_node["attack_type"].as<std::string>() : "melee";
    int at_idx = (cur_at == "ranged") ? 1 : 0;
    const char* at_items[] = {"melee", "ranged"};
    if (ImGui::Combo("attack_type", &at_idx, at_items, 2)) {
        hero_node["attack_type"] = std::string(at_items[at_idx]);
        changed = true;
    }
    changed |= drag_double("attack_range",     hero_node, 5.0f,  0.0,  2000.0, "%.0f");
    if (at_idx == 1) {
        changed |= drag_double("projectile_speed", hero_node, 10.0f, 0.0,  3000.0, "%.0f");
    }

    return changed;
}

} // namespace

HeroPanelResult draw_hero_panel(YAML::Node root, HeroPanelState& s,
                                  const std::vector<std::string>& ability_pool) {
    HeroPanelResult res;
    if (!root || !root.IsMap()) {
        ImGui::TextDisabled("(no hero loaded)");
        return res;
    }

    if (!root["hero"] || !root["hero"].IsMap()) {
        root["hero"] = YAML::Node(YAML::NodeType::Map);
    }
    YAML::Node hero_node = root["hero"];

    ImGui::SeparatorText("Hero metadata");
    if (draw_hero_meta(hero_node)) s.dirty = true;

    ImGui::SeparatorText("Abilities (ref list)");

    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        root["abilities"] = YAML::Node(YAML::NodeType::Sequence);
    }
    YAML::Node refs = root["abilities"];

    int move_from = -1, move_to = -1, remove_idx = -1;
    for (std::size_t i = 0; i < refs.size(); ++i) {
        const std::string name =
            refs[i].IsScalar() ? refs[i].as<std::string>() : "(non-scalar)";
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%2zu. %s", i + 1, name.c_str());
        ImGui::SameLine();
        const float btn_w = 28.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - btn_w * 3 - 12.0f +
                         ImGui::GetCursorPosX());
        if (ImGui::SmallButton("^") && i > 0) {
            move_from = static_cast<int>(i);
            move_to   = static_cast<int>(i) - 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < refs.size()) {
            move_from = static_cast<int>(i);
            move_to   = static_cast<int>(i) + 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            remove_idx = static_cast<int>(i);
        }
        ImGui::PopID();
    }
    if (move_from >= 0 && move_to >= 0) {
        try {
            tools::hero_move_ability_ref(root,
                static_cast<std::size_t>(move_from),
                static_cast<std::size_t>(move_to));
            s.dirty = true;
        } catch (const std::exception& e) {
            res.status = std::string("move failed: ") + e.what();
        }
    }
    if (remove_idx >= 0) {
        try {
            tools::hero_remove_ability_ref_at(root,
                static_cast<std::size_t>(remove_idx));
            s.dirty = true;
            res.status = "removed ability ref";
        } catch (const std::exception& e) {
            res.status = std::string("remove failed: ") + e.what();
        }
    }

    // Add 行: combo 选已有 ability + 按钮.
    ImGui::SeparatorText("Add ability ref");
    if (ability_pool.empty()) {
        ImGui::TextDisabled("(abilities/ 目录里没有可用 ability)");
        return res;
    }
    if (s.add_ref_combo_idx < 0 ||
        s.add_ref_combo_idx >= static_cast<int>(ability_pool.size())) {
        s.add_ref_combo_idx = 0;
    }
    const std::string& cur = ability_pool[s.add_ref_combo_idx];
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("##add_ref", cur.c_str())) {
        for (std::size_t i = 0; i < ability_pool.size(); ++i) {
            const bool sel = (static_cast<int>(i) == s.add_ref_combo_idx);
            if (ImGui::Selectable(ability_pool[i].c_str(), sel)) {
                s.add_ref_combo_idx = static_cast<int>(i);
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Add")) {
        try {
            tools::hero_add_ability_ref(root, ability_pool[s.add_ref_combo_idx]);
            s.dirty = true;
            res.status = std::string("added ref: ") +
                          ability_pool[s.add_ref_combo_idx];
        } catch (const std::exception& e) {
            res.status = std::string("add failed: ") + e.what();
        }
    }

    return res;
}

} // namespace dota::hero_workshop
