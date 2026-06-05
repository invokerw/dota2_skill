cmake_minimum_required(VERSION 3.20)

# Protobuf 协议定义
find_package(Protobuf REQUIRED)

set(PROTO_FILES
  common.proto
  messages.proto
)

# 手动调用 protoc 以支持 proto3 optional
set(PROTO_SRCS)
set(PROTO_HDRS)

foreach(PROTO_FILE ${PROTO_FILES})
  get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
  set(PROTO_SRC "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
  set(PROTO_HDR "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")

  list(APPEND PROTO_SRCS ${PROTO_SRC})
  list(APPEND PROTO_HDRS ${PROTO_HDR})

  add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --experimental_allow_proto3_optional
         -I${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Running protoc on ${PROTO_FILE}"
    VERBATIM
  )
endforeach()

# 创建库
add_library(server_proto STATIC
  ${PROTO_SRCS}
  ${PROTO_HDRS}
)

target_link_libraries(server_proto PUBLIC
  protobuf::libprotobuf
)

# Protobuf 5.x 依赖 absl，需要显式链接
if(TARGET absl::base)
  target_link_libraries(server_proto PUBLIC
    absl::log_internal_check_op
    absl::log_internal_message
  )
endif()

# 包含生成的头文件目录
target_include_directories(server_proto PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
  ${Protobuf_INCLUDE_DIRS}
)

# 设置生成文件的编译选项（忽略警告）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(${PROTO_SRCS} PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-array-bounds"
  )
endif()
