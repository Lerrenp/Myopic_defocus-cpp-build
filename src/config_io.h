#pragma once

#include "config.h"
#include <string>
#include <filesystem>

// ==========================================
// 配置持久化层 (JSON I/O)
// 基于 nlohmann/json (单 header, 已 vendored 到 third_party/)
// ==========================================

namespace config_io {

// 配置文件 JSON 字段名
namespace key {
    inline constexpr const char* DiagInch          = "diagInch";
    inline constexpr const char* ScreenDistanceCM   = "screenDistanceCM";
    inline constexpr const char* PupilSizeUm        = "pupilSizeUm";
    inline constexpr const char* EffectStrength     = "effectStrength";
    inline constexpr const char* ResX               = "resX";
    inline constexpr const char* ResY               = "resY";
    inline constexpr const char* TargetFps          = "targetFps";
    inline constexpr const char* Version            = "version";
    inline constexpr const char* Note               = "note";
}

// 将 Config 序列化为 JSON 字符串 (带缩进,易读)
// 返回的 std::string 可直接写文件
std::string ToJsonString(const Config& cfg);

// 从 JSON 字符串解析,缺失字段保留 cfg 当前值
// 返回 true 表示至少有一个字段被识别,即使有错误
bool FromJsonString(const std::string& jsonStr, Config& cfg);

// 便捷函数: 从可执行文件所在目录加载 config.json
// - 文件不存在返回 false (使用默认值)
// - 文件存在但解析失败也返回 false,但在 stderr 打印警告
// - 第二个参数返回是否实际加载了文件
bool LoadFromExeDir(const wchar_t* fileName, Config& cfg, bool& loaded);

// 便捷函数: 保存到可执行文件所在目录
// 返回 true 表示写入成功
bool SaveToExeDir(const wchar_t* fileName, const Config& cfg);

// 获取 EXE 所在目录的完整路径
std::filesystem::path GetExeDirectory();

// 拼接 EXE 目录 + 文件名
std::filesystem::path GetExeConfigPath(const wchar_t* fileName);

} // namespace config_io
