#pragma once

#include "config.h"

// ==========================================
// 光学算法层：纯函数（无 D3D 依赖，可单测）
// 1:1 还原 JS get_blur_circles_px 的物理模型
// ==========================================

// 各通道的模糊半径（像素）
struct BlurRadii {
    float blue;   // 蓝通道（波长短，离焦大）
    float green;  // 绿通道
};

// 根据屏幕几何 + 眼睛参数 + 离焦模型，计算蓝/绿通道的高斯模糊半径。
// 单位：像素（已应用经验系数 0.32）
BlurRadii ComputeBlurRadii(const Config& cfg);
