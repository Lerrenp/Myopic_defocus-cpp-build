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
// 【配置区域】 (写死参数在这里修改)
// ==========================================
struct Config {
    // 1. 屏幕对角线尺寸 (英寸) - 影响 PPI 计算
    // 请根据您的显示器实际参数修改，例如 14.0, 15.6, 24.0, 27.0
    float diagInch = 28.0f; // <--- 【修改这里】屏幕英寸

    // 2. 观看距离 (厘米)
    float screenDistanceCM = 40.0f; // <--- 【修改这里】视距

    // 3. 瞳孔大小 (微米) - 影响模糊光斑大小
    // 通常范围 3000 ~ 7000
    float pupilSizeUm = 6500.0f; // <--- 【修改这里】瞳孔大小

    // 4. 初始效果强度 (0.0 - 1.0)
    float effectStrength = 0.2f; // <--- 【修改这里】初始强度

    // 分辨率 (程序会自动获取，无需手动修改)
    float resX = 3840.0f;
    float resY = 2160.0f;
} g_config;

// 目标帧率控制 (30 FPS 省电且足够)
const int TARGET_FPS = 60;
const std::chrono::milliseconds FRAME_DURATION(1000 / TARGET_FPS);

// ==========================================
// Shader (蓝绿通道分离高斯模糊)
// ==========================================
const char* shaderCode = R"(
cbuffer Params : register(b0) {
    float width;
    float height;
    float blurB;    // 蓝通道模糊半径 (像素)
    float blurG;    // 绿通道模糊半径 (像素)
    float strength; // 混合强度
    float3 padding;
};

Texture2D tex : register(t0);
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

// 高斯权重计算
float Gaussian(float x, float sigma) {
    if (sigma <= 0.0) return x == 0.0 ? 1.0 : 0.0;
    return exp(-(x*x) / (2 * sigma * sigma)) / (2.506628 * sigma);
}

float4 PS_Main(VS_Out input) : SV_Target {
    // 安全检查，防止分辨率未初始化导致除以零
    if (width < 1.0 || height < 1.0) return float4(0,0,0,0);

    float4 original = tex.Sample(sam, input.uv);

    // 如果没抓到图(Alpha=0)或者强度为0，直接显示原图
    // 注意：必须返回 Alpha=1.0，否则 DWM 会让窗口透明
    if (original.a < 0.1 || strength <= 0.001) {
        return float4(original.rgb, 1.0);
    }

    float3 sum = float3(0,0,0);
    float3 totalWeight = float3(0,0,0);
    
    // 采样范围，为了性能写死为 6 (即 13x13 核)
    // 如果模糊很大，可以适当增加 range
    int range = 6; 
    
    float2 texSize = float2(width, height);

    for (int y = -range; y <= range; y++) {
        for (int x = -range; x <= range; x++) {
            float2 offset = float2(float(x), float(y)) / texSize;
            float3 sampleColor = tex.Sample(sam, input.uv + offset).rgb;
            
            float dist = length(float2(x, y));
            
            // 核心逻辑：蓝/绿通道使用不同的模糊半径
            float wB = Gaussian(dist, blurB);
            float wG = Gaussian(dist, blurG);
            
            // 红通道不模糊 (假设红光聚焦在视网膜上)
            // 这里为了简化 Shader 计算，红色只取中心点权重 1.0，周围 0
            // 实际上是在最后合成时直接用原图的红色
            
            sum.b += sampleColor.b * wB;
            totalWeight.b += wB;
            
            sum.g += sampleColor.g * wG;
            totalWeight.g += wG;
        }
    }
    
    float3 blurred;
    // 红色保持原样 (聚焦)
    blurred.r = original.r;
    // 绿/蓝应用模糊结果
    blurred.g = (totalWeight.g > 0) ? sum.g / totalWeight.g : original.g;
    blurred.b = (totalWeight.b > 0) ? sum.b / totalWeight.b : original.b;
    
    // 根据强度混合原图和处理后的图
    float3 finalRGB = lerp(original.rgb, blurred, strength);

    // 强制不透明，确保 DWM 显示窗口
    return float4(finalRGB, 1.0);
}
)";

