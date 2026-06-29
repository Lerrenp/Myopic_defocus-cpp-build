#pragma once

// ==========================================
// 【配置区域】 参数集中定义
// 修改后需要重新编译,或通过交互配置模式修改后写入 JSON
// ==========================================
struct Config {
    // -------- 显示器物理参数 --------
    // 对应 JS: diagInch
    float diagInch = 28.0f;

    // -------- 眼睛/观看距离参数 --------
    // 对应 JS: screenDistanceCM (JS里是MM，这里用CM方便设置，计算时会转)
    float screenDistanceCM = 40.0f;

    // 对应 JS: pupilSizeUm (JS 默认 6500)
    float pupilSizeUm = 6500.0f;

    // -------- 效果参数 --------
    // 对应 JS: effectStrengthPercent
    float effectStrength = 0.1f; // 0.0 - 1.0

    // -------- 运行时参数 --------
    // 0 表示自动从系统获取
    float resX = 0.0f;
    float resY = 0.0f;

    // 期望帧率 (1~240),默认 120
    int targetFps = 120;

    // JSON 文件路径 (相对 EXE 目录),默认 "config.json"
    // 可由命令行 --config=path.json 覆盖
    wchar_t configFileName[260] = L"config.json";
};

// 全局配置实例（程序唯一）
extern Config g_config;

// 默认配置文件名
inline constexpr const wchar_t* kDefaultConfigName = L"config.json";
