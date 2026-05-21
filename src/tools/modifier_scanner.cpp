#include "dota/tools/modifier_scanner.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

// 去掉每行 -- 之后的注释 (单行风格). 不处理 --[[ ]] 多行块, 但注册行用
// 多行注释包裹的写法在本仓库里没用过, 不投入解析复杂度.
std::string strip_line_comments(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    std::size_t i = 0;
    while (i < src.size()) {
        const char c = src[i];
        if (c == '-' && i + 1 < src.size() && src[i + 1] == '-') {
            // 跳到行尾
            while (i < src.size() && src[i] != '\n') ++i;
        } else {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

} // namespace

std::vector<std::string> extract_register_names(const std::string& source) {
    const std::string s = strip_line_comments(source);
    // register_modifier 紧跟可选空白 + ( + 引号 + name + 引号
    static const std::regex re(
        R"(register_modifier\s*\(\s*["']([A-Za-z_][A-Za-z0-9_]*)["'])");
    std::vector<std::string> out;
    auto begin = std::sregex_iterator(s.begin(), s.end(), re);
    const auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        out.push_back((*it)[1].str());
    }
    return out;
}

std::vector<ModifierSourceInfo> scan_modifier_dir(const fs::path& dir) {
    std::vector<ModifierSourceInfo> out;
    if (!fs::exists(dir) || !fs::is_directory(dir)) return out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".lua") continue;
        ModifierSourceInfo info;
        info.file_path = e.path();
        info.file_stem = e.path().stem().string();
        info.register_names = extract_register_names(read_text(e.path()));
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  return a.file_stem < b.file_stem;
              });
    return out;
}

} // namespace dota::tools
