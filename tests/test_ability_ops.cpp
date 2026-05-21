// ability_ops: 在 hero yaml node 上 add / remove ability, 并对 ability_lua
// 删除时把脚本 trash. 用临时 data 沙箱跑.

#include "dota/ability/registry.hpp"
#include "dota/tools/ability_ops.hpp"
#include "dota/tools/hero_writer.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace dota;
using namespace dota::tools;
namespace fs = std::filesystem;

namespace {
constexpr const char* kRealDataDir = DOTA_DATA_DIR;

fs::path mk_data_sandbox(const std::string& tag,
                         const std::vector<std::string>& heroes) {
    static int counter = 0;
    const fs::path root = fs::temp_directory_path() /
        ("ability_ops_" + tag + "_" + std::to_string(++counter));
    fs::remove_all(root);
    fs::create_directories(root / "heroes");
    fs::create_directories(root / "scripts" / "abilities");

    const fs::path src = kRealDataDir;
    for (const auto& stem : heroes) {
        fs::copy_file(src / "heroes" / (stem + ".yaml"),
                      root / "heroes" / (stem + ".yaml"));
        const std::string prefix = stem + "_";
        for (const auto& e :
             fs::directory_iterator(src / "scripts" / "abilities")) {
            if (!e.is_regular_file()) continue;
            const auto fname = e.path().filename().string();
            if (fname.rfind(prefix, 0) == 0) {
                fs::copy_file(e.path(),
                              root / "scripts" / "abilities" / fname);
            }
        }
    }
    return root;
}

} // namespace

TEST(AbilityOps, AddDataDrivenAppendsToList) {
    const fs::path root = mk_data_sandbox("add_dd", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();

    const std::size_t before = n["abilities"].size();
    const std::size_t idx =
        add_datadriven_ability(n, "lion_test_ability");
    EXPECT_EQ(idx, before);
    EXPECT_EQ(n["abilities"].size(), before + 1);
    EXPECT_EQ(n["abilities"][idx]["name"].as<std::string>(),
              "lion_test_ability");

    EXPECT_THROW(add_datadriven_ability(n, "lion_test_ability"),
                 std::runtime_error);

    // 写盘 + 重 parse 得到同名 ability.
    doc.save_to((root / "heroes" / "lion.yaml").string());
    AbilityRegistry reg;
    reg.load_file((root / "heroes" / "lion.yaml").string());
    EXPECT_NE(reg.find("lion_test_ability"), nullptr);

    fs::remove_all(root);
}

TEST(AbilityOps, AddLuaAbilityWritesTemplate) {
    const fs::path root = mk_data_sandbox("add_lua", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();

    const std::size_t idx = add_lua_ability(
        n, root, "lion_blast", "lion_blast.lua");
    EXPECT_GT(n["abilities"].size(), 0u);
    EXPECT_EQ(n["abilities"][idx]["base_class"].as<std::string>(),
              "ability_lua");
    EXPECT_EQ(n["abilities"][idx]["script"].as<std::string>(),
              "abilities/lion_blast.lua");
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" / "lion_blast.lua"));

    // 文件已存在 -> 抛.
    EXPECT_THROW(add_lua_ability(n, root, "lion_blast2", "lion_blast.lua"),
                 std::runtime_error);

    fs::remove_all(root);
}

TEST(AbilityOps, RemoveLuaAbilityTrashesScript) {
    const fs::path root = mk_data_sandbox("rm_lua", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();

    const int idx = find_ability_index(n, "lion_finger_of_death");
    ASSERT_GE(idx, 0);
    const fs::path script_path =
        root / "scripts" / "abilities" / "lion_finger_of_death.lua";
    ASSERT_TRUE(fs::exists(script_path));

    auto recs = remove_ability_at(n, root,
                                  static_cast<std::size_t>(idx));
    EXPECT_EQ(recs.size(), 1u);
    EXPECT_FALSE(fs::exists(script_path));
    EXPECT_TRUE(fs::exists(recs.front().trash_path));
    EXPECT_LT(find_ability_index(n, "lion_finger_of_death"), 0);

    fs::remove_all(root);
}

TEST(AbilityOps, RemoveDataDrivenLeavesNoTrash) {
    const fs::path root = mk_data_sandbox("rm_dd", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();

    const int idx = find_ability_index(n, "lion_earth_spike");
    ASSERT_GE(idx, 0);

    auto recs = remove_ability_at(n, root,
                                  static_cast<std::size_t>(idx));
    EXPECT_TRUE(recs.empty()) << "datadriven 没有独占脚本";
    EXPECT_LT(find_ability_index(n, "lion_earth_spike"), 0);

    fs::remove_all(root);
}

TEST(AbilityOps, SetAbilitySpecialReplacesAllKeys) {
    const fs::path root = mk_data_sandbox("special", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();
    const int idx = find_ability_index(n, "lion_earth_spike");
    ASSERT_GE(idx, 0);
    YAML::Node ab = n["abilities"][idx];

    set_ability_special(ab, {
        {"damage",        {100.0, 200.0, 300.0, 400.0}, false},
        {"stun_duration", {1.0,   1.5,   2.0,   2.5},   false},
    });
    doc.save_to((root / "heroes" / "lion.yaml").string());

    AbilityRegistry reg;
    reg.load_file((root / "heroes" / "lion.yaml").string());
    const AbilityDef* def = reg.find("lion_earth_spike");
    ASSERT_NE(def, nullptr);
    auto it = def->ability_special.find("damage");
    ASSERT_NE(it, def->ability_special.end());
    EXPECT_DOUBLE_EQ(it->second.get_float(1), 100.0);
    EXPECT_DOUBLE_EQ(it->second.get_float(4), 400.0);
    fs::remove_all(root);
}
