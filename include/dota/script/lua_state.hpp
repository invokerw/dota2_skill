#pragma once

#include <sol/sol.hpp>

#include <functional>
#include <memory>
#include <string>

namespace dota {

class World;
class LuaModifierRegistry;

// sol::state 的 RAII 包装器。保持默认开放的库（base/table/string/math）
// 对编写的脚本可用。`register_bindings()` 在构造时调用一次；
// ScriptedAbility/ScriptedModifier 假定用户类型表（Unit/World/Damage 等）
// 已经存在于全局命名空间中。
class LuaState {
public:
    LuaState();
    ~LuaState();
    LuaState(const LuaState&) = delete;
    LuaState& operator=(const LuaState&) = delete;

    sol::state&       state()       { return lua_; }
    const sol::state& state() const { return lua_; }

    LuaModifierRegistry&       modifier_registry();
    const LuaModifierRegistry& modifier_registry() const;

    // 传递给 load_module 的相对路径会相对于此根目录解析。
    // 默认值是 DOTA_SCRIPT_DIR（仓库的 scripts/ 目录），
    // 测试和 duel 示例都依赖于此。
    void               set_script_root(std::string root) { script_root_ = std::move(root); }
    const std::string& script_root() const               { return script_root_; }

    // 加载一个文件，返回它 `return` 的表。任何错误时返回空的 sol::table；
    // 错误通过注册的错误回调报告。绝对路径（以 '/' 开头）按原样使用；
    // 相对路径与 `script_root_` 拼接。
    sol::table load_module(const std::string& path);

    // 安装/替换错误回调。默认打印到 stderr。
    using ErrorHandler = std::function<void(const std::string&)>;
    void set_error_handler(ErrorHandler h) { error_handler_ = std::move(h); }

    // 报告一个 Lua 错误（由 ScriptedAbility/ScriptedModifier 在 pcall
    // 失败时使用，以便引擎继续运行）。
    void report_error(const std::string& where, const std::string& what);

private:
    sol::state    lua_;
    ErrorHandler  error_handler_;
    std::string   script_root_;
    std::unique_ptr<LuaModifierRegistry> modifier_registry_;
};

// 在给定的 sol::state 上注册所有 C++ user_types / 辅助函数。
// 分离出来以便测试可以在它们自己拥有的 state 上构建绑定。
// 当 owner 不为空时，`register_modifier` 等绑定会写入它的注册中心；
// 否则降级为 no-op（仅作 enum 暴露）。
void register_bindings(sol::state& lua, LuaState* owner = nullptr);

} // namespace dota
