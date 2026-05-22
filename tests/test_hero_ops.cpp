// hero_ops: create / duplicate / rename / delete. ability yaml 是共享资源,
// 所以 hero_ops 现在只管 hero yaml 本身 (改名 / 复制 / trash); 不再克隆 lua
// 脚本或 ability yaml.

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

// 在临时目录里搭一个最小的 data 沙箱: heroes/ + abilities/ + scripts/abilities/.
// real_heroes 列出的 stem 会从真实仓库复制 hero yaml 过来; 同时把整个
// abilities/ 目录复制过来 (因为 hero yaml 引用列表需要解引用).
fs::path mk_data_sandbox(const std::string& tag,
                         const std::vector<std::string>& real_heroes) {
    static int counter = 0;
    const fs::path root = fs::temp_directory_path() /
        ("hero_ops_" + tag + "_" + std::to_string(++counter));
    fs::remove_all(root);
    fs::create_directories(root / "heroes");
    fs::create_directories(root / "abilities");
    fs::create_directories(root / "scripts" / "abilities");

    const fs::path src_root = kRealDataDir;
    // 整体复制 abilities/ (共享资源).
    for (const auto& e : fs::directory_iterator(src_root / "abilities")) {
        if (e.is_regular_file() && e.path().extension() == ".yaml") {
            fs::copy_file(e.path(),
                          root / "abilities" / e.path().filename());
        }
    }
    // 拷选定的 hero yaml + 其引用相关的 lua 脚本 (按文件名前缀启发式匹配).
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

TEST(HeroOps, CreateHeroFromTemplate) {
    const fs::path root = mk_data_sandbox("create", {});
    const fs::path created = create_hero(root, "newbie");
    ASSERT_TRUE(fs::exists(created));

    HeroCatalog cat;
    cat.scan((root / "heroes").string());
    const HeroEntry* h = cat.find("newbie");
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->display_name, "npc_dota_hero_newbie");
    EXPECT_EQ(h->abilities.size(), 0u);

    EXPECT_THROW(create_hero(root, "newbie"), std::runtime_error);
    EXPECT_THROW(create_hero(root, "Bad"), std::runtime_error);
    fs::remove_all(root);
}

TEST(HeroOps, DuplicateCopiesHeroYamlOnly) {
    const fs::path root = mk_data_sandbox("dup", {"lina"});
    const fs::path dst = duplicate_hero(root, "lina", "lina_clone");
    ASSERT_TRUE(fs::exists(dst));

    HeroCatalog cat;
    cat.scan((root / "heroes").string());
    const HeroEntry* clone = cat.find("lina_clone");
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->display_name, "npc_dota_hero_lina_clone");

    // ability yaml / lua 脚本不被复制 (共享语义); ability 引用列表保持原样,
    // 仍指向 data/abilities/<原 ability name>.yaml.
    const HeroEntry* src = cat.find("lina");
    ASSERT_NE(src, nullptr);
    ASSERT_EQ(clone->abilities.size(), src->abilities.size());
    for (std::size_t i = 0; i < clone->abilities.size(); ++i) {
        EXPECT_EQ(clone->abilities[i].name, src->abilities[i].name);
    }

    // 没有产生新的 ability lua / yaml.
    EXPECT_FALSE(fs::exists(root / "scripts" / "abilities" /
                            "lina_clone_dragon_slave.lua"));
    EXPECT_FALSE(fs::exists(root / "abilities" /
                            "lina_clone_dragon_slave.yaml"));

    // 新文件能被 AbilityRegistry 通过共享 abilities/ 加载.
    AbilityRegistry reg;
    EXPECT_NO_THROW(reg.load_hero(dst.string(),
                                    (root / "abilities").string()));

    EXPECT_THROW(duplicate_hero(root, "lina", "lina_clone"), std::runtime_error);
    fs::remove_all(root);
}

TEST(HeroOps, RenameMovesHeroYamlOnly) {
    const fs::path root = mk_data_sandbox("rename", {"sven"});
    rename_hero(root, "sven", "sven_v2");

    EXPECT_FALSE(fs::exists(root / "heroes" / "sven.yaml"));
    EXPECT_TRUE(fs::exists(root / "heroes" / "sven_v2.yaml"));

    // ability 脚本 / yaml 维持原名 (共享).
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" /
                           "sven_storm_hammer.lua"));
    EXPECT_TRUE(fs::exists(root / "abilities" / "sven_storm_hammer.yaml"));

    HeroCatalog cat;
    cat.scan((root / "heroes").string());
    const HeroEntry* h = cat.find("sven_v2");
    ASSERT_NE(h, nullptr);
    ASSERT_GE(h->abilities.size(), 1u);
    EXPECT_EQ(h->abilities[0].name, "sven_storm_hammer");

    fs::remove_all(root);
}

TEST(HeroOps, DeleteMovesYamlToTrash) {
    const fs::path root = mk_data_sandbox("delete", {"juggernaut"});
    const auto recs = delete_hero(root, "juggernaut");
    EXPECT_FALSE(fs::exists(root / "heroes" / "juggernaut.yaml"));
    // ability 脚本/共享资源不被影响.
    EXPECT_TRUE(fs::exists(root / "scripts" / "abilities" /
                           "juggernaut_blade_fury.lua"));
    EXPECT_TRUE(fs::exists(root / "abilities" / "juggernaut_blade_fury.yaml"));
    ASSERT_EQ(recs.size(), 1u);
    EXPECT_TRUE(fs::exists(recs.front().trash_path));
    EXPECT_NE(recs.front().trash_path.string().find(".trash"),
              std::string::npos);
    fs::remove_all(root);
}
