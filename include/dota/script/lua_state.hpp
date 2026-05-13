#pragma once

#include <sol/sol.hpp>

#include <functional>
#include <string>

namespace dota {

class World;

// RAII wrapper around sol::state. Keeps the default-open libs (base/table/
// string/math) available to authored scripts. `register_bindings()` is called
// once at construction; ScriptedAbility/ScriptedModifier assume the user-type
// tables (Unit/World/Damage etc.) already exist in the global namespace.
class LuaState {
public:
    LuaState();

    sol::state&       state()       { return lua_; }
    const sol::state& state() const { return lua_; }

    // Relative paths passed to load_module are resolved against this root. The
    // default is DOTA_SCRIPT_DIR (the repo's scripts/ directory), which tests
    // and the duel example rely on.
    void               set_script_root(std::string root) { script_root_ = std::move(root); }
    const std::string& script_root() const               { return script_root_; }

    // Loads a file, returns the table it `return`s. Empty sol::table on any
    // error; the error is reported via the registered error callback. Absolute
    // paths (starting with '/') are used as-is; relative paths are joined with
    // `script_root_`.
    sol::table load_module(const std::string& path);

    // Install/replace the error callback. Default prints to stderr.
    using ErrorHandler = std::function<void(const std::string&)>;
    void set_error_handler(ErrorHandler h) { error_handler_ = std::move(h); }

    // Report a Lua error (used by ScriptedAbility/ScriptedModifier on pcall
    // failures so the engine keeps ticking).
    void report_error(const std::string& where, const std::string& what);

private:
    sol::state    lua_;
    ErrorHandler  error_handler_;
    std::string   script_root_;
};

// Register all C++ user_types / helpers on the given sol::state. Split out so
// tests can build bindings onto a state they own.
void register_bindings(sol::state& lua);

} // namespace dota
