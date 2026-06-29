#pragma once

// ==========================================
// 模糊着色器 HLSL 源码
// (对应 JS SVG Filter 逻辑：两 Pass 分离高斯 + LCA 色差模拟)
// ==========================================
inline const char* kBlurShaderHLSL = R"(
cbuffer Params : register(b0) {
    float width;
    float height;
    float blurB;
    float blurG;
    float strength;
    int direction;
    float2 padding;
};

Texture2D tex : register(t0);         // 当前输入
Texture2D texOriginal : register(t1); // 原始图像（用于最后混合）
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

float Gaussian(float x, float sigma) {
    if (sigma <= 0.05) return x == 0.0 ? 1.0 : 0.0;
    return exp(-(x*x) / (2.0 * sigma * sigma));
}

float4 PS_Main(VS_Out input) : SV_Target {
    float4 currentSample = tex.SampleLevel(sam, input.uv, 0);

    float3 sum = 0;
    float2 totalW = 0;
    float2 step = (direction == 0) ? float2(1.0/width, 0) : float2(0, 1.0/height);

    // 动态卷积核范围：取蓝/绿两通道 σ 的最大值，按 JS 的 1.7σ 经验系数
    // JS: bigsize = Math.floor(stdev * 1.7); for (i=-bigsize; i<=bigsize; ++i)
    // 下限 8 防止极小模糊时采样不足
    int range = max(8, (int)floor(max(blurB, blurG) * 1.7));
    for (int i = -range; i <= range; i++) {
        float3 col = tex.SampleLevel(sam, input.uv + step * (float)i, 0).rgb;
        float wB = Gaussian((float)i, blurB);
        float wG = Gaussian((float)i, blurG);
        sum.b += col.b * wB;
        totalW.x += wB;
        sum.g += col.g * wG;
        totalW.y += wG;
    }

    float3 blurred;
    blurred.r = currentSample.r;
    blurred.g = (totalW.y > 0) ? sum.g / totalW.y : currentSample.g;
    blurred.b = (totalW.x > 0) ? sum.b / totalW.x : currentSample.b;

    if (direction == 1) {
        // 第二次 Pass：使用 t1 寄存器中的原始图进行强度混合
        float3 realOriginal = texOriginal.SampleLevel(sam, input.uv, 0).rgb;
        return float4(lerp(realOriginal, blurred, strength), 1.0);
    }

    return float4(blurred, 1.0);
}
)";
