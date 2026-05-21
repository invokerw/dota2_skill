#pragma once

// HeroWriter: 把英雄 yaml 重新 emit 到磁盘, 保留全部字段 + 强制可读的 key
// 顺序 / flow 风格. Stage 1 仅提供 round-trip; 后续 stage 通过修改持有的
// YAML::Node 间接编辑 hero / ability 字段.
//
// 注意: yaml-cpp 不保留原文件的注释, 因此 round-trip 后注释会丢失.
// 这是设计决定, 接受重排版.

#include <yaml-cpp/yaml.h>

#include <string>

namespace dota::tools {

// 持有一份 hero yaml 的可变副本. 加载时直接拿 YAML::Node, save 走我们自己
// 的 ordered emitter. 后续 stage 会暴露便捷 setter 来改 hero / ability 字段.
class HeroDoc {
public:
    static HeroDoc load(const std::string& path);

    YAML::Node&       root()       { return root_; }
    const YAML::Node& root() const { return root_; }

    std::string emit() const;
    void        save_to(const std::string& path) const;

private:
    YAML::Node root_;
};

// 内部 / 测试也用得到: 直接对一个 YAML::Node 走 ordered emit, 返回字符串.
// root 必须是 hero yaml 顶层 (含 hero 块和 abilities 列表).
std::string emit_hero_yaml(const YAML::Node& root);

} // namespace dota::tools
