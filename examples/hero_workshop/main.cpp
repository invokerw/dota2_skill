// hero_workshop -- 英雄 / 技能 / Modifier 编辑器.
//
// 左侧垂直 tab bar 切 Heroes / Abilities / Modifiers 三个工作区, 各 tab 自带
// 列表 + 详情, 各自管 dirty / save. 顶部菜单栏 / 底部状态栏全局共享.

#include "ability_panel.hpp"
#include "hero_panel.hpp"
#include "modifier_panel.hpp"

#include "dota/tools/ability_doc.hpp"
#include "dota/tools/ability_ops.hpp"
#include "dota/tools/hero_catalog.hpp"
#include "dota/tools/hero_ops.hpp"
#include "dota/tools/hero_writer.hpp"
#include "dota/tools/trash.hpp"

#include "raylib.h"
#include "imgui.h"
#include "rlImGui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
    void glfwSetWindowShouldClose(void* window, int value);
}

namespace fs = std::filesystem;

namespace {

constexpr int kWindowW   = 1280;
constexpr int kWindowH   = 720;
constexpr float kTabBarW = 48.0f;
constexpr float kListW   = 220.0f;

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

enum class Tab { Heroes, Abilities, Modifiers };

// 各 tab 专属 modal 状态 (复用一个聚合结构, kind 区分).
enum class ModalKind {
    None,
    HeroNew, HeroDuplicate, HeroRename, HeroDelete,
    AbilityNewDD, AbilityNewLua, AbilityDuplicate, AbilityRename, AbilityDelete,
    DirtyGuard, QuitGuard,
    Diff,
};

struct ModalState {
    ModalKind kind = ModalKind::None;
    bool      pending_open = false;
    char      input[64]{};
    std::string source_name;
    std::vector<std::string> impacted;
    std::string error;
    std::string pending_select;  // DirtyGuard: 切到这个 stem
    Tab         pending_tab    = Tab::Heroes;
    bool        pending_tab_switch = false;
    std::string diff_disk;
    std::string diff_emit;
};

struct State {
    Tab    tab = Tab::Heroes;
    bool   quit_requested = false;
    std::string status_message;
    double      status_until = 0.0;
    ModalState  modal;

    // Heroes
    dota::tools::HeroCatalog              hero_catalog;
    int                                   hero_idx = 0;
    std::optional<dota::tools::HeroDoc>   hero_doc;
    dota::hero_workshop::HeroPanelState   hero_panel;

    // Abilities
    dota::tools::AbilityCatalog                ability_catalog;
    int                                        ability_idx = 0;
    std::optional<dota::tools::AbilityDoc>     ability_doc;
    dota::hero_workshop::AbilityPanelState     ability_panel;

