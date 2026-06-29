#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl.h>
#include "config.h"

using namespace Microsoft::WRL;

// ==========================================
// 屏幕捕获层：DXGI Desktop Duplication API
// ==========================================

// ---- 本模块私有资源（定义在 capture.cpp） ----
extern ComPtr<IDXGIOutputDuplication>  g_duplication;
extern ComPtr<ID3D11Texture2D>         g_capturedTexture;
extern ComPtr<ID3D11ShaderResourceView> g_capturedSRV;

// ---- 抓屏触发后会更新 config 分辨率 + 调用 UpdateShaderParams ----
void UpdateShaderParams();  // 实际定义在 MyopicDefocus.cpp（协调器）

// ---- 对外接口 ----

// 初始化 DXGI 桌面复制。失败返回 false（已写日志）。
bool InitDuplication();

// 抓取一帧到 g_capturedSRV。会自适应分辨率 / HDR 格式变化。
void CaptureFrame();
