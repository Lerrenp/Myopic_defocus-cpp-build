#pragma once

#include "config.h"
#include <string>
#include <optional>

// ==========================================
// 命令行 + 交互配置层
// ==========================================
//
// 行为契约:
//   1) 双击 EXE (无参数,或 stdin 不为交互终端):
//      - 尝试加载 EXE 同目录 config.json
//      - 文件存在 -> 用 JSON 参数启动滤镜
//      - 文件缺失 -> 用默认参数启动滤镜
//      - stderr 显示启动信息
//
//   2) 带参数运行:
//      --config           : 强制进入交互配置模式 (无论是否 JSON 存在)
//      --config=<path>    : 指定加载/写入的 JSON 文件路径
//      --diag=<float>     : 直接指定屏幕对角线 (英寸),跳过交互
//      --distance=<float> : 直接指定观看距离 (cm)
//      --pupil=<float>    : 直接指定瞳孔直径 (μm)
//      --effect=<float>   : 直接指定效果强度 (0.0~1.0)
//      --fps=<int>        : 直接指定帧率
//      --reset            : 清除 JSON 中的字段,恢复默认
//      --help / -h        : 打印帮助并退出
//
//   3) 配置完成后调用 SaveToExeDir 写回 JSON

namespace cli {

// 启动模式
enum class Mode {
    Overlay,            // 直接启动遮罩滤镜
    Configure,          // 进入交互配置向导
    Help,               // 打印帮助
};

// 命令行解析结果
struct CliArgs {
    Mode mode = Mode::Overlay;
    Config overrides;       // 命令行覆盖值 (可为默认值)
    bool hasOverrides = false;  // 是否有任何覆盖
    std::wstring configPath;    // 自定义 JSON 路径 (为空则用 config.json)
};

// 从 wWinMain 的 lpCmdLine 解析
CliArgs Parse(int argc, wchar_t* argv[]);

// 打印帮助到 stdout/stderr
void PrintHelp();

// 交互式配置向导
//  - 询问每项配置,允许按 Enter 保留当前值
//  - 返回最终填好的 Config
Config InteractiveConfig(const Config& current);

// 单行浮点输入(允许空表示保留默认)
std::optional<float> PromptFloat(const wchar_t* prompt, float current, float minVal, float maxVal);
std::optional<int>   PromptInt  (const wchar_t* prompt, int   current, int   minVal, int   maxVal);

// 把窄字符串 (UTF-8) 输出到控制台 (Win32 helper)
void PrintUtf8(const char* s);

} // namespace cli
