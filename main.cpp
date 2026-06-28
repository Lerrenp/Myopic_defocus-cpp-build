#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <dwmapi.h> 
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")

// ==========================================
// 【配置区域】 (对应 JS options-storage.js)
// ==========================================
struct Config {
    // 对应 JS: diagInch
    float diagInch = 28.0f;

    // 对应 JS: screenDistanceCM (JS里是MM，这里用CM方便设置，计算时会转)
    float screenDistanceCM = 40.0f;

    // 对应 JS: pupilSizeUm (JS 默认 6500)
    float pupilSizeUm = 6500.0f;

    // 对应 JS: effectStrengthPercent
    float effectStrength = 0.1f; // 0.0 - 1.0

    // 运行时自动获取
    float resX = 2560.0f;
    float resY = 1440.0f;
} g_config;

// 帧率限制 (30FPS)
const int TARGET_FPS = 120;
const std::chrono::milliseconds FRAME_DURATION(1000 / TARGET_FPS);

// ==========================================
// Shader (对应 JS SVG Filter 逻辑)
// ==========================================
const char* shaderCode = R"(
cbuffer Params : register(b0) {
    float width;
    float height;
    float blurB;
    float blurG;
    float strength;
    int direction; 
    float2 padding;
};

Texture2D tex : register(t0);         // 当前输入
Texture2D texOriginal : register(t1); // 原始图像（用于最后混合）
SamplerState sam : register(s0);

struct VS_Out {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VS_Out VS_Main(uint id : SV_VertexID) {
    VS_Out output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return output;
}

float Gaussian(float x, float sigma) {
    if (sigma <= 0.05) return x == 0.0 ? 1.0 : 0.0;
    return exp(-(x*x) / (2.0 * sigma * sigma));
}

float4 PS_Main(VS_Out input) : SV_Target {
    float4 currentSample = tex.SampleLevel(sam, input.uv, 0);
    
    float3 sum = 0;
    float2 totalW = 0;
    float2 step = (direction == 0) ? float2(1.0/width, 0) : float2(0, 1.0/height);
    
    // 卷积核范围
    int range = 8; 
    for (int i = -range; i <= range; i++) {
        float3 col = tex.SampleLevel(sam, input.uv + step * (float)i, 0).rgb;
        float wB = Gaussian((float)i, blurB);
        float wG = Gaussian((float)i, blurG);
        sum.b += col.b * wB;
        totalW.x += wB;
        sum.g += col.g * wG;
        totalW.y += wG;
    }

    float3 blurred;
    blurred.r = currentSample.r;
    blurred.g = (totalW.y > 0) ? sum.g / totalW.y : currentSample.g;
    blurred.b = (totalW.x > 0) ? sum.b / totalW.x : currentSample.b;

    if (direction == 1) {
        // 第二次 Pass：使用 t1 寄存器中的原始图进行强度混合
        float3 realOriginal = texOriginal.SampleLevel(sam, input.uv, 0).rgb;
        return float4(lerp(realOriginal, blurred, strength), 1.0);
    }
    
    return float4(blurred, 1.0);
}
)";

// ==========================================
// 全局资源
// ==========================================
float g_currentBlurB = 0.0f;
float g_currentBlurG = 0.0f;

struct ShaderParams {
    float width, height, blurB, blurG, strength;
    int direction; // 0 为水平，1 为垂直
    float pad[2];  // 填充对齐
};

bool g_running = true;

ID3D11Device* g_d3dDevice = nullptr;
ID3D11DeviceContext* g_d3dContext = nullptr;
IDXGISwapChain1* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11Buffer* g_cbParams = nullptr;
ID3D11SamplerState* g_sampler = nullptr;

IDXGIOutputDuplication* g_duplication = nullptr;
ID3D11Texture2D* g_capturedTexture = nullptr;
ID3D11ShaderResourceView* g_capturedSRV = nullptr;

