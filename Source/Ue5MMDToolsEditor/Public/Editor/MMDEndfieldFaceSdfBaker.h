#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UTexture2D;

/**
 * V5 自动头投影角度阈值场；保留旧 API 名称，不是原作手绘数据或真实遮挡 SDF。
 * UV = (0.5 + headRightPosition/(3R), 0.5 - headUpPosition/(3R))；不改网格任何 UV，越界不 wrap。
 * RG = 左右 cutAngle/PI，正 yaw 消费 R，负 yaw 消费 G。
 * B = clamp(0.5 + headForwardPosition/(3R), 0, 1)，A = frontmost 外脸有效覆盖。
 * 同一投影仅保存最大 Forward 的皮肤表面，B 不再是 V4 镜像位，RG 不再是 oct 法线。
 * 前向 yaw=0 对所有有效像素均受光；头发/场景遮挡仍是独立的原生阴影。
 */
namespace MMDEnfieldFaceSdf
{
    constexpr int32 DirectionFieldVersion = 5;
    constexpr int32 LightAngleSamples = 64;
    constexpr float KeyLightPitch = 0.35f;

    struct FBakeSettings
    {
        FVector HeadCenter = FVector::ZeroVector;
        float HeadRadius = 0.0f;
        FVector HeadForward = FVector(0.0, 1.0, 0.0);
        FVector HeadRight = FVector(1.0, 0.0, 0.0);
        FVector HeadUp = FVector(0.0, 0.0, 1.0);
        int32 TextureSize = 512;
        /** 真实法线只筛外脸覆盖，不参与 RG 阈值，避免保留鼻翼/口部细碎暗斑。 */
        float FaceNormalThreshold = 0.05f;
        float ChinHeight = -1.5f;
        float CrownHeight = 1.5f;
        float FaceForwardMinimum = 0.0f;
        float MaximumHeadDistance = 2.5f;
        /** 旧调用参数保留；V5 没有法线混合，输出统计为 0。 */
        float GeometryNormalWeight = 0.0f;
        /** -1 表示计算头投影；不能作为 TextureCoordinate 的通道索引。 */
        int32 UVChannel = -1;
        TArray<int32> MaterialIndices;
        bool bAutoDetectFaceMaterials = true;
        float UVOverlapThresholdTolerance = 0.08f;
        float DirectionNormalAngleToleranceDegrees = 10.0f;
        float MirrorPositionTolerance = 0.025f;
        float MirrorSideMinimum = 0.02f;
        /** 只扩展 RGB，A 保持 0；frontmost 深度也随同扩展。 */
        int32 PaddingTexels = 2;
        /** 旧参数保留但 V5 不读取全模型 bounds；没有有效骨尺度即安全失败。 */
        bool bAllowBoundsFallback = false;
    };

    struct FBakeResult
    {
        int32 DataVersion = DirectionFieldVersion;
        TArray<uint8> Pixels;
        int32 Size = 0;
        int32 FaceTexelCount = 0;
        /** V5 指 yaw=0 的有效受光 texel 数，等于 FaceTexelCount。 */
        int32 LitTexelCount = 0;
        int32 ConsideredVertexCount = 0;
        int32 ConsideredTriangleCount = 0;
        int32 RejectedTriangleCount = 0;
        /** V5 使用 frontmost，不再有 UV 镜像冲突；旧统计固定为 0。 */
        int32 ConflictingTexelCount = 0;
        int32 MirroredTexelCount = 0;
        int32 PaddingTexelCount = 0;
        /** V5 是两个连续 yaw 区间，前向全亮；这两个旧统计均为 0，不代表遮挡正确。 */
        int32 FrontDarkTexelCount = 0;
        int32 NonContinuousTexelCount = 0;
        float GeometryNormalWeight = 0.0f;
        /** 有不同 Forward 深度竞争的独立像素数；只保留前层，不把这些像素置无效。 */
        int32 ProjectedOccludedTexelCount = 0;
        int32 ProjectedClippedTriangleCount = 0;
        int32 DepthClampedTexelCount = 0;
        TArray<int32> SkippedTranslucentMaterialIndices;
        TArray<int32> BakedMaterialIndices;
        bool bSuccess = false;
        FString Error;
    };

    enum class EHeadFrameSource : uint8
    {
        HeadBone,
        /** 兼容旧枚举；V5 永远不返回 bounds 兜底。 */
        BoundsFallback,
        Failed
    };

    /** 骨尺度 + 头附近 Skin 几何稳健分位拟合，返回参考组件空间中心与半径；不足时用头骨原点。 */
    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
                         FBakeSettings& OutSettings, EHeadFrameSource& OutSource, FString& OutError);

    /** 单 opaque/masked Skin 槽独立投影；Binding 逐槽调用。透明槽拒绝绑定，不声称五槽生效。 */
    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult);
    bool WriteTga(const FBakeResult& Result, const FString& FilePath);
    UTexture2D* CreateTexture(const FBakeResult& Result, UObject* Outer, FName Name,
                             EObjectFlags Flags, FString& OutError);
    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError);
}
