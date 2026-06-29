#include "renderer.h"
#include "capture.h"
#include "log.h"
#include <cstring>

// ==========================================
// 本模块私有全局资源定义 (ComPtr, 默认 = nullptr)
// ==========================================
ComPtr<ID3D11Device>           g_d3dDevice;
ComPtr<ID3D11DeviceContext>    g_d3dContext;
ComPtr<IDXGISwapChain1>        g_swapChain;
ComPtr<ID3D11RenderTargetView> g_rtv;
ComPtr<ID3D11VertexShader>     g_vs;
ComPtr<ID3D11PixelShader>      g_ps;
ComPtr<ID3D11Buffer>           g_cbParams;
ComPtr<ID3D11SamplerState>     g_sampler;

ComPtr<ID3D11Texture2D>         g_tempTexture;
ComPtr<ID3D11RenderTargetView>  g_tempRTV;
ComPtr<ID3D11ShaderResourceView> g_tempSRV;

float g_currentBlurB = 0.0f;
float g_currentBlurG = 0.0f;

// ==========================================
// DirectX 初始化
// ==========================================
bool InitD3D(HWND hwnd) {
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   createDeviceFlags, featureLevels, 1,
                                   D3D11_SDK_VERSION, &g_d3dDevice, nullptr, &g_d3dContext);
    if (FAILED(hr)) {
        LogHr("D3D11CreateDevice", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(g_d3dDevice.As(&dxgiDevice))) {
        LogHr("g_d3dDevice->IDXGIDevice", hr);
        return false;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    if (FAILED(dxgiDevice->GetAdapter(&dxgiAdapter))) {
        LogHr("dxgiDevice->GetAdapter", hr);
        return false;
    }

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory);
    if (FAILED(hr)) {
        LogHr("dxgiAdapter->GetParent(IDXGIFactory2)", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd = { 0 };
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    hr = dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), hwnd, &scd, nullptr, nullptr, &g_swapChain);
    if (FAILED(hr)) {
        LogHr("CreateSwapChainForHwnd", hr);
        return false;
    }

    // 编译着色器
    ComPtr<ID3DBlob> vsBlob, psBlob;
    hr = D3DCompile(kBlurShaderHLSL, strlen(kBlurShaderHLSL), nullptr, nullptr, nullptr,
                    "VS_Main", "vs_5_0", 0, 0, &vsBlob, nullptr);
    if (FAILED(hr)) {
        LogHr("D3DCompile(VS_Main)", hr);
        return false;
    }
    hr = D3DCompile(kBlurShaderHLSL, strlen(kBlurShaderHLSL), nullptr, nullptr, nullptr,
                    "PS_Main", "ps_5_0", 0, 0, &psBlob, nullptr);
    if (FAILED(hr)) {
        LogHr("D3DCompile(PS_Main)", hr);
        return false;
    }

    g_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_vs);
    g_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_ps);

    // 常量缓冲区
    D3D11_BUFFER_DESC bd = { 0 };
    bd.ByteWidth = sizeof(ShaderParams);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.Usage = D3D11_USAGE_DEFAULT;
    hr = g_d3dDevice->CreateBuffer(&bd, nullptr, &g_cbParams);
    if (FAILED(hr)) {
        LogHr("CreateBuffer(cbParams)", hr);
        return false;
    }

    // Sampler
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = g_d3dDevice->CreateSamplerState(&sd, &g_sampler);
    if (FAILED(hr)) {
        LogHr("CreateSamplerState", hr);
        return false;
    }

    InitDuplication();
    return true;
}

void CreateTempResources(UINT w, UINT h) {
    // ComPtr 自动释放旧资源 (operator= 先 Release 再 Assign)
    g_tempTexture = nullptr;
    g_tempRTV     = nullptr;
    g_tempSRV     = nullptr;

    D3D11_TEXTURE2D_DESC desc = { 0 };
    desc.Width     = w;
    desc.Height    = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr;
    hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, &g_tempTexture);
    if (FAILED(hr)) { LogHr("CreateTempTexture", hr); return; }

    hr = g_d3dDevice->CreateRenderTargetView(g_tempTexture.Get(), nullptr, &g_tempRTV);
    if (FAILED(hr)) { LogHr("CreateTempRTV", hr); return; }

    hr = g_d3dDevice->CreateShaderResourceView(g_tempTexture.Get(), nullptr, &g_tempSRV);
    if (FAILED(hr)) { LogHr("CreateTempSRV", hr); return; }
}

void ResizeSwapChain() {
    if (!g_swapChain) return;
    g_rtv = nullptr;  // ComPtr 自动 Release 旧 RTV

    g_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        LogHr("GetBuffer(0)", hr);
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);

    hr = g_d3dDevice->CreateRenderTargetView(backBuffer.Get(), nullptr, &g_rtv);
    if (FAILED(hr)) {
        LogHr("CreateRTV from backBuffer", hr);
        return;
    }

    g_config.resX = (float)desc.Width;
    g_config.resY = (float)desc.Height;

    D3D11_VIEWPORT vp = { 0, 0, (float)desc.Width, (float)desc.Height, 0.0f, 1.0f };
    g_d3dContext->RSSetViewports(1, &vp);
    UpdateShaderParams();
    CreateTempResources(desc.Width, desc.Height);
}

void Render() {
    CaptureFrame();
    if (!g_rtv || !g_d3dContext || !g_capturedSRV || !g_tempRTV) return;

    g_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g_d3dContext->VSSetShader(g_vs.Get(), nullptr, 0);

    ID3D11PixelShader* ps = g_ps.Get();
    g_d3dContext->PSSetShader(ps, nullptr, 0);

    ID3D11Buffer* cb = g_cbParams.Get();
    g_d3dContext->PSSetConstantBuffers(0, 1, &cb);

    ID3D11SamplerState* smp = g_sampler.Get();
    g_d3dContext->PSSetSamplers(0, 1, &smp);

    // --- PASS 1: 水平模糊 ---
    ShaderParams p1 = { g_config.resX, g_config.resY,
                        g_currentBlurB, g_currentBlurG,
                        g_config.effectStrength, 0 };
    g_d3dContext->UpdateSubresource(g_cbParams.Get(), 0, nullptr, &p1, 0, 0);

    float clearColor[4] = { 0, 0, 0, 0 };
    ID3D11RenderTargetView* rtv = g_tempRTV.Get();
    g_d3dContext->ClearRenderTargetView(rtv, clearColor);
    g_d3dContext->OMSetRenderTargets(1, &rtv, nullptr);

    ID3D11ShaderResourceView* srvCap = g_capturedSRV.Get();
    g_d3dContext->PSSetShaderResources(0, 1, &srvCap);
    g_d3dContext->Draw(3, 0);

    ID3D11ShaderResourceView* nullViews[2] = { nullptr, nullptr };
    g_d3dContext->PSSetShaderResources(0, 2, nullViews);

    // --- PASS 2: 垂直模糊并混合 ---
    ShaderParams p2 = p1;
    p2.direction = 1;
    g_d3dContext->UpdateSubresource(g_cbParams.Get(), 0, nullptr, &p2, 0, 0);

    ID3D11RenderTargetView* rtvOut = g_rtv.Get();
    g_d3dContext->OMSetRenderTargets(1, &rtvOut, nullptr);

    ID3D11ShaderResourceView* views[2] = { g_tempSRV.Get(), g_capturedSRV.Get() };
    g_d3dContext->PSSetShaderResources(0, 2, views);
    g_d3dContext->Draw(3, 0);

    g_d3dContext->PSSetShaderResources(0, 2, nullViews);
    g_swapChain->Present(0, 0);
}