ID3D11Texture2D* g_tempTexture = nullptr;
ID3D11RenderTargetView* g_tempRTV = nullptr;
ID3D11ShaderResourceView* g_tempSRV = nullptr;
// ==========================================
// 核心算法：1:1 还原 JS get_blur_circles_px
// ==========================================
void UpdateShaderParams() {
    // 1. 计算物理像素比例 (mm per pixel)
    // JS: const diag_px = Math.sqrt(resx*resx + resy*resy);
    float diag_px = std::sqrt(std::pow(g_config.resX, 2) + std::pow(g_config.resY, 2));
    if (diag_px < 1.0f) diag_px = 1.0f;

    // JS: const diag_mm = options.diagInch * 25.4;
    float diag_mm = g_config.diagInch * 25.4f;

    // JS: const mm_per_px = diag_mm/diag_px;
    float mm_per_px = diag_mm / diag_px;

    // JS: let pix = realWidthMm / resx; (这其实就是 mm_per_px)
    float pix = mm_per_px;

    // 2. 准备物理参数
    // JS: const pupil = p_pupilSizeUm/1000.0;
    float pupil = g_config.pupilSizeUm / 1000.0f;

    // JS: const screen = p_screenDistanceMm;
    float screen = g_config.screenDistanceCM * 10.0f;

    // 3. 屈光度常数 (Diopters) - 完全对应 JS 常量
    // JS: const lca_nat_r = -0.23; ... const sh = -lca_nat_r (0.23)
    float sh = 0.23f;

    // JS: const lca_rif_b = 1.10 + sh;
    float lca_b = 1.10f + sh; // 1.33

    // JS: const lca_rif_g = 0.24 + sh;
    float lca_g = 0.24f + sh; // 0.47

    // 4. 计算蓝色通道模糊 (Blue)
    // JS: const G = 1000 / (1000 / screen + lca);
    float G_b = 1000.0f / (1000.0f / screen + lca_b);

    // JS: const circ = pupil * ((screen - G) / G);
    // 注意：JS里没有取绝对值，但在光学计算弥散圆时直径应为正数。
    // 如果 (screen - G) 为负，代表虚像或焦点在屏幕后，物理上光斑依然存在。
    float circ_b = std::abs(pupil * ((screen - G_b) / G_b));

    // JS: blur_b = circ / pix;
    float blur_b_raw = circ_b / pix;

    // 5. 计算绿色通道模糊 (Green)
    float G_g = 1000.0f / (1000.0f / screen + lca_g);
    float circ_g = std::abs(pupil * ((screen - G_g) / G_g));
    float blur_g_raw = circ_g / pix;

    // 6. 应用系数 (JS init() 中的逻辑)
    // JS: const blur_b = blur_b_got * 0.32;
    // 将结果保存到全局变量，解决 Render 函数中“爆红未定义”的问题
    g_currentBlurB = blur_b_raw * 0.32f;
    g_currentBlurG = blur_g_raw * 0.32f;

    // 提示：UpdateSubresource 现在在 Render() 中按 Pass 执行
}

// ==========================================
// DXGI 抓屏逻辑
// ==========================================
HRESULT InitDuplication() {
    if (g_duplication) { g_duplication->Release(); g_duplication = nullptr; }
    if (g_capturedTexture) { g_capturedTexture->Release(); g_capturedTexture = nullptr; }
    if (g_capturedSRV) { g_capturedSRV->Release(); g_capturedSRV = nullptr; }

    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice))) return E_FAIL;
    IDXGIAdapter* dxgiAdapter = nullptr;
    dxgiDevice->GetAdapter(&dxgiAdapter);
    dxgiDevice->Release();

    IDXGIOutput* dxgiOutput = nullptr;
    dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    dxgiAdapter->Release();
    if (!dxgiOutput) return E_FAIL;

    IDXGIOutput1* dxgiOutput1 = nullptr;
    dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&dxgiOutput1);
    dxgiOutput->Release();
    if (!dxgiOutput1) return E_FAIL;

    HRESULT hr = dxgiOutput1->DuplicateOutput(g_d3dDevice, &g_duplication);
    dxgiOutput1->Release();
    return hr;
}

