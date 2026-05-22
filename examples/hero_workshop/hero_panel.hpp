#pragma once

// Heroes tab: 左侧 hero 列表 + 右侧详情. 详情包括 hero 元字段 (name /
// base_*) 和 abilities 引用列表 (上下移 / 删除 / + 选已有 ability).

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <string>
#include <vector>

namespace dota::hero_workshop {

struct HeroPanelState {
    bool dirty = false;     // 与 main.cpp 的 doc dirty 一起聚合到 any_dirty.
    int  add_ref_combo_idx = 0;   // "Add ref" 的下拉选择
};

struct HeroPanelResult {
    std::string status;
};

// 在 root (HeroDoc::root()) 上原地编辑. ability_pool 是可选 ability 名列表
// (从 AbilityCatalog 拿). 返回 status 文本.
HeroPanelResult draw_hero_panel(YAML::Node root,
                                 HeroPanelState& s,
                                 const std::vector<std::string>& ability_pool);

} // namespace dota::hero_workshop
