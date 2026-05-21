#include "dota/tools/modifier_ops.hpp"

#include "dota/tools/modifier_scanner.hpp"

#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace dota::tools {

namespace fs = std::filesystem;

namespace {

fs::path modifiers_dir(const fs::path& data_root) {
    return data_root / "scripts" / "modifiers";
}

std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("modifier_ops: 读失败 " + p.string());
    std::ostringstream os;
    os << f.rdbuf();
    return os.str();
}

void write_text_new(const fs::path& p, const std::string& body) {
    if (fs::exists(p)) {
        throw std::runtime_error("modifier_ops: 文件已存在 " + p.string());
    }
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("modifier_ops: 写失败 " + p.string());
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
}

} // namespace

bool is_valid_modifier_name(std::string_view name) {
    if (name.empty()) return false;
    if (name.front() == '_') return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

fs::path modifier_file_path(const fs::path& data_root,
                             std::string_view name) {
    return modifiers_dir(data_root) / (std::string(name) + ".lua");
}

std::string render_modifier_template(const std::string& name,
                                      ModifierTemplate kind) {
    std::ostringstream os;
    os << "-- " << name << ": ";
    switch (kind) {
        case ModifierTemplate::Empty:
            os << "空模板. 在这里填字段.\n\n"
               << "register_modifier(\"" << name << "\", {\n"
               << "    IsHidden    = false,\n"
               << "    IsDebuff    = false,\n"
               << "    IsPurgable  = true,\n"
               << "    States      = {},\n"
               << "    Properties  = {},\n"
               << "})\n";
            break;
        case ModifierTemplate::DoT:
            os << "周期性伤害模板.\n\n"
               << "register_modifier(\"" << name << "\", {\n"
               << "    IsHidden        = false,\n"
               << "    IsDebuff        = true,\n"
               << "    IsPurgable      = true,\n"
               << "    ThinkInterval   = 1.0,\n"
               << "    OnIntervalThink = function(self, owner)\n"
               << "        owner:apply_damage(DamageType.MAGICAL, 50.0)\n"
               << "    end,\n"
               << "})\n";
            break;
        case ModifierTemplate::Shield:
            os << "护盾模板. 通过 OnPreTakeDamage 返回吸收数值.\n\n"
               << "register_modifier(\"" << name << "\", {\n"
               << "    IsHidden        = false,\n"
               << "    IsPurgable      = true,\n"
               << "    IsDispellable   = true,\n"
               << "    OnCreated = function(self, _owner)\n"
               << "        self._remaining = self._remaining or 200.0\n"
               << "    end,\n"
               << "    OnPreTakeDamage = function(self, _owner, ev)\n"
               << "        if ev.amount <= 0.0 then return 0.0 end\n"
               << "        local eat = math.min(self._remaining or 0.0, ev.amount)\n"
               << "        self._remaining = (self._remaining or 0.0) - eat\n"
               << "        return eat\n"
               << "    end,\n"
               << "})\n";
            break;
        case ModifierTemplate::AoEThinker:
            os << "Thinker AoE 模板. 配合 World:create_thinker 使用.\n\n"
               << "register_modifier(\"" << name << "\", {\n"
               << "    IsHidden        = true,\n"
               << "    ThinkInterval   = 0.5,\n"
               << "    States          = {\n"
               << "        ModifierState.UNTARGETABLE,\n"
               << "        ModifierState.NO_UNIT_COLLISION,\n"
               << "    },\n"
               << "    OnIntervalThink = function(self, owner)\n"
               << "        -- TODO: \n"
               << "    end,\n"
               << "})\n";
            break;
        case ModifierTemplate::MotionController:
            os << "Motion Controller 模板. 每 motion tick 改写 owner 位置.\n\n"
               << "register_modifier(\"" << name << "\", {\n"
               << "    IsHidden           = false,\n"
               << "    IsDebuff           = true,\n"
               << "    IsMotionController = true,\n"
               << "    MotionPriority     = 5,\n"
               << "    States             = { ModifierState.STUNNED },\n"
               << "    OnMotionTick = function(self, owner, dt)\n"
               << "        -- TODO: \n"
               << "    end,\n"
               << "})\n";
            break;
    }
    return os.str();
}

fs::path create_modifier(const fs::path& data_root,
                          const std::string& name,
                          ModifierTemplate kind) {
    if (!is_valid_modifier_name(name)) {
        throw std::runtime_error("modifier_ops: 名字非法 " + name);
    }
    const fs::path target = modifier_file_path(data_root, name);
    write_text_new(target, render_modifier_template(name, kind));
    return target;
}

std::string rewrite_register_name(const std::string& source,
                                   const std::string& src_name,
                                   const std::string& dst_name) {
    // 替换 register_modifier 后第 1 参里的 src_name. 双引号 / 单引号都支持.
    static const std::string lhs = "register_modifier";
    std::string out = source;
    std::size_t pos = 0;
    while ((pos = out.find(lhs, pos)) != std::string::npos) {
        std::size_t i = pos + lhs.size();
        // 跳空白
        while (i < out.size() && std::isspace(static_cast<unsigned char>(out[i]))) ++i;
        if (i >= out.size() || out[i] != '(') { pos = i; continue; }
        ++i;
        while (i < out.size() && std::isspace(static_cast<unsigned char>(out[i]))) ++i;
        if (i >= out.size() || (out[i] != '"' && out[i] != '\'')) { pos = i; continue; }
        const char q = out[i];
        const std::size_t name_begin = i + 1;
        const std::size_t name_end = out.find(q, name_begin);
        if (name_end == std::string::npos) break;
        const std::string n = out.substr(name_begin, name_end - name_begin);
        if (n == src_name) {
            out.replace(name_begin, n.size(), dst_name);
            pos = name_begin + dst_name.size() + 1;
        } else {
            pos = name_end + 1;
        }
    }
    return out;
}

fs::path duplicate_modifier(const fs::path& data_root,
                             const std::string& src_name,
                             const std::string& dst_name) {
    if (!is_valid_modifier_name(dst_name)) {
        throw std::runtime_error("modifier_ops: 目标名非法 " + dst_name);
    }
    if (src_name == dst_name) {
        throw std::runtime_error("modifier_ops: src == dst");
    }
    const fs::path src = modifier_file_path(data_root, src_name);
    const fs::path dst = modifier_file_path(data_root, dst_name);
    if (!fs::exists(src)) {
        throw std::runtime_error("modifier_ops: 源不存在 " + src.string());
    }
    const std::string body = read_text(src);
    const std::string rewritten = rewrite_register_name(body, src_name, dst_name);
    write_text_new(dst, rewritten);
    return dst;
}

fs::path rename_modifier(const fs::path& data_root,
                          const std::string& src_name,
                          const std::string& dst_name) {
    if (!is_valid_modifier_name(dst_name)) {
        throw std::runtime_error("modifier_ops: 目标名非法 " + dst_name);
    }
    if (src_name == dst_name) {
        throw std::runtime_error("modifier_ops: src == dst");
    }
    const fs::path src = modifier_file_path(data_root, src_name);
    const fs::path dst = modifier_file_path(data_root, dst_name);
    if (!fs::exists(src)) {
        throw std::runtime_error("modifier_ops: 源不存在 " + src.string());
    }
    if (fs::exists(dst)) {
        throw std::runtime_error("modifier_ops: 目标已存在 " + dst.string());
    }
    const std::string rewritten =
        rewrite_register_name(read_text(src), src_name, dst_name);
    // 先写新文件再删旧文件, 失败可手动恢复
    write_text_new(dst, rewritten);
    std::error_code ec;
    fs::remove(src, ec);
    if (ec) {
        throw std::runtime_error("modifier_ops: 删旧文件失败 " + ec.message());
    }
    return dst;
}

TrashRecord delete_modifier(const fs::path& data_root,
                             const std::string& name) {
    const fs::path target = modifier_file_path(data_root, name);
    if (!fs::exists(target)) {
        throw std::runtime_error("modifier_ops: 不存在 " + target.string());
    }
    return move_to_trash(data_root, target);
}

} // namespace dota::tools