void CaptureFrame() {
    if (!g_duplication && FAILED(InitDuplication())) return;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    IDXGIResource* desktopResource = nullptr;

    HRESULT hr = g_duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);

    if (SUCCEEDED(hr)) {
        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex))) {
            D3D11_TEXTURE2D_DESC srcDesc;
            tex->GetDesc(&srcDesc);

            D3D11_TEXTURE2D_DESC currentDesc = { 0 };
            if (g_capturedTexture) g_capturedTexture->GetDesc(&currentDesc);

            bool needRecreate = !g_capturedTexture ||
                currentDesc.Width != srcDesc.Width ||
                currentDesc.Height != srcDesc.Height ||
                currentDesc.Format != srcDesc.Format;

            if (needRecreate) {
                if (g_capturedTexture) g_capturedTexture->Release();
                if (g_capturedSRV) g_capturedSRV->Release();

                D3D11_TEXTURE2D_DESC newDesc = srcDesc;
                newDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                newDesc.MiscFlags = 0;
                newDesc.CPUAccessFlags = 0;
                newDesc.Usage = D3D11_USAGE_DEFAULT;
                newDesc.MipLevels = 1;
                newDesc.ArraySize = 1;
                newDesc.SampleDesc.Count = 1;
                newDesc.SampleDesc.Quality = 0;

                if (SUCCEEDED(g_d3dDevice->CreateTexture2D(&newDesc, nullptr, &g_capturedTexture))) {
                    g_config.resX = (float)newDesc.Width;
                    g_config.resY = (float)newDesc.Height;
                    g_d3dDevice->CreateShaderResourceView(g_capturedTexture, nullptr, &g_capturedSRV);
                    UpdateShaderParams();
                }
                else {
                    tex->Release();
                    desktopResource->Release();
                    g_duplication->ReleaseFrame();
                    return;
                }
            }
            if (g_capturedTexture) g_d3dContext->CopyResource(g_capturedTexture, tex);
            tex->Release();
        }
        desktopResource->Release();
        g_duplication->ReleaseFrame();
    }
    else {
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            if (g_duplication) { g_duplication->Release(); g_duplication = nullptr; }
        }
    }
}

void CreateTempResources(UINT w, UINT h) {
    // 先释放旧的
    if (g_tempTexture) { g_tempTexture->Release(); g_tempTexture = nullptr; }
    if (g_tempRTV) { g_tempRTV->Release(); g_tempRTV = nullptr; }
    if (g_tempSRV) { g_tempSRV->Release(); g_tempSRV = nullptr; }

    D3D11_TEXTURE2D_DESC desc = { 0 };
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // 与交换链格式一致
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    g_d3dDevice->CreateTexture2D(&desc, nullptr, &g_tempTexture);
    g_d3dDevice->CreateRenderTargetView(g_tempTexture, nullptr, &g_tempRTV);
    g_d3dDevice->CreateShaderResourceView(g_tempTexture, nullptr, &g_tempSRV);
}

// ==========================================
// DirectX 初始化
// ==========================================
bool InitD3D(HWND hwnd) {
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevels, 1, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, &g_d3dContext))) {
        return false;
    }

    IDXGIDevice* dxgiDevice = nullptr; g_d3dDevice->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
    IDXGIAdapter* dxgiAdapter = nullptr; dxgiDevice->GetAdapter(&dxgiAdapter);
    IDXGIFactory2* dxgiFactory = nullptr; dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice, hwnd, &scd, nullptr, nullptr, &g_swapChain);

    dxgiFactory->Release(); dxgiAdapter->Release(); dxgiDevice->Release();

    if (FAILED(hr)) return false;

    ID3DBlob* vs, * ps;
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "VS_Main", "vs_5_0", 0, 0, &vs, nullptr);
    D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "PS_Main", "ps_5_0", 0, 0, &ps, nullptr);
    g_d3dDevice->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs);
    g_d3dDevice->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps);
    vs->Release(); ps->Release();

    D3D11_BUFFER_DESC bd = { 0 }; bd.ByteWidth = 64; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.Usage = D3D11_USAGE_DEFAULT;
    g_d3dDevice->CreateBuffer(&bd, nullptr, &g_cbParams);

    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    g_d3dDevice->CreateSamplerState(&sd, &g_sampler);

    InitDuplication();
    return true;
}

