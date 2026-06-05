// server/test/test_kcp_session.cpp
// KCP Session 单元测试 - Stage 1

#include <gtest/gtest.h>

// TODO: Stage 1 实现后取消注释
// #include "server/network/kcp_session.hpp"
// #include <event2/event.h>

// 占位测试，确保测试框架正常工作
TEST(KcpSessionTest, Placeholder) {
  EXPECT_TRUE(true);
}

// TODO: Stage 1 实现以下测试

// TEST(KcpSessionTest, CreateAndDestroy) {
//   event_base* base = event_base_new();
//   ASSERT_NE(base, nullptr);
//
//   std::vector<uint8_t> output_buffer;
//   auto output_cb = [&](const char* buf, int len, void* user) {
//     output_buffer.insert(output_buffer.end(), buf, buf + len);
//   };
//
//   dota::network::KcpSession session(123, base, output_cb);
//   EXPECT_EQ(session.conv(), 123);
//   EXPECT_TRUE(session.is_alive());
//
//   event_base_free(base);
// }

// TEST(KcpSessionTest, SendAndReceive) {
//   event_base* base = event_base_new();
//   std::vector<uint8_t> output_buffer;
//   std::vector<uint8_t> received_message;
//
//   auto output_cb = [&](const char* buf, int len, void* user) {
//     output_buffer.insert(output_buffer.end(), buf, buf + len);
//   };
//
//   dota::network::KcpSession session(123, base, output_cb);
//
//   session.set_message_callback([&](const uint8_t* data, size_t len) {
//     received_message.assign(data, data + len);
//   });
//
//   // 发送消息
//   std::string msg = "Hello KCP";
//   session.send(reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
//
//   // 模拟接收 (将输出喂给 input)
//   session.input(output_buffer.data(), output_buffer.size());
//
//   // 处理接收
//   session.update();
//
//   EXPECT_EQ(std::string(received_message.begin(), received_message.end()), msg);
//
//   event_base_free(base);
// }
