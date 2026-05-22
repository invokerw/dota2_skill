// AbilityRegistry::load_dir / load_hero: 新格式独立 ability 文件 +
// hero yaml 引用列表加载.

#include "dota/ability/registry.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace dota;

namespace {

fs::path mk_sandbox(const std::string& tag) {
    static int counter = 0;
    const fs::path root = fs::temp_directory_path() /
        ("registry_dir_" + tag + "_" + std::to_string(++counter));
    fs::remove_all(root);
    fs::create_directories(root / "abilities");
    fs::create_directories(root / "heroes");
    return root;
}

void write_text(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << s;
}

const std::string kAbilityFoo =
    "name: foo_zap\n"
    "base_class: ability_datadriven\n"
    "behavior: [UNIT_TARGET]\n"
    "target_team: ENEMY\n"
    "cast_point: 0.3\n"
    "cooldown: [10]\n"
    "mana_cost: [50]\n"
    "cast_range: 600\n"
    "ability_special:\n"
    "  damage: [100]\n";

const std::string kAbilityBar =
    "name: bar_blast\n"
    "base_class: ability_datadriven\n"
    "behavior: [NO_TARGET]\n"
    "target_team: NONE\n"
    "cast_point: 0.0\n"
    "cooldown: [5]\n"
    "mana_cost: [25]\n"
    "ability_special:\n"
    "  damage: [50]\n";

} // namespace

TEST(AbilityRegistryDir, LoadsAllYamlFilesInDir) {
    const fs::path root = mk_sandbox("two");
    write_text(root / "abilities" / "foo_zap.yaml", kAbilityFoo);
    write_text(root / "abilities" / "bar_blast.yaml", kAbilityBar);

    AbilityRegistry reg;
    const std::size_t n = reg.load_dir((root / "abilities").string());
    EXPECT_EQ(n, 2u);
    EXPECT_NE(reg.find("foo_zap"), nullptr);
    EXPECT_NE(reg.find("bar_blast"), nullptr);

    fs::remove_all(root);
}

TEST(AbilityRegistryDir, LoadFileSingleAbilityFormat) {
    const fs::path root = mk_sandbox("single");
    const fs::path file = root / "abilities" / "foo_zap.yaml";
    write_text(file, kAbilityFoo);

    AbilityRegistry reg;
    EXPECT_EQ(reg.load_file(file.string()), 1u);
    const auto* def = reg.find("foo_zap");
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->base_class, "ability_datadriven");

    fs::remove_all(root);
}

TEST(AbilityRegistryDir, LoadHeroResolvesReferencesOnly) {
    const fs::path root = mk_sandbox("ref");
    write_text(root / "abilities" / "foo_zap.yaml", kAbilityFoo);
    write_text(root / "abilities" / "bar_blast.yaml", kAbilityBar);

    // hero 只引用 foo_zap, 不引用 bar_blast.
    const std::string hero =
        "hero:\n"
        "  name: npc_dota_hero_test\n"
        "abilities:\n"
        "  - foo_zap\n";
    write_text(root / "heroes" / "test.yaml", hero);

    AbilityRegistry reg;
    const std::size_t n = reg.load_hero(
        (root / "heroes" / "test.yaml").string(),
        (root / "abilities").string());
    EXPECT_EQ(n, 1u);
    EXPECT_NE(reg.find("foo_zap"), nullptr);
    EXPECT_EQ(reg.find("bar_blast"), nullptr);

    fs::remove_all(root);
}

TEST(AbilityRegistryDir, LoadHeroThrowsOnMissingRef) {
    const fs::path root = mk_sandbox("missing");
    fs::create_directories(root / "abilities");

    const std::string hero =
        "hero:\n"
        "  name: npc_dota_hero_test\n"
        "abilities:\n"
        "  - ghost_skill\n";
    write_text(root / "heroes" / "test.yaml", hero);

    AbilityRegistry reg;
    EXPECT_THROW(
        reg.load_hero((root / "heroes" / "test.yaml").string(),
                      (root / "abilities").string()),
        std::runtime_error);

    fs::remove_all(root);
}

TEST(AbilityRegistryDir, LoadHeroRejectsInlineMapEntry) {
    // 引用列表只接受 scalar 名; 内嵌 map (老格式) 应被拒绝.
    const fs::path root = mk_sandbox("inline_reject");
    fs::create_directories(root / "abilities");

    const std::string hero =
        "hero:\n"
        "  name: npc_dota_hero_test\n"
        "abilities:\n"
        "  - name: inline_skill\n"
        "    base_class: ability_datadriven\n"
        "    behavior: [NO_TARGET]\n"
        "    cooldown: [1]\n"
        "    mana_cost: [0]\n";
    write_text(root / "heroes" / "test.yaml", hero);

    AbilityRegistry reg;
    EXPECT_THROW(
        reg.load_hero((root / "heroes" / "test.yaml").string(),
                      (root / "abilities").string()),
        std::runtime_error);

    fs::remove_all(root);
}
