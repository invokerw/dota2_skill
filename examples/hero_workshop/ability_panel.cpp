#include "ability_panel.hpp"

#include "dota/ability/behavior.hpp"
#include "dota/tools/ability_ops.hpp"
#include "dota/tools/trash.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace dota::hero_workshop {

namespace fs = std::filesystem;

namespace {

// --- behavior bitmask 复选框 -----------------------------------------------

struct BehaviorEntry {
    BehaviorFlag flag;
    const char*  label;
    const char*  yaml_token;
};

const std::vector<BehaviorEntry>& behavior_entries() {
    static const std::vector<BehaviorEntry> v = {
        {BehaviorFlag::NoTarget,        "no_target",       "NO_TARGET"},
        {BehaviorFlag::UnitTarget,      "unit_target",     "UNIT_TARGET"},
        {BehaviorFlag::PointTarget,     "point_target",    "POINT_TARGET"},
        {BehaviorFlag::Passive,         "passive",         "PASSIVE"},
        {BehaviorFlag::Channelled,      "channelled",      "CHANNELLED"},
        {BehaviorFlag::AoE,             "aoe",             "AOE"},
        {BehaviorFlag::IgnoreSilence,   "ignore_silence",  "IGNORE_SILENCE"},
        {BehaviorFlag::IgnoreMagicImmune,
                                       "ignore_magic_immune",
                                                          "IGNORE_MAGIC_IMMUNE"},
    };
    return v;
}

const char* target_team_token(int team_idx) {
    static const char* tokens[] = {"NONE", "ENEMY", "FRIENDLY", "BOTH"};
    if (team_idx < 0 || team_idx > 3) return "NONE";
    return tokens[team_idx];
}
int parse_target_team_idx(const std::string& s) {
    const auto t = parse_target_team(s);
    switch (t) {
        case TargetTeam::None:     return 0;
        case TargetTeam::Enemy:    return 1;
        case TargetTeam::Friendly: return 2;
        case TargetTeam::Both:     return 3;
    }
    return 0;
}

// 读 ability.behavior 列表 (csv 或 yaml seq) 为 bitmask.
std::uint32_t read_behavior_mask(const YAML::Node& a) {
    if (!a["behavior"]) return 0;
    if (a["behavior"].IsScalar()) {
        return parse_behavior_flags(a["behavior"].as<std::string>());
    }
    if (a["behavior"].IsSequence()) {
        std::string csv;
        for (auto it = a["behavior"].begin(); it != a["behavior"].end(); ++it) {
            if (!csv.empty()) csv += ",";
            csv += it->as<std::string>();
        }
        return parse_behavior_flags(csv);
    }
    return 0;
}

void write_behavior_mask(YAML::Node a, std::uint32_t mask) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (const auto& e : behavior_entries()) {
        if (mask & to_mask(e.flag)) seq.push_back(std::string(e.yaml_token));
    }
    if (seq.size() == 0) seq.push_back(std::string("NO_TARGET"));
    a["behavior"] = seq;
}

// --- level array 编辑 ------------------------------------------------------

std::vector<double> read_double_seq(const YAML::Node& n) {
    std::vector<double> out;
    if (!n) return out;
    if (n.IsScalar()) {
        try { out.push_back(n.as<double>()); } catch (...) {}
        return out;
    }
    if (n.IsSequence()) {
        for (const auto& e : n) {
            try { out.push_back(e.as<double>()); } catch (...) {}
        }
    }
    return out;
}

void write_double_seq(YAML::Node a, const char* key,
                      const std::vector<double>& vals, bool as_int) {
    YAML::Node seq(YAML::NodeType::Sequence);
    for (double v : vals) {
        if (as_int) seq.push_back(static_cast<long>(std::llround(v)));
        else        seq.push_back(v);
    }
    a[key] = seq;
}

