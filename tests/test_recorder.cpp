// Stage B: Recorder 把 EventBus 上的事件序列化到 JSONL.
// 仅做粗粒度断言 (子串匹配), 不实现完整 JSON parser.
#include "dota/ability/ability.hpp"
#include "dota/ability/registry.hpp"
#include "dota/core/unit.hpp"
#include "dota/core/world.hpp"
#include "dota/replay/recorder.hpp"
#include "dota/script/lua_state.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

using namespace dota;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;

UnitStats hero_stats() {
    UnitStats s;
    s.max_health = 1500.0;
    s.max_mana   = 600.0;
    s.magic_resist = 0.25;
    return s;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else            { cur += c; }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

bool any_contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (auto& l : lines) if (l.find(needle) != std::string::npos) return true;
    return false;
}

int count_containing(const std::vector<std::string>& lines, const std::string& needle) {
    int n = 0;
    for (auto& l : lines) if (l.find(needle) != std::string::npos) ++n;
    return n;
}
} // namespace

TEST(Recorder, MeatHookSceneProducesValidJsonl) {
    LuaState lua;
    AbilityRegistry reg;
    reg.set_lua(&lua);
    reg.load_file(std::string(kDataDir) + "/heroes/pudge.yaml");

    std::ostringstream out;
    World world;
    Recorder rec(world, out);
    rec.write_header("test");

    Unit* pudge = world.spawn("Pudge", Team::Radiant, hero_stats(), {0, 0});
    Unit* lina  = world.spawn("Lina",  Team::Dire,    hero_stats(), {500, 0});
    (void)pudge; (void)lina;

    Ability* hook = reg.instantiate("pudge_meat_hook", *pudge);
    ASSERT_NE(hook, nullptr);
    CastTarget t; t.point = {1300, 0}; t.has_point = true;
    hook->order_cast(t, world);

    // 推进 1.5 秒 (cast point 0.30 + 命中 + 拖拽完成)
    world.advance(1.5);

    const std::string text = out.str();
    auto lines = split_lines(text);

    // header + 至少 30 个 tick 帧 (1.5s × 30Hz)
    ASSERT_GE(lines.size(), 31u);

    // 第一行是 header
    EXPECT_NE(lines[0].find("\"v\":1"), std::string::npos);
    EXPECT_NE(lines[0].find("\"tick_rate\":30"), std::string::npos);
    EXPECT_NE(lines[0].find("\"scenario\":\"test\""), std::string::npos);

    // frames 必须含 tick / positions / events 字段
    EXPECT_NE(lines[1].find("\"tick\":1"), std::string::npos);
    EXPECT_NE(lines[1].find("\"positions\":["), std::string::npos);
    EXPECT_NE(lines[1].find("\"events\":["), std::string::npos);

    // 关键事件子串都应出现
    EXPECT_TRUE(any_contains(lines, "\"type\":\"unit_spawn\""));
    EXPECT_TRUE(any_contains(lines, "\"name\":\"Pudge\""));
    EXPECT_TRUE(any_contains(lines, "\"name\":\"Lina\""));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"cast_start\""));
    EXPECT_TRUE(any_contains(lines, "\"ability\":\"pudge_meat_hook\""));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"projectile_spawn\""));
    EXPECT_TRUE(any_contains(lines, "\"tracking\":false"));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"projectile_hit\""));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"projectile_finish\""));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"damage\""));
    EXPECT_TRUE(any_contains(lines, "\"dtype\":\"magical\""));
    EXPECT_TRUE(any_contains(lines, "\"type\":\"modifier_add\""));

    // frames_written 与帧行数 (除 header) 一致
    EXPECT_EQ(rec.frames_written(), lines.size() - 1);

    // tick 序号严格递增 1, 2, 3 ...
    EXPECT_NE(lines[2].find("\"tick\":2"), std::string::npos);
    EXPECT_NE(lines[3].find("\"tick\":3"), std::string::npos);

    // damage 行至少 1 条
    EXPECT_GE(count_containing(lines, "\"type\":\"damage\""), 1);
}