// ==========================================
// 全局资源
// ==========================================
struct ShaderParams {
    float width, height, blurB, blurG, strength;
    float pad[3];
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

// ==========================================
// 核心算法：计算物理模糊半径
// ==========================================
void UpdateShaderParams() {
    // 1. 分辨率适配：计算当前分辨率下的 PPI (每英寸像素数)
    float div = (float)std::sqrt(std::pow(g_config.resX, 2) + std::pow(g_config.resY, 2));
    if (div < 0.1f) div = 1.0f; // 防止除零

    // 计算物理尺寸对应的像素比例
    float diag_mm = g_config.diagInch * 25.4f;
    float mm_per_px = diag_mm / div; // 每个像素代表多少毫米
    float pix = mm_per_px;

    // 2. 引入物理参数
    float screen_mm = g_config.screenDistanceCM * 10.0f;
    float pupil = g_config.pupilSizeUm / 1000.0f; // 转换为 mm

    // 3. 计算色差 (LCA) 和 模糊半径
    // 参数说明：1.33 和 0.47 是模拟近视散焦的常用屈光度差值 (Diopters)

    // 蓝光通道 (Blue)
    float lca_b = 1.33f; // 甚至可以更大，取决于模拟的镜片色散
    float G_b = 1000.0f / (1000.0f / screen_mm + lca_b);
    float blur_b_mm = std::abs(pupil * (screen_mm - G_b) / G_b);
    float blur_b_px = (blur_b_mm / pix) * 0.32f; // 0.32 是经验系数

    // 绿光通道 (Green)
    float lca_g = 0.47f;
    float G_g = 1000.0f / (1000.0f / screen_mm + lca_g);
    float blur_g_mm = std::abs(pupil * (screen_mm - G_g) / G_g);
    float blur_g_px = (blur_g_mm / pix) * 0.32f;

    // 更新到 Shader
    if (g_d3dContext && g_cbParams) {
        ShaderParams params = {
            g_config.resX,
            g_config.resY,
            blur_b_px,
            blur_g_px,
            g_config.effectStrength,
            {0}
        };
        g_d3dContext->UpdateSubresource(g_cbParams, 0, nullptr, &params, 0, 0);
    }
}

// ==========================================
// DXGI 抓屏逻辑 (Desktop Duplication)
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

            // 格式/尺寸检查 (兼容 HDR)
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
                    // 分辨率适配：一旦纹理重建，立即更新全局配置中的分辨率
                    g_config.resX = (float)newDesc.Width;
                    g_config.resY = (float)newDesc.Height;

                    g_d3dDevice->CreateShaderResourceView(g_capturedTexture, nullptr, &g_capturedSRV);
                    UpdateShaderParams(); // 重新计算模糊参数
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

// ==========================================
// DirectX 初始化 & 窗口管理
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

        // 自动适配：更新分辨率并重新计算 Shader 参数
        g_config.resX = (float)desc.Width;
        g_config.resY = (float)desc.Height;

        D3D11_VIEWPORT vp = { 0, 0, (float)desc.Width, (float)desc.Height, 0.0f, 1.0f };
        g_d3dContext->RSSetViewports(1, &vp);
        UpdateShaderParams();
    }
}

void Render() {
    CaptureFrame();
    if (!g_rtv || !g_d3dContext) return;

    float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_d3dContext->ClearRenderTargetView(g_rtv, clearColor);

    if (g_capturedSRV) {
        g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        g_d3dContext->VSSetShader(g_vs, nullptr, 0);
        g_d3dContext->PSSetShader(g_ps, nullptr, 0);
        g_d3dContext->PSSetConstantBuffers(0, 1, &g_cbParams);
        g_d3dContext->PSSetShaderResources(0, 1, &g_capturedSRV);
        g_d3dContext->PSSetSamplers(0, 1, &g_sampler);
        g_d3dContext->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_d3dContext->Draw(3, 0);
    }

    g_swapChain->Present(0, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_SIZE) ResizeSwapChain();
    if (msg == WM_DESTROY) PostQuitMessage(0);
    // 快捷键: 上/下调整强度，ESC 退出
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