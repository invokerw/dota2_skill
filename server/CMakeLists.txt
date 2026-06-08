cmake_minimum_required(VERSION 3.20)

# Server 子项目
project(dota2_server VERSION 0.1.0 LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 依赖已在父项目中查找
# Protobuf, libevent::core, libevent::extra 已经可用

# ============ Protobuf 协议编译 ============
add_subdirectory(proto)

# ============ KCP 库 ============
# 使用项目根的 third_party/kcp 或本地 third_party/kcp
set(KCP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/kcp")
if(NOT EXISTS ${KCP_DIR})
  set(KCP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/kcp")
endif()

add_library(kcp STATIC
  ${KCP_DIR}/ikcp.c
)
target_include_directories(kcp PUBLIC ${KCP_DIR})

# ============ 服务器网络库 ============
add_library(server_network STATIC
  src/network/kcp_session.cpp
  src/network/server_network.cpp
  src/network/message_handler.cpp
)

target_include_directories(server_network PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(server_network PUBLIC
  dota_core
  server_proto
  kcp
  libevent::core
  libevent::extra
)

# ============ 服务器逻辑库 ============
add_library(server_logic STATIC
  src/server/game_server.cpp
  src/server/game_server_handler.cpp
  src/server/game_session.cpp
  src/server/player_state.cpp
  src/mode/survivor_mode.cpp
  src/mode/wave_spawner.cpp
  src/mode/experience_system.cpp
  src/mode/skill_pool.cpp
)

target_include_directories(server_logic PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(server_logic PUBLIC
  dota_core
  server_proto
  server_network
)

# ============ 服务器可执行文件 ============
add_executable(game_server
  src/main.cpp
)

target_link_libraries(game_server PRIVATE
  server_logic
)

# ============ 工具 ============
add_executable(simple_client
  tools/simple_client.cpp
)

target_link_libraries(simple_client PRIVATE
  server_network
)

# 压力测试客户端 (Unix/Linux only, uses arpa/inet.h and POSIX sockets)
if(NOT WIN32)
  add_executable(stress_test_client
    tools/stress_test_client.cpp
  )

  target_link_libraries(stress_test_client PRIVATE
    server_network
  )
endif()

# ============ 测试 ============
if(BUILD_TESTING)
  add_subdirectory(test)
endif()

# ============ 安装 ============
install(TARGETS game_server
  RUNTIME DESTINATION bin
)

install(DIRECTORY config/
  DESTINATION etc/dota2_server
)
