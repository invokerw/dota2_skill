#pragma once

#include "dota/ability/behavior.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dota::tools {

// 单个技能的元数据 (从 yaml 直接抽取, 不实例化 Ability).
struct AbilityMeta {
    std::string   name;
    std::uint32_t behavior     = 0;
    TargetTeam    target_team  = TargetTeam::None;
    double        cast_range   = 0.0;
    double        cast_point   = 0.0;
    bool          is_passive   = false;
    bool          is_channelled = false;
};

// 单个英雄 yaml 顶层信息 + 技能列表.
struct HeroEntry {
    std::string yaml_name;       // 文件名 stem, e.g. "lion"
    std::string display_name;    // hero.name 字段
    std::string yaml_path;       // 绝对路径, 给 AbilityRegistry::load_file 用
    double      base_health  = 0.0;
    double      base_mana    = 0.0;
    double      base_armor   = 0.0;
    double      magic_resist = 0.25;
    std::vector<AbilityMeta> abilities;  // 按 yaml 顺序
};

// 扫描英雄目录, 收集 HeroEntry. 可重复调用 (会清空旧数据).
class HeroCatalog {
public:
    // 扫 directory 下所有 *.yaml. 没有 hero: 块的文件跳过.
    // 返回新加入的英雄数量. 解析失败时抛出 std::runtime_error.
    std::size_t scan(const std::string& directory);

    const std::vector<HeroEntry>& heroes() const { return heroes_; }

    // 按 yaml_name 查找. 找不到返回 nullptr.
    const HeroEntry* find(const std::string& yaml_name) const;

private:
    std::vector<HeroEntry> heroes_;
};

} // namespace dota::tools
