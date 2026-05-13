#include "dota/script/lua_state.hpp"

#include <cstdio>

namespace dota {

LuaState::LuaState()
#ifdef DOTA_SCRIPT_DIR
    : script_root_(DOTA_SCRIPT_DIR)
#endif
{
    lua_.open_libraries(sol::lib::base,
                         sol::lib::table,
                         sol::lib::string,
                         sol::lib::math);
    error_handler_ = [](const std::string& msg) {
        std::fprintf(stderr, "[lua error] %s\n", msg.c_str());
    };
    register_bindings(lua_);
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
