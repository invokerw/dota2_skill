// modifier_ops: 文件级 CRUD. 模板要能让 LuaState 正常 register_modifier
// 而不报错; rewrite_register_name 只动 register_modifier 的第一个字符串实参.

#include "dota/tools/modifier_ops.hpp"
#include "dota/tools/modifier_scanner.hpp"
#include "dota/script/lua_state.hpp"
#include "dota/modifier/registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace dota;
using namespace dota::tools;
namespace fs = std::filesystem;

namespace {

fs::path mk_sandbox(const std::string& tag) {
    static int counter = 0;
    const fs::path root = fs::temp_directory_path() /
        ("modifier_ops_" + tag + "_" + std::to_string(++counter));
    fs::remove_all(root);
    fs::create_directories(root / "scripts" / "modifiers");
    return root;
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream os; os << f.rdbuf();
    return os.str();
}

bool template_loads(ModifierTemplate kind, const std::string& name) {
    const std::string src = render_modifier_template(name, kind);
    LuaState lua;
    auto r = lua.state().safe_script(src, &sol::script_pass_on_error);
    if (!r.valid()) return false;
    return lua.modifier_registry().contains(name);
}

} // namespace

TEST(ModifierOps, ValidNameCheck) {
    EXPECT_TRUE(is_valid_modifier_name("modifier_test"));
    EXPECT_TRUE(is_valid_modifier_name("foo123"));
    EXPECT_FALSE(is_valid_modifier_name(""));
    EXPECT_FALSE(is_valid_modifier_name("_leading"));
    EXPECT_FALSE(is_valid_modifier_name("Has_Upper"));
    EXPECT_FALSE(is_valid_modifier_name("with space"));
}

TEST(ModifierOps, AllTemplatesLoadInLua) {
    EXPECT_TRUE(template_loads(ModifierTemplate::Empty,           "modifier_demo_empty"));
    EXPECT_TRUE(template_loads(ModifierTemplate::DoT,             "modifier_demo_dot"));
    EXPECT_TRUE(template_loads(ModifierTemplate::Shield,          "modifier_demo_shield"));
    EXPECT_TRUE(template_loads(ModifierTemplate::AoEThinker,      "modifier_demo_aoe"));
    EXPECT_TRUE(template_loads(ModifierTemplate::MotionController,"modifier_demo_mc"));
}

TEST(ModifierOps, CreateWritesFileWithEmbeddedName) {
    const fs::path root = mk_sandbox("create");
    const fs::path p = create_modifier(root, "modifier_my_new",
                                        ModifierTemplate::DoT);
    EXPECT_TRUE(fs::exists(p));
    const std::string body = read_text(p);
    EXPECT_NE(body.find("register_modifier(\"modifier_my_new\""),
              std::string::npos);

    // 同名再创建 -> 抛
    EXPECT_THROW(create_modifier(root, "modifier_my_new",
                                  ModifierTemplate::Empty),
                 std::runtime_error);
    fs::remove_all(root);
}

TEST(ModifierOps, DuplicateRewritesOnlyRegisterName) {
    const fs::path root = mk_sandbox("dup");
    {
        std::ofstream f(root / "scripts" / "modifiers" / "modifier_src.lua");
        // 故意让其他位置也出现 modifier_src 字样 (注释里), 应保持不动.
        f << "-- 复制时, 这一行 modifier_src 不应被改写.\n"
          << "register_modifier(\"modifier_src\", {\n"
          << "    IsHidden = false,\n"
          << "})\n";
    }
    const fs::path dst = duplicate_modifier(root,
        "modifier_src", "modifier_dst");
    EXPECT_TRUE(fs::exists(dst));
    const std::string body = read_text(dst);
    EXPECT_NE(body.find("register_modifier(\"modifier_dst\""),
              std::string::npos);
    EXPECT_EQ(body.find("register_modifier(\"modifier_src\""),
              std::string::npos);
    EXPECT_NE(body.find("-- 复制时, 这一行 modifier_src"),
              std::string::npos);

    // dst 已存在 -> 抛
    EXPECT_THROW(duplicate_modifier(root, "modifier_src", "modifier_dst"),
                 std::runtime_error);
    // 原文件保留
    EXPECT_TRUE(fs::exists(root / "scripts" / "modifiers" / "modifier_src.lua"));
    fs::remove_all(root);
}

TEST(ModifierOps, RenameMovesFileAndPatchesName) {
    const fs::path root = mk_sandbox("rename");
    create_modifier(root, "modifier_pre", ModifierTemplate::Empty);
    const fs::path dst = rename_modifier(root, "modifier_pre", "modifier_post");
    EXPECT_TRUE(fs::exists(dst));
    EXPECT_FALSE(fs::exists(root / "scripts" / "modifiers" / "modifier_pre.lua"));
    const std::string body = read_text(dst);
    EXPECT_NE(body.find("register_modifier(\"modifier_post\""),
              std::string::npos);
    fs::remove_all(root);
}

TEST(ModifierOps, DeleteSendsToTrash) {
    const fs::path root = mk_sandbox("delete");
    create_modifier(root, "modifier_kill", ModifierTemplate::Empty);
    const auto rec = delete_modifier(root, "modifier_kill");
    EXPECT_FALSE(fs::exists(root / "scripts" / "modifiers" / "modifier_kill.lua"));
    EXPECT_TRUE(fs::exists(rec.trash_path));
    fs::remove_all(root);
}

TEST(ModifierOps, RewriteSpecificRegisterCallOnly) {
    const std::string src =
        "register_modifier(\"a\", {})\n"
        "register_modifier(\"b\", {})\n"
        "-- print(\"a\") 不该被改\n";
    const std::string out = rewrite_register_name(src, "a", "z");
    EXPECT_NE(out.find("register_modifier(\"z\""), std::string::npos);
    EXPECT_NE(out.find("register_modifier(\"b\""), std::string::npos);
    // 注释里的 "a" 字面量保留
    EXPECT_NE(out.find("print(\"a\")"), std::string::npos);
}
