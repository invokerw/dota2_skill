// ability_ops: 文件级 CRUD on data/abilities/<name>.yaml. rename 同步 hero 引用,
// delete 在被引用时拒绝. 还覆盖 hero 引用列表的增删改助手.

#include "dota/ability/registry.hpp"
#include "dota/tools/ability_doc.hpp"
#include "dota/tools/ability_ops.hpp"
#include "dota/tools/hero_writer.hpp"

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
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
    fs::create_directories(root / "abilities");
    fs::create_directories(root / "scripts" / "abilities");

    const fs::path src = kRealDataDir;
    // 全量复制 abilities/.
    for (const auto& e : fs::directory_iterator(src / "abilities")) {
        if (e.is_regular_file() && e.path().extension() == ".yaml") {
            fs::copy_file(e.path(), root / "abilities" / e.path().filename());
        }
    }
    // 复制选定的 hero yaml + 它"按前缀启发式"独占的 lua 脚本.
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

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

TEST(AbilityOps, IsValidAbilityName) {
    EXPECT_TRUE(is_valid_ability_name("sven_storm_hammer"));
    EXPECT_TRUE(is_valid_ability_name("a"));
    EXPECT_FALSE(is_valid_ability_name(""));
    EXPECT_FALSE(is_valid_ability_name("_underscore"));
    EXPECT_FALSE(is_valid_ability_name("Bad"));
    EXPECT_FALSE(is_valid_ability_name("a-b"));
}

TEST(AbilityOps, CreateDataDrivenWritesFile) {
    const fs::path root = mk_data_sandbox("create_dd", {});
    const fs::path p = create_datadriven_ability_file(root, "test_ability");
    ASSERT_TRUE(fs::exists(p));

    AbilityRegistry reg;
    EXPECT_EQ(reg.load_file(p.string()), 1u);
    const auto* def = reg.find("test_ability");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->base_class, "ability_datadriven");

    EXPECT_THROW(create_datadriven_ability_file(root, "test_ability"),
                 std::runtime_error);
    fs::remove_all(root);
}

TEST(AbilityOps, CreateLuaWritesFileAndScript) {
    const fs::path root = mk_data_sandbox("create_lua", {});
    const fs::path p = create_lua_ability_file(root, "test_blast");
    ASSERT_TRUE(fs::exists(p));
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" / "test_blast.lua"));

    YAML::Node n = YAML::LoadFile(p.string());
    EXPECT_EQ(n["base_class"].as<std::string>(), "ability_lua");
    EXPECT_EQ(n["script"].as<std::string>(), "abilities/test_blast.lua");

    EXPECT_THROW(create_lua_ability_file(root, "test_blast"),
                 std::runtime_error);
    fs::remove_all(root);
}

TEST(AbilityOps, DuplicateCopiesYamlAndLua) {
    const fs::path root = mk_data_sandbox("dup", {"lion"});
    // lion_finger_of_death 是 ability_lua, 走 lua 复制路径.
    const fs::path dst = duplicate_ability_file(
        root, "lion_finger_of_death", "lion_finger_v2");
    EXPECT_TRUE(fs::exists(dst));
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" / "lion_finger_v2.lua"));

    AbilityRegistry reg;
    EXPECT_EQ(reg.load_file(dst.string()), 1u);
    const auto* def = reg.find("lion_finger_v2");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->base_class, "ability_lua");
    EXPECT_EQ(def->script_path, "abilities/lion_finger_v2.lua");

    fs::remove_all(root);
}

TEST(AbilityOps, RenameMovesYamlAndSyncsHeroRefs) {
    const fs::path root = mk_data_sandbox("rename", {"lion"});
    const auto res = rename_ability_file(root, "lion_earth_spike",
                                          "lion_earth_spike_v2");
    EXPECT_FALSE(fs::exists(root / "abilities" / "lion_earth_spike.yaml"));
    EXPECT_TRUE(fs::exists(root / "abilities" / "lion_earth_spike_v2.yaml"));
    EXPECT_EQ(res.new_yaml_path,
              root / "abilities" / "lion_earth_spike_v2.yaml");
    EXPECT_TRUE(contains(res.updated_hero_stems, "lion"))
        << "lion 引用了 lion_earth_spike, 应被同步";

    // hero yaml 引用名已替换.
    YAML::Node hero = YAML::LoadFile((root / "heroes" / "lion.yaml").string());
    bool found = false;
    for (const auto& a : hero["abilities"]) {
        if (a.IsScalar() && a.as<std::string>() == "lion_earth_spike_v2") {
            found = true;
        }
        EXPECT_NE(a.as<std::string>(), "lion_earth_spike");
    }
    EXPECT_TRUE(found);

    AbilityRegistry reg;
    EXPECT_NO_THROW(reg.load_hero((root / "heroes" / "lion.yaml").string(),
                                    (root / "abilities").string()));
    EXPECT_NE(reg.find("lion_earth_spike_v2"), nullptr);
    fs::remove_all(root);
}

