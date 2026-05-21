// hero_workshop -- 英雄 / 技能 / Modifier 编辑器.
//
// 顶部 MenuBar (File / Edit / Help), 中部 Heroes + Editor, 底部 StatusBar.
// 标题栏显示当前 yaml 文件 + 未保存 [*]; Ctrl+S 保存; 关闭窗口若有未保存
// 改动会拦截并弹出确认.

#include "ability_panel.hpp"
#include "modifier_panel.hpp"

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

// raylib 链接进了 glfw3 但不暴露其头. 我们只需要清掉 close flag 来拦截关
// 窗事件, 自己声明这个 C API 即可.
extern "C" {
    void glfwSetWindowShouldClose(void* window, int value);
}

namespace fs = std::filesystem;

namespace {

constexpr int kWindowW   = 1280;
constexpr int kWindowH   = 720;
constexpr int kHeroListW = 240;

std::string data_dir() {
    if (const char* d = std::getenv("DOTA_DATA_DIR")) return d;
    return DOTA_DATA_DIR;
}

enum class ModalKind {
    None, New, Duplicate, Rename, Delete, DirtyGuard, Diff, QuitGuard
};

struct ModalState {
    ModalKind kind = ModalKind::None;
    bool      pending_open = false;
    char      input[64]{};
    std::string source_stem;
    std::vector<std::string> impacted_files;
    std::string error;

    // DirtyGuard: 切英雄拦截.
    std::string pending_select;

    // Diff: 当前 doc 与磁盘内容对照.
    std::string diff_disk;
    std::string diff_emit;
};

struct WorkshopState {
    int                                  selected_hero = 0;
    dota::tools::HeroCatalog             catalog;
    std::optional<dota::tools::HeroDoc>  current_doc;
    std::string                          status_message;
    double                               status_until = 0.0;
    bool                                 dirty = false;
    bool                                 quit_requested = false;
    ModalState                           modal;
    dota::hero_workshop::AbilityPanelState  ability_panel;
    dota::hero_workshop::ModifierPanelState modifier_panel;
};

void set_status(WorkshopState& s, std::string msg) {
    s.status_message = std::move(msg);
    s.status_until   = ImGui::GetTime() + 6.0;
}

void select_by_stem(WorkshopState& s, const std::string& stem) {
    for (std::size_t i = 0; i < s.catalog.heroes().size(); ++i) {
        if (s.catalog.heroes()[i].yaml_name == stem) {
            s.selected_hero = static_cast<int>(i);
            return;
        }
    }
    s.selected_hero = 0;
}

void reload(WorkshopState& s, const std::string& heroes_dir,
            const std::string& select_stem = {}) {
    try {
        s.catalog.scan(heroes_dir);
    } catch (const std::exception& e) {
        set_status(s, std::string("scan failed: ") + e.what());
        s.current_doc.reset();
        return;
    }
    if (s.catalog.heroes().empty()) {
        s.current_doc.reset();
        return;
    }
    if (!select_stem.empty()) {
        select_by_stem(s, select_stem);
    } else if (s.selected_hero < 0 ||
               s.selected_hero >= static_cast<int>(s.catalog.heroes().size())) {
        s.selected_hero = 0;
    }
    const auto& entry = s.catalog.heroes()[s.selected_hero];
    try {
        s.current_doc = dota::tools::HeroDoc::load(entry.yaml_path);
        s.dirty = false;
        dota::hero_workshop::reset_for_new_doc(s.ability_panel);
    } catch (const std::exception& e) {
        s.current_doc.reset();
        set_status(s, std::string("load failed: ") + e.what());
    }
}

void open_modal(WorkshopState& s, ModalKind k, const std::string& src = {}) {
    s.modal.kind = k;
    s.modal.pending_open = true;
    s.modal.error.clear();
    std::memset(s.modal.input, 0, sizeof(s.modal.input));
    s.modal.source_stem = src;
    s.modal.impacted_files.clear();

    if (k == ModalKind::Duplicate || k == ModalKind::Rename) {
        std::string seed = src + (k == ModalKind::Duplicate ? "_copy" : "");
        if (seed.size() < sizeof(s.modal.input)) {
            std::memcpy(s.modal.input, seed.c_str(), seed.size());
        }
    } else if (k == ModalKind::Delete) {
        const auto files = dota::tools::collect_hero_files(
            fs::path(data_dir()), src);
        s.modal.impacted_files.push_back(files.yaml_path.string());
        for (const auto& p : files.ability_scripts) {
            s.modal.impacted_files.push_back(p.string());
        }
    }
}

void close_modal(WorkshopState& s) {
    s.modal.kind = ModalKind::None;
    s.modal.error.clear();
    ImGui::CloseCurrentPopup();
}

bool any_dirty(const WorkshopState& s) {
    return s.dirty || s.ability_panel.dirty;
}

bool do_save(WorkshopState& s, const std::string& heroes_dir) {
    if (!s.current_doc) return false;
    const auto& entry = s.catalog.heroes()[s.selected_hero];
    try {
        s.current_doc->save_to(entry.yaml_path);
        set_status(s, "saved: " + entry.yaml_path);
        reload(s, heroes_dir, entry.yaml_name);
        return true;
    } catch (const std::exception& e) {
        set_status(s, std::string("save failed: ") + e.what());
        return false;
    }
}

std::string read_file_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream os; os << f.rdbuf();
    return os.str();
}

