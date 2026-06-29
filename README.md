# 近视散焦模拟器 (C++ / DirectX 11)

基于 **纵向色差 (LCA)** 原理的屏幕离焦滤镜，通过 **DirectX 11** + **DXGI Desktop Duplication API** 实现全 GPU 管线的高性能实现。

> 上游算法参考：[refractify.io](https://refractify.io/) · [myopic_defocus (Python)](https://github.com/zcf0508/myopic_defocus) · [myopic_defocus-main (JS)](https://github.com/zcf0508/myopic_defocus-main)

## ✨ 特性

- 🚀 **全 GPU 管线**：截屏、模糊、渲染全在显存内完成，CPU 占用极低
- 🖥️ **桌面复制 API**：基于 DXGI Output Duplication，支持 HDR/10bit（自动适配，建议关闭 HDR）
- 👁️ **LCA 物理模拟**：蓝/绿通道分别高斯模糊，红通道透传，模拟近视防控离焦
- 🖱️ **无干扰叠加**：窗口全透明、鼠标点击穿透、对截图软件隐藏 (`WDA_EXCLUDEFROMCAPTURE`)
- ⚡ **节能优化**：内置帧率限制（默认 120 FPS，可配 1–240）
- 📏 **DPI 感知**：自动适配 2K/4K 与任意缩放比例

## 📦 编译

### 环境

- Windows 10 / 11
- Visual Studio 2022（C++ 桌面开发工作负载）
- MSBuild 18.x
- Windows 11 SDK

### 一键构建 (MSBuild)

```powershell
cd Myopic_defocus-cpp-build-main
msbuild MyopicDefocus.slnx -p:Configuration=Release -p:Platform=x64
```

产物在 `bin\Release\`：

| EXE | 子系统 | 用途 |
|---|---|---|
| `MyopicDefocus.exe` | Windows | 隐形 overlay 滤镜 |
| `MyopicDefocusConfig.exe` | Console | 配置向导（交互/CLI/JSON） |
| `MyopicDefocusTests.exe` | Console | 单元测试（43 断言） |

### Visual Studio

双击 `MyopicDefocus.slnx`，选择 `Release | x64`，F5 启动。

### 测试

```powershell
.\bin\Release\MyopicDefocusTests.exe
# 预期: 总计: 43 通过, 0 失败
```

## ⚙️ 配置

`MyopicDefocusConfig.exe` 提供三种配置方式：

### 交互向导

```powershell
.\MyopicDefocusConfig.exe
# 逐项询问, Enter 保留当前值, 完成后自动写入 config.json
```

### 命令行覆盖

```powershell
.\MyopicDefocusConfig.exe --diag=27 --distance=60 --effect=0.5 --fps=60
```

### 重置 / 帮助

```powershell
.\MyopicDefocusConfig.exe --reset         # 重置 config.json 为默认值
.\MyopicDefocusConfig.exe --help          # 显示帮助
```

### 参数表

| 参数 | 含义 | 默认 |
|---|---|---|
| `--diag=<n>` | 屏幕对角线 (英寸) | 28 |
| `--distance=<n>` | 观看距离 (cm) | 40 |
| `--pupil=<n>` | 瞳孔直径 (μm) | 6500 |
| `--effect=<n>` | 效果强度 (0~1) | 0.1 |
| `--fps=<n>` | 目标帧率 (1~240) | 120 |
| `--resx=<n> --resy=<n>` | 分辨率 (0=自动) | 0, 0 |
| `--reset` | 重置 JSON 为默认 | — |
| `--config=<file>` | 使用自定义 JSON | — |
| `--help` | 显示帮助 | — |

> **配置优先级**: 命令行 > JSON 文件 > 默认值
> **JSON 缺失**: 字段保留 Config 默认值
> **resX/resY 为 0**: 使用运行时检测的屏幕分辨率

## 🎮 使用

```powershell
# 1. 首次使用: 生成 config.json
.\MyopicDefocusConfig.exe

# 2. 启动滤镜 (全屏 overlay, Esc 退出)
.\MyopicDefocus.exe
```

滤镜启动后覆盖全屏，按 `Esc` 退出。焦点丢失时可用任务管理器结束进程。

## 🛠️ 架构

```
src/
├── MyopicDefocus.cpp / .h         # GUI 入口 (wWinMain)
├── MyopicDefocusConfig.cpp        # Config 入口 (wmain)
├── test_optical_model.cpp         # 光学算法单测 (19 断言)
├── test_config_io.cpp             # JSON I/O 单测 (24 断言)
├── config.h                       # Config 结构 + 全局实例
├── config_io.h / .cpp             # JSON 持久化 (nlohmann/json)
├── cli.h / .cpp                   # 命令行 + 交互向导
├── optical_model.h / .cpp         # 1:1 还原 JS LCA 算法
├── blur_shader.h                  # HLSL 着色器源码
├── capture.h / .cpp               # DXGI 桌面复制 (ComPtr)
├── renderer.h / .cpp              # D3D11 双 Pass 模糊管线
├── log.h                          # LogHr / LogMsg
└── third_party/nlohmann/json.hpp  # vendored 3.12.0
```

**三 EXE 共享 `src/`**，由独立 vcxproj 编译：
- `MyopicDefocus/` — GUI（Windows subsystem）
- `MyopicDefocusConfig/` — Config（Console subsystem）
- `MyopicDefocusTests/` — Tests（Console subsystem）

## 🔧 故障排除

| 现象 | 原因 / 解决 |
|---|---|
| 屏幕全黑 / 全蓝 | 抓屏失败；将全屏独占游戏切到「无边框窗口」 |
| 只有左上角有画面 | DPI 缩放问题；确保 `SetProcessDpiAwarenessContext` 已调用 |
| `CopyResource ... Formats not the same` | HDR / 10bit 色深；代码已自动重建纹理，如仍报错请关闭 HDR |
| 画面没模糊 | 视距过远或屏幕 PPI 计算异常导致模糊半径 ≈ 0 |
| 杀软报「DLL 注入」 | DXGI 抓屏的常见误报；源码 `src/capture.cpp` 无任何网络/外发代码 |

## 📝 技术栈

- **C++20** / MSVC v145
- **DirectX 11**（D3D11 + DXGI 1.2）
- **HLSL** Pixel Shader 5.0（双 Pass 可分离高斯 + LCA）
- **Win32 + DWM**（透明叠层窗口）
- **ComPtr RAII**（零裸 COM 指针）

## ⚠️ 免责声明

本软件仅供学习与实验。**不构成任何医疗建议**。使用过程中如感到眼部不适（头晕、恶心等），请立即停止使用。LCA 离焦参数来源于上游 Python/JS 项目。

## 📄 许可证

MIT License © 2026 Lerrenp
