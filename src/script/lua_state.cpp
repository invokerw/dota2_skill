#include "dota/script/lua_state.hpp"

#include "dota/modifier/registry.hpp"

#include <cstdio>
#include <filesystem>

namespace dota {

LuaState::LuaState()
#ifdef DOTA_SCRIPT_DIR
    : script_root_(DOTA_SCRIPT_DIR)
#endif
{
    modifier_registry_ = std::make_unique<LuaModifierRegistry>();
    lua_.open_libraries(sol::lib::base,
                         sol::lib::table,
                         sol::lib::string,
                         sol::lib::math);
    error_handler_ = [](const std::string& msg) {
        std::fprintf(stderr, "[lua error] %s\n", msg.c_str());
    };
    register_bindings(lua_, this);

    // 自动加载 <script_root>/modifiers 目录下的所有 .lua -- 它们在文件顶层调用
    // register_modifier(...). 这样调用方不需要手动 require 每个 modifier.
    if (!script_root_.empty()) {
        namespace fs = std::filesystem;
        const fs::path modifiers_dir = fs::path(script_root_) / "modifiers";
        if (fs::is_directory(modifiers_dir)) {
            for (const auto& entry : fs::directory_iterator(modifiers_dir)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".lua") continue;
                auto r = lua_.safe_script_file(entry.path().string(),
                                                &sol::script_pass_on_error);
                if (!r.valid()) {
                    sol::error err = r;
                    report_error("auto-load " + entry.path().filename().string(), err.what());
                }
            }
        }
    }
}

LuaState::~LuaState() = default;

LuaModifierRegistry& LuaState::modifier_registry() {
    return *modifier_registry_;
}
const LuaModifierRegistry& LuaState::modifier_registry() const {
    return *modifier_registry_;
}

sol::table LuaState::load_module(const std::string& path) {
    std::string resolved = path;
    if (!path.empty() && path.front() != '/' && !script_root_.empty()) {
        resolved = script_root_;
        if (resolved.back() != '/') resolved.push_back('/');
        resolved += path;
    }
    sol::protected_function_result r = lua_.safe_script_file(
        resolved, &sol::script_pass_on_error);
    if (!r.valid()) {
        const sol::error err = r;
        report_error("load_module " + resolved, err.what());
        return sol::table{};
    }
    sol::object obj = r;
    if (!obj.is<sol::table>()) {
        report_error("load_module " + resolved, "script did not return a table");
        return sol::table{};
    }
    return obj.as<sol::table>();
}

void LuaState::report_error(const std::string& where, const std::string& what) {
    if (error_handler_) error_handler_(where + ": " + what);
}

} // namespace dota
