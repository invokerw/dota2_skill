// HeroWriter / HeroDoc: 把英雄 yaml round-trip 后, AbilityRegistry 解析的语义
// 必须等价 (字段值一致). 注意: 我们不保证文本 byte-equal -- 注释会丢失,
// 空白可能不同.

#include "dota/ability/registry.hpp"
#include "dota/ability/datadriven.hpp"
#include "dota/tools/hero_catalog.hpp"
#include "dota/tools/hero_writer.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace dota;
using namespace dota::tools;

namespace {
constexpr const char* kDataDir = DOTA_DATA_DIR;

// 写一份 reg, 加载完整 hero yaml, 给个比较 helper.
struct AbilityFingerprint {
    std::string   name;
    std::string   base_class;
    std::uint32_t behavior;
    TargetTeam    team;
    double        cast_point;
    double        backswing;
    double        channel_time;
    double        cast_range;
    std::vector<double> cooldowns;
    std::vector<double> mana_costs;
    std::string   script_path;
    std::size_t   special_count;
    std::size_t   action_count;
};

AbilityFingerprint fingerprint(const AbilityDef& d) {
    AbilityFingerprint f;
    f.name = d.name;
    f.base_class = d.base_class;
    f.behavior = d.behavior;
    f.team = d.target_team;
    f.cast_point = d.cast_point;
    f.backswing = d.backswing;
    f.channel_time = d.channel_time;
    f.cast_range = d.cast_range;
    f.cooldowns = d.cooldowns;
    f.mana_costs = d.mana_costs;
    f.script_path = d.script_path;
    f.special_count = d.ability_special.size();
    f.action_count = d.on_spell_start.size();
    return f;
}

bool equiv(const AbilityFingerprint& a, const AbilityFingerprint& b) {
    return a.name == b.name && a.base_class == b.base_class &&
           a.behavior == b.behavior && a.team == b.team &&
           a.cast_point == b.cast_point && a.backswing == b.backswing &&
           a.channel_time == b.channel_time && a.cast_range == b.cast_range &&
           a.cooldowns == b.cooldowns && a.mana_costs == b.mana_costs &&
           a.script_path == b.script_path &&
           a.special_count == b.special_count &&
           a.action_count == b.action_count;
}

std::vector<AbilityFingerprint> abilities_from(const std::string& yaml_path,
                                                 const std::string& abilities_dir = {}) {
    AbilityRegistry reg;
    reg.load_hero(yaml_path, abilities_dir);
    std::vector<AbilityFingerprint> out;
    // 我们读 yaml 自取 ability 名再 find.
    YAML::Node root = YAML::LoadFile(yaml_path);
    for (const auto& a : root["abilities"]) {
        const std::string n = a.IsScalar()
            ? a.as<std::string>()
            : a["name"].as<std::string>();
        const AbilityDef* def = reg.find(n);
        if (def) out.push_back(fingerprint(*def));
    }
    return out;
}

} // namespace

TEST(HeroWriter, RoundTripPreservesSemanticsForAllHeroes) {
    namespace fs = std::filesystem;
    const fs::path heroes_dir = fs::path(kDataDir) / "heroes";

    int hero_count = 0;
    for (auto& e : fs::directory_iterator(heroes_dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".yaml") continue;
        ++hero_count;

        const std::string src_path = e.path().string();
        const std::string abil_dir =
            (fs::path(kDataDir) / "abilities").string();

        // 1) 解析原文件得到 fingerprint
        const auto before = abilities_from(src_path, abil_dir);
        ASSERT_FALSE(before.empty()) << "no abilities parsed for " << src_path;

        // 2) HeroDoc -> emit -> 写到 tmp 文件
        HeroDoc doc = HeroDoc::load(src_path);
        const std::string emitted = doc.emit();
        EXPECT_FALSE(emitted.empty());
        EXPECT_NE(emitted.find("hero:"), std::string::npos)
            << "emitted yaml lost top-level hero block: " << src_path;
        EXPECT_NE(emitted.find("abilities:"), std::string::npos);

        const fs::path tmp = fs::temp_directory_path() /
            ("hero_writer_rt_" + e.path().stem().string() + ".yaml");
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f.write(emitted.data(),
                    static_cast<std::streamsize>(emitted.size()));
        }

        // 3) 重新解析, 与 before 对比. 用真实 abilities 目录解引用.
        const auto after = abilities_from(tmp.string(), abil_dir);
        ASSERT_EQ(before.size(), after.size())
            << "ability count mismatch after round-trip: " << src_path;
        for (std::size_t i = 0; i < before.size(); ++i) {
            EXPECT_TRUE(equiv(before[i], after[i]))
                << "mismatch in " << src_path << " ability #" << i
                << " (" << before[i].name << ")";
        }

        fs::remove(tmp);
    }
    EXPECT_GE(hero_count, 6) << "应至少扫到 6 个英雄";
}

TEST(HeroWriter, EmitsKeyOrderHeroFirst) {
    HeroDoc doc = HeroDoc::load(std::string(kDataDir) + "/heroes/lion.yaml");
    const std::string out = doc.emit();
    const auto pos_hero      = out.find("hero:");
    const auto pos_abilities = out.find("abilities:");
    ASSERT_NE(pos_hero, std::string::npos);
    ASSERT_NE(pos_abilities, std::string::npos);
    EXPECT_LT(pos_hero, pos_abilities);
}

TEST(HeroWriter, SaveToWritesExpectedContents) {
    namespace fs = std::filesystem;
    HeroDoc doc = HeroDoc::load(std::string(kDataDir) + "/heroes/sven.yaml");
    const fs::path tmp = fs::temp_directory_path() / "hero_writer_save.yaml";
    doc.save_to(tmp.string());

    std::ifstream f(tmp, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    EXPECT_NE(text.find("hero:"), std::string::npos);
    EXPECT_NE(text.find("sven_storm_hammer"), std::string::npos);
    fs::remove(tmp);
}