    // Modifiers
    dota::hero_workshop::ModifierPanelState modifier_panel;
};

void set_status(State& s, std::string msg) {
    s.status_message = std::move(msg);
    s.status_until   = ImGui::GetTime() + 6.0;
}

const std::string& current_hero_stem(const State& s) {
    static const std::string empty;
    if (s.hero_idx < 0 ||
        s.hero_idx >= static_cast<int>(s.hero_catalog.heroes().size())) return empty;
    return s.hero_catalog.heroes()[s.hero_idx].yaml_name;
}

const std::string& current_ability_stem(const State& s) {
    static const std::string empty;
    if (s.ability_idx < 0 ||
        s.ability_idx >= static_cast<int>(s.ability_catalog.entries().size())) return empty;
    return s.ability_catalog.entries()[s.ability_idx].yaml_stem;
}

void load_hero_doc(State& s) {
    if (s.hero_idx < 0 ||
        s.hero_idx >= static_cast<int>(s.hero_catalog.heroes().size())) {
        s.hero_doc.reset();
        return;
    }
    try {
        s.hero_doc = dota::tools::HeroDoc::load(
            s.hero_catalog.heroes()[s.hero_idx].yaml_path);
        s.hero_panel.dirty = false;
    } catch (const std::exception& e) {
        s.hero_doc.reset();
        set_status(s, std::string("hero load failed: ") + e.what());
    }
}

void load_ability_doc(State& s) {
    if (s.ability_idx < 0 ||
        s.ability_idx >= static_cast<int>(s.ability_catalog.entries().size())) {
        s.ability_doc.reset();
        return;
    }
    try {
        s.ability_doc = dota::tools::AbilityDoc::load(
            s.ability_catalog.entries()[s.ability_idx].yaml_path);
        dota::hero_workshop::reset_for_new_doc(s.ability_panel);
    } catch (const std::exception& e) {
        s.ability_doc.reset();
        set_status(s, std::string("ability load failed: ") + e.what());
    }
}

void scan_heroes(State& s, const std::string& select_stem = {}) {
    try {
        s.hero_catalog.scan(fs::path(data_dir()) / "heroes");
    } catch (const std::exception& e) {
        set_status(s, std::string("hero scan failed: ") + e.what());
        s.hero_doc.reset();
        return;
    }
    if (!select_stem.empty()) {
        for (std::size_t i = 0; i < s.hero_catalog.heroes().size(); ++i) {
            if (s.hero_catalog.heroes()[i].yaml_name == select_stem) {
                s.hero_idx = static_cast<int>(i);
                break;
            }
        }
    }
    if (s.hero_idx < 0 ||
        s.hero_idx >= static_cast<int>(s.hero_catalog.heroes().size())) {
        s.hero_idx = 0;
    }
    load_hero_doc(s);
}

void scan_abilities(State& s, const std::string& select_stem = {}) {
    try {
        s.ability_catalog.scan(fs::path(data_dir()) / "abilities");
    } catch (const std::exception& e) {
        set_status(s, std::string("ability scan failed: ") + e.what());
        s.ability_doc.reset();
        return;
    }
    if (!select_stem.empty()) {
        for (std::size_t i = 0; i < s.ability_catalog.entries().size(); ++i) {
            if (s.ability_catalog.entries()[i].yaml_stem == select_stem) {
                s.ability_idx = static_cast<int>(i);
                break;
            }
        }
    }
    if (s.ability_idx < 0 ||
        s.ability_idx >= static_cast<int>(s.ability_catalog.entries().size())) {
        s.ability_idx = 0;
    }
    load_ability_doc(s);
}

bool any_dirty(const State& s) {
    return s.hero_panel.dirty || s.ability_panel.dirty;
}

bool save_current_tab(State& s) {
    if (s.tab == Tab::Heroes) {
        if (!s.hero_doc) return false;
        try {
            s.hero_doc->save_to(s.hero_catalog.heroes()[s.hero_idx].yaml_path);
            set_status(s, "saved hero: " + current_hero_stem(s));
            scan_heroes(s, current_hero_stem(s));
            return true;
        } catch (const std::exception& e) {
            set_status(s, std::string("save failed: ") + e.what());
            return false;
        }
    }
    if (s.tab == Tab::Abilities) {
        if (!s.ability_doc) return false;
        try {
            s.ability_doc->save_to(
                s.ability_catalog.entries()[s.ability_idx].yaml_path);
            set_status(s, "saved ability: " + current_ability_stem(s));
            scan_abilities(s, current_ability_stem(s));
            return true;
        } catch (const std::exception& e) {
            set_status(s, std::string("save failed: ") + e.what());
            return false;
        }
    }
    return false;
}

std::string read_file_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream os; os << f.rdbuf();
    return os.str();
}