template <typename Op>
void draw_input_modal(WorkshopState& s, const char* label, const char* hint,
                      Op&& op) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup(label);
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(label, nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", hint);
        if (!s.modal.source_stem.empty()) {
            ImGui::Text("source: %s", s.modal.source_stem.c_str());
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##stem_input", s.modal.input,
                         sizeof(s.modal.input));
        ImGui::TextDisabled("允许 a-z 0-9 _ , 不以 _ 开头.");
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.modal.error.c_str());
        }
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
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

void draw_delete_modal(WorkshopState& s, const std::string& heroes_dir) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Delete hero");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Delete hero", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "确认把 [%s] 移到 .trash/? 受影响文件 (%zu):",
            s.modal.source_stem.c_str(),
            s.modal.impacted_files.size());
        ImGui::BeginChild("##impacted", ImVec2(460.0f, 120.0f), true);
        for (const auto& p : s.modal.impacted_files) {
            ImGui::TextWrapped("%s", p.c_str());
        }
        ImGui::EndChild();
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.modal.error.c_str());
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
                auto recs = dota::tools::delete_hero(
                    fs::path(data_dir()), s.modal.source_stem);
                set_status(s,
                    std::string("trashed ") + std::to_string(recs.size()) +
                        " files for " + s.modal.source_stem);
                close_modal(s);
                reload(s, heroes_dir);
            } catch (const std::exception& e) {
                s.modal.error = e.what();
            }
        }
        ImGui::EndPopup();
    }
}

void draw_dirty_guard_modal(WorkshopState& s, const std::string& heroes_dir) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Unsaved changes");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "[%s] 有未保存改动. 切到 [%s] 之前怎么处理?",
            s.modal.source_stem.c_str(),
            s.modal.pending_select.c_str());

        if (ImGui::Button("Save & switch", ImVec2(140.0f, 0.0f))) {
            if (do_save(s, heroes_dir)) {
                const std::string target = s.modal.pending_select;
                close_modal(s);
                reload(s, heroes_dir, target);
            } else {
                s.modal.error = "save failed; 留在原地";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f))) {
            const std::string target = s.modal.pending_select;
            close_modal(s);
            s.dirty = false;
            s.ability_panel.dirty = false;
            reload(s, heroes_dir, target);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.modal.error.c_str());
        }
        ImGui::EndPopup();
    }
}

