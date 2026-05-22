cmake_minimum_required(VERSION 3.20)
project(dota2_skill CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W3 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

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
    if(MSVC)
        target_compile_options(lua PRIVATE /w)
    else()
        target_compile_options(lua PRIVATE -w)
    endif()
endif()

# --- Visualization dependency: raylib (only needed by skill_tester) ---
# 仅在 BUILD_VISUAL 开启时拉取, 默认开启; 如想跳过请 -DBUILD_VISUAL=OFF.
option(BUILD_VISUAL "Build raylib-based visual demos" ON)
if(BUILD_VISUAL)
    # 直接拉 tarball 而非 git clone, 在网络较慢时更稳
    CPMAddPackage(
        NAME raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.0.tar.gz
        URL_HASH SHA256=98f049b9ea2a9c40a14e4e543eeea1a7ec3090ebdcd329c4ca2cf98bc9793482
        OPTIONS
            "BUILD_EXAMPLES OFF"
            "WITH_PIC ON"
    )

    # Dear ImGui: immediate-mode GUI 库. 用 docking 之外的 stable tag.
    # rlImGui 提供 raylib 的渲染/输入后端 (Setup / Begin / End / Shutdown).
    CPMAddPackage(
        NAME imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz
        URL_HASH SHA256=5d9dc738af74efa357f2a9fc39fe4a28d29ef1dfc725dd2977ccf3f3194e996e
        DOWNLOAD_ONLY YES
    )
    if(imgui_ADDED)
        add_library(imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
            "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        )
        target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
        if(MSVC)
            target_compile_options(imgui PRIVATE /w)
        else()
            target_compile_options(imgui PRIVATE -w)
        endif()
    endif()

    # rlImGui: imgui 的 raylib 后端. master 分支稳定够用; 固定到一个具体 commit
    # 以保证可复现.
    CPMAddPackage(
        NAME rlImGui
        GITHUB_REPOSITORY raylib-extras/rlImGui
        GIT_TAG d765c1ef3d37cf939f88aaa272a59a2713d654c9
        DOWNLOAD_ONLY YES
    )
    if(rlImGui_ADDED)
        add_library(rlImGui STATIC "${rlImGui_SOURCE_DIR}/rlImGui.cpp")
        target_include_directories(rlImGui PUBLIC "${rlImGui_SOURCE_DIR}")
        target_link_libraries(rlImGui PUBLIC imgui raylib)
        if(MSVC)
            target_compile_options(rlImGui PRIVATE /w)
        else()
            target_compile_options(rlImGui PRIVATE -w)
        endif()
    endif()
endif()

# --- CJK font (hero_workshop 中文显示) ---
# 下载一个开源 CJK TTF, 把绝对路径以宏 DOTA_CJK_FONT_PATH 传给 hero_workshop.
# 默认开启; 网络环境不允许时可 -DDOWNLOAD_CJK_FONT=OFF 跳过, 此时会退化到
# 系统字体路径搜索.
option(DOWNLOAD_CJK_FONT "Download a CJK font for hero_workshop" ON)
set(DOTA_CJK_FONT_PATH "" CACHE STRING "Path to CJK font TTF/TTC")
if(DOWNLOAD_CJK_FONT AND BUILD_VISUAL AND NOT DOTA_CJK_FONT_PATH)
    set(_cjk_dir   "${CMAKE_BINARY_DIR}/cjk_font")
    set(_cjk_file  "${_cjk_dir}/LXGWWenKai-Regular.ttf")
    set(_cjk_url
        "https://github.com/lxgw/LxgwWenKai/releases/download/v1.522/LXGWWenKai-Regular.ttf")
    if(NOT EXISTS "${_cjk_file}")
        file(MAKE_DIRECTORY "${_cjk_dir}")
        message(STATUS "Downloading CJK font: ${_cjk_url}")
        file(DOWNLOAD "${_cjk_url}" "${_cjk_file}"
             SHOW_PROGRESS
             STATUS _cjk_status
             TLS_VERIFY ON)
        list(GET _cjk_status 0 _cjk_code)
        if(NOT _cjk_code EQUAL 0)
            message(WARNING "CJK font download failed: ${_cjk_status}")
            file(REMOVE "${_cjk_file}")
        endif()
    endif()
    if(EXISTS "${_cjk_file}")
        set(DOTA_CJK_FONT_PATH "${_cjk_file}" CACHE STRING "" FORCE)
    endif()
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
    src/replay/recorder.cpp
    src/replay/player.cpp
    src/tools/hero_catalog.cpp
    src/tools/hero_writer.cpp
    src/tools/hero_ops.cpp
    src/tools/ability_doc.cpp
    src/tools/ability_ops.cpp
    src/tools/modifier_scanner.cpp
    src/tools/modifier_ops.cpp
    src/tools/trash.cpp
)
target_include_directories(dota_core PUBLIC include)
target_compile_features(dota_core PUBLIC cxx_std_20)
target_link_libraries(dota_core PUBLIC yaml-cpp::yaml-cpp sol2)
target_compile_definitions(dota_core PUBLIC
    DOTA_SCRIPT_DIR=\"${CMAKE_SOURCE_DIR}/data/scripts\")

# --- Example: skill_tester (raylib + imgui 交互式技能测试器) ---
if(BUILD_VISUAL)
    add_executable(skill_tester
        examples/skill_tester/main.cpp
        examples/skill_tester/scene.cpp
        examples/skill_tester/aim.cpp
        examples/skill_tester/input.cpp
        examples/skill_tester/waypoints.cpp
        examples/skill_tester/panels.cpp
        examples/skill_tester/modifier_catalog.cpp
    )
    target_link_libraries(skill_tester PRIVATE dota_core raylib rlImGui)
    target_compile_definitions(skill_tester PRIVATE
        DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")

    # --- Example: hero_workshop (英雄/技能/Modifier 编辑器) ---
    add_executable(hero_workshop
        examples/hero_workshop/main.cpp
        examples/hero_workshop/hero_panel.cpp
        examples/hero_workshop/ability_panel.cpp
        examples/hero_workshop/modifier_panel.cpp
    )
    target_link_libraries(hero_workshop PRIVATE dota_core raylib rlImGui)
    target_compile_definitions(hero_workshop PRIVATE
        DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")
    if(DOTA_CJK_FONT_PATH)
        target_compile_definitions(hero_workshop PRIVATE
            DOTA_CJK_FONT_PATH=\"${DOTA_CJK_FONT_PATH}\")
    endif()
endif()

# --- Tests ---
enable_testing()
add_executable(dota_tests
    tests/test_event_bus.cpp
    tests/test_unit_basic.cpp
    tests/test_unit_collision.cpp
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
    tests/test_movement_basic.cpp
    tests/test_order_queue.cpp
    tests/test_movement_wall_trace.cpp
    tests/test_movement_motion_controller.cpp
    tests/test_projectile.cpp
    tests/test_thinker.cpp
    tests/test_hero_sven.cpp
    tests/test_hero_earthshaker.cpp
    tests/test_hero_pudge.cpp
    tests/test_event_emission.cpp
    tests/test_recorder.cpp
    tests/test_replay_player.cpp
    tests/test_hero_catalog.cpp
    tests/test_skill_tester_smoke.cpp
    tests/test_hero_writer.cpp
    tests/test_trash.cpp
    tests/test_hero_ops.cpp
    tests/test_ability_ops.cpp
    tests/test_modifier_scanner.cpp
    tests/test_modifier_ops.cpp
    tests/test_ability_registry_dir.cpp
    tests/test_passive_intrinsic.cpp
    tests/test_modifier_on_ability_executed.cpp
    tests/test_attack_record.cpp
    tests/test_attack_bonus_damage.cpp
    tests/test_attack_orb.cpp
    tests/test_attack_projectile_name.cpp
    tests/test_hero_drow_ranger.cpp
    tests/test_hero_luna.cpp
    tests/test_hero_sven_cleave.cpp
    tests/test_hero_viper.cpp
    tests/test_hero_obsidian_destroyer.cpp
)
target_link_libraries(dota_tests PRIVATE dota_core GTest::gtest_main)
target_compile_definitions(dota_tests PRIVATE
    DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\")

include(GoogleTest)
gtest_discover_tests(dota_tests)