void open_modal(State& s, ModalKind k, const std::string& src = {}) {
    s.modal.kind = k;
    s.modal.pending_open = true;
    s.modal.error.clear();
    std::memset(s.modal.input, 0, sizeof(s.modal.input));
    s.modal.source_name = src;
    s.modal.impacted.clear();

    if (k == ModalKind::HeroDuplicate || k == ModalKind::HeroRename) {
        const std::string seed = src + (k == ModalKind::HeroDuplicate ? "_copy" : "");
        std::snprintf(s.modal.input, sizeof(s.modal.input), "%s", seed.c_str());
    } else if (k == ModalKind::AbilityDuplicate || k == ModalKind::AbilityRename) {
        const std::string seed = src + (k == ModalKind::AbilityDuplicate ? "_copy" : "");
        std::snprintf(s.modal.input, sizeof(s.modal.input), "%s", seed.c_str());
    } else if (k == ModalKind::HeroDelete) {
        s.modal.impacted.push_back(
            (fs::path(data_dir()) / "heroes" / (src + ".yaml")).string());
    } else if (k == ModalKind::AbilityDelete) {
        s.modal.impacted = dota::tools::heroes_referencing(
            fs::path(data_dir()), src);
    }
}

void close_modal(State& s) {
    s.modal.kind = ModalKind::None;
    s.modal.error.clear();
    ImGui::CloseCurrentPopup();
}

template <typename Op>
void draw_input_modal(State& s, const char* label, const char* hint, Op&& op) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup(label);
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(label, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", hint);
        if (!s.modal.source_name.empty()) {
            ImGui::Text("source: %s", s.modal.source_name.c_str());
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##stem_input", s.modal.input, sizeof(s.modal.input));
        ImGui::TextDisabled("允许 a-z 0-9 _ , 不以 _ 开头.");
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s", s.modal.error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) close_modal(s);
        ImGui::SameLine();
        if (ImGui::Button("Confirm", ImVec2(120.0f, 0.0f))) {
            try {
                op(std::string{s.modal.input});
                close_modal(s);
            } catch (const std::exception& e) {
                s.modal.error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void draw_hero_delete_modal(State& s) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Delete hero");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete hero", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("把 [%s] 移到 .trash/?", s.modal.source_name.c_str());
        ImGui::TextDisabled("ability yaml / lua 脚本不会被删除 (共享).");
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s", s.modal.error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) close_modal(s);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.18f, 0.18f, 1.0f));
        const bool confirm = ImGui::Button("Move to .trash", ImVec2(140.0f, 0.0f));
        ImGui::PopStyleColor();
        if (confirm) {
            try {
                dota::tools::delete_hero(fs::path(data_dir()), s.modal.source_name);
                set_status(s, "trashed hero: " + s.modal.source_name);
                close_modal(s);
                scan_heroes(s);
            } catch (const std::exception& e) {
                s.modal.error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void draw_ability_delete_modal(State& s) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Delete ability");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete ability", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        if (s.modal.impacted.empty()) {
            ImGui::TextWrapped("把 ability [%s] 移到 .trash/? 当前没有 hero 引用.",
                                s.modal.source_name.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "ability [%s] 被以下 hero 引用, 拒绝删除:",
                s.modal.source_name.c_str());
            ImGui::BeginChild("##refs", ImVec2(500.0f, 120.0f), true);
            for (const auto& h : s.modal.impacted) {
                ImGui::TextWrapped("- %s", h.c_str());
            }
            ImGui::EndChild();
        }
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s", s.modal.error.c_str());
        }
        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) close_modal(s);
        if (s.modal.impacted.empty()) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.18f, 0.18f, 1.0f));
            const bool confirm = ImGui::Button("Move to .trash", ImVec2(140.0f, 0.0f));
            ImGui::PopStyleColor();
            if (confirm) {
                try {
                    dota::tools::delete_ability_file(
                        fs::path(data_dir()), s.modal.source_name);
                    set_status(s, "trashed ability: " + s.modal.source_name);
                    close_modal(s);
                    scan_abilities(s);
                } catch (const std::exception& e) {
                    s.modal.error = e.what();
                }
            }
        }
        ImGui::EndPopup();
    }
}