void draw_diff_modal(WorkshopState& s, const std::string& heroes_dir) {
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

        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
        ImGui::SameLine();
        if (ImGui::Button("Confirm save", ImVec2(160.0f, 0.0f))) {
            close_modal(s);
            do_save(s, heroes_dir);
        }
        ImGui::EndPopup();
    }
}

void draw_quit_guard_modal(WorkshopState& s, const std::string& heroes_dir) {
    if (s.modal.pending_open) {
        ImGui::OpenPopup("Unsaved changes");
        s.modal.pending_open = false;
    }
    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "[%s] 有未保存改动. 退出前怎么处理?",
            s.modal.source_stem.c_str());
        if (ImGui::Button("Save & quit", ImVec2(140.0f, 0.0f))) {
            if (do_save(s, heroes_dir)) {
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
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            close_modal(s);
        }
        if (!s.modal.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "%s", s.modal.error.c_str());
        }
        ImGui::EndPopup();
    }
}

void draw_modals(WorkshopState& s, const std::string& heroes_dir) {
    switch (s.modal.kind) {
        case ModalKind::New:
            draw_input_modal(s, "New hero",
                "新建一个英雄 yaml. 文件名 = stem.yaml.",
                [&](const std::string& stem) {
                    dota::tools::create_hero(fs::path(data_dir()), stem);
                    set_status(s, "created: " + stem);
                    reload(s, heroes_dir, stem);
                });
            break;
        case ModalKind::Duplicate:
            draw_input_modal(s, "Duplicate hero",
                "复制英雄 + 它独占的 ability_lua 脚本, 自动重写 stem 前缀.",
                [&](const std::string& stem) {
                    dota::tools::duplicate_hero(fs::path(data_dir()),
                                                s.modal.source_stem, stem);
                    set_status(s, "duplicated -> " + stem);
                    reload(s, heroes_dir, stem);
                });
            break;
        case ModalKind::Rename:
            draw_input_modal(s, "Rename hero",
                "重命名 yaml 和它独占的脚本.",
                [&](const std::string& stem) {
                    dota::tools::rename_hero(fs::path(data_dir()),
                                             s.modal.source_stem, stem);
                    set_status(s, "renamed -> " + stem);
                    reload(s, heroes_dir, stem);
                });
            break;
        case ModalKind::Delete:
            draw_delete_modal(s, heroes_dir);
            break;
        case ModalKind::DirtyGuard:
            draw_dirty_guard_modal(s, heroes_dir);
            break;
        case ModalKind::Diff:
            draw_diff_modal(s, heroes_dir);
            break;
        case ModalKind::QuitGuard:
            draw_quit_guard_modal(s, heroes_dir);
            break;
        case ModalKind::None:
            break;
    }
}

void open_diff_modal(WorkshopState& s) {
    if (!s.current_doc) return;
    const auto& entry = s.catalog.heroes()[s.selected_hero];
    open_modal(s, ModalKind::Diff, entry.yaml_name);
    s.modal.diff_disk = read_file_text(fs::path(entry.yaml_path));
    try {
        s.modal.diff_emit = s.current_doc->emit();
    } catch (const std::exception& e) {
        s.modal.diff_emit = std::string("emit failed: ") + e.what();
    }
}

void open_trash_dir() {
    const fs::path trash = fs::path(data_dir()) / ".trash";
    fs::create_directories(trash);
    const std::string cmd = "open \"" + trash.string() + "\" &";
    std::system(cmd.c_str());
}

