#include "modifier_panel.hpp"

#include "dota/tools/modifier_scanner.hpp"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace dota::hero_workshop {

namespace fs = std::filesystem;
using dota::tools::ModifierTemplate;

namespace {

const char* kTemplateNames[] = {
    "Empty", "DoT", "Shield", "AoE Thinker", "Motion Controller"
};
constexpr int kTemplateCount = 5;

ModifierTemplate idx_to_template(int idx) {
    switch (idx) {
        case 0: return ModifierTemplate::Empty;
        case 1: return ModifierTemplate::DoT;
        case 2: return ModifierTemplate::Shield;
        case 3: return ModifierTemplate::AoEThinker;
        case 4: return ModifierTemplate::MotionController;
    }
    return ModifierTemplate::Empty;
}

void open_modal(ModifierPanelState& s, ModifierPanelState::Mode m,
                const std::string& src = {}, const std::string& seed = {}) {
    s.mode = m;
    s.pending_open = true;
    s.source = src;
    s.error.clear();
    std::memset(s.input, 0, sizeof(s.input));
    if (!seed.empty() && seed.size() < sizeof(s.input)) {
        std::memcpy(s.input, seed.c_str(), seed.size());
    }
}

void close_modal(ModifierPanelState& s) {
    s.mode = ModifierPanelState::Mode::None;
    s.error.clear();
    ImGui::CloseCurrentPopup();
}

template <typename Op>
void draw_input_modal(ModifierPanelState& s, const char* label,
                      const char* hint, Op&& op) {
    if (s.pending_open) {
        ImGui::OpenPopup(label);
        s.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(label, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", hint);
        if (!s.source.empty()) {
            ImGui::Text("source: %s", s.source.c_str());
        }
        if (s.mode == ModifierPanelState::Mode::New) {
            ImGui::Combo("template", &s.template_idx,
                         kTemplateNames, kTemplateCount);
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##name", "modifier_<short>",
                                 s.input, sizeof(s.input));
        ImGui::TextDisabled("允许 a-z 0-9 _ , 不以 _ 开头.");
        if (!s.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
        ImGui::SameLine();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f))) {
            try {
                op(std::string{s.input});
                close_modal(s);
            } catch (const std::exception& e) {
                s.error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void draw_delete_modal(ModifierPanelState& s,
                       const fs::path& data_root,
                       std::string& out_status,
                       bool& out_dirty) {
    if (s.pending_open) {
        ImGui::OpenPopup("Delete modifier");
        s.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete modifier", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("把 [%s] 移到 .trash/?", s.source.c_str());
        if (!s.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
                              ImVec4(0.6f, 0.18f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.8f, 0.25f, 0.25f, 1.0f));
        const bool confirm = ImGui::Button("Move to .trash",
                                            ImVec2(140.0f, 0.0f));
        ImGui::PopStyleColor(2);
        if (confirm) {
            try {
                dota::tools::delete_modifier(data_root, s.source);
                out_status = "trashed " + s.source;
                out_dirty = true;
                close_modal(s);
            } catch (const std::exception& e) {
                s.error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void open_in_editor(const fs::path& path) {
    const char* editor = std::getenv("EDITOR");
    const std::string cmd = std::string(editor ? editor : "open") +
                            " \"" + path.string() + "\" &";
    std::system(cmd.c_str());
}

} // namespace

ModifierPanelResult draw_modifier_panel(ModifierPanelState& s,
                                          const fs::path& data_root) {
    ModifierPanelResult res;
    const fs::path mod_dir = data_root / "scripts" / "modifiers";
    const auto infos = dota::tools::scan_modifier_dir(mod_dir);

    if (s.selected >= static_cast<int>(infos.size())) s.selected = -1;

    // --- 列表
    ImGui::BeginChild("##modifier_list", ImVec2(260.0f, 0.0f), true);
    ImGui::TextUnformatted(mod_dir.string().c_str());
    ImGui::Separator();

    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto& info = infos[i];
        const bool sel = (static_cast<int>(i) == s.selected);
        std::string label = info.file_stem;
        if (info.register_names.size() != 1 ||
            info.register_names.front() != info.file_stem) {
            label += " (";
            for (std::size_t k = 0; k < info.register_names.size(); ++k) {
                if (k) label += ", ";
                label += info.register_names[k];
            }
            label += ")";
        }
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(label.c_str(), sel)) {
            s.selected = static_cast<int>(i);
        }
        if (ImGui::BeginPopupContextItem("##mod_ctx")) {
            s.selected = static_cast<int>(i);
            const std::string sel_name =
                info.register_names.empty()
                    ? info.file_stem
                    : info.register_names.front();
            if (ImGui::MenuItem("Duplicate ...")) {
                open_modal(s, ModifierPanelState::Mode::Duplicate,
                           sel_name, sel_name + "_copy");
            }
            if (ImGui::MenuItem("Rename ...")) {
                open_modal(s, ModifierPanelState::Mode::Rename,
                           sel_name, sel_name);
            }
            if (ImGui::MenuItem("Open in $EDITOR")) {
                open_in_editor(info.file_path);
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
            if (ImGui::MenuItem("Delete ...")) {
                open_modal(s, ModifierPanelState::Mode::Delete, sel_name);
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // 空白处右键: New
    if (ImGui::BeginPopupContextWindow("##mod_empty_ctx",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("New modifier ...")) {
            open_modal(s, ModifierPanelState::Mode::New, {}, "modifier_");
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();

    // --- 详情
    ImGui::SameLine();
    ImGui::BeginChild("##modifier_detail", ImVec2(0.0f, 0.0f), false);
    const bool has_sel = s.selected >= 0 &&
                         s.selected < static_cast<int>(infos.size());
    if (has_sel) {
        const auto& info = infos[s.selected];
        ImGui::Text("path: %s", info.file_path.string().c_str());
        ImGui::Text("registers: ");
        for (const auto& n : info.register_names) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f),
                               "%s", n.c_str());
        }
        if (info.register_names.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(none)");
        }
        if (ImGui::Button("Open in $EDITOR", ImVec2(180.0f, 28.0f))) {
            open_in_editor(info.file_path);
        }
    } else {
        ImGui::TextDisabled("(选一个 modifier)");
    }

    if (s.dirty_disk) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
            "(磁盘已改动. 重启 hero_workshop 让 LuaState 重新加载)");
    }

    ImGui::EndChild();

    // --- modal
    switch (s.mode) {
        case ModifierPanelState::Mode::New:
            draw_input_modal(s, "New modifier",
                "用模板新建. 写入 data/scripts/modifiers/<name>.lua.",
                [&](const std::string& name) {
                    dota::tools::create_modifier(
                        data_root, name, idx_to_template(s.template_idx));
                    res.status = "created " + name;
                    s.dirty_disk = true;
                });
            break;
        case ModifierPanelState::Mode::Duplicate:
            draw_input_modal(s, "Duplicate modifier",
                "拷贝并改写 register_modifier 的 name 实参.",
                [&](const std::string& name) {
                    dota::tools::duplicate_modifier(
                        data_root, s.source, name);
                    res.status = "duplicated -> " + name;
                    s.dirty_disk = true;
                });
            break;
        case ModifierPanelState::Mode::Rename:
            draw_input_modal(s, "Rename modifier",
                "改文件名 + 改 register_modifier 的 name.",
                [&](const std::string& name) {
                    dota::tools::rename_modifier(
                        data_root, s.source, name);
                    res.status = "renamed -> " + name;
                    s.dirty_disk = true;
                });
            break;
        case ModifierPanelState::Mode::Delete:
            draw_delete_modal(s, data_root, res.status, s.dirty_disk);
            break;
        case ModifierPanelState::Mode::None:
            break;
    }

    return res;
}

} // namespace dota::hero_workshop
