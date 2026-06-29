#include "framework.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

#include "config.h"
#include "config_io.h"
#include "blur_shader.h"
#include "optical_model.h"

Config g_config;

// 帧率上下限 (实际值由 g_config.targetFps 决定)
const int kMinFps = 1;
const int kMaxFps = 240;

bool g_running = true;
int g_currentFps = 120;

#include "capture.h"
#include "renderer.h"

void UpdateShaderParams() {
    BlurRadii r = ComputeBlurRadii(g_config);
    g_currentBlurB = r.blue;
    g_currentBlurG = r.green;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SIZE) ResizeSwapChain();
    if (msg == WM_DESTROY) PostQuitMessage(0);

    // 仅保留 Esc 退出.
    // (强度调节 Up/Down 已移除 - 改为运行前通过 JSON 配置)
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostQuitMessage(0);
        UpdateShaderParams();  // 不必要的调用, 但保持渲染状态有效
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ==========================================
// 主入口 (Windows subsystem, 唯一模式 = overlay)
//
// 本 EXE 不再解析命令行, 不再做 Configure/Help 模式.
// 那些功能请使用 MyopicDefocusConfig.exe
// ==========================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // 1) 加载 JSON 配置 (无文件则使用默认)
    bool loaded = false;
    config_io::LoadFromExeDir(kDefaultConfigName, g_config, loaded);

    // 2) 帧率限制
    g_currentFps = (std::clamp)(g_config.targetFps, kMinFps, kMaxFps);
    const auto frameDuration = std::chrono::milliseconds(1000 / g_currentFps);

    // 3) 窗口初始化
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, nullptr, nullptr, nullptr, nullptr, L"MyopicOverlay", nullptr };
    RegisterClassEx(&wc);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

    if (g_config.resX > 0.0f && g_config.resY > 0.0f) {
        w = static_cast<int>(g_config.resX);
        h = static_cast<int>(g_config.resY);
    } else {
        g_config.resX = static_cast<float>(w);
        g_config.resY = static_cast<float>(h);
    }

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"MyopicOverlay", L"Overlay",
        WS_POPUP | WS_VISIBLE,
        0, 0, w, h,
        nullptr, nullptr, hInstance, nullptr);

    MARGINS margins = { -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    if (!InitD3D(hwnd)) {
        MessageBox(NULL, L"D3D Init Failed!", L"Error", MB_ICONERROR);
        return -1;
    }

    ResizeSwapChain();
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    UpdateShaderParams();

    auto next_frame = std::chrono::steady_clock::now();

    MSG msg;
    while (g_running) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            auto now = std::chrono::steady_clock::now();
            if (now < next_frame) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next_frame - now).count();
                if (remaining > 1) Sleep(static_cast<DWORD>(remaining));
            } else {
                Render();
                next_frame = now + frameDuration;
            }
        }
    }
    return 0;
}