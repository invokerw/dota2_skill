// hero_ops: create / duplicate / rename / delete 全部需要在隔离的 data 沙箱
// 里跑, 否则会污染 repo 的真 yaml. 每个 test 复制一份 fixture 到 temp_dir.

#include "dota/ability/registry.hpp"
#include "dota/tools/hero_catalog.hpp"
#include "dota/tools/hero_ops.hpp"

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

// 在临时目录里搭一个最小的 data 沙箱: heroes/ + scripts/abilities/.
// real_heroes 列出的 stem 会从真实仓库复制 yaml + 它独占的 lua 脚本过来.
fs::path mk_data_sandbox(const std::string& tag,
                         const std::vector<std::string>& real_heroes) {
    static int counter = 0;
    const fs::path root = fs::temp_directory_path() /
        ("hero_ops_" + tag + "_" + std::to_string(++counter));
    fs::remove_all(root);
    fs::create_directories(root / "heroes");
    fs::create_directories(root / "scripts" / "abilities");

    const fs::path src_root = kRealDataDir;
    for (const auto& stem : real_heroes) {
        fs::copy_file(src_root / "heroes" / (stem + ".yaml"),
                      root / "heroes" / (stem + ".yaml"));
        const std::string prefix = stem + "_";
        for (const auto& e :
             fs::directory_iterator(src_root / "scripts" / "abilities")) {
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

TEST(HeroOps, IsValidHeroStem) {
    EXPECT_TRUE(is_valid_hero_stem("lion"));
    EXPECT_TRUE(is_valid_hero_stem("lina_2"));
    EXPECT_FALSE(is_valid_hero_stem(""));
    EXPECT_FALSE(is_valid_hero_stem("_starts_underscore"));
    EXPECT_FALSE(is_valid_hero_stem("Lion"));
    EXPECT_FALSE(is_valid_hero_stem("hero name"));
    EXPECT_FALSE(is_valid_hero_stem("a-b"));
}

TEST(HeroOps, CollectsOwnedScripts) {
    const fs::path root = mk_data_sandbox("collect", {"lina"});
    const HeroFiles f = collect_hero_files(root, "lina");
    EXPECT_TRUE(fs::exists(f.yaml_path));
    EXPECT_GE(f.ability_scripts.size(), 4u) << "lina 有 4 个独占脚本";
    for (const auto& p : f.ability_scripts) {
        EXPECT_EQ(p.parent_path().filename(), "abilities");
        EXPECT_TRUE(p.filename().string().rfind("lina_", 0) == 0);
    }
    fs::remove_all(root);
}

TEST(HeroOps, CreateHeroFromTemplate) {
    const fs::path root = mk_data_sandbox("create", {});
    const fs::path created = create_hero(root, "newbie");
    ASSERT_TRUE(fs::exists(created));

    HeroCatalog cat;
    cat.scan(root / "heroes");
    const HeroEntry* h = cat.find("newbie");
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->display_name, "npc_dota_hero_newbie");
    EXPECT_EQ(h->abilities.size(), 0u);

    EXPECT_THROW(create_hero(root, "newbie"), std::runtime_error);
    EXPECT_THROW(create_hero(root, "Bad"), std::runtime_error);
    fs::remove_all(root);
}

TEST(HeroOps, DuplicateRewritesAllStems) {
    const fs::path root = mk_data_sandbox("dup", {"lina"});
    const fs::path dst = duplicate_hero(root, "lina", "lina_clone");
    ASSERT_TRUE(fs::exists(dst));

    HeroCatalog cat;
    cat.scan(root / "heroes");
    const HeroEntry* clone = cat.find("lina_clone");
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->display_name, "npc_dota_hero_lina_clone");

    // 所有 ability name 都被改前缀.
    for (const auto& a : clone->abilities) {
        EXPECT_TRUE(a.name.rfind("lina_clone_", 0) == 0)
            << "expected lina_clone_ prefix, got " << a.name;
    }

    // ability lua 脚本也被复制并改名.
    const fs::path scripts = root / "scripts" / "abilities";
    EXPECT_TRUE(fs::exists(scripts / "lina_clone_dragon_slave.lua"));
    EXPECT_TRUE(fs::exists(scripts / "lina_clone_laguna_blade.lua"));
    // 原英雄文件保留.
    EXPECT_TRUE(fs::exists(scripts / "lina_dragon_slave.lua"));

    // 新 yaml 的 script 字段也被重写.
    YAML::Node n = YAML::LoadFile(dst.string());
    bool checked_any = false;
    for (const auto& a : n["abilities"]) {
        if (a["script"]) {
            const std::string s = a["script"].as<std::string>();
            EXPECT_TRUE(s.find("lina_clone_") != std::string::npos)
                << "script: " << s;
            checked_any = true;
        }
    }
    EXPECT_TRUE(checked_any);

    // 新文件能被 AbilityRegistry 加载.
    AbilityRegistry reg;
    EXPECT_NO_THROW(reg.load_file(dst.string()));
    EXPECT_NE(reg.find("lina_clone_dragon_slave"), nullptr);

    EXPECT_THROW(duplicate_hero(root, "lina", "lina_clone"), std::runtime_error);
    fs::remove_all(root);
}

TEST(HeroOps, RenameMovesYamlAndScripts) {
    const fs::path root = mk_data_sandbox("rename", {"sven"});
    rename_hero(root, "sven", "sven_v2");

    EXPECT_FALSE(fs::exists(root / "heroes" / "sven.yaml"));
    EXPECT_TRUE(fs::exists(root / "heroes" / "sven_v2.yaml"));
    EXPECT_FALSE(fs::exists(root / "scripts" / "abilities" /
                            "sven_storm_hammer.lua"));
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" /
                           "sven_v2_storm_hammer.lua"));

    HeroCatalog cat;
    cat.scan(root / "heroes");
    const HeroEntry* h = cat.find("sven_v2");
    ASSERT_NE(h, nullptr);
    ASSERT_GE(h->abilities.size(), 1u);
    EXPECT_EQ(h->abilities[0].name, "sven_v2_storm_hammer");

    fs::remove_all(root);
}

TEST(HeroOps, DeleteMovesYamlAndScriptsToTrash) {
    const fs::path root = mk_data_sandbox("delete", {"juggernaut"});
    const auto recs = delete_hero(root, "juggernaut");
    EXPECT_FALSE(fs::exists(root / "heroes" / "juggernaut.yaml"));
    EXPECT_FALSE(fs::exists(root / "scripts" / "abilities" /
                            "juggernaut_blade_fury.lua"));
    EXPECT_GE(recs.size(), 4u) << "yaml + 3 个脚本";
    for (const auto& r : recs) {
        EXPECT_TRUE(fs::exists(r.trash_path));
        EXPECT_NE(r.trash_path.string().find(".trash"), std::string::npos);
    }
    fs::remove_all(root);
}
