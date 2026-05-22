#pragma once

// Abilities tab: 左侧 ability 文件列表 + 右侧单 ability 的字段表单.
//
// 每个 ability 是 data/abilities/<name>.yaml 独立文件; 多个 hero 可引用同一个
// ability. 因此 panel 操作的是 AbilityDoc::root() (单 ability 顶层), 不再是
// hero yaml 内的 abilities[i].
//
// 列表 / 详情统一管理一份 AbilityDoc; selected 改变时由调用方 reload doc.

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <string>

namespace dota::hero_workshop {

struct AbilityPanelState {
    bool dirty = false;     // 当前 doc 有未保存改动
};

void reset_for_new_doc(AbilityPanelState& s);

struct AbilityPanelResult {
    std::string status;
};

// root 是 AbilityDoc::root() 浅引用. data_root 用于 Open in $EDITOR 拼绝对路径.
AbilityPanelResult draw_ability_form(YAML::Node root,
                                       AbilityPanelState& s,
                                       const std::filesystem::path& data_root);

} // namespace dota::hero_workshop
