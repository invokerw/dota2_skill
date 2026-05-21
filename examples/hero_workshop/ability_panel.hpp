#pragma once

// Ability 编辑面板. 直接对当前 HeroDoc 的 abilities 列表进行结构化编辑;
// 写盘走主程序的 Save 按钮.

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <string>

namespace dota::hero_workshop {

// 当前 ability 选中索引等编辑状态.
struct AbilityPanelState {
    int  selected = -1;        // -1 表示没选; 切英雄时调 reset.
    bool dirty    = false;     // 上次 reload 之后有过编辑

    // 右键 "New ability" modal 的状态.
    enum class AddMode { None, DataDriven, Lua } add_mode = AddMode::None;
    bool add_pending_open = false;
    char add_input[64]{};
    std::string add_error;
};

void reset_for_new_doc(AbilityPanelState& s);

// root 是 HeroDoc::root() (浅引用); 函数内部直接对它修改. data_root 用于
// 删除 lua 时找脚本和 trash.
// 返回 status 文本 (空 = 无新事件), 调用方可用于 toast.
//
// stem 是当前英雄 yaml 的文件 stem (e.g. "lion"), 用于 lua 模板默认文件名.
//
// 调用方应该在改完之后用 HeroDoc::save_to() 落盘.
struct AbilityPanelResult {
    std::string status;       // 提示文本 (新建/删除/错误)
};
AbilityPanelResult draw_ability_panel(YAML::Node root,
                                       AbilityPanelState& s,
                                       const std::filesystem::path& data_root,
                                       const std::string& hero_stem);

} // namespace dota::hero_workshop
