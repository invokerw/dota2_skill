// server/test/test_protobuf_serialization.cpp
// Protobuf 序列化测试 - Stage 1

#include <gtest/gtest.h>

// TODO: Stage 1 实现后取消注释
// #include "messages.pb.h"
// #include "common.pb.h"

// 占位测试
TEST(ProtobufTest, Placeholder) {
  EXPECT_TRUE(true);
}

// TODO: Stage 1 实现以下测试

// TEST(ProtobufTest, Vec2Serialization) {
//   dota::network::Vec2 vec;
//   vec.set_x(1.5f);
//   vec.set_y(2.5f);
//
//   // 序列化
//   std::string buffer;
//   ASSERT_TRUE(vec.SerializeToString(&buffer));
//   EXPECT_GT(buffer.size(), 0);
//
//   // 反序列化
//   dota::network::Vec2 vec2;
//   ASSERT_TRUE(vec2.ParseFromString(buffer));
//   EXPECT_FLOAT_EQ(vec2.x(), 1.5f);
//   EXPECT_FLOAT_EQ(vec2.y(), 2.5f);
// }

// TEST(ProtobufTest, PacketWithConnect) {
//   dota::network::Packet packet;
//   packet.set_sequence(1);
//   packet.set_timestamp(12345);
//
//   auto* connect = packet.mutable_connect();
//   connect->set_player_name("TestPlayer");
//   connect->set_version("0.1.0");
//
//   // 序列化
//   std::vector<uint8_t> buffer(packet.ByteSizeLong());
//   ASSERT_TRUE(packet.SerializeToArray(buffer.data(), buffer.size()));
//
//   // 反序列化
//   dota::network::Packet packet2;
//   ASSERT_TRUE(packet2.ParseFromArray(buffer.data(), buffer.size()));
//   EXPECT_EQ(packet2.sequence(), 1);
//   EXPECT_EQ(packet2.timestamp(), 12345);
//   EXPECT_TRUE(packet2.has_connect());
//   EXPECT_EQ(packet2.connect().player_name(), "TestPlayer");
// }

// TEST(ProtobufTest, SnapshotSerialization) {
//   dota::network::S2C_Snapshot snapshot;
//   snapshot.set_tick(100);
//   snapshot.set_game_time(10.5f);
//
//   // 添加实体
//   auto* entity = snapshot.add_entities();
//   entity->set_id(1);
//   entity->set_type(dota::network::ENTITY_PLAYER);
//   entity->mutable_position()->set_x(100.0f);
//   entity->mutable_position()->set_y(200.0f);
//   entity->set_health(500.0f);
//   entity->set_max_health(600.0f);
//
//   // 序列化
//   size_t size = snapshot.ByteSizeLong();
//   std::vector<uint8_t> buffer(size);
//   ASSERT_TRUE(snapshot.SerializeToArray(buffer.data(), size));
//
//   // 反序列化
//   dota::network::S2C_Snapshot snapshot2;
//   ASSERT_TRUE(snapshot2.ParseFromArray(buffer.data(), size));
//   EXPECT_EQ(snapshot2.tick(), 100);
//   EXPECT_EQ(snapshot2.entities_size(), 1);
//   EXPECT_EQ(snapshot2.entities(0).id(), 1);
//   EXPECT_FLOAT_EQ(snapshot2.entities(0).position().x(), 100.0f);
// }
