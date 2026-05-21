// modifier_scanner: 文本扫 register_modifier 第 1 参 name. 用真 data dir.

#include "dota/tools/modifier_scanner.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace dota::tools;
namespace fs = std::filesystem;

namespace {
constexpr const char* kRealDataDir = DOTA_DATA_DIR;
} // namespace

TEST(ModifierScanner, ExtractsAllRegisteredNamesFromRealData) {
    const auto infos = scan_modifier_dir(
        fs::path(kRealDataDir) / "scripts" / "modifiers");

    std::vector<std::string> names;
    for (const auto& i : infos) {
        for (const auto& n : i.register_names) names.push_back(n);
    }
    std::sort(names.begin(), names.end());

    auto has = [&](const std::string& s) {
        return std::binary_search(names.begin(), names.end(), s);
    };
    EXPECT_TRUE(has("modifier_test_evasion"));
    EXPECT_TRUE(has("modifier_test_shield"));
    EXPECT_TRUE(has("modifier_test_dot"));
    EXPECT_TRUE(has("modifier_test_aoe_thinker"));
    EXPECT_TRUE(has("modifier_pudge_hook_drag"));
    EXPECT_TRUE(has("modifier_earthshaker_fissure_blocker"));
}

TEST(ModifierScanner, IgnoresLineComments) {
    const std::string src =
        "-- register_modifier(\"fake_one\", {})\n"
        "register_modifier('real_one', {})\n"
        "register_modifier(\"real_two\", {\n  IsHidden = true,\n})\n";
    const auto names = extract_register_names(src);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "real_one");
    EXPECT_EQ(names[1], "real_two");
}

TEST(ModifierScanner, MultiRegisterInOneFile) {
    const fs::path tmp = fs::temp_directory_path() / "mod_scan_multi";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    {
        std::ofstream f(tmp / "combo.lua");
        f << "register_modifier(\"mod_a\", {})\n"
          << "register_modifier(\"mod_b\", {})\n";
    }
    const auto infos = scan_modifier_dir(tmp);
    ASSERT_EQ(infos.size(), 1u);
    ASSERT_EQ(infos[0].register_names.size(), 2u);
    EXPECT_EQ(infos[0].register_names[0], "mod_a");
    EXPECT_EQ(infos[0].register_names[1], "mod_b");
    fs::remove_all(tmp);
}
