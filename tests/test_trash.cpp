// trash 工具: 把文件移到 <data_root>/.trash/<ts>/<rel>. 测试覆盖正常路径,
// 跨目录 reject, 和重入避重命名.

#include "dota/tools/trash.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace dota::tools;
namespace fs = std::filesystem;

namespace {

fs::path mk_tempdir(const std::string& tag) {
    const fs::path base = fs::temp_directory_path() /
        ("dota_trash_" + tag + "_" +
         std::to_string(std::hash<std::string>{}(tag)));
    fs::remove_all(base);
    fs::create_directories(base);
    return base;
}

void write_file(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << body;
}

} // namespace

TEST(Trash, MovesFileUnderDataRoot) {
    const fs::path root = mk_tempdir("basic");
    const fs::path target = root / "heroes" / "ghost.yaml";
    write_file(target, "hero: {}\n");
    ASSERT_TRUE(fs::exists(target));

    const TrashRecord rec = move_to_trash(root, target);
    EXPECT_FALSE(fs::exists(target));
    EXPECT_TRUE(fs::exists(rec.trash_path));

    // trash_path 必须在 root/.trash/ 下, 且包含 heroes/ghost.yaml 末尾.
    const std::string s = rec.trash_path.string();
    EXPECT_NE(s.find(".trash"), std::string::npos);
    EXPECT_NE(s.find("ghost.yaml"), std::string::npos);

    fs::remove_all(root);
}

TEST(Trash, RejectsPathOutsideDataRoot) {
    const fs::path root   = mk_tempdir("outside_root");
    const fs::path other  = mk_tempdir("outside_other");
    const fs::path target = other / "stranger.txt";
    write_file(target, "no");

    EXPECT_THROW(move_to_trash(root, target), std::runtime_error);

    fs::remove_all(root);
    fs::remove_all(other);
}

TEST(Trash, ReentrantSameSecondAvoidsClobber) {
    const fs::path root = mk_tempdir("reentrant");
    const fs::path a = root / "a.txt";
    write_file(a, "first");
    const TrashRecord r1 = move_to_trash(root, a);

    write_file(a, "second");
    const TrashRecord r2 = move_to_trash(root, a);

    EXPECT_NE(r1.trash_path, r2.trash_path);
    EXPECT_TRUE(fs::exists(r1.trash_path));
    EXPECT_TRUE(fs::exists(r2.trash_path));

    fs::remove_all(root);
}
