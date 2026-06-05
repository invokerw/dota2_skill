# Server Tests

if(NOT TARGET GTest::gtest_main)
  message(FATAL_ERROR "GoogleTest not available for server tests")
endif()

# 包含 GoogleTest 模块
include(GoogleTest)

# Stage 1 测试
add_executable(server_test_stage1
  test_kcp_session.cpp
  test_protobuf_serialization.cpp
  # test_server_network.cpp  # 待 Stage 1 完成后添加
)

target_link_libraries(server_test_stage1 PRIVATE
  server_network
  GTest::gtest_main
)

target_compile_definitions(server_test_stage1 PRIVATE
  DOTA_DATA_DIR=\"${CMAKE_SOURCE_DIR}/data\"
)

gtest_discover_tests(server_test_stage1)

# Stage 2 测试 (待实现)
# add_executable(server_test_stage2
#   test_message_handler.cpp
#   test_game_server.cpp
#   test_connection_management.cpp
# )
# target_link_libraries(server_test_stage2 PRIVATE
#   server_logic
#   GTest::gtest_main
# )
# gtest_discover_tests(server_test_stage2)
