// ==========================================
// MyopicDefocusConfig - 独立配置器 (Console subsystem)
//
// 职责:
//   1. 解析命令行 (--config/--help/--reset)
//   2. 交互式配置向导 (自动写 JSON)
//   3. 验证配置不启动滤镜
//
// 与主程序解耦:
//   - 用 config_io 共享 JSON 解析器
//   - 用 cli 共享命令行解析
//   - 子系统 = Console (从 CMD 启动天然获得 stdin, 无须 AllocConsole)
// ==========================================
#include "framework.h"
#include "cli.h"
#include "config_io.h"
#include "config.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>

// 把 wstring 路径打印为 UTF-8 → stdout (printf 可见)
static void PrintWpath(const char* label, const std::wstring& path) {
    cli::PrintUtf8(label);
    int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n > 0) {
        std::string u8(static_cast<size_t>(n) - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, u8.data(), n, nullptr, nullptr);
        fprintf(stdout, "%s\n", u8.c_str());
        fflush(stdout);
    }
}

// 子系统 = Console, 入口用 wmain
int wmain(int argc, wchar_t* argv[]) {
    // 设置控制台输出为 UTF-8 (否则中文乱码)
    SetConsoleOutputCP(CP_UTF8);
    fflush(stdout);

    // 1. 解析命令行
    cli::CliArgs args = cli::Parse(argc, argv);

    // 2. Help 模式
    if (args.mode == cli::Mode::Help) {
        cli::PrintHelp();
        cli::PrintUtf8("\n按 Enter 退出...\n");
        std::string dummy;
        std::getline(std::cin, dummy);
        return 0;
    }

    // 3. 处理 --reset: 重置为默认值然后直接写回 JSON 退出
    if (args.hasOverrides) {
        // cli::Parse 把 --reset 转成 args.overrides = Config{}
        // 检测: 所有 overrides 字段都等于默认值 即认定是 reset
        const Config defaults{};
        const auto& o = args.overrides;
        bool isReset =
            o.diagInch         == defaults.diagInch         &&
            o.screenDistanceCM == defaults.screenDistanceCM &&
            o.pupilSizeUm      == defaults.pupilSizeUm      &&
            o.effectStrength   == defaults.effectStrength   &&
            o.targetFps        == defaults.targetFps        &&
            o.resX             == defaults.resX             &&
            o.resY             == defaults.resY;
        if (isReset) {
            const wchar_t* resetFile = args.configPath.empty()
                                       ? kDefaultConfigName
                                       : args.configPath.c_str();
            if (config_io::SaveToExeDir(resetFile, defaults)) {
                PrintWpath("[OK] 配置已重置为默认值并写入: ",
                           config_io::GetExeConfigPath(resetFile).wstring());
            } else {
                cli::PrintUtf8("[ERR] 重置失败!\n");
                return 1;
            }
            return 0;
        }
    }

    // 4. 默认 Configure 模式
    const wchar_t* fileName = args.configPath.empty()
                              ? kDefaultConfigName
                              : args.configPath.c_str();

    // 5. 加载现有 JSON (如果有)
    Config g_config;
    bool loaded = false;
    config_io::LoadFromExeDir(fileName, g_config, loaded);

    PrintWpath("【配置模式】当前 JSON 路径: ",
               config_io::GetExeConfigPath(fileName).wstring());
    if (loaded) cli::PrintUtf8("已加载现有配置 (缺省字段保留默认)\n");
    else        cli::PrintUtf8("未找到现有配置 - 将使用默认值\n");

    // 6. 应用命令行覆盖
    if (args.hasOverrides) {
        const auto& o = args.overrides;
        if (o.diagInch         != Config{}.diagInch)         g_config.diagInch         = o.diagInch;
        if (o.screenDistanceCM != Config{}.screenDistanceCM) g_config.screenDistanceCM = o.screenDistanceCM;
        if (o.pupilSizeUm      != Config{}.pupilSizeUm)      g_config.pupilSizeUm      = o.pupilSizeUm;
        if (o.effectStrength   != Config{}.effectStrength)   g_config.effectStrength   = o.effectStrength;
        if (o.targetFps        != Config{}.targetFps)        g_config.targetFps        = o.targetFps;
        if (o.resX > 0.0f) g_config.resX = o.resX;
        if (o.resY > 0.0f) g_config.resY = o.resY;
    }

    // 7. 交互输入
    Config newCfg = cli::InteractiveConfig(g_config);
    g_config = newCfg;

    // 7. 保存
    if (config_io::SaveToExeDir(fileName, g_config)) {
        PrintWpath("\n[OK] 配置已保存到: ",
                   config_io::GetExeConfigPath(fileName).wstring());
        cli::PrintUtf8("      启动 MyopicDefocus.exe 即可应用新配置。\n");
    } else {
        cli::PrintUtf8("\n[ERR] 保存失败!\n");
        return 1;
    }
    cli::PrintUtf8("\n按 Enter 退出...\n");
    std::string dummy;
    std::getline(std::cin, dummy);
    return 0;
}