void ResizeSwapChain() {
    if (!g_swapChain) return;
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    g_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* backBuffer = nullptr;
    g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (backBuffer) {
        g_d3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
        D3D11_TEXTURE2D_DESC desc; backBuffer->GetDesc(&desc);
        backBuffer->Release();

        g_config.resX = (float)desc.Width;
        g_config.resY = (float)desc.Height;

        D3D11_VIEWPORT vp = { 0, 0, (float)desc.Width, (float)desc.Height, 0.0f, 1.0f };
        g_d3dContext->RSSetViewports(1, &vp);
        UpdateShaderParams();
        CreateTempResources((UINT)desc.Width, (UINT)desc.Height);
    }
}

void Render() {
    CaptureFrame();
    // 基础检查：确保所有资源已就绪
    if (!g_rtv || !g_d3dContext || !g_capturedSRV || !g_tempRTV) return;

    // 绑定通用资源
    g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_d3dContext->VSSetShader(g_vs, nullptr, 0);
    g_d3dContext->PSSetShader(g_ps, nullptr, 0);
    g_d3dContext->PSSetConstantBuffers(0, 1, &g_cbParams);
    g_d3dContext->PSSetSamplers(0, 1, &g_sampler);

    // --- PASS 1: 水平模糊 (从 原始截屏 渲染到 中间缓冲区) ---
    ShaderParams p1 = { g_config.resX, g_config.resY, g_currentBlurB, g_currentBlurG, g_config.effectStrength, 0 };
    g_d3dContext->UpdateSubresource(g_cbParams, 0, nullptr, &p1, 0, 0);

    float clearColor[4] = { 0, 0, 0, 0 };
    g_d3dContext->ClearRenderTargetView(g_tempRTV, clearColor);
    g_d3dContext->OMSetRenderTargets(1, &g_tempRTV, nullptr);

    g_d3dContext->PSSetShaderResources(0, 1, &g_capturedSRV); // t0 = 原始截屏
    g_d3dContext->Draw(3, 0);

    // 解绑 SRV (防止 D3D 警告：资源不能同时作为输入和输出)
    ID3D11ShaderResourceView* nullViews[2] = { nullptr, nullptr };
    g_d3dContext->PSSetShaderResources(0, 2, nullViews);

    // --- PASS 2: 垂直模糊并混合 (从 中间缓冲区 渲染到 屏幕) ---
    ShaderParams p2 = p1;
    p2.direction = 1; // 切换到垂直方向
    g_d3dContext->UpdateSubresource(g_cbParams, 0, nullptr, &p2, 0, 0);

    g_d3dContext->OMSetRenderTargets(1, &g_rtv, nullptr);

    // 关键：t0 = 水平模糊后的图，t1 = 原始截屏图（用于混合）
    ID3D11ShaderResourceView* views[] = { g_tempSRV, g_capturedSRV };
    g_d3dContext->PSSetShaderResources(0, 2, views);

    g_d3dContext->Draw(3, 0);

    // 清理并呈现
    g_d3dContext->PSSetShaderResources(0, 2, nullViews);
    g_swapChain->Present(0, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SIZE) ResizeSwapChain();
    if (msg == WM_DESTROY) PostQuitMessage(0);

    // 快捷键控制
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_UP) g_config.effectStrength = std::fmin(1.0f, g_config.effectStrength + 0.05f);
        if (wParam == VK_DOWN) g_config.effectStrength = std::fmax(0.0f, g_config.effectStrength - 0.05f);
        if (wParam == VK_ESCAPE) PostQuitMessage(0);
        UpdateShaderParams();
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, nullptr, nullptr, nullptr, nullptr, L"MyopicOverlay", nullptr };
    RegisterClassEx(&wc);

    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);

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

    auto next_frame = std::chrono::steady_clock::now();

    MSG msg;
    while (g_running) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            auto now = std::chrono::steady_clock::now();
            if (now < next_frame) {
                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next_frame - now).count();
                if (remaining > 1) Sleep((DWORD)remaining);
            }
            else {
                Render();
                next_frame = now + FRAME_DURATION;
            }
        }
    }
    return 0;
}