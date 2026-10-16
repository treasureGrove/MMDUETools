#pragma once

// 原创程序化调色配方，不包含客户端 LUT 像素、重采样值或拟合系数。
// 输入为编码 RGB，输出为线性 RGB；三档透过率随线性亮度平滑插值，保留黑色和源色细节。
namespace MMDEndfieldSkinLutData
{
    inline constexpr uint32 recipe_version = 1;
    inline constexpr float layout_version = 41.0f;
    inline constexpr int32 cube_side = 6;
    inline constexpr int32 cube_sample_count = cube_side * cube_side * cube_side;
    inline constexpr float shadow_transmission[3][3] = {
        {0.52f, 0.28f, 0.18f},
        {0.61f, 0.40f, 0.29f},
        {0.69f, 0.51f, 0.41f}
    };

    inline float DecodeSRGB(float encoded)
    {
        return encoded <= 0.04045f ? encoded / 12.92f : FMath::Pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    inline FLinearColor EvaluateEncoded(float encoded_r, float encoded_g, float encoded_b)
    {
        const float linear[3] = {DecodeSRGB(encoded_r), DecodeSRGB(encoded_g), DecodeSRGB(encoded_b)};
        const float luma = linear[0] * 0.2126f + linear[1] * 0.7152f + linear[2] * 0.0722f;
        const int32 interval = luma < 0.35f ? 0 : 1;
        const float start = interval == 0 ? 0.0f : 0.35f;
        const float width = interval == 0 ? 0.35f : 0.65f;
        const float coordinate = FMath::Clamp((luma - start) / width, 0.0f, 1.0f);
        const float blend = coordinate * coordinate * (3.0f - 2.0f * coordinate);
        float output[3];
        for (int32 channel = 0; channel < 3; ++channel)
        {
            const float density_start = -FMath::Loge(shadow_transmission[interval][channel]);
            const float density_end = -FMath::Loge(shadow_transmission[interval + 1][channel]);
            const float transmission = FMath::Exp(-FMath::Lerp(density_start, density_end, blend));
            output[channel] = linear[channel] * transmission;
        }
        return FLinearColor(output[0], output[1], output[2], 1.0f);
    }

    inline FLinearColor EvaluateSlot(int32 slot)
    {
        // r + 6*g + 36*b；剩余 40 槽重复最后一个有效颜色，不能按 216 槽拉伸到整条曲线。
        const int32 index = FMath::Clamp(slot, 0, cube_sample_count - 1);
        const float step = 1.0f / static_cast<float>(cube_side - 1);
        return EvaluateEncoded(static_cast<float>(index % cube_side) * step,
            static_cast<float>((index / cube_side) % cube_side) * step,
            static_cast<float>(index / (cube_side * cube_side)) * step);
    }
}
