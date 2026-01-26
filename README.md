***
# 近视模拟器（C++ / DirectX 11 高性能）
**近视散焦模拟器 - C++ 高性能重构版**
本项目是将原本基于 Python(https://github.com/zcf0508/myopic_defocus) 的近视散焦模拟工具重构为 C++ 版本。通过使用 **DirectX 11** 和 **DXGI Desktop Duplication API**，实现了极低的延迟和极高的性能，解决了 Python 版本在高帧率屏幕下的卡顿和高 CPU 占用问题。
##✨ 核心特性 (Features)

*   **🚀 极致性能**: 采用全 GPU 管线（Zero-Copy）。截屏、模糊计算、渲染全流程在显存内完成，CPU 占用率低。
*   **🖥️ 桌面复制 API**：使用 Windows 的 `Desktop Duplication API`，支持 HDR 和高色深（10bit）格式，兼容 AMD/NVIDIA 独显。
*   **👁️ 物理光学模拟**: 模拟**纵向色差 (Longitudinal Chromatic Aberration, LCA)**。不同于普通的高斯模糊，本程序对蓝光和绿光通道分离计算模糊半径，以模拟近视防控中的离焦效果。
*   **🖱️ 无干扰叠加**: 窗口全透明、鼠标点击穿透、对截图软件隐藏（防止无限镜像）。
*   **⚡ 节能优化**：内置帧率限制器（默认 30 FPS），在保证视觉效果的同时最大化降低显卡功耗。
*   **📏 自动适配**：支持 DPI 感知，自动适配不同分辨率（2K/4K）和缩放比例。

##🛠️ 编译指南 (Build Instructions)

### 环境要求
*   Windows 10 或 Windows 11
*   Visual Studio 2022
*   Windows 11 SDK（安装 VS 时勾选 C++ 桌面开发即可）

### 步骤
1.  **创建项目**: 在 Visual Studio 中创建一个新的 **Windows 桌面应用程序 (Windows Desktop Application)** 项目 (C++)。
2.  **设置标准**：在项目属性中，将 C++ 语言标准设置为 **ISO C++20**（或 C++17）。
3.  **添加依赖**：在 `项目属性 -> 链接器 -> 输入 -> 附加依赖项` 中，确保包含以下库：
    ```文本
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
    dwmapi.lib
    windowsapp.lib
    ```
4.  **DPI 感知**：在`项目属性 -> 清单工具 -> 输入和输出 -> DPI 感知`中选择**"Per Monitor High DPI Aware"**（或者代码中已通过`SetProcessDpiAwarenessContext`设置，此处可跳过）。
5.  **编译**: 选择 **Release** 和 **x64** 模式进行编译。

## ⚙️ 参数配置 (配置)

由于移除了复杂的 UI 界面以换取性能，参数配置目前位于源码 `main.cpp` 的顶部的 `Config` 结构体中。请根据您的实际设备修改以下参数：

```C++
// 在 main.cpp 中找到此区域
结构体配置 {
    // 屏幕对角线尺寸 (英寸) - 影响 PPI 计算
    // 常见值: 13.3, 14.0, 15.6, 24.0, 27.0, 32.0
    浮点数 diagInch = 27.0f;

    // 观看距离 (厘米)
    浮点屏幕距离厘米 = 60.0f;

    // 瞳孔大小 (微米) - 影响模糊光斑大小
    // 范围通常在 3000 ~ 7000 之间
    浮点数瞳孔大小微米 = 6500.0f;

    // 初始效果强度 (0.0 - 1.0)
    // 建议从 0.5 开始尝试
    浮点数 effectStrength = 0.8f;

    // 分辨率会自动获取，无需修改
    浮点数 resX = 2560.0f;
    浮点数 resY = 1440.0f;
} g_config;
```

**修改帧率限制：**
```C++
// 默认 30 FPS，如果需要更流畅可改为 60
常量 int 目标帧率 = 30;
```

修改完参数后，重新编译即可生效。

## 🎮 操作说明 

程序启动后会覆盖全屏，初始状态下根据配置显示模糊效果。

*   **⬆️ 方向键上 (Up Arrow)**: 增加滤镜强度 (+5%)。
*   **⬇️ 方向键下 (Down Arrow)**: 减小滤镜强度 (-5%)。
*   **Esc 键**: 退出程序。

> **注意**：如果您将强度降为 0，程序将显示原始清晰图像，此时仅相当于一个透明层。

## 🔧 常见问题 (常见问题解决)

1.  **屏幕全黑或全蓝？**
    *   这是因为没有抓取到屏幕内容。请尝试将全屏独占的游戏切换为“无边框窗口 (Borderless Window)”模式。
    *   检查是否有多显示器，目前代码默认抓取主显示器 (Index 0)。

2.  **只有左上角有画面？**
    *   DPI 缩放问题。请确保代码中保留了 `SetProcessDpiAwarenessContext` 调用，或在编译设置中开启 DPI 感知。

3.  **报错 "CopyResource ... Formats not the same"？**
    *   已在代码中修复。这是由于开启了 HDR 或使用了 10bit 色深。程序会自动检测格式并重建纹理。

4.  **画面没有模糊效果？**
    *   检查 `Config` 中的 `screenDistanceCM` (视距) 和 `diagInch` (屏幕尺寸)。如果视距非常远，或者屏幕 PPI 计算错误，模糊半径可能接近 0。

## 📝 技术细节 (Tech Stack)

*   **Language**: C++
*   **Graphics API**: DirectX 11 (D3D11)
*   **Shader Language**: HLSL (Pixel Shader 5.0)
*   **Capture API**: DXGI Output Duplication
*   **Window Manager**: Win32 API + DWM (Direct Window Manager)

## 📝 鸣谢
源项目：https://refractify.io/
Python项目：https://github.com/zcf0508/myopic_defocus

## ⚠️ 免责声明 (Disclaimer)
* 本软件仅供学习和实验用途。虽然其基于近视离焦原理编写，但**不构成任何医疗建议**。使用过程中如感到眼部不适（如头晕、恶心），请立即停止使用。
* 提示：本项目大量使用人工智能代码构建技术。
* 本项目离焦参数来源于python项目 python原项目则来源于refractify.io 如有参数错误请提issues
*Created by lerrenp*
