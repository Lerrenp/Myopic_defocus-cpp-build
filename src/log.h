#pragma once

#include <windows.h>
#include <cstdio>

// ==========================================
// 简易错误日志层
// ==========================================
//
// 同时在 OutputDebugString (VS 输出窗口) 和 stderr 输出。
// stderr 在 Windows subsystem EXE 中默认不可见，
// 但 OutputDebugStringA 在 VS 调试器中实时可见。
// Release 下可通过 DebugView (Sysinternals) 捕获。

inline void LogHr(const char* context, HRESULT hr) {
    char buf[256];
    sprintf_s(buf, "[MyopicDefocus] %s failed: HR=0x%08X\n", context, hr);
    OutputDebugStringA(buf);
    fputs(buf, stderr);
}

inline void LogMsg(const char* msg) {
    OutputDebugStringA(msg);
    fputs(msg, stderr);
}