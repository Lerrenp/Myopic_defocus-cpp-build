// ==========================================
// config_io 简易插桩单测
// 测试 JSON 序列化/反序列化往返一致性 + 默认值保留
// ==========================================
#include "config_io.h"
#include "cli.h"          // cli::PrintUtf8 - Unicode 写到控制台, 不动 codepage
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace config_io;

// 由 test_optical_model.cpp 定义
extern int g_passed;
extern int g_failed;

// 输出助手: 与 test_optical_model.cpp::out 等价, 用 cli::PrintUtf8 (WriteConsoleW)
// 解决 cmd.exe 默认 GBK (CP936) 下中文 printf 乱码; 不修改控制台 codepage
static void out(const char* fmt, ...) {
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    cli::PrintUtf8(buf);
}

#define EXPECT_EQ_INT(actual, expected) \
    do { \
        int _a = (actual), _e = (expected); \
        if (_a == _e) { \
            ++g_passed; \
            out("  [PASS] %s == %d\n", #actual, _e); \
        } else { \
            ++g_failed; \
            out("  [FAIL] %s: got %d expected %d\n", #actual, _a, _e); \
        } \
    } while(0)

#define EXPECT_NEAR(actual, expected, tol) \
    do { \
        float _a = (actual), _e = (expected); \
        if (std::fabs(_a - _e) <= (tol)) { \
            ++g_passed; \
            out("  [PASS] %s ~= %g (diff=%g)\n", #actual, _e, std::fabs(_a-_e)); \
        } else { \
            ++g_failed; \
            out("  [FAIL] %s: got %g expected %g (diff=%g > %g)\n", \
                #actual, _a, _e, std::fabs(_a-_e), (float)(tol)); \
        } \
    } while(0)

#define CASE(name) out("\n[TEST] %s\n", name)

// 往返一致性: serialize -> deserialize 应该得到完全相同的值
static void test_roundtrip() {
    CASE("往返一致性 (serialize -> parse)");
    Config cfg;
    cfg.diagInch = 27.5f;
    cfg.screenDistanceCM = 50.0f;
    cfg.pupilSizeUm = 7000.0f;
    cfg.effectStrength = 0.42f;
    cfg.resX = 1920.0f;
    cfg.resY = 1080.0f;
    cfg.targetFps = 60;

    std::string json = ToJsonString(cfg);
    out("  JSON output:\n%s\n", json.c_str());

    Config cfg2;
    FromJsonString(json, cfg2);

    EXPECT_NEAR(cfg2.diagInch,         27.5f,  0.001f);
    EXPECT_NEAR(cfg2.screenDistanceCM, 50.0f,  0.001f);
    EXPECT_NEAR(cfg2.pupilSizeUm,      7000.0f,0.001f);
    EXPECT_NEAR(cfg2.effectStrength,   0.42f,  0.001f);
    EXPECT_NEAR(cfg2.resX,             1920.0f,0.001f);
    EXPECT_NEAR(cfg2.resY,             1080.0f,0.001f);
    EXPECT_EQ_INT(cfg2.targetFps,      60);
}

// 缺失字段保留 cfg 当前值 (例如先有默认 Config,JSON 只有 diagInch)
static void test_partial_load_keeps_defaults() {
    CASE("部分加载: JSON 仅含部分字段, 缺失字段保留 Config 当前值");
    std::string partial = R"({"diagInch": 32.0, "targetFps": 30})";

    Config cfg;  // 默认值: diag=28, fps=120
    bool ok = FromJsonString(partial, cfg);
    EXPECT_EQ_INT(ok, 1);

    EXPECT_NEAR(cfg.diagInch,         32.0f,  0.001f);
    EXPECT_EQ_INT(cfg.targetFps,     30);
    // 缺失字段应该保留 Config 默认
    EXPECT_NEAR(cfg.screenDistanceCM, 40.0f,  0.001f);
    EXPECT_NEAR(cfg.pupilSizeUm,      6500.0f,0.001f);
}

// resX/resY = 0 应该序列化为 null
static void test_auto_resolution_serializes_null() {
    CASE("分辨率=0 序列化为 null");
    Config cfg;
    cfg.resX = 0.0f;
    cfg.resY = 0.0f;

    std::string json = ToJsonString(cfg);

    // 检查字段存在且值为 null
    if (json.find("\"resX\": null") != std::string::npos &&
        json.find("\"resY\": null") != std::string::npos) {
        ++g_passed;
        out("  [PASS] resX/resY serialized as null\n");
    } else {
        ++g_failed;
        out("  [FAIL] resX/resY should be null:\n%s\n", json.c_str());
    }
}

// 无效 JSON 应该返回 false 并保留 cfg 原值
static void test_invalid_json_returns_false() {
    CASE("无效 JSON 输入, 返回 false, cfg 保留原值");
    Config cfg;
    cfg.diagInch = 14.0f;

    bool ok = FromJsonString("{not json}", cfg);
    EXPECT_EQ_INT(ok, 0);
    EXPECT_NEAR(cfg.diagInch, 14.0f, 0.001f);  // 没被改坏
}

// 文件加载/保存
static void test_file_save_load() {
    CASE("文件: 保存 -> 读取 -> 一致");
    Config cfg;
    cfg.diagInch = 13.3f;
    cfg.screenDistanceCM = 35.0f;
    cfg.effectStrength = 0.7f;
    cfg.targetFps = 144;

    const wchar_t* testPath = L"test_config_io_tmp.json";
    bool saved = SaveToExeDir(testPath, cfg);
    EXPECT_EQ_INT(saved, 1);

    Config loaded;
    bool wasLoaded = false;
    bool ok = LoadFromExeDir(testPath, loaded, wasLoaded);
    EXPECT_EQ_INT(ok, 1);
    EXPECT_EQ_INT(wasLoaded, 1);

    EXPECT_NEAR(loaded.diagInch,         13.3f,  0.001f);
    EXPECT_NEAR(loaded.screenDistanceCM, 35.0f,  0.001f);
    EXPECT_NEAR(loaded.effectStrength,   0.7f,   0.001f);
    EXPECT_EQ_INT(loaded.targetFps,     144);

    // 清理
    std::filesystem::path p = GetExeConfigPath(testPath);
    std::filesystem::remove(p);
}

// 不存在的文件: loaded=false, ok=true (不是错误)
static void test_missing_file() {
    CASE("文件不存在: loaded=false, ok=true (视为默认)");
    const wchar_t* phantom = L"nonexistent_config_abc123.json";
    Config cfg;
    bool wasLoaded = false;
    bool ok = LoadFromExeDir(phantom, cfg, wasLoaded);
    EXPECT_EQ_INT(ok, 1);
    EXPECT_EQ_INT(wasLoaded, 0);
}

int RunConfigIoTests() {
    out("  ---- config_io 简易插桩 ----\n");
    std::fflush(stdout);

    test_roundtrip();
    test_partial_load_keeps_defaults();
    test_auto_resolution_serializes_null();
    test_invalid_json_returns_false();
    test_file_save_load();
    test_missing_file();

    // 关键: 显式刷新所有 stream 再返回
    std::fflush(stdout);
    std::fflush(stderr);
    return g_failed > 0 ? 1 : 0;
}