bool draw_level_array(const char* label, std::vector<double>& vals,
                      bool as_int, double min_v, double max_v, float speed) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::Text("%s (%d level%s)", label,
                static_cast<int>(vals.size()),
                vals.size() == 1 ? "" : "s");
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) {
        vals.push_back(vals.empty() ? 0.0 : vals.back());
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("-") && !vals.empty()) {
        vals.pop_back();
        changed = true;
    }
    for (std::size_t i = 0; i < vals.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(60.0f);
        if (as_int) {
            int v = static_cast<int>(std::llround(vals[i]));
            if (ImGui::DragInt("##v", &v, speed,
                               static_cast<int>(min_v),
                               static_cast<int>(max_v))) {
                vals[i] = v;
                changed = true;
            }
        } else {
            float v = static_cast<float>(vals[i]);
            if (ImGui::DragFloat("##v", &v, speed,
                                 static_cast<float>(min_v),
                                 static_cast<float>(max_v),
                                 "%.2f")) {
                vals[i] = v;
                changed = true;
            }
        }
        if ((i + 1) % 6 != 0 && i + 1 < vals.size()) ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

// --- ability_special 编辑 --------------------------------------------------

bool detect_int(const YAML::Node& seq) {
    if (!seq || !seq.IsSequence()) return false;
    for (const auto& e : seq) {
        const std::string s = e.as<std::string>();
        if (s.find('.') != std::string::npos) return false;
    }
    return true;
}

bool draw_ability_special(YAML::Node a) {
    bool changed = false;
    if (!a["ability_special"] || !a["ability_special"].IsMap()) {
        a["ability_special"] = YAML::Node(YAML::NodeType::Map);
    }
    YAML::Node spec = a["ability_special"];

    std::string remove_key;
    for (auto it = spec.begin(); it != spec.end(); ++it) {
        const std::string k = it->first.as<std::string>();
        ImGui::PushID(k.c_str());
        const bool open = ImGui::TreeNodeEx(
            "##sk", ImGuiTreeNodeFlags_DefaultOpen,
            "%s", k.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            remove_key = k;
        }
        if (open) {
            const bool as_int = detect_int(it->second);
            std::vector<double> vals = read_double_seq(it->second);
            if (draw_level_array("levels", vals, as_int,
                                 -100000.0, 100000.0,
                                 as_int ? 1.0f : 0.5f)) {
                write_double_seq(spec, k.c_str(), vals, as_int);
                changed = true;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (!remove_key.empty()) {
        spec.remove(remove_key);
        changed = true;
    }

    static char new_key[64] = {};
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##new_special_key",
                             "new key", new_key, sizeof(new_key));
    ImGui::SameLine();
    if (ImGui::Button("Add key") && new_key[0]) {
        if (!spec[new_key]) {
            YAML::Node arr(YAML::NodeType::Sequence);
            arr.push_back(0.0);
            spec[new_key] = arr;
            changed = true;
        }
        new_key[0] = '\0';
    }
    if (changed) a["ability_special"] = spec;
    return changed;
}

// --- on_spell_start (datadriven) ------------------------------------------

bool draw_action_list(YAML::Node a) {
    bool changed = false;
    if (!a["on_spell_start"] || !a["on_spell_start"].IsSequence()) {
        a["on_spell_start"] = YAML::Node(YAML::NodeType::Sequence);
    }
    YAML::Node list = a["on_spell_start"];

    int remove_idx = -1;
    for (std::size_t i = 0; i < list.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        YAML::Node action = list[i];
        if (action.IsMap() && action.size() == 1) {
            const std::string kind = action.begin()->first.as<std::string>();
            const bool open = ImGui::TreeNodeEx(
                "##act", ImGuiTreeNodeFlags_DefaultOpen,
                "[%zu] %s", i, kind.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) remove_idx = static_cast<int>(i);
            if (open) {
                YAML::Node body = action[kind];
                ImGui::TextDisabled("(action body 仅展示, 后续 stage 加表单)");
                ImGui::TextWrapped("%s", YAML::Dump(body).c_str());
                ImGui::TreePop();
            }
        } else {
            ImGui::TextDisabled("[%zu] (malformed action)", i);
        }
        ImGui::PopID();
    }
    if (remove_idx >= 0) {
        list.remove(static_cast<std::size_t>(remove_idx));
        changed = true;
    }

    if (ImGui::Button("+ damage TARGET")) {
        YAML::Node act(YAML::NodeType::Map);
        YAML::Node body(YAML::NodeType::Map);
        body["target"] = "TARGET";
        body["type"]   = "MAGICAL";
        body["amount"] = "%damage";
        act["damage"]  = body;
        list.push_back(act);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ heal CASTER")) {
        YAML::Node act(YAML::NodeType::Map);
        YAML::Node body(YAML::NodeType::Map);
        body["target"] = "CASTER";
        body["amount"] = "100";
        act["heal"]    = body;
        list.push_back(act);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ apply_modifier TARGET")) {
        YAML::Node act(YAML::NodeType::Map);
        YAML::Node body(YAML::NodeType::Map);
        body["target"]   = "TARGET";
        body["name"]     = "modifier_stunned";
        body["duration"] = "%stun_duration";
        act["apply_modifier"] = body;
        list.push_back(act);
        changed = true;
    }
    if (changed) a["on_spell_start"] = list;
    return changed;
}

// --- 单个 ability 表单 -----------------------------------------------------

bool draw_ability_form(YAML::Node a, const fs::path& data_root) {
    bool changed = false;

    // base_class 单选
    bool is_lua = a["base_class"] &&
                  a["base_class"].as<std::string>() == "ability_lua";
    if (ImGui::RadioButton("ability_datadriven", !is_lua)) {
        a["base_class"] = "ability_datadriven";
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("ability_lua", is_lua)) {
        a["base_class"] = "ability_lua";
        changed = true;
    }

    // name (只读 -- rename 留给 hero_ops 风格的专用流程, 避免引用悬空)
    if (a["name"]) {
        ImGui::Text("name: %s", a["name"].as<std::string>().c_str());
    }

    if (a["base_class"].as<std::string>() == "ability_lua") {
        const std::string script =
            a["script"] ? a["script"].as<std::string>() : "";
        ImGui::Text("script: %s", script.c_str());
        const fs::path full = data_root / "scripts" / script;
        ImGui::Text("path: %s", full.string().c_str());
        if (ImGui::Button("Open in $EDITOR")) {
            const char* editor = std::getenv("EDITOR");
            const std::string cmd = std::string(editor ? editor : "open") +
                                    " \"" + full.string() + "\" &";
            std::system(cmd.c_str());
        }
    }

    ImGui::SeparatorText("Behavior");
    std::uint32_t mask = read_behavior_mask(a);
    bool mask_changed = false;
    for (const auto& e : behavior_entries()) {
        bool on = (mask & to_mask(e.flag)) != 0;
        if (ImGui::Checkbox(e.label, &on)) {
            if (on) mask |= to_mask(e.flag);
            else    mask &= ~to_mask(e.flag);
            mask_changed = true;
        }
    }
    if (mask_changed) {
        write_behavior_mask(a, mask);
        changed = true;
    }

    int team_idx = a["target_team"] ?
        parse_target_team_idx(a["target_team"].as<std::string>()) : 0;
    const char* team_items[] = {"NONE", "ENEMY", "FRIENDLY", "BOTH"};
    if (ImGui::Combo("target_team", &team_idx,
                     team_items, IM_ARRAYSIZE(team_items))) {
        a["target_team"] = std::string(target_team_token(team_idx));
        changed = true;
    }

    ImGui::SeparatorText("Timing");
    auto drag_double_field = [&](const char* key, float speed,
                                  double min_v, double max_v,
                                  const char* fmt) -> bool {
        double v = a[key] ? a[key].as<double>() : 0.0;
        float fv = static_cast<float>(v);
        if (ImGui::DragFloat(key, &fv, speed,
                             static_cast<float>(min_v),
                             static_cast<float>(max_v),
                             fmt)) {
            a[key] = static_cast<double>(fv);
            return true;
        }
        return false;
    };
    changed |= drag_double_field("cast_point",   0.01f, 0.0, 10.0, "%.2f");
    changed |= drag_double_field("backswing",    0.01f, 0.0, 10.0, "%.2f");
    changed |= drag_double_field("cast_range",   5.0f,  0.0, 5000.0, "%.0f");
    changed |= drag_double_field("channel_time", 0.05f, 0.0, 60.0, "%.2f");

    ImGui::SeparatorText("Cooldown / Mana");
    {
        std::vector<double> cds = read_double_seq(a["cooldown"]);
        if (draw_level_array("cooldown", cds, false, 0.0, 600.0, 0.5f)) {
            write_double_seq(a, "cooldown", cds, false);
            changed = true;
        }
        std::vector<double> mc = read_double_seq(a["mana_cost"]);
        if (draw_level_array("mana_cost", mc, true, 0.0, 5000.0, 1.0f)) {
            write_double_seq(a, "mana_cost", mc, true);
            changed = true;
        }
    }

    ImGui::SeparatorText("Ability Special");
    changed |= draw_ability_special(a);

    if (a["base_class"].as<std::string>() == "ability_datadriven") {
        ImGui::SeparatorText("on_spell_start (actions)");
        changed |= draw_action_list(a);
    }

    return changed;
}

} // namespace

void reset_for_new_doc(AbilityPanelState& s) {
    s.selected = -1;
    s.dirty = false;
    s.add_mode = AbilityPanelState::AddMode::None;
    s.add_pending_open = false;
    s.add_error.clear();
    std::memset(s.add_input, 0, sizeof(s.add_input));
}

// 处理右键 "New ability" modal. add_mode 决定 datadriven vs lua.
void draw_add_ability_modal(YAML::Node root,
                             AbilityPanelState& s,
                             const fs::path& data_root,
                             AbilityPanelResult& res) {
    if (s.add_mode == AbilityPanelState::AddMode::None) return;
    const char* label = (s.add_mode == AbilityPanelState::AddMode::Lua)
        ? "New ability_lua" : "New ability_datadriven";
    if (s.add_pending_open) {
        ImGui::OpenPopup(label);
        s.add_pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(label, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("ability name (建议 <hero_stem>_<short>):");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##newname", s.add_input, sizeof(s.add_input));
        if (!s.add_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.add_error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            s.add_mode = AbilityPanelState::AddMode::None;
            s.add_error.clear();
            std::memset(s.add_input, 0, sizeof(s.add_input));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add", ImVec2(120.0f, 0.0f)) && s.add_input[0]) {
            try {
                std::size_t idx;
                if (s.add_mode == AbilityPanelState::AddMode::Lua) {
                    const std::string fname =
                        std::string(s.add_input) + ".lua";
                    idx = dota::tools::add_lua_ability(
                        root, data_root, s.add_input, fname);
                } else {
                    idx = dota::tools::add_datadriven_ability(
                        root, s.add_input);
                }
                s.selected = static_cast<int>(idx);
                s.dirty = true;
                res.status = std::string("added ") + s.add_input;
                s.add_mode = AbilityPanelState::AddMode::None;
                s.add_error.clear();
                std::memset(s.add_input, 0, sizeof(s.add_input));
                ImGui::CloseCurrentPopup();
            } catch (const std::exception& e) {
                s.add_error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void open_add_modal(AbilityPanelState& s,
                     AbilityPanelState::AddMode m) {
    s.add_mode = m;
    s.add_pending_open = true;
    s.add_error.clear();
    std::memset(s.add_input, 0, sizeof(s.add_input));
}

AbilityPanelResult draw_ability_panel(YAML::Node root,
                                       AbilityPanelState& s,
                                       const fs::path& data_root,
                                       const std::string& /*hero_stem*/) {
    AbilityPanelResult res;
    if (!root || !root.IsMap()) {
        ImGui::TextDisabled("(no hero loaded)");
        return res;
    }
    if (!root["abilities"] || !root["abilities"].IsSequence()) {
        ImGui::TextDisabled("(abilities 不是序列)");
        return res;
    }
    YAML::Node abs = root["abilities"];

    // --- 列表
    ImGui::BeginChild("##ability_list", ImVec2(220.0f, 0.0f), true);

    int remove_idx = -1;
    for (std::size_t i = 0; i < abs.size(); ++i) {
        const std::string n = abs[i]["name"] ?
            abs[i]["name"].as<std::string>() : "(no name)";
        const bool sel = (static_cast<int>(i) == s.selected);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(n.c_str(), sel)) {
            s.selected = static_cast<int>(i);
        }
        if (ImGui::BeginPopupContextItem("##ab_ctx")) {
            s.selected = static_cast<int>(i);
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
            if (ImGui::MenuItem("Remove ability")) {
                remove_idx = static_cast<int>(i);
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // 空白处右键: New ability_datadriven / New ability_lua
    if (ImGui::BeginPopupContextWindow("##ab_empty_ctx",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("New ability_datadriven ...")) {
            open_add_modal(s, AbilityPanelState::AddMode::DataDriven);
        }
        if (ImGui::MenuItem("New ability_lua ...")) {
            open_add_modal(s, AbilityPanelState::AddMode::Lua);
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    if (remove_idx >= 0) {
        try {
            auto recs = dota::tools::remove_ability_at(
                root, data_root,
                static_cast<std::size_t>(remove_idx));
            res.status = std::string("removed ability") +
                (recs.empty() ? "" :
                 std::string(" (trashed ") +
                 std::to_string(recs.size()) + " script)");
            if (s.selected == remove_idx) s.selected = -1;
            else if (s.selected > remove_idx) --s.selected;
            s.dirty = true;
        } catch (const std::exception& e) {
            res.status = std::string("remove failed: ") + e.what();
        }
    }

    // --- 详情
    ImGui::SameLine();
    ImGui::BeginChild("##ability_form", ImVec2(0.0f, 0.0f), false);
    if (s.selected < 0 || s.selected >= static_cast<int>(abs.size())) {
        ImGui::TextDisabled("(选一个 ability 开始编辑)");
    } else {
        YAML::Node a = abs[s.selected];
        if (draw_ability_form(a, data_root)) {
            s.dirty = true;
        }
    }
    ImGui::EndChild();

    draw_add_ability_modal(root, s, data_root, res);

    return res;
}

} // namespace dota::hero_workshop
