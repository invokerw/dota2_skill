#pragma once

#include "dota/core/types.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace dota {

class World;

// JSONL 录像写入器. 订阅 World 上的所有可视化事件, 按 tick 边界 (TickEndEvent)
// flush 一行 JSON 到输出流. 详细字段定义见 doc/recording_schema.md.
//
// 用法:
//   std::ofstream out("duel.jsonl");
//   Recorder rec(world, out);
//   rec.write_header("duel");
//   ... world.advance(...) ...
//   // 析构时自动 flush 最后一帧 (如有未写出的 events)
class Recorder {
public:
    Recorder(World& world, std::ostream& out);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // 写出 header 行. 必须在任何 advance 之前调用.
    void write_header(const std::string& scenario_label = "");

    // 已写入的 frame 数量(便于测试统计).
    std::size_t frames_written() const { return frames_written_; }

private:
    void on_tick_end();
    void flush_frame_line(double t, std::uint64_t tick);

    World&        world_;
    std::ostream& out_;

    // 累积事件序列, 按 tick 重置. 用 string 缓存即可, 避免引入 JSON 库.
    std::vector<std::string> tick_events_;
    std::size_t              frames_written_{0};

    // 订阅 token (用于析构时取消订阅, 此处不必, 因为 World 通常先析构)
};

} // namespace dota
