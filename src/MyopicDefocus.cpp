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
#include "log.h"

Config g_config;

// 帧率上下限 (实际值由 g_config.targetFps 决定)
const int kMinFps = 1;
const int kMaxFps = 240;

bool g_running = true;
int g_currentFps = 120;

// 全局退出热键 (Ctrl+Alt+M)：叠加层因 WS_EX_NOACTIVATE 永远不会获得键盘焦点，
// 窗口内的 Esc 消息基本收不到，因此用系统级热键退出。
constexpr int kExitHotkeyId = 1;

#include "capture.h"
#include "renderer.h"

void UpdateShaderParams() {
    BlurRadii r = ComputeBlurRadii(g_config);
    g_currentBlurB = r.blue;
    g_currentBlurG = r.green;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SIZE) ResizeSwapChain();
    if (msg == WM_DESTROY) {
        UnregisterHotKey(hwnd, kExitHotkeyId);
        PostQuitMessage(0);
        return 0;
    }

    // 全局退出热键 Ctrl+Alt+M
    if (msg == WM_HOTKEY && wParam == kExitHotkeyId) {
        PostQuitMessage(0);
        return 0;
    }

    // 点击穿透：WS_EX_LAYERED | WS_EX_TRANSPARENT 是跨进程可靠生效的标准组合
    // （win32k 命中测试会跳过该分层窗口）。不要在 WM_NCHITTEST 里返回 HTTRANSPARENT，
    // 该返回值只在同一线程的窗口间传递消息，跨进程会直接吞掉点击。
    // 交换链必须用位块传输模型 (DXGI_SWAP_EFFECT_DISCARD)，翻转模型在分层窗口上会黑屏。

    // 兜底 Esc 退出（仅当叠加层意外获得焦点时生效；正常情况下请用 Ctrl+Alt+M）
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        PostQuitMessage(0);
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

    // 关键说明：
    // 1) 点击穿透：WS_EX_LAYERED | WS_EX_TRANSPARENT 是跨进程可靠生效的标准组合，
    //    WS_EX_TRANSPARENT 单独在非分层窗口上不产生穿透（实测确认）。
    //    不要用 WM_NCHITTEST 返回 HTTRANSPARENT，它只对同线程窗口有效。
    // 2) 交换链必须用位块传输模型 (DXGI_SWAP_EFFECT_DISCARD)，
    //    翻转模型 (FLIP_DISCARD) 在分层窗口上 Present 会黑屏。
    // 3) WDA_EXCLUDEFROMCAPTURE 与分层属性必须作用在同一个顶层窗口上，
    //    且设置顺序：扩展样式(创建时) → SetLayeredWindowAttributes → SetWindowDisplayAffinity(最后)。
    // 4) WS_EX_NOACTIVATE：叠加层永不成为前台窗口，不抢键盘焦点（否则点击会中断文本输入）。
    // 5) 先以隐藏方式创建窗口，完成首帧抓取+渲染后再显示，
    //    避免 DDA 首帧捕获到"尚未渲染的黑色叠加层"而锁定黑屏。
    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"MyopicOverlay", L"Overlay",
        WS_POPUP,               // 不设 WS_VISIBLE，稍后手动显示
        0, 0, w, h,
        nullptr, nullptr, hInstance, nullptr);

    // 分层窗口显示属性：整体不透明（我们的画面本来就是全不透明的模糊桌面）
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    // 注册全局退出热键 Ctrl+Alt+M（叠加层永无焦点，窗口键盘消息收不到）
    RegisterHotKey(hwnd, kExitHotkeyId, MOD_CONTROL | MOD_ALT, 'M');

    if (!InitD3D(hwnd)) {
        MessageBox(NULL, L"D3D Init Failed!", L"Error", MB_ICONERROR);
        return -1;
    }

    ResizeSwapChain();

    // 将叠加层自身从屏幕捕获(DDA)中排除，防止自捕获反馈。
    // Win10 2004+ 支持；要求 WDA 与分层属性作用于同一个顶层窗口（本窗口符合），
    // 并且 SetWindowDisplayAffinity 必须在 SetLayeredWindowAttributes 之后调用（当前顺序正确）。
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        LogMsg("[MyopicDefocus] WARNING: SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed\n");
    }

    UpdateShaderParams();

    // 窗口仍隐藏时抓取并渲染第一帧，保证用户看到的第一帧就是正确的模糊画面
    Render();

    // SW_SHOWNOACTIVATE：显示但不激活，不抢前台焦点
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

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