void draw_dirty_guard_modal(State& s) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Unsaved changes");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("[%s] 有未保存改动. 切到 [%s] 之前怎么处理?",
                            s.modal.source_name.c_str(),
                            s.modal.pending_select.c_str());
        if (ImGui::Button("Save & switch", ImVec2(140.0f, 0.0f))) {
            if (save_current_tab(s)) {
                const std::string target = s.modal.pending_select;
                close_modal(s);
                if (s.tab == Tab::Heroes) scan_heroes(s, target);
                else                       scan_abilities(s, target);
            } else {
                s.modal.error = "save failed; 留在原地";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f))) {
            const std::string target = s.modal.pending_select;
            close_modal(s);
            s.hero_panel.dirty = false;
            s.ability_panel.dirty = false;
            if (s.tab == Tab::Heroes) scan_heroes(s, target);
            else                       scan_abilities(s, target);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) close_modal(s);
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s", s.modal.error.c_str());
        }
        ImGui::EndPopup();
    }
}

void draw_quit_guard_modal(State& s) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Unsaved changes");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("有未保存改动 (%s). 退出前怎么处理?",
                            s.modal.source_name.c_str());
        if (ImGui::Button("Save & quit", ImVec2(140.0f, 0.0f))) {
            if (save_current_tab(s)) {
                close_modal(s);
                s.quit_requested = true;
            } else {
                s.modal.error = "save failed; 留在原地";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard & quit", ImVec2(140.0f, 0.0f))) {
            close_modal(s);
            s.quit_requested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) close_modal(s);
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s", s.modal.error.c_str());
        }
        ImGui::EndPopup();
    }
}

void draw_diff_modal(State& s) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Save preview");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(820.0f, 540.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Save preview", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("左: 磁盘当前内容    右: 即将写入的 emit() 输出");
        ImGui::Columns(2, "##diff_cols", true);
        ImGui::TextUnformatted("on disk");
        ImGui::InputTextMultiline("##disk", s.modal.diff_disk.data(),
            s.modal.diff_disk.size() + 1,
            ImVec2(-FLT_MIN, 380.0f),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::NextColumn();
        ImGui::TextUnformatted("emit()");
        ImGui::InputTextMultiline("##emit", s.modal.diff_emit.data(),
            s.modal.diff_emit.size() + 1,
            ImVec2(-FLT_MIN, 380.0f),
            ImGuiInputTextFlags_ReadOnly);
        ImGui::Columns(1);
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) close_modal(s);
        ImGui::SameLine();
        if (ImGui::Button("Confirm save", ImVec2(160.0f, 0.0f))) {
            close_modal(s);
            save_current_tab(s);
        }
        ImGui::EndPopup();
    }
}

void open_diff_modal(State& s) {
    if (s.tab == Tab::Heroes && s.hero_doc) {
        const auto& entry = s.hero_catalog.heroes()[s.hero_idx];
        open_modal(s, ModalKind::Diff, entry.yaml_name);
        s.modal.diff_disk = read_file_text(entry.yaml_path);
        try { s.modal.diff_emit = s.hero_doc->emit(); }
        catch (const std::exception& e) {
            s.modal.diff_emit = std::string("emit failed: ") + e.what();
        }
    } else if (s.tab == Tab::Abilities && s.ability_doc) {
        const auto& entry = s.ability_catalog.entries()[s.ability_idx];
        open_modal(s, ModalKind::Diff, entry.yaml_stem);
        s.modal.diff_disk = read_file_text(entry.yaml_path);
        try { s.modal.diff_emit = s.ability_doc->emit(); }
        catch (const std::exception& e) {
            s.modal.diff_emit = std::string("emit failed: ") + e.what();
        }
    }
}

void open_trash_dir() {
    const fs::path trash = fs::path(data_dir()) / ".trash";
    fs::create_directories(trash);
    const std::string cmd = "open \"" + trash.string() + "\" &";
    std::system(cmd.c_str());
}

void draw_modals(State& s) {
    switch (s.modal.kind) {
        case ModalKind::HeroNew:
            draw_input_modal(s, "New hero",
                "新建一个英雄 yaml. 文件名 = stem.yaml.",
                [&](const std::string& stem) {
                    dota::tools::create_hero(fs::path(data_dir()), stem);
                    set_status(s, "created hero: " + stem);
                    scan_heroes(s, stem);
                });
            break;
        case ModalKind::HeroDuplicate:
            draw_input_modal(s, "Duplicate hero",
                "复制 hero yaml. ability 引用列表保持原样 (共享).",
                [&](const std::string& stem) {
                    dota::tools::duplicate_hero(fs::path(data_dir()),
                        s.modal.source_name, stem);
                    set_status(s, "duplicated -> " + stem);
                    scan_heroes(s, stem);
                });
            break;
        case ModalKind::HeroRename:
            draw_input_modal(s, "Rename hero",
                "重命名 hero yaml. ability yaml / 脚本不动.",
                [&](const std::string& stem) {
                    dota::tools::rename_hero(fs::path(data_dir()),
                        s.modal.source_name, stem);
                    set_status(s, "renamed -> " + stem);
                    scan_heroes(s, stem);
                });
            break;
        case ModalKind::HeroDelete:    draw_hero_delete_modal(s); break;

        case ModalKind::AbilityNewDD:
            draw_input_modal(s, "New ability_datadriven",
                "新建独立 ability yaml (datadriven, on_spell_start 列表).",
                [&](const std::string& name) {
                    dota::tools::create_datadriven_ability_file(
                        fs::path(data_dir()), name);
                    set_status(s, "created ability: " + name);
                    scan_abilities(s, name);
                });
            break;
        case ModalKind::AbilityNewLua:
            draw_input_modal(s, "New ability_lua",
                "新建独立 ability_lua yaml + 同名 lua 脚本.",
                [&](const std::string& name) {
                    dota::tools::create_lua_ability_file(
                        fs::path(data_dir()), name);
                    set_status(s, "created ability_lua: " + name);
                    scan_abilities(s, name);
                });
            break;
        case ModalKind::AbilityDuplicate:
            draw_input_modal(s, "Duplicate ability",
                "复制 ability yaml + 关联 lua 脚本 (若有).",
                [&](const std::string& name) {
                    dota::tools::duplicate_ability_file(
                        fs::path(data_dir()), s.modal.source_name, name);
                    set_status(s, "duplicated ability -> " + name);
                    scan_abilities(s, name);
                });
            break;
        case ModalKind::AbilityRename:
            draw_input_modal(s, "Rename ability",
                "重命名 ability yaml + 同步所有 hero 引用.",
                [&](const std::string& name) {
                    auto r = dota::tools::rename_ability_file(
                        fs::path(data_dir()), s.modal.source_name, name);
                    std::string msg = "renamed ability -> " + name;
                    if (!r.updated_hero_stems.empty()) {
                        msg += " (synced ";
                        for (std::size_t i = 0;
                             i < r.updated_hero_stems.size(); ++i) {
                            if (i) msg += ", ";
                            msg += r.updated_hero_stems[i];
                        }
                        msg += ")";
                    }
                    set_status(s, std::move(msg));
                    scan_abilities(s, name);
                    scan_heroes(s, current_hero_stem(s));
                });
            break;
        case ModalKind::AbilityDelete: draw_ability_delete_modal(s); break;

        case ModalKind::DirtyGuard:    draw_dirty_guard_modal(s);    break;
        case ModalKind::QuitGuard:     draw_quit_guard_modal(s);     break;
        case ModalKind::Diff:          draw_diff_modal(s);           break;
        case ModalKind::None:          break;
    }
}