float draw_main_menu(WorkshopState& s, const std::string& heroes_dir) {
    float h = 0.0f;
    if (ImGui::BeginMainMenuBar()) {
        const bool has_doc = s.current_doc.has_value();
        const bool has_sel = !s.catalog.heroes().empty() &&
                             s.selected_hero >= 0 &&
                             s.selected_hero <
                                 static_cast<int>(s.catalog.heroes().size());
        const std::string sel_stem = has_sel
            ? s.catalog.heroes()[s.selected_hero].yaml_name : std::string{};

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New hero ...", "Ctrl+N")) {
                open_modal(s, ModalKind::New);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, has_doc)) {
                do_save(s, heroes_dir);
            }
            if (ImGui::MenuItem("Diff & Save ...", nullptr, false, has_doc)) {
                open_diff_modal(s);
            }
            if (ImGui::MenuItem("Reload", "F5")) {
                reload(s, heroes_dir, sel_stem);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open .trash dir")) {
                open_trash_dir();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Cmd+Q")) {
                if (any_dirty(s)) {
                    open_modal(s, ModalKind::QuitGuard, sel_stem);
                } else {
                    s.quit_requested = true;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Hero")) {
            if (ImGui::MenuItem("Duplicate ...", nullptr, false, has_sel)) {
                open_modal(s, ModalKind::Duplicate, sel_stem);
            }
            if (ImGui::MenuItem("Rename ...", nullptr, false, has_sel)) {
                open_modal(s, ModalKind::Rename, sel_stem);
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
            if (ImGui::MenuItem("Delete ...", nullptr, false, has_sel)) {
                open_modal(s, ModalKind::Delete, sel_stem);
            }
            ImGui::PopStyleColor();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::TextDisabled("dota2_skill -- hero workshop");
            ImGui::TextDisabled("右键英雄 / ability / modifier 弹出操作菜单.");
            ImGui::EndMenu();
        }
        h = ImGui::GetWindowSize().y;
        ImGui::EndMainMenuBar();
    }
    return h;
}

