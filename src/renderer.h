#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl.h>
#include "config.h"
#include "blur_shader.h"

using namespace Microsoft::WRL;

// ==========================================
// 渲染层：D3D11 设备、交换链、双 Pass 模糊管线
// ==========================================

// ---- 渲染模块私有资源（定义见 renderer.cpp） ----
extern ComPtr<ID3D11Device>           g_d3dDevice;
extern ComPtr<ID3D11DeviceContext>    g_d3dContext;
extern ComPtr<IDXGISwapChain1>        g_swapChain;
extern ComPtr<ID3D11RenderTargetView> g_rtv;
extern ComPtr<ID3D11VertexShader>     g_vs;
extern ComPtr<ID3D11PixelShader>      g_ps;
extern ComPtr<ID3D11Buffer>           g_cbParams;
extern ComPtr<ID3D11SamplerState>     g_sampler;

// 中间渲染目标（双 Pass 高斯模糊用）
extern ComPtr<ID3D11Texture2D>         g_tempTexture;
extern ComPtr<ID3D11RenderTargetView>  g_tempRTV;
extern ComPtr<ID3D11ShaderResourceView> g_tempSRV;

// 当前正在使用的模糊半径（由 UpdateShaderParams 写入）
extern float g_currentBlurB;
extern float g_currentBlurG;

// 配置变化时调用（MyopicDefocus.cpp 中定义）
void UpdateShaderParams();

// GPU 常量缓冲区布局（与 HLSL cbuffer 对应）
struct ShaderParams {
    float width, height, blurB, blurG, strength;
    int   direction; // 0 = 水平, 1 = 垂直
    float pad[2];
};

// ---- 对外接口 ----

// 初始化 D3D 设备、上下文、交换链、着色器、常量缓冲、Sampler
bool InitD3D(HWND hwnd);

// 重建交换链 + 创建/调整中间纹理
void ResizeSwapChain();

// 抓一帧 + 双 Pass 模糊 + 呈现
void Render();

// 创建中间渲染目标纹理（与交换链格式一致）
void CreateTempResources(UINT w, UINT h);