float draw_main_menu(State& s) {
    float h = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        const bool has_hero    = s.hero_doc.has_value();
        const bool has_ability = s.ability_doc.has_value();
        const bool tab_has_doc =
            (s.tab == Tab::Heroes && has_hero) ||
            (s.tab == Tab::Abilities && has_ability);

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save", "Ctrl+S", false, tab_has_doc)) {
                save_current_tab(s);
            }
            if (ImGui::MenuItem("Diff & Save ...", nullptr, false, tab_has_doc)) {
                open_diff_modal(s);
            }
            if (ImGui::MenuItem("Reload", "F5")) {
                if (s.tab == Tab::Heroes)    scan_heroes(s, current_hero_stem(s));
                if (s.tab == Tab::Abilities) scan_abilities(s, current_ability_stem(s));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open .trash dir")) open_trash_dir();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Cmd+Q")) {
                if (any_dirty(s)) {
                    open_modal(s, ModalKind::QuitGuard,
                                s.tab == Tab::Heroes ? current_hero_stem(s)
                                                      : current_ability_stem(s));
                } else {
                    s.quit_requested = true;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("dota2_skill -- hero workshop");
            ImGui::TextDisabled("左侧 tab 切 Heroes/Abilities/Modifiers.");
            ImGui::TextDisabled("各列表右键弹 New / Duplicate / Rename / Delete.");
            ImGui::EndMenu();
        }
        h = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }
    return h;
}

void try_switch_tab(State& s, Tab target) {
    if (s.tab == target) return;
    if (any_dirty(s)) {
        s.modal.pending_tab        = target;
        s.modal.pending_tab_switch = true;
        open_modal(s, ModalKind::DirtyGuard,
                   s.tab == Tab::Heroes ? current_hero_stem(s)
                                         : current_ability_stem(s));
        // pending_select 此处复用空; tab switch 由 commit dirty 时单独处理.
        s.modal.pending_select.clear();
        return;
    }
    s.tab = target;
}

void draw_left_tab_bar(State& s, float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(ImVec2(kTabBarW, vp->WorkSize.y - top - bottom));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##tabbar", nullptr, flags)) {
        const ImVec2 btn_size(kTabBarW - 16.0f, 36.0f);
        auto entry = [&](const char* label, Tab t) {
            const bool sel = (s.tab == t);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.25f, 0.45f, 0.7f, 1.0f));
            if (ImGui::Button(label, btn_size)) try_switch_tab(s, t);
            if (sel) ImGui::PopStyleColor();
        };
        entry("H", Tab::Heroes);
        entry("A", Tab::Abilities);
        entry("M", Tab::Modifiers);
    }
    ImGui::End();
}

