#include "cli.h"
#include "config_io.h"

#include <iostream>
#include <string>
#include <sstream>
#include <cstdlib>
#include <clocale>
#include <io.h>
#include <fcntl.h>
#include <Windows.h>

namespace cli {

void PrintUtf8(const char* s) {
    if (!s) return;
    // 优先 stdout (管道/文件可见) → 其次 WriteConsoleW (控制台可见)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        // 真控制台: 用 WriteConsoleW 直接写 wide char
        int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (needed > 0) {
            std::wstring ws(static_cast<size_t>(needed) - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s, -1, ws.data(), needed);
            DWORD written = 0;
            WriteConsoleW(hOut, ws.c_str(), static_cast<DWORD>(ws.size()), &written, nullptr);
        }
    } else {
        // 管道 / 文件 / 父进程重定向: fprintf(stdout) 写 UTF-8
        fprintf(stdout, "%s", s);
        fflush(stdout);
    }
}

static std::string ReadUtf8Line() {
    std::string line;
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t buf[512] = {};
    DWORD read = 0;
    if (!ReadConsoleW(hin, buf, 511, &read, nullptr) || read == 0) {
        return line;
    }
    while (read > 0 && (buf[read-1] == L'\r' || buf[read-1] == L'\n')) {
        buf[--read] = L'\0';
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return line;
    line.resize(static_cast<size_t>(needed) - 1);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, line.data(), needed, nullptr, nullptr);
    return line;
}

static bool IsFlag(const std::wstring& a, const wchar_t* short_, const wchar_t* long_) {
    return a == short_ || a == long_;
}

static bool TryGetValue(const std::wstring& a, const wchar_t* flag, std::wstring& out) {
    std::wstring prefix = std::wstring(flag) + L"=";
    if (a.size() > prefix.size() && a.compare(0, prefix.size(), prefix) == 0) {
        out = a.substr(prefix.size());
        return true;
    }
    return false;
}