void draw_status_bar(const WorkshopState& s, float status_h) {
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
        } else if (s.current_doc) {
            const auto& entry = s.catalog.heroes()[s.selected_hero];
            ImGui::Text("%s%s", entry.yaml_path.c_str(),
                        any_dirty(s) ? "  [unsaved]" : "");
        } else {
            ImGui::TextDisabled("(no hero loaded)");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void draw_hero_list(WorkshopState& s, const std::string& heroes_dir,
                    float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(kHeroListW),
               vp->WorkSize.y - top - bottom));
    if (ImGui::Begin("Heroes", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        const int prev = s.selected_hero;
        for (std::size_t i = 0; i < s.catalog.heroes().size(); ++i) {
            const auto& entry = s.catalog.heroes()[i];
            const bool sel = (static_cast<int>(i) == s.selected_hero);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(entry.yaml_name.c_str(), sel)) {
                s.selected_hero = static_cast<int>(i);
            }
            if (ImGui::BeginPopupContextItem("##hero_ctx")) {
                s.selected_hero = static_cast<int>(i);
                if (ImGui::MenuItem("Duplicate ...")) {
                    open_modal(s, ModalKind::Duplicate, entry.yaml_name);
                }
                if (ImGui::MenuItem("Rename ...")) {
                    open_modal(s, ModalKind::Rename, entry.yaml_name);
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
                if (ImGui::MenuItem("Delete ...")) {
                    open_modal(s, ModalKind::Delete, entry.yaml_name);
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
                open_modal(s, ModalKind::New);
            }
            if (ImGui::MenuItem("Reload")) {
                reload(s, heroes_dir);
            }
            ImGui::EndPopup();
        }
        if (s.selected_hero != prev) {
            if (any_dirty(s)) {
                const auto& target = s.catalog.heroes()[s.selected_hero];
                std::string source =
                    s.catalog.heroes()[prev].yaml_name;
                std::string pending = target.yaml_name;
                s.selected_hero = prev;
                open_modal(s, ModalKind::DirtyGuard, source);
                s.modal.pending_select = std::move(pending);
            } else {
                reload(s, heroes_dir);
            }
        }
    }
    ImGui::End();
}

void draw_center_panel(WorkshopState& s, float top, float bottom) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + static_cast<float>(kHeroListW),
               vp->WorkPos.y + top));
    ImGui::SetNextWindowSize(
        ImVec2(vp->WorkSize.x - static_cast<float>(kHeroListW),
               vp->WorkSize.y - top - bottom));
    if (ImGui::Begin("Editor", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus)) {
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Abilities")) {
                if (!s.current_doc) {
                    ImGui::TextDisabled("(选一个英雄开始编辑)");
                } else {
                    const auto& entry = s.catalog.heroes()[s.selected_hero];
                    auto res = dota::hero_workshop::draw_ability_panel(
                        s.current_doc->root(),
                        s.ability_panel,
                        fs::path(data_dir()),
                        entry.yaml_name);
                    if (s.ability_panel.dirty) s.dirty = true;
                    if (!res.status.empty()) {
                        set_status(s, std::move(res.status));
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Modifiers")) {
                auto res = dota::hero_workshop::draw_modifier_panel(
                    s.modifier_panel, fs::path(data_dir()));
                if (!res.status.empty()) {
                    set_status(s, std::move(res.status));
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void update_window_title(const WorkshopState& s) {
    std::string title = "dota2_skill -- hero workshop";
    if (s.current_doc) {
        const auto& entry = s.catalog.heroes()[s.selected_hero];
        title += " -- " + entry.yaml_name + ".yaml";
        if (any_dirty(s)) title += " *";
    }
    SetWindowTitle(title.c_str());
}

void handle_shortcuts(WorkshopState& s, const std::string& heroes_dir) {
    // 让 ImGui 文本输入框优先消费按键.
    if (ImGui::GetIO().WantTextInput) return;

    const bool ctrl = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        do_save(s, heroes_dir);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        open_modal(s, ModalKind::New);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        const std::string sel = (!s.catalog.heroes().empty() &&
                                 s.selected_hero >= 0 &&
                                 s.selected_hero <
                                     static_cast<int>(s.catalog.heroes().size()))
            ? s.catalog.heroes()[s.selected_hero].yaml_name : std::string{};
        reload(s, heroes_dir, sel);
    }
}

void load_cjk_font() {
#ifdef DOTA_CJK_FONT_PATH
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseFull();
    ImFont* font = io.Fonts->AddFontFromFileTTF(
        DOTA_CJK_FONT_PATH, 16.0f, nullptr, ranges);
    if (!font) {
        io.Fonts->AddFontDefault();
    }
    rlImGuiReloadFonts();
#endif
}

} // namespace

int main() {
    const std::string root = data_dir();
    const std::string heroes_dir = root + "/heroes";

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(kWindowW, kWindowH, "dota2_skill -- hero workshop");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    rlImGuiSetup(true);
    load_cjk_font();

    WorkshopState state;
    reload(state, heroes_dir);

    while (!state.quit_requested) {
        // 拦截 OS 关窗 (raylib 检测到后会让 WindowShouldClose 返回 true).
        if (WindowShouldClose()) {
            void* handle = GetWindowHandle();
            glfwSetWindowShouldClose(handle, 0);
            if (any_dirty(state)) {
                const std::string sel =
                    state.current_doc
                        ? state.catalog.heroes()[state.selected_hero].yaml_name
                        : std::string{};
                open_modal(state, ModalKind::QuitGuard, sel);
            } else {
                state.quit_requested = true;
                break;
            }
        }

        rlImGuiBegin();
        BeginDrawing();
        ClearBackground(Color{18, 22, 28, 255});

        const float menu_h   = draw_main_menu(state, heroes_dir);
        const float status_h = ImGui::GetFrameHeight();
        draw_hero_list(state, heroes_dir, menu_h, status_h);
        draw_center_panel(state, menu_h, status_h);
        draw_status_bar(state, status_h);
        draw_modals(state, heroes_dir);

        handle_shortcuts(state, heroes_dir);
        update_window_title(state);

        rlImGuiEnd();
        EndDrawing();
    }

    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
