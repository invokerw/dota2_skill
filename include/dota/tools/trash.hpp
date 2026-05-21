#pragma once

// 软删除: 把文件 (或整个目录) 挪到 <data_root>/.trash/<YYYYMMDD-HHMMSS>/<rel_path>
// 下. 失败时抛 std::runtime_error.
//
// 用 data_root 把回收站锚定在数据目录里, 便于人工恢复; 不依赖系统 Trash.

#include <filesystem>
#include <string>

namespace dota::tools {

struct TrashRecord {
    std::filesystem::path original_path;   // 原始绝对路径
    std::filesystem::path trash_path;      // 移动后绝对路径
};

// data_root 通常 = data/. src 必须在 data_root 内, 否则抛出.
TrashRecord move_to_trash(const std::filesystem::path& data_root,
                          const std::filesystem::path& src);

} // namespace dota::tools
