// ==========================================
// 简易插桩单测 - ComputeBlurRadii
// 不依赖任何测试框架，仅用 printf + 退出码
// ==========================================
#include "optical_model.h"
#include "config.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

// 前置声明 SetConsoleOutputCP, 避免引入 <windows.h> 的宏污染
// (<windows.h> 会定义 near/far 宏, 与下方测试代码的 near/far 变量名冲突)
extern "C" int __stdcall SetConsoleOutputCP(unsigned int wCodePageID);
// CP_UTF8 = 65001 (windows.h 中定义; 此处硬编码以避免依赖该头文件)
static constexpr unsigned int kCP_UTF8 = 65001;

// 全局测试计数器,跨翻译单元共享 (test_config_io.cpp 也写入)
int g_passed = 0;
int g_failed = 0;

// 由 test_config_io.cpp 提供
int RunConfigIoTests();

// ----------- 断言宏 -----------
#define EXPECT_NEAR(actual, expected, tol) \
    do { \
        float _a = (actual); \
        float _e = (expected); \
        float _d = std::fabs(_a - _e); \
        if (_d <= (tol)) { \
            ++g_passed; \
            std::printf("  [PASS] %-32s  got=%.4f  expected=%.4f  diff=%.5f\n", \
                #actual, _a, _e, _d); \
        } else { \
            ++g_failed; \
            std::printf("  [FAIL] %-32s  got=%.4f  expected=%.4f  diff=%.5f > tol=%.4f\n", \
                #actual, _a, _e, _d, (float)(tol)); \
        } \
    } while(0)

#define EXPECT_TRUE(cond) \
    do { \
        if (cond) { \
            ++g_passed; \
            std::printf("  [PASS] %s\n", #cond); \
        } else { \
            ++g_failed; \
            std::printf("  [FAIL] %s\n", #cond); \
        } \
    } while(0)

#define CASE(name) std::printf("\n[TEST] %s\n", name)

// ----------- 测试用例 -----------

// 用 JS 上游默认参数验证 1:1 一致性
// (config: diagInch=14, resX=2560, resY=1440, dist=40cm, pupil=6500um)
// 手算: blur_b = (6.5 * (138.9/261.1) / 0.12107) * 0.32 ≈ 9.14
//       blur_g = (6.5 * (63.3/336.7) / 0.12107) * 0.32 ≈ 3.23
static void test_js_upstream_defaults() {
    CASE("JS 上游默认参数 (14\", 2560x1440, 40cm, 6500um)");
    Config cfg;
    cfg.diagInch = 14.0f;
    cfg.resX = 2560.0f;
    cfg.resY = 1440.0f;
    cfg.screenDistanceCM = 40.0f;
    cfg.pupilSizeUm = 6500.0f;

    BlurRadii r = ComputeBlurRadii(cfg);
    EXPECT_NEAR(r.blue,  9.1396f, 0.01f);
    EXPECT_NEAR(r.green, 3.2298f, 0.01f);
    EXPECT_TRUE(r.blue > r.green);  // 蓝光离焦更大
}

// 当前 C++ 默认参数 (28" 显示器，mm/px 比 14" 大一倍，模糊半径小一半)
static void test_cpp_defaults() {
    CASE("C++ 默认参数 (28\", 2560x1440, 40cm, 6500um)");
    Config cfg;
    cfg.diagInch = 28.0f;
    cfg.resX = 2560.0f;
    cfg.resY = 1440.0f;
    cfg.screenDistanceCM = 40.0f;
    cfg.pupilSizeUm = 6500.0f;

    BlurRadii r = ComputeBlurRadii(cfg);
    EXPECT_NEAR(r.blue,  4.5698f, 0.01f);
    EXPECT_NEAR(r.green, 1.6149f, 0.01f);
}

// 4K 分辨率: 同样 14"，更高 DPI → 模糊更大
static void test_4k_resolution() {
    CASE("4K 分辨率 (14\", 3840x2160, 40cm, 6500um)");
    Config cfg;
    cfg.diagInch = 14.0f;
    cfg.resX = 3840.0f;
    cfg.resY = 2160.0f;
    cfg.screenDistanceCM = 40.0f;
    cfg.pupilSizeUm = 6500.0f;

    BlurRadii r = ComputeBlurRadii(cfg);
    EXPECT_NEAR(r.blue,  13.7095f, 0.01f);
    EXPECT_NEAR(r.green, 4.8451f, 0.01f);
}

// 安全性: 零分辨率不能崩溃
static void test_zero_resolution_safe() {
    CASE("边界: 零分辨率 (不崩溃, 输出有限值)");
    Config cfg;
    cfg.diagInch = 14.0f;
    cfg.resX = 0.0f;
    cfg.resY = 0.0f;
    cfg.screenDistanceCM = 40.0f;
    cfg.pupilSizeUm = 6500.0f;

    BlurRadii r = ComputeBlurRadii(cfg);
    EXPECT_TRUE(std::isfinite(r.blue));
    EXPECT_TRUE(std::isfinite(r.green));
    EXPECT_TRUE(r.blue  >= 0.0f);
    EXPECT_TRUE(r.green >= 0.0f);
}

// 物理直觉: 屏幕越远，眼睛越难对焦，模糊越大
// 推导: circ = pupil*(screen-G)/G，screen 增大时此式单调递增
static void test_far_distance_larger_blur() {
    CASE("物理直觉: 视距 200cm > 40cm 的模糊 (屏幕越远越糊)");
    Config cfg;
    cfg.diagInch = 14.0f;
    cfg.resX = 2560.0f;
    cfg.resY = 1440.0f;
    cfg.pupilSizeUm = 6500.0f;

    cfg.screenDistanceCM = 40.0f;
    BlurRadii near = ComputeBlurRadii(cfg);

    cfg.screenDistanceCM = 200.0f;
    BlurRadii far = ComputeBlurRadii(cfg);

    EXPECT_TRUE(far.blue  > near.blue);
    EXPECT_TRUE(far.green > near.green);
}

// 物理直觉: 屏幕非常近时 (screen → 0)，模糊趋近 0
// 因为 (screen - G)/G 在 screen → 0 时也 → 0
static void test_very_close_small_blur() {
    CASE("物理直觉: 视距 5cm → 模糊趋近 0");
    Config cfg;
    cfg.diagInch = 14.0f;
    cfg.resX = 2560.0f;
    cfg.resY = 1440.0f;
    cfg.pupilSizeUm = 6500.0f;
    cfg.screenDistanceCM = 5.0f;

    BlurRadii r = ComputeBlurRadii(cfg);
    EXPECT_TRUE(r.blue  < 2.0f);
    EXPECT_TRUE(r.green < 2.0f);
}

// 物理直觉: 蓝通道始终比绿通道模糊更多 (LCA 决定)
static void test_blue_always_more_blur() {
    CASE("物理直觉: blue > green (LCA 决定)");
    const float dists[] = { 20.0f, 40.0f, 80.0f, 200.0f };
    for (float d : dists) {
        Config cfg;
        cfg.diagInch = 14.0f;
        cfg.resX = 2560.0f;
        cfg.resY = 1440.0f;
        cfg.screenDistanceCM = d;
        cfg.pupilSizeUm = 6500.0f;

        BlurRadii r = ComputeBlurRadii(cfg);
        EXPECT_TRUE(r.blue > r.green);
    }
}

int main() {
    // 让 cmd.exe (默认 GBK/CP936) 按 UTF-8 解释 printf 输出的中文字节
    // 与 MyopicDefocusConfig.cpp::wmain 入口处理保持一致
    SetConsoleOutputCP(kCP_UTF8);

    std::printf("============================================\n");
    std::printf("  MyopicDefocus 单测套件\n");
    std::printf("============================================\n");

    std::printf("\n>>>>>>>> Module 1: ComputeBlurRadii <<<<<<<<\n");
    test_js_upstream_defaults();
    test_cpp_defaults();
    test_4k_resolution();
    test_zero_resolution_safe();
    test_far_distance_larger_blur();
    test_very_close_small_blur();
    test_blue_always_more_blur();
    int opticalPassed = g_passed, opticalFailed = g_failed;
    std::printf("  [ComputeBlurRadii 阶段] passed=%d failed=%d\n", opticalPassed, opticalFailed);

    std::printf("\n>>>>>>>> Module 2: config_io <<<<<<<<\n");
    int ioPassed = g_passed, ioFailed = g_failed;
    RunConfigIoTests();
    ioPassed = g_passed - ioPassed;
    ioFailed = g_failed - ioFailed;
    std::printf("  [config_io 阶段] passed=%d failed=%d\n", ioPassed, ioFailed);

    std::printf("\n============================================\n");
    std::printf("  总计: %d 通过, %d 失败\n", g_passed, g_failed);
    std::printf("============================================\n");

    return g_failed > 0 ? 1 : 0;
}
