#include "dota/tools/trash.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

std::string now_stamp() {
    using clock = std::chrono::system_clock;
    const auto t = clock::to_time_t(clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return ss.str();
}

// 校验 src 是否在 root 子树内 (按 lexically_relative 处理).
fs::path relative_under(const fs::path& root, const fs::path& src) {
    const fs::path r = fs::weakly_canonical(root);
    const fs::path s = fs::weakly_canonical(src);
    const fs::path rel = s.lexically_relative(r);
    // Windows 上 path::native() 是 wstring, 不能直接对它 rfind("..").
    // 用 path::operator== 比较第一个组件, 自带平台转换, 跨平台干净.
    if (rel.empty() || *rel.begin() == fs::path("..")) {
        throw std::runtime_error("move_to_trash: src 不在 data_root 内: " +
                                 src.string());
    }
    return rel;
}

} // namespace

TrashRecord move_to_trash(const fs::path& data_root, const fs::path& src) {
    if (!fs::exists(src)) {
        throw std::runtime_error("move_to_trash: 不存在: " + src.string());
    }
    const fs::path rel = relative_under(data_root, src);

    const fs::path trash_root = data_root / ".trash" / now_stamp();
    fs::path dst = trash_root / rel;

    // 同一秒内重复触发, 给 dst 再加 -N 后缀避重.
    if (fs::exists(dst)) {
        for (int n = 1; n < 1000; ++n) {
            fs::path candidate = dst;
            candidate += "-" + std::to_string(n);
            if (!fs::exists(candidate)) {
                dst = candidate;
                break;
            }
        }
    }

    std::error_code ec;
    fs::create_directories(dst.parent_path(), ec);
    if (ec) {
        throw std::runtime_error("move_to_trash: 无法创建回收站目录: " +
                                 dst.parent_path().string() + ": " + ec.message());
    }

    fs::rename(src, dst, ec);
    if (ec) {
        // 跨设备时 rename 会失败, fallback 到拷贝 + 删除.
        fs::copy(src, dst,
                 fs::copy_options::recursive | fs::copy_options::copy_symlinks,
                 ec);
        if (ec) {
            throw std::runtime_error("move_to_trash: 拷贝失败 " + src.string() +
                                     " -> " + dst.string() + ": " + ec.message());
        }
        fs::remove_all(src, ec);
        if (ec) {
            throw std::runtime_error("move_to_trash: 移除原始文件失败 " +
                                     src.string() + ": " + ec.message());
        }
    }

    TrashRecord rec;
    rec.original_path = src;
    rec.trash_path    = dst;
    return rec;
}

} // namespace dota::tools
