cmake_minimum_required(VERSION 3.20)
project(dota2_skill CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

# --- Core library ---
add_library(dota_core
    src/core/event_bus.cpp
    src/core/unit.cpp
    src/core/world.cpp
    src/modifier/modifier.cpp
    src/modifier/manager.cpp
)
target_include_directories(dota_core PUBLIC include)
target_compile_features(dota_core PUBLIC cxx_std_20)

# --- Example ---
add_executable(duel examples/duel.cpp)
target_link_libraries(duel PRIVATE dota_core)

# --- Tests ---
enable_testing()
add_executable(dota_tests
    tests/test_event_bus.cpp
    tests/test_unit_basic.cpp
    tests/test_modifier_property.cpp
    tests/test_modifier_state.cpp
    tests/test_modifier_event.cpp
    tests/test_modifier_lifecycle.cpp
)
target_link_libraries(dota_tests PRIVATE dota_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(dota_tests)