CliArgs Parse(int /*argc*/, wchar_t* argv[]) {
    CliArgs args;
    int argc = 0;
    while (argv[argc] != nullptr) ++argc;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];

        if (a == L"--help" || a == L"-h" || a == L"/?") {
            args.mode = Mode::Help;
            return args;
        }

        std::wstring v;
        if (IsFlag(a, L"--config", L"-c")) {
            args.mode = Mode::Configure;
            if (i + 1 < argc && argv[i+1][0] != L'-') {
                args.configPath = argv[++i];
            }
            continue;
        }
        if (a == L"--reset") {
            args.overrides = Config{};
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--config", v)) {
            args.mode = Mode::Configure;
            args.configPath = v;
            continue;
        }
        if (TryGetValue(a, L"--diag", v)) {
            args.overrides.diagInch = std::stof(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--distance", v)) {
            args.overrides.screenDistanceCM = std::stof(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--pupil", v)) {
            args.overrides.pupilSizeUm = std::stof(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--effect", v)) {
            args.overrides.effectStrength = std::stof(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--fps", v)) {
            args.overrides.targetFps = std::stoi(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--resx", v)) {
            args.overrides.resX = std::stof(v);
            args.hasOverrides = true;
            continue;
        }
        if (TryGetValue(a, L"--resy", v)) {
            args.overrides.resY = std::stof(v);
            args.hasOverrides = true;
            continue;
        }

        std::wcerr << L"[cli] Unknown option: " << a << std::endl;
    }

    return args;
}

void PrintHelp() {
    const char* help =
        "Myopic Defocus - 离焦模拟器\n"
        "用法:\n"
        "  MyopicDefocus.exe              启动滤镜 (自动加载 config.json)\n"
        "  MyopicDefocusConfig.exe         进入交互配置模式\n"
        "  MyopicDefocusConfig.exe --config=my.json  使用自定义配置文件\n"
        "  MyopicDefocusConfig.exe --diag=27 --distance=40  命令行设定值后退出\n"
        "  MyopicDefocusConfig.exe --reset  重置 config.json 为默认值\n"
        "  MyopicDefocusConfig.exe --help   显示此帮助\n"
        "\n"
        "参数说明:\n"
        "  --diag=<float>       屏幕对角线 (英寸),默认 28\n"
        "  --distance=<float>   观看距离 (cm),默认 40\n"
        "  --pupil=<float>      瞳孔直径 (um),默认 6500\n"
        "  --effect=<float>     效果强度 (0.0~1.0),默认 0.1\n"
        "  --fps=<int>          目标帧率 (1~240),默认 120\n"
        "  --resx=<float>       分辨率宽 (0 表示自动),默认 0\n"
        "  --resy=<float>       分辨率高 (0 表示自动),默认 0\n"
        "\n"
        "使用流程:\n"
        "  1. MyopicDefocusConfig.exe 进行交互配置 (自动保存 config.json)\n"
        "  2. MyopicDefocus.exe 启动滤镜 (读取上一步生成的 config.json)\n"
        "  3. MyopicDefocus.exe  按 Esc 退出滤镜\n"
        "  4. 需要调参: 重新运行 MyopicDefocusConfig.exe, 改完后重启滤镜\n"
        "\n"
        "交互模式:\n"
        "  逐项询问各参数,直接按 Enter 保留当前值\n"
        "  配置完成后自动保存到 config.json\n"
        "\n"
        "滤镜运行时:\n"
        "  Esc      退出\n";
    PrintUtf8(help);
}

static std::optional<float> TryParseFloat(const std::string& s, float& out) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        out = std::stof(s, &pos);
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

static std::optional<int> TryParseInt(const std::string& s, int& out) {
    if (s.empty()) return std::nullopt;
    try {
        size_t pos = 0;
        out = std::stoi(s, &pos);
        return out;
    } catch (...) {
        return std::nullopt;
    }
}

// 宽字符提示 → UTF-8, 然后 printf 输出 (Console CP_UTF8 下正确显示)
static void WprintPrompt(const wchar_t* prompt, float current, float minVal, float maxVal) {
    int n1 = WideCharToMultiByte(CP_UTF8, 0, prompt, -1, nullptr, 0, nullptr, nullptr);
    std::string p8(static_cast<size_t>(n1) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, prompt, -1, p8.data(), n1, nullptr, nullptr);
    fprintf(stdout, "%s [%.1f] (%.0f~%.0f): ", p8.c_str(), current, minVal, maxVal);
    fflush(stdout);
}

static void WprintPromptInt(const wchar_t* prompt, int current, int minVal, int maxVal) {
    int n1 = WideCharToMultiByte(CP_UTF8, 0, prompt, -1, nullptr, 0, nullptr, nullptr);
    std::string p8(static_cast<size_t>(n1) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, prompt, -1, p8.data(), n1, nullptr, nullptr);
    fprintf(stdout, "%s [%d] (%d~%d): ", p8.c_str(), current, minVal, maxVal);
    fflush(stdout);
}

std::optional<float> PromptFloat(const wchar_t* prompt, float current, float minVal, float maxVal) {
    while (true) {
        WprintPrompt(prompt, current, minVal, maxVal);
        std::string line = ReadUtf8Line();
        if (line.empty()) return current;
        float v;
        auto r = TryParseFloat(line, v);
        if (r && v >= minVal && v <= maxVal) return v;
        PrintUtf8("  输入无效,请重新输入 (或按 Enter 跳过)\n");
    }
}

std::optional<int> PromptInt(const wchar_t* prompt, int current, int minVal, int maxVal) {
    while (true) {
        WprintPromptInt(prompt, current, minVal, maxVal);
        std::string line = ReadUtf8Line();
        if (line.empty()) return current;
        int v;
        auto r = TryParseInt(line, v);
        if (r && v >= minVal && v <= maxVal) return v;
        PrintUtf8("  输入无效,请重新输入 (或按 Enter 跳过)\n");
    }
}

Config InteractiveConfig(const Config& current) {
    PrintUtf8("\n=== 交互配置模式 ===\n");
    PrintUtf8("直接按 Enter 保留当前值,输入新值后回车生效。\n\n");

    Config c = current;

    PrintUtf8("[显示器物理]\n");
    c.diagInch = *PromptFloat(L"  屏幕对角线 (英寸)", c.diagInch, 5.0f, 100.0f);

    PrintUtf8("[观看参数]\n");
    c.screenDistanceCM = *PromptFloat(L"  观看距离 (cm)", c.screenDistanceCM, 5.0f, 500.0f);
    c.pupilSizeUm      = *PromptFloat(L"  瞳孔直径 (um)", c.pupilSizeUm,      1000.0f, 12000.0f);

    PrintUtf8("[效果]\n");
    c.effectStrength   = *PromptFloat(L"  效果强度 (0~1)", c.effectStrength,   0.0f, 1.0f);

    PrintUtf8("[运行时]\n");
    c.targetFps = *PromptInt(L"  目标帧率", c.targetFps, 1, 240);

    fprintf(stdout, "  分辨率宽 (像素,0=自动) [%.0f]: ", c.resX); fflush(stdout);
    std::string xs = ReadUtf8Line();
    if (!xs.empty()) {
        float v; if (TryParseFloat(xs, v)) c.resX = v;
    }
    fprintf(stdout, "  分辨率高 (像素,0=自动) [%.0f]: ", c.resY); fflush(stdout);
    std::string ys = ReadUtf8Line();
    if (!ys.empty()) {
        float v; if (TryParseFloat(ys, v)) c.resY = v;
    }

    return c;
}

} // namespace cli