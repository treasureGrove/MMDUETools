#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UTexture2D;

/**
 * 脸部 SDF 自动烘焙。
 *
 * 终末地脸部阴影依赖一张"逐 texel 阴影边界方位角阈值"图。原版由美术/工具产出，
 * 属于模型配套资产；本工具把它变成可从网格自动生成的派生资产，
 * 因此不违反"不依赖手绘配套贴图"的通用性要求（任何 PMX 导入后都能重烘）。
 *
 * 输出 RGBA8：
 *   R = 光沿 head_right 转向时的归一化阈值（0..1，乘 π 得弧度）。
 *   G = 光沿 -head_right 转向时的独立阈值；运行时选择通道，不镜像 UV。
 *   B   = 0：直接左右阈值；1：已验证镜像几何共用 UV，R/G 为大/小阈值，按几何侧恢复。
 *   A   = 脸部遮罩（供角色轮廓光与脸部区域使用）。
 */
namespace MMDEnfieldFaceSdf
{
    // 每一侧从正前方到正后方的方位角区间数。
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
        /** 脸部位置必须在头心前侧，排除后脑与后颈。 */
        float FaceForwardMinimum = -0.15f;
        /** 头部球的最大归一化距离。 */
        float MaximumHeadDistance = 1.35f;
        /** 阈值法线中真实几何的权重；其余来自头部球面，缓和鼻部灰块并保留眼窝细节。 */
        float GeometryNormalWeight = 0.25f;
        /** 烘焙使用的 UV 通道；运行时必须采样同一通道的 frac(UV)。 */
        int32 UVChannel = 0;
        /** 非空时仅烘焙这些材质槽；建议按 Skin 槽分别生成贴图。 */
        TArray<int32> MaterialIndices;
        /** 未指定材质槽时只使用自动识别为 Skin 的槽，排除头发、眼球和饰品。 */
        bool bAutoDetectFaceMaterials = true;
        /** 不同几何共用 UV 但左右阈值矛盾时，使该 texel 无效而退回普通 toon。 */
        float UVOverlapThresholdTolerance = 0.08f;
        /** 镜像几何的位置误差上限，单位为头部半径；仅交换 R/G 不足以判定为镜像。 */
        float MirrorPositionTolerance = 0.025f;
        /** 接近中心线的几何不可靠地判左右，继续使用直接阈值或冲突回退。 */
        float MirrorSideMinimum = 0.02f;
        /** 只扩展 RGB，A 仍为零，避免双线性采样在有效边界读到黑色阈值。 */
        int32 PaddingTexels = 2;
        /** 普通自动路径不猜测缺失的头骨；显式开启才使用旧包围盒兜底。 */
        bool bAllowBoundsFallback = false;
    };

    struct FBakeResult
    {
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
     * 从骨骼网格烘焙脸部 SDF。
     *
     * @return 成功返回 true；失败时 OutResult.Error 说明原因，且不写出任何文件。
     */
    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult);

    /** 写出未压缩 32 位 TGA，便于在引擎外查看中间结果。 */
    bool WriteTga(const FBakeResult& Result, const FString& FilePath);

    /** 创建可绑定的线性 SDF 纹理；临时验证和保存资产共用相同像素格式。 */
    UTexture2D* CreateTexture(const FBakeResult& Result, UObject* Outer, FName Name,
                             EObjectFlags Flags, FString& OutError);

    /** 把烘焙结果写入（或更新）一个 UTexture2D 资产并保存。 */
    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError);
}
