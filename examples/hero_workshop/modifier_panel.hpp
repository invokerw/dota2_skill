#pragma once

// Modifier 编辑面板. 文件级 CRUD + 模板生成 + Open in $EDITOR; 复杂的
// spec 表交给外部编辑器, 不在 IDE 端 emit Lua.

#include "dota/tools/modifier_ops.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace dota::hero_workshop {

struct ModifierPanelState {
    int  selected = -1;
    char input[64]{};                                  // new / dup / rename 的目标名
    int  template_idx = 0;                             // ModifierTemplate 下拉选择
    bool dirty_disk = false;                           // 上次操作改了磁盘 -> 提示重 scan
    enum class Mode { None, New, Duplicate, Rename, Delete } mode =
        Mode::None;
    bool                       pending_open = false;
    std::string                source;                 // dup/rename/delete 的源 name
    std::string                error;
};

struct ModifierPanelResult {
    std::string status;
};

// data_root 用于定位 scripts/modifiers; 调用方负责绘制窗体.
ModifierPanelResult draw_modifier_panel(
    ModifierPanelState& s,
    const std::filesystem::path& data_root);

} // namespace dota::hero_workshop
