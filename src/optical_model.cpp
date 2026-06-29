#include "optical_model.h"
#include <cmath>

// ==========================================
// 核心算法：1:1 还原 JS get_blur_circles_px
// ==========================================
BlurRadii ComputeBlurRadii(const Config& cfg) {
    // 1. 计算物理像素比例 (mm per pixel)
    // JS: const diag_px = Math.sqrt(resx*resx + resy*resy);
    float diag_px = std::sqrt(std::pow(cfg.resX, 2) + std::pow(cfg.resY, 2));
    if (diag_px < 1.0f) diag_px = 1.0f;

    // JS: const diag_mm = options.diagInch * 25.4;
    float diag_mm = cfg.diagInch * 25.4f;

    // JS: const mm_per_px = diag_mm/diag_px;
    float mm_per_px = diag_mm / diag_px;

    // JS: let pix = realWidthMm / resx; (这其实就是 mm_per_px)
    float pix = mm_per_px;

    // 2. 准备物理参数
    // JS: const pupil = p_pupilSizeUm/1000.0;
    float pupil = cfg.pupilSizeUm / 1000.0f;

    // JS: const screen = p_screenDistanceMm;
    float screen = cfg.screenDistanceCM * 10.0f;

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
    // 1:1 还原 JS：不做 abs，允许负值（与上游 SVG 滤镜保持一致）
    float circ_b = pupil * ((screen - G_b) / G_b);

    // JS: blur_b = circ / pix;
    float blur_b_raw = circ_b / pix;

    // 5. 计算绿色通道模糊 (Green)
    float G_g = 1000.0f / (1000.0f / screen + lca_g);
    float circ_g = pupil * ((screen - G_g) / G_g);
    float blur_g_raw = circ_g / pix;

    // 6. 应用系数 (JS init() 中的逻辑)
    // JS: const blur_b = blur_b_got * 0.32;
    BlurRadii out;
    out.blue  = blur_b_raw * 0.32f;
    out.green = blur_g_raw * 0.32f;
    return out;
}