TEST(AbilityOps, RenameLuaAlsoMovesScript) {
    const fs::path root = mk_data_sandbox("rename_lua", {"lion"});
    rename_ability_file(root, "lion_finger_of_death", "lion_finger_v2");
    EXPECT_FALSE(fs::exists(root / "scripts" / "abilities" /
                            "lion_finger_of_death.lua"));
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" / "lion_finger_v2.lua"));

    YAML::Node n = YAML::LoadFile(
        (root / "abilities" / "lion_finger_v2.yaml").string());
    EXPECT_EQ(n["script"].as<std::string>(), "abilities/lion_finger_v2.lua");
    fs::remove_all(root);
}

TEST(AbilityOps, DeleteRejectsWhenReferenced) {
    const fs::path root = mk_data_sandbox("del_ref", {"lion"});
    EXPECT_THROW(delete_ability_file(root, "lion_earth_spike"),
                 std::runtime_error);
    EXPECT_TRUE(fs::exists(root / "abilities" / "lion_earth_spike.yaml"));
    fs::remove_all(root);
}

TEST(AbilityOps, DeleteOrphanedTrashesYamlAndScript) {
    const fs::path root = mk_data_sandbox("del_orphan", {});
    // 创建一个孤儿 ability_lua, 没有 hero 引用.
    create_lua_ability_file(root, "orphan_blast");
    EXPECT_TRUE(fs::exists(root / "abilities" / "orphan_blast.yaml"));
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" / "orphan_blast.lua"));

    const auto res = delete_ability_file(root, "orphan_blast");
    EXPECT_FALSE(fs::exists(root / "abilities" / "orphan_blast.yaml"));
    EXPECT_FALSE(fs::exists(root / "scripts" / "abilities" / "orphan_blast.lua"));
    EXPECT_TRUE(fs::exists(res.yaml_record.trash_path));
    ASSERT_TRUE(res.script_record.has_value());
    EXPECT_TRUE(fs::exists(res.script_record->trash_path));
    fs::remove_all(root);
}

TEST(AbilityOps, HeroesReferencingFindsAllHeroes) {
    // 同时复制 lion + sven, 然后 lion 引用了 lion_earth_spike, sven 没引用.
    const fs::path root = mk_data_sandbox("refs", {"lion", "sven"});
    const auto refs = heroes_referencing(root, "lion_earth_spike");
    EXPECT_TRUE(contains(refs, "lion"));
    EXPECT_FALSE(contains(refs, "sven"));
    fs::remove_all(root);
}

TEST(AbilityOps, HeroAddRemoveRefRoundTrip) {
    const fs::path root = mk_data_sandbox("hero_ref", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();
    const std::size_t before = n["abilities"].size();

    hero_add_ability_ref(n, "lina_dragon_slave");
    EXPECT_EQ(n["abilities"].size(), before + 1);
    EXPECT_GE(hero_find_ability_ref(n, "lina_dragon_slave"), 0);
    EXPECT_THROW(hero_add_ability_ref(n, "lina_dragon_slave"),
                 std::runtime_error);

    const int idx = hero_find_ability_ref(n, "lina_dragon_slave");
    hero_remove_ability_ref_at(n, static_cast<std::size_t>(idx));
    EXPECT_EQ(n["abilities"].size(), before);
    EXPECT_LT(hero_find_ability_ref(n, "lina_dragon_slave"), 0);

    fs::remove_all(root);
}

TEST(AbilityOps, HeroMoveRefShufflesOrder) {
    const fs::path root = mk_data_sandbox("hero_move", {"lion"});
    HeroDoc doc = HeroDoc::load((root / "heroes" / "lion.yaml").string());
    YAML::Node n = doc.root();
    ASSERT_GE(n["abilities"].size(), 2u);
    const std::string first = n["abilities"][0].as<std::string>();
    const std::string second = n["abilities"][1].as<std::string>();
    hero_move_ability_ref(n, 0, 1);
    EXPECT_EQ(n["abilities"][0].as<std::string>(), second);
    EXPECT_EQ(n["abilities"][1].as<std::string>(), first);
    fs::remove_all(root);
}

TEST(AbilityOps, SetAbilitySpecialReplacesAllKeys) {
    const fs::path root = mk_data_sandbox("special", {});
    const fs::path p = root / "abilities" / "lion_earth_spike.yaml";
    ASSERT_TRUE(fs::exists(p));

    AbilityDoc doc = AbilityDoc::load(p);
    YAML::Node a = doc.root();
    set_ability_special(a, {
        {"damage",        {100.0, 200.0, 300.0, 400.0}, false},
        {"stun_duration", {1.0,   1.5,   2.0,   2.5},   false},
    });
    doc.save_to(p);

    AbilityRegistry reg;
    reg.load_file(p.string());
    const AbilityDef* def = reg.find("lion_earth_spike");
    ASSERT_NE(def, nullptr);
    auto it = def->ability_special.find("damage");
    ASSERT_NE(it, def->ability_special.end());
    EXPECT_DOUBLE_EQ(it->second.get_float(1), 100.0);
    EXPECT_DOUBLE_EQ(it->second.get_float(4), 400.0);
    fs::remove_all(root);
}
