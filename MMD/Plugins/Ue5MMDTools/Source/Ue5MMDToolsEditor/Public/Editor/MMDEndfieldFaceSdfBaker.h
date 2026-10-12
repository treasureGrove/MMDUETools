#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/**
 * 脸部 SDF 自动烘焙。
 *
 * 终末地脸部阴影依赖一张"逐 texel 阴影边界方位角阈值"图。原版由美术/工具产出，
 * 属于模型配套资产；本工具把它变成可从网格自动生成的派生资产，
 * 因此不违反"不依赖手绘配套贴图"的通用性要求（任何 PMX 导入后都能重烘）。
 *
 * 输出 RGBA8：
 *   R/G = 归一化阈值（0..1，乘 π 得弧度）；数值越大，该处越晚进入阴影。
 *   B   = 正面受光占比。
 *   A   = 脸部遮罩（供角色轮廓光与脸部区域使用）。
 */
namespace MMDEnfieldFaceSdf
{
    // 光照方位角采样数。uint64 位掩码上限为 64。
    constexpr int32 LightAngleSamples = 64;
    // 主光俯角（弧度），约 20°，与舞台预设的 KeyLightRotation 同量级。
    constexpr float KeyLightPitch = 0.35f;

    struct FBakeSettings
    {
        /** 头部中心（与网格顶点同一空间，通常为组件空间）。 */
        FVector HeadCenter = FVector::ZeroVector;
        /** 头部半径，用于归一化脸部判定。必须 > 0。 */
        float HeadRadius = 0.0f;
        /** 头部朝前方向（角色面向）。 */
        FVector HeadForward = FVector(0.0, 1.0, 0.0);
        /** 头部朝右方向。 */
        FVector HeadRight = FVector(1.0, 0.0, 0.0);
        /** 头顶方向。 */
        FVector HeadUp = FVector(0.0, 0.0, 1.0);
        /** 输出分辨率（边长，会被钳到 64..4096）。 */
        int32 TextureSize = 512;
        /** 脸部判定：法线与 forward 的余弦下限。 */
        float FaceNormalThreshold = -0.25f;
        /** 下颌高度下限（归一化到头部半径）。 */
        float ChinHeight = -0.95f;
        /** 头顶高度上限（归一化到头部半径）。 */
        float CrownHeight = 1.30f;
    };

    struct FBakeResult
    {
        /** RGBA8 像素，行优先，V 向下（与 UE 纹理存储一致）。 */
        TArray<u8> Pixels;
        int32 Size = 0;
        int32 FaceTexelCount = 0;
        int32 LitTexelCount = 0;
        int32 ConsideredVertexCount = 0;
        bool bSuccess = false;
        FString Error;
    };

    /** 头部基推导结果的来源，便于人工核对与报告记录。 */
    enum class EHeadFrameSource : uint8
    {
        /** 来自骨骼参考姿势的 Head 骨。 */
        HeadBone,
        /** 从网格包围盒推理（骨骼缺失时的兜底）。 */
        BoundsFallback,
        /** 推导失败。 */
        Failed
    };

    /**
     * 从骨骼网格自动推导头部基。
     *
     * 优先使用 @p HeadBoneName 骨骼的参考姿势变换；骨架或该骨骼缺失时，
     * 退回包围盒推理并在 @p OutSource 标注来源。半径按"头心到最高顶点"估算。
     *
     * @return 成功返回 true；失败时 OutError 说明原因。
     */
    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
                         FBakeSettings& OutSettings, EHeadFrameSource& OutSource, FString& OutError);

    /**
     * 从骨骼网格烘焙脸部 SDF。
     *
     * @return 成功返回 true；失败时 OutResult.Error 说明原因，且不写出任何文件。
     */
    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult);

    /** 写出未压缩 32 位 TGA，便于在引擎外查看中间结果。 */
    bool WriteTga(const FBakeResult& Result, const FString& FilePath);

    /** 把烘焙结果写入（或更新）一个 UTexture2D 资产并保存。 */
    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError);
}
