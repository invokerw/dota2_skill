#pragma once

// Inspector "Add Modifier" 用的 modifier catalog. 把内置 c++ modifier 工厂 + Lua
// 注册的 modifier 统一封装成 ModifierAddSpec, 每条都带"创建函数"和"参数描述",
// 让 imgui 端可以纯按 spec 渲染参数控件 + 一键 attach.

#include "dota/core/types.hpp"
#include "dota/core/unit.hpp"
#include "dota/modifier/enums.hpp"
#include "dota/modifier/modifier.hpp"

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dota::skill_tester {

class Scene;

enum class ModifierParamKind {
    Number,
    Int,
    Property,
    Vec2,
};

struct ModifierParamSpec {
    std::string       key;
    std::string       label;
    ModifierParamKind kind{ModifierParamKind::Number};
    double            number_default{0.0};
    double            min{0.0};
    double            max{0.0};
    float             speed{0.1f};
    const char*       format{"%.2f"};
    int               int_default{0};
    int               int_min{0};
    int               int_max{0};
    Vec2              vec_default{0.0, 0.0};
};

struct ModifierParamValue {
    double number{0.0};
    int    integer{0};
    int    property_index{0};
    Vec2   vec{0.0, 0.0};
};

using ModifierParamBag = std::unordered_map<std::string, ModifierParamValue>;
using ModifierAddFactory =
    std::function<std::unique_ptr<Modifier>(Unit&, const ModifierParamBag&)>;

struct ModifierAddSpec {
    std::string                    name;
    std::string                    label;
    std::vector<ModifierParamSpec> params;
    ModifierAddFactory             create;
};

ModifierParamSpec number_param(std::string key, std::string label,
                               double def, double min_v, double max_v,
                               float speed, const char* format);
ModifierParamSpec int_param(std::string key, std::string label,
                            int def, int min_v, int max_v, float speed);
ModifierParamSpec property_param(std::string key, std::string label,
                                 ModifierProperty def);
ModifierParamSpec vec2_param(std::string key, std::string label,
                             Vec2 def, double min_v, double max_v,
                             float speed, const char* format);

// 大部分修饰器都共用 duration / stacks 两个参数, 提取成 helper.
std::vector<ModifierParamSpec> common_modifier_params();
std::vector<ModifierParamSpec>
with_common_params(std::initializer_list<ModifierParamSpec> extra);

void reset_modifier_param_values(const ModifierAddSpec& spec, ModifierParamBag& values);
void draw_modifier_param_controls(const ModifierAddSpec& spec, ModifierParamBag& values);

// 全部内置 c++ 修饰器 + Scene 当前 LuaState 中所有已注册的 lua modifier.
std::vector<ModifierAddSpec> build_modifier_catalog(Scene& scene);

} // namespace dota::skill_tester