void draw_hero_list_pane(State& s, float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + kTabBarW, vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(ImVec2(kListW, vp->WorkSize.y - top - bottom));
    if (ImGui::Begin("Heroes", nullptr,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoCollapse)) {
        const int prev = s.hero_idx;
        for (std::size_t i = 0; i < s.hero_catalog.heroes().size(); ++i) {
            const auto& entry = s.hero_catalog.heroes()[i];
            const bool sel = (static_cast<int>(i) == s.hero_idx);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(entry.yaml_name.c_str(), sel)) {
                s.hero_idx = static_cast<int>(i);
            }
            if (ImGui::BeginPopupContextItem("##hero_ctx")) {
                s.hero_idx = static_cast<int>(i);
                if (ImGui::MenuItem("Duplicate ...")) {
                    open_modal(s, ModalKind::HeroDuplicate, entry.yaml_name);
                }
                if (ImGui::MenuItem("Rename ...")) {
                    open_modal(s, ModalKind::HeroRename, entry.yaml_name);
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                       ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                if (ImGui::MenuItem("Delete ...")) {
                    open_modal(s, ModalKind::HeroDelete, entry.yaml_name);
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (ImGui::BeginPopupContextWindow("##hero_window_ctx",
                ImGuiPopupFlags_MouseButtonRight |
                ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New hero ...")) {
                open_modal(s, ModalKind::HeroNew);
            }
            if (ImGui::MenuItem("Reload")) scan_heroes(s, current_hero_stem(s));
            ImGui::EndPopup();
        }
        if (s.hero_idx != prev) {
            if (any_dirty(s)) {
                const std::string source = s.hero_catalog.heroes()[prev].yaml_name;
                const std::string target = s.hero_catalog.heroes()[s.hero_idx].yaml_name;
                s.hero_idx = prev;
                open_modal(s, ModalKind::DirtyGuard, source);
                s.modal.pending_select = target;
            } else {
                load_hero_doc(s);
            }
        }
    }
    ImGui::End();
}

void draw_ability_list_pane(State& s, float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + kTabBarW, vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(ImVec2(kListW, vp->WorkSize.y - top - bottom));
    if (ImGui::Begin("Abilities", nullptr,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoCollapse)) {
        const int prev = s.ability_idx;
        for (std::size_t i = 0; i < s.ability_catalog.entries().size(); ++i) {
            const auto& entry = s.ability_catalog.entries()[i];
            const bool sel = (static_cast<int>(i) == s.ability_idx);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(entry.yaml_stem.c_str(), sel)) {
                s.ability_idx = static_cast<int>(i);
            }
            if (ImGui::BeginPopupContextItem("##ab_ctx")) {
                s.ability_idx = static_cast<int>(i);
                if (ImGui::MenuItem("Duplicate ...")) {
                    open_modal(s, ModalKind::AbilityDuplicate, entry.yaml_stem);
                }
                if (ImGui::MenuItem("Rename ...")) {
                    open_modal(s, ModalKind::AbilityRename, entry.yaml_stem);
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                       ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                if (ImGui::MenuItem("Delete ...")) {
                    open_modal(s, ModalKind::AbilityDelete, entry.yaml_stem);
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (ImGui::BeginPopupContextWindow("##ab_window_ctx",
                ImGuiPopupFlags_MouseButtonRight |
                ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New ability_datadriven ...")) {
                open_modal(s, ModalKind::AbilityNewDD);
            }
            if (ImGui::MenuItem("New ability_lua ...")) {
                open_modal(s, ModalKind::AbilityNewLua);
            }
            if (ImGui::MenuItem("Reload")) scan_abilities(s, current_ability_stem(s));
            ImGui::EndPopup();
        }
        if (s.ability_idx != prev) {
            if (any_dirty(s)) {
                const std::string source = s.ability_catalog.entries()[prev].yaml_stem;
                const std::string target = s.ability_catalog.entries()[s.ability_idx].yaml_stem;
                s.ability_idx = prev;
                open_modal(s, ModalKind::DirtyGuard, source);
                s.modal.pending_select = target;
            } else {
                load_ability_doc(s);
            }
        }
    }
    ImGui::End();
}

void draw_detail_pane(State& s, float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float left = kTabBarW + (s.tab == Tab::Modifiers ? 0.0f : kListW);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + left, vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - left,
                                      vp->WorkSize.y - top - bottom));
    if (ImGui::Begin("##detail", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        if (s.tab == Tab::Heroes) {
            if (!s.hero_doc) {
                ImGui::TextDisabled("(选一个英雄开始编辑)");
            } else {
                std::vector<std::string> pool;
                pool.reserve(s.ability_catalog.entries().size());
                for (const auto& e : s.ability_catalog.entries()) {
                    pool.push_back(e.yaml_stem);
                }
                auto res = dota::hero_workshop::draw_hero_panel(
                    s.hero_doc->root(), s.hero_panel, pool);
                if (!res.status.empty()) set_status(s, std::move(res.status));
            }
        } else if (s.tab == Tab::Abilities) {
            if (!s.ability_doc) {
                ImGui::TextDisabled("(选一个 ability 开始编辑)");
            } else {
                auto res = dota::hero_workshop::draw_ability_form(
                    s.ability_doc->root(), s.ability_panel,
                    fs::path(data_dir()));
                if (!res.status.empty()) set_status(s, std::move(res.status));
            }
        } else {
            auto res = dota::hero_workshop::draw_modifier_panel(
                s.modifier_panel, fs::path(data_dir()));
            if (!res.status.empty()) set_status(s, std::move(res.status));
        }
    }
    ImGui::End();
}

void draw_status_bar(const State& s, float status_h) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x,
                                    vp->WorkPos.y + vp->WorkSize.y - status_h));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, status_h));
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        const bool show_msg = ImGui::GetTime() < s.status_until &&
                               !s.status_message.empty();
        if (show_msg) {
            ImGui::TextUnformatted(s.status_message.c_str());
        } else if (s.tab == Tab::Heroes && s.hero_doc) {
            const auto& entry = s.hero_catalog.heroes()[s.hero_idx];
            ImGui::Text("%s%s", entry.yaml_path.c_str(),
                         s.hero_panel.dirty ? "  [unsaved]" : "");
        } else if (s.tab == Tab::Abilities && s.ability_doc) {
            const auto& entry = s.ability_catalog.entries()[s.ability_idx];
            ImGui::Text("%s%s", entry.yaml_path.c_str(),
                         s.ability_panel.dirty ? "  [unsaved]" : "");
        } else {
            ImGui::TextDisabled("(no document)");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void update_window_title(const State& s) {
    std::string title = "dota2_skill -- hero workshop";
    if (s.tab == Tab::Heroes && s.hero_doc) {
        title += " [Heroes] " + current_hero_stem(s) + ".yaml";
        if (s.hero_panel.dirty) title += " *";
    } else if (s.tab == Tab::Abilities && s.ability_doc) {
        title += " [Abilities] " + current_ability_stem(s) + ".yaml";
        if (s.ability_panel.dirty) title += " *";
    } else if (s.tab == Tab::Modifiers) {
        title += " [Modifiers]";
    }
    SetWindowTitle(title.c_str());
}

void handle_shortcuts(State& s) {
    if (ImGui::GetIO().WantTextInput) return;
    const bool ctrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save_current_tab(s);
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (s.tab == Tab::Heroes)    scan_heroes(s, current_hero_stem(s));
        if (s.tab == Tab::Abilities) scan_abilities(s, current_ability_stem(s));
    }
    if (ImGui::IsKeyPressed(ImGuiKey_1, false) &&
        !ImGui::GetIO().WantCaptureKeyboard) try_switch_tab(s, Tab::Heroes);
    if (ImGui::IsKeyPressed(ImGuiKey_2, false) &&
        !ImGui::GetIO().WantCaptureKeyboard) try_switch_tab(s, Tab::Abilities);
    if (ImGui::IsKeyPressed(ImGuiKey_3, false) &&
        !ImGui::GetIO().WantCaptureKeyboard) try_switch_tab(s, Tab::Modifiers);
}

void load_cjk_font() {
    // 字体路径解析: 运行期 env DOTA_CJK_FONT_PATH 优先, 否则用编译期烤进去的宏.
    // 打包发布的可执行文件可以通过启动器把 env 指向相对路径下的字体.
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

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(kWindowW, kWindowH, "dota2_skill -- hero workshop");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    rlImGuiSetup(true);
    load_cjk_font();

    State state;
    scan_heroes(state);
    scan_abilities(state);

    while (!state.quit_requested) {
        if (WindowShouldClose()) {
            void* handle = GetWindowHandle();
            glfwSetWindowShouldClose(handle, 0);
            if (any_dirty(state)) {
                open_modal(state, ModalKind::QuitGuard,
                            state.tab == Tab::Heroes
                                ? current_hero_stem(state)
                                : current_ability_stem(state));
            } else {
                state.quit_requested = true;
                break;
            }
        }

        rlImGuiBegin();
        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        const float menu_h   = draw_main_menu(state);
        const float status_h = ImGui::GetFrameHeight();
        draw_left_tab_bar(state, menu_h, status_h);
        if (state.tab == Tab::Heroes)    draw_hero_list_pane(state, menu_h, status_h);
        if (state.tab == Tab::Abilities) draw_ability_list_pane(state, menu_h, status_h);
        draw_detail_pane(state, menu_h, status_h);
        draw_status_bar(state, status_h);
        draw_modals(state);

        handle_shortcuts(state);
        update_window_title(state);

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
