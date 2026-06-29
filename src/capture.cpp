#include "capture.h"
#include "renderer.h"   // 获取 g_d3dDevice / g_d3dContext (定义在 renderer.cpp)
#include "log.h"

// ==========================================
// 本模块私有全局资源定义 (ComPtr, 默认 = nullptr)
// ==========================================
ComPtr<IDXGIOutputDuplication>  g_duplication;
ComPtr<ID3D11Texture2D>         g_capturedTexture;
ComPtr<ID3D11ShaderResourceView> g_capturedSRV;

// ==========================================
// DXGI 抓屏逻辑 (Desktop Duplication)
// ==========================================
bool InitDuplication() {
    // ComPtr 赋值 = 自动 Release 旧资源
    g_duplication      = nullptr;
    g_capturedTexture  = nullptr;
    g_capturedSRV      = nullptr;

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = g_d3dDevice.As(&dxgiDevice);
    if (FAILED(hr)) { LogHr("InitDup: D3DDevice->IDXGIDevice", hr); return false; }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    hr = dxgiDevice->GetAdapter(&dxgiAdapter);
    if (FAILED(hr)) { LogHr("InitDup: GetAdapter", hr); return false; }

    ComPtr<IDXGIOutput> dxgiOutput;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    if (FAILED(hr) || !dxgiOutput) {
        LogHr("InitDup: EnumOutputs(0)", hr);
        return false;
    }

    ComPtr<IDXGIOutput1> dxgiOutput1;
    hr = dxgiOutput.As(&dxgiOutput1);
    if (FAILED(hr)) { LogHr("InitDup: Output->Output1", hr); return false; }

    hr = dxgiOutput1->DuplicateOutput(g_d3dDevice.Get(), &g_duplication);
    if (FAILED(hr)) {
        LogHr("InitDup: DuplicateOutput", hr);
        return false;
    }

    return true;
}

void CaptureFrame() {
    if (!g_duplication && !InitDuplication()) return;

    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    ComPtr<IDXGIResource> desktopResource;

    HRESULT hr = g_duplication->AcquireNextFrame(0, &frameInfo, &desktopResource);

    if (SUCCEEDED(hr)) {
        ComPtr<ID3D11Texture2D> tex;
        hr = desktopResource.As(&tex);
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC srcDesc;
            tex->GetDesc(&srcDesc);

            D3D11_TEXTURE2D_DESC currentDesc = { 0 };
            if (g_capturedTexture) g_capturedTexture->GetDesc(&currentDesc);

            bool needRecreate = !g_capturedTexture ||
                currentDesc.Width  != srcDesc.Width ||
                currentDesc.Height != srcDesc.Height ||
                currentDesc.Format != srcDesc.Format;

            if (needRecreate) {
                g_capturedTexture = nullptr;
                g_capturedSRV     = nullptr;

                D3D11_TEXTURE2D_DESC newDesc = srcDesc;
                newDesc.BindFlags     = D3D11_BIND_SHADER_RESOURCE;
                newDesc.MiscFlags     = 0;
                newDesc.CPUAccessFlags = 0;
                newDesc.Usage         = D3D11_USAGE_DEFAULT;
                newDesc.MipLevels     = 1;
                newDesc.ArraySize     = 1;
                newDesc.SampleDesc.Count = 1;
                newDesc.SampleDesc.Quality = 0;

                hr = g_d3dDevice->CreateTexture2D(&newDesc, nullptr, &g_capturedTexture);
                if (FAILED(hr)) {
                    LogHr("CaptureFrame: CreateTexture2D", hr);
                    g_duplication->ReleaseFrame();
                    return;
                }

                g_config.resX = (float)newDesc.Width;
                g_config.resY = (float)newDesc.Height;

                hr = g_d3dDevice->CreateShaderResourceView(g_capturedTexture.Get(), nullptr, &g_capturedSRV);
                if (FAILED(hr)) {
                    LogHr("CaptureFrame: CreateSRV", hr);
                    g_duplication->ReleaseFrame();
                    return;
                }

                UpdateShaderParams();
            }

            if (g_capturedTexture) {
                g_d3dContext->CopyResource(g_capturedTexture.Get(), tex.Get());
            }
        }
        g_duplication->ReleaseFrame();
    }
    else if (hr == DXGI_ERROR_ACCESS_LOST) {
        g_duplication = nullptr;  // 下次 CaptureFrame 自动重 InitDuplication
    }
    // 其他失败 (DXGI_ERROR_WAIT_TIMEOUT 等) 静默跳过
}
