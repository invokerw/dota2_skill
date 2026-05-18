cmake_minimum_required(VERSION 3.20)
project(dota2_skill CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include(cmake/CPM.cmake)

# --- Stage 1 dependency: GoogleTest ---
CPMAddPackage(
    NAME googletest
    GITHUB_REPOSITORY google/googletest
    GIT_TAG v1.14.0
    OPTIONS
        "INSTALL_GTEST OFF"
        "gtest_force_shared_crt ON"
)

# --- Stage 3 dependency: yaml-cpp ---
# yaml-cpp 0.8.0 still declares an ancient minimum CMake version; let the
# configure pass through until they bump it upstream.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
CPMAddPackage(
    NAME yaml-cpp
    GITHUB_REPOSITORY jbeder/yaml-cpp
    GIT_TAG 0.8.0
    OPTIONS
        "YAML_CPP_BUILD_TESTS OFF"
        "YAML_CPP_BUILD_TOOLS OFF"
        "YAML_CPP_INSTALL OFF"
        "YAML_CPP_FORMAT_SOURCE OFF"
)

# --- Stage 4 dependency: Lua 5.4 (upstream ships no CMakeLists — build our
# own static lib from the source tarball) ---
CPMAddPackage(
    NAME lua
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
    URL_HASH SHA256=7d5ea1b9cb6aa0b59ca3dde1c6adcb57ef83a1ba8e5432c0ecd06bf439b3ad88
    DOWNLOAD_ONLY YES
)
if(lua_ADDED)
    file(GLOB LUA_SRCS "${lua_SOURCE_DIR}/src/*.c")
    list(REMOVE_ITEM LUA_SRCS
        "${lua_SOURCE_DIR}/src/lua.c"
        "${lua_SOURCE_DIR}/src/luac.c")
    add_library(lua STATIC ${LUA_SRCS})
    target_include_directories(lua PUBLIC "${lua_SOURCE_DIR}/src")
    if(APPLE)
        target_compile_definitions(lua PUBLIC LUA_USE_MACOSX)
    elseif(UNIX)
        target_compile_definitions(lua PUBLIC LUA_USE_LINUX)
    endif()
    # Lua's C code triggers pedantic C warnings; silence them locally.
    target_compile_options(lua PRIVATE -w)
endif()

# --- Stage 4 dependency: sol2 (header-only C++ bindings) ---
CPMAddPackage(
    NAME sol2
    GITHUB_REPOSITORY ThePhD/sol2
    GIT_TAG v3.3.0
    DOWNLOAD_ONLY YES
)
if(sol2_ADDED)
    add_library(sol2 INTERFACE)
    target_include_directories(sol2 INTERFACE "${sol2_SOURCE_DIR}/include")
    target_link_libraries(sol2 INTERFACE lua)
endif()

# --- Core library ---
add_library(dota_core
    src/combat/damage.cpp
    src/core/event_bus.cpp
    src/core/unit.cpp
    src/core/world.cpp
    src/modifier/modifier.cpp
    src/modifier/manager.cpp
    src/modifier/registry.cpp
    src/modifier/scripted.cpp
    src/projectile/projectile.cpp
    src/projectile/manager.cpp
    src/ability/ability.cpp
    src/ability/behavior_parse.cpp
    src/ability/datadriven.cpp
    src/ability/manager.cpp
    src/ability/registry.cpp
    src/ability/scripted.cpp
    src/script/lua_state.cpp
    src/script/bindings.cpp
)
target_include_directories(dota_core PUBLIC include)
target_compile_features(dota_core PUBLIC cxx_std_20)
target_link_libraries(dota_core PUBLIC yaml-cpp::yaml-cpp sol2)
target_compile_definitions(dota_core PUBLIC
    DOTA_SCRIPT_DIR=\"${CMAKE_SOURCE_DIR}/data/scripts\")

# --- Example ---
add_executable(duel examples/duel.cpp)
target_link_libraries(duel PRIVATE dota_core)
target_compile_definitions(duel PRIVATE
    DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")

# --- Tests ---
enable_testing()
add_executable(dota_tests
    tests/test_event_bus.cpp
    tests/test_unit_basic.cpp
    tests/test_modifier_property.cpp
    tests/test_modifier_state.cpp
    tests/test_modifier_event.cpp
    tests/test_modifier_lifecycle.cpp
    tests/test_ability_lifecycle.cpp
    tests/test_datadriven_loader.cpp
    tests/test_lion_earth_spike.cpp
    tests/test_lua_bindings.cpp
    tests/test_lua_ability.cpp
    tests/test_lua_modifier.cpp
    tests/test_lua_error_safety.cpp
    tests/test_damage_pipeline.cpp
    tests/test_heal_pipeline.cpp
    tests/test_hero_lion.cpp
    tests/test_hero_juggernaut.cpp
    tests/test_hero_lina.cpp
    tests/test_spatial_queries.cpp
    tests/test_lua_modifier_registry.cpp
    tests/test_purge.cpp
    tests/test_motion_knockback.cpp
    tests/test_projectile.cpp
    tests/test_thinker.cpp
    tests/test_hero_sven.cpp
    tests/test_hero_earthshaker.cpp
    tests/test_hero_pudge.cpp
    tests/test_event_emission.cpp
)
target_link_libraries(dota_tests PRIVATE dota_core GTest::gtest_main)
target_compile_definitions(dota_tests PRIVATE
    DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")

include(GoogleTest)
gtest_discover_tests(dota_tests)
