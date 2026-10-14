#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UTexture2D;

/**
 * V4 脸部几何方向场自动烘焙；保留旧 API 名称供导入工具调用。
 * 保存完整漫反射方向，不是实际遮挡 SDF，不截断固定仰角的亮区。
 *
 * 输出 RGBA8：
 *   RG = head-local (Right, Forward, Up) 单位方向的标准 oct 编码，[-1,1] 映射到 [0,1]。
 *   B = 0：直接方向；255：验证镜像共 UV，RG 是正 Right 几何侧方向，x 允许为负。
 *       镜像读取时仅 direction.x 乘当前几何侧符号，不能对 x 先 abs。
 *   A = 255 有效 / 0 回退。四个采样点先分别解 oct、镜像，再插值单位方向。
 */
namespace MMDEnfieldFaceSdf
{
    constexpr int32 DirectionFieldVersion = 4;
    // 旧 V3 校准条件仅保留作统计，不影响 V4 方向编码。
    constexpr int32 LightAngleSamples = 64;
    // 统计参考仰角（弧度）。
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
        /** 脸部位置必须在头心前侧，排除后脑与后颈。 */
        float FaceForwardMinimum = -0.15f;
        /** 头部球的最大归一化距离。 */
        float MaximumHeadDistance = 1.35f;
        /** 漫反射方向中真实几何的权重；其余来自头部球面。 */
        float GeometryNormalWeight = 0.25f;
        /** 烘焙使用的 UV 通道；运行时必须采样同一通道的 frac(UV)。 */
        int32 UVChannel = 0;
        /** 非空时仅烘焙这些材质槽；建议按 Skin 槽分别生成贴图。 */
        TArray<int32> MaterialIndices;
        /** 未指定材质槽时只使用自动识别为 Skin 的槽，排除头发、眼球和饰品。 */
        bool bAutoDetectFaceMaterials = true;
        /** 旧 V3 调用参数保留，不参与 V4 方向比较。 */
        float UVOverlapThresholdTolerance = 0.08f;
        /** 同一几何或镜像几何重叠时允许的方向角误差；复杂重叠仍回退。 */
        float DirectionNormalAngleToleranceDegrees = 10.0f;
        /** 镜像几何的位置误差上限，单位为头部半径；仅交换 R/G 不足以判定为镜像。 */
        float MirrorPositionTolerance = 0.025f;
        /** 接近中心线的几何不可靠地判左右，继续使用直接阈值或冲突回退。 */
        float MirrorSideMinimum = 0.02f;
        /** 只扩展 RGB，A 仍为零；方向与 B 类别应分别解码后插值。 */
        int32 PaddingTexels = 2;
        /** 普通自动路径不猜测缺失的头骨；显式开启才使用旧包围盒兜底。 */
        bool bAllowBoundsFallback = false;
    };

    struct FBakeResult
    {
        int32 DataVersion = DirectionFieldVersion;
        /** RGBA8 像素，行优先，V 向下（与 UE 纹理存储一致）。 */
        TArray<uint8> Pixels;
        int32 Size = 0;
        int32 FaceTexelCount = 0;
        int32 LitTexelCount = 0;
        int32 ConsideredVertexCount = 0;
        int32 ConsideredTriangleCount = 0;
        int32 RejectedTriangleCount = 0;
        int32 ConflictingTexelCount = 0;
        int32 MirroredTexelCount = 0;
        int32 PaddingTexelCount = 0;
        int32 FrontDarkTexelCount = 0;
        int32 NonContinuousTexelCount = 0;
        float GeometryNormalWeight = 0.25f;
        TArray<int32> BakedMaterialIndices;
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
     * 优先使用 @p HeadBoneName，再尝试运行时支持的常见头骨名称。
     * 参考姿势转换到组件空间，头心、眼骨偏移、轴和半径与 HeadBoneSubsystem 一致。
     * 缺失有效头骨默认失败；只在显式允许时使用包围盒兜底。
     *
     * @return 成功返回 true；失败时 OutError 说明原因。
     */
    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
                         FBakeSettings& OutSettings, EHeadFrameSource& OutSource, FString& OutError);

    /**
     * 从骨骼网格烘焙 V4 脸部方向场；不新增或修改任何 UV。
     *
     * @return 成功返回 true；失败时 OutResult.Error 说明原因，且不写出任何文件。
     */
    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult);

    /** 写出未压缩 32 位 TGA，便于在引擎外查看中间结果。 */
    bool WriteTga(const FBakeResult& Result, const FString& FilePath);

    /** 创建可绑定的线性 V4 方向纹理；临时验证和保存共用相同像素格式。 */
    UTexture2D* CreateTexture(const FBakeResult& Result, UObject* Outer, FName Name,
                             EObjectFlags Flags, FString& OutError);

    /** 把烘焙结果写入（或更新）一个 UTexture2D 资产并保存。 */
    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError);
}
