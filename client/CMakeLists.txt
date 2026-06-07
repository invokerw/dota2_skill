cmake_minimum_required(VERSION 3.20)

# Client 子项目
project(dota2_client VERSION 0.1.0 LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============ 客户端网络库 ============
# 复用服务器的网络代码
add_library(client_network STATIC
  src/network_client.cpp
)

target_include_directories(client_network PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/server/include
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(client_network PUBLIC
  dota_core
  server_proto
  kcp
  libevent::core
)

# Windows 特殊处理：避免 Windows.h 宏污染
if(WIN32)
  target_compile_definitions(client_network PRIVATE
    WIN32_LEAN_AND_MEAN
    NOGDI
    NOUSER
  )
endif()

# 需要链接 server_network 以获取 KcpSession 实现
# 但 server_network 不在 BUILD_CLIENT 时构建，所以需要单独编译 KcpSession
# 简化方案：将 KcpSession 源文件直接添加到 client_network
target_sources(client_network PRIVATE
  ${CMAKE_SOURCE_DIR}/server/src/network/kcp_session.cpp
)

# ============ 客户端游戏逻辑 ============
add_library(client_game STATIC
  src/game_state.cpp
  src/renderer.cpp
  src/input_handler.cpp
)

target_include_directories(client_game PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(client_game PUBLIC
  dota_core
  client_network
  raylib
)

# Windows 特殊处理：避免 Windows.h 宏污染
if(WIN32)
  target_compile_definitions(client_game PRIVATE
    WIN32_LEAN_AND_MEAN
    NOGDI
    NOUSER
  )
endif()

# ============ 客户端可执行文件 ============
add_executable(game_client
  src/main.cpp
)

target_link_libraries(game_client PRIVATE
  client_game
)

# ============ 资源文件复制 ============
add_custom_command(TARGET game_client POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    ${CMAKE_CURRENT_BINARY_DIR}/assets
  COMMENT "Copying assets to build directory"
)

# ============ 安装 ============
install(TARGETS game_client
  RUNTIME DESTINATION bin
)

install(DIRECTORY assets/
  DESTINATION share/dota2_client/assets
)
