// 脸部 SDF 自动烘焙器。
//
// 目的：终末地脸部阴影依赖一张"逐 texel 的阴影边界方位角阈值"图。原版由美术/工具产出，
// 属于模型配套资产；本工具把它变成**可自动生成**的派生资产，从网格 + 头部基直接烘出，
// 因此不违反"不依赖手绘配套贴图"的通用性要求（任何 PMX 导入后都能一键重烘）。
//
// 算法：
//   1. 取 LOD0 的顶点位置、法线、UV。
//   2. 用头部包围球建立头部局部基（forward = 角色前方，right = 角色右方，up = 头顶）。
//   3. 判定"脸部顶点"：法线朝前 + 位于头部半径内 + 下颌以上 + 头顶以下。
//   4. 对每个脸部 texel，在【水平面内绕头旋转】采样 LightAngleSamples 个光照方位角 θ，
//      记录该 texel 从哪个角度起进入阴影。
//   5. 阈值定义为"从正前方(θ=0)起连续受光的最远角度"：这是最稳健的 SDF 语义，
//      对应原版中数值越大的像素越晚变暗。
//   6. 运行时由 _FlipSDFThreshold 按光照所在侧镜像采样 U，因此烘焙侧不镜像，
//      两侧 texel 各自拥有真实阈值（避免假定 UV 对称）。
//
// 内存：逐 texel 用 uint64 位掩码记录受光角度（LightAngleSamples <= 64），
//       512² 仅需 2 MB，而不是逐角计数的 67 MB。
//
// 输出：RGBA8。R/G = 归一化阈值（0..1，乘 π 得弧度），B = 正面受光占比，
//       A = 脸部遮罩（供角色轮廓光与脸部分区使用）。
#include "Editor/MMDEndfieldFaceSdfBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Skeleton.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkeletalMeshTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MMDEnfieldFaceSdf
{
    /** 建立正交头部基；失败返回 false。 */
    bool BuildHeadFrame(const FBakeSettings& Settings,
                        FVector& OutForward, FVector& OutRight, FVector& OutUp)
    {
        OutForward = Settings.HeadForward.GetSafeNormal();
        if (OutForward.IsNearlyZero())
            OutForward = FVector(0.0, 1.0, 0.0);
        FVector Right = Settings.HeadRight - OutForward * FVector::DotProduct(Settings.HeadRight, OutForward);
        Right = Right.GetSafeNormal();
        if (Right.IsNearlyZero())
            return false;
        OutRight = Right;
        OutUp = FVector::CrossProduct(OutRight, OutForward).GetSafeNormal();
        return !OutUp.IsNearlyZero();
    }

    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
                         FBakeSettings& OutSettings, EHeadFrameSource& OutSource, FString& OutError)
    {
        OutSource = EHeadFrameSource::Failed;
        if (!Mesh)
        {
            OutError = TEXT("骨骼网格为空");
            return false;
        }

        // ---- 路径 1：骨骼参考姿势 ----
        const USkeleton* Skeleton = Mesh->GetSkeleton();
        const FReferenceSkeleton* RefSkeleton = Skeleton ? &Skeleton->GetReferenceSkeleton() : nullptr;
        int32 HeadBoneIndex = INDEX_NONE;
        if (RefSkeleton && !HeadBoneName.IsNone())
            HeadBoneIndex = RefSkeleton->FindBoneIndex(HeadBoneName);

        // 用 LOD0 顶点包围盒确定网格尺度，供骨骼路径估算半径与兜底路径使用。
        const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        FBox Bounds(EForceInit::ForceInit);
        if (RenderData && RenderData->LODRenderData.Num() > 0)
        {
            const FPositionVertexBuffer& PositionBuffer =
                RenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer;
            for (uint32 Index = 0; Index < PositionBuffer.GetNumVertices(); ++Index)
                Bounds += FVector(PositionBuffer.VertexPosition(Index));
        }

        if (HeadBoneIndex != INDEX_NONE && RefSkeleton)
        {
            const FTransform LocalTransform = RefSkeleton->GetRefBonePose()[HeadBoneIndex];
            OutSettings.HeadCenter = LocalTransform.GetLocation();
            // 骨骼局部轴：X = 前方（UE 骨骼约定），Y = 右方，Z = 上方。
            OutSettings.HeadForward = LocalTransform.GetUnitAxis(EAxis::X);
            OutSettings.HeadRight = LocalTransform.GetUnitAxis(EAxis::Y);
            OutSettings.HeadUp = LocalTransform.GetUnitAxis(EAxis::Z);
            // 头心到最高顶点的距离作为头部半径；失败则用包围盒高度的 12%。
            const float VerticalExtent = Bounds.IsValid ? static_cast<float>(Bounds.Max.Z) : 0.0f;
            const float Radius = VerticalExtent - static_cast<float>(OutSettings.HeadCenter.Z);
            OutSettings.HeadRadius = Radius > KINDA_SMALL_NUMBER
                                   ? Radius
                                   : (Bounds.IsValid ? static_cast<float>(Bounds.GetSize().Z) * 0.12f : 0.0f);
            if (OutSettings.HeadRadius > KINDA_SMALL_NUMBER)
            {
                OutSource = EHeadFrameSource::HeadBone;
                return true;
            }
        }

        // ---- 路径 2：包围盒兜底 ----
        // 没有任何头部信息时，用"上方 1/6 高度的中心"当头部，仅在包围盒有效时使用，
        // 并在报告中明确标注来源为兜底，提示人工核对。
        if (!Bounds.IsValid)
        {
            OutError = TEXT("骨架无 Head 骨，且网格包围盒无效：无法推导头部基");
            return false;
        }
        const FVector Size = Bounds.GetSize();
        const FVector Center = Bounds.GetCenter();
        OutSettings.HeadCenter = FVector(Center.X, Center.Y, Bounds.Max.Z - Size.Z * 0.10);
        OutSettings.HeadForward = FVector(0.0, 1.0, 0.0);
        OutSettings.HeadRight = FVector(1.0, 0.0, 0.0);
        OutSettings.HeadUp = FVector(0.0, 0.0, 1.0);
        OutSettings.HeadRadius = static_cast<float>(Size.Z) * 0.10f;
        if (OutSettings.HeadRadius <= KINDA_SMALL_NUMBER)
        {
            OutError = TEXT("包围盒过小，无法推导有效头部半径");
            return false;
        }
        OutSource = EHeadFrameSource::BoundsFallback;
        OutError = TEXT("未找到 Head 骨，已用包围盒兜底推导头部基；结果偏差较大，请人工核对");
        return true;
    }

    /**
     * 从骨骼网格烘焙脸部 SDF。
     *
     * @param Mesh       目标骨骼网格（读取 LOD0）。
     * @param Settings   头部基与脸部判定参数。
     * @param OutResult  输出的 RGBA8 像素与统计。
     * @return 成功返回 true；失败时 OutResult.Error 说明原因，且不写出任何文件。
     */
    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult)
    {
        OutResult = FBakeResult();
        if (!Mesh)
        {
            OutResult.Error = TEXT("骨骼网格为空");
            return false;
        }
        if (Settings.HeadRadius <= KINDA_SMALL_NUMBER)
        {
            OutResult.Error = TEXT("头部半径无效：无法建立头部局部基");
            return false;
        }
        FVector Forward;
        FVector Right;
        FVector Up;
        if (!BuildHeadFrame(Settings, Forward, Right, Up))
        {
            OutResult.Error = TEXT("头部基退化（forward 与 right 共线）");
            return false;
        }
        FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
        if (!RenderData || RenderData->LODRenderData.Num() == 0)
        {
            OutResult.Error = TEXT("骨骼网格没有可用的 LOD0 渲染数据");
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[0];
        const FPositionVertexBuffer& PositionBuffer = LODData.StaticVertexBuffers.PositionVertexBuffer;
        const FStaticMeshVertexBuffer& StaticBuffer = LODData.StaticVertexBuffers.StaticMeshVertexBuffer;
        const int32 VertexCount = static_cast<int32>(PositionBuffer.GetNumVertices());
        if (VertexCount == 0)
        {
            OutResult.Error = TEXT("LOD0 顶点缓冲为空");
            return false;
        }

        const int32 Size = FMath::Clamp(Settings.TextureSize, 64, 4096);
        const int32 TexelCount = Size * Size;

        // 逐 texel：受光角度位掩码、是否有脸部顶点、正面受光占比累加。
        TArray<uint64> LitAngleMask;
        TArray<uint8> FaceTexel;
        TArray<float> FrontLitSum;
        TArray<int32> FaceVertexCount;
        LitAngleMask.SetNumZeroed(TexelCount);
        FaceTexel.SetNumZeroed(TexelCount);
        FrontLitSum.SetNumZeroed(TexelCount);
        FaceVertexCount.SetNumZeroed(TexelCount);

        // 预计算采样角方向：从正前方起绕 Up 轴旋转，带固定俯角。
        TArray<FVector> LightDirections;
        LightDirections.SetNum(LightAngleSamples);
        for (int32 Index = 0; Index < LightAngleSamples; ++Index)
        {
            const float Theta = (2.0f * PI * static_cast<float>(Index)) / static_cast<float>(LightAngleSamples);
            const FVector Horizontal = Forward * FMath::Cos(Theta) + Right * FMath::Sin(Theta);
            LightDirections[Index] = (Horizontal * FMath::Cos(KeyLightPitch)
                                     + Up * FMath::Sin(KeyLightPitch)).GetSafeNormal();
        }

        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector3f Position = PositionBuffer.VertexPosition(VertexIndex);
            // VertexTangentZ 返回 FVector4f（w 为手性符号），取 xyz 作几何法线。
            const FVector4f TangentZ = StaticBuffer.VertexTangentZ(VertexIndex);
            const FVector3f Normal(static_cast<float>(TangentZ.X), static_cast<float>(TangentZ.Y), static_cast<float>(TangentZ.Z));
            // 每个顶点可有多套 UV，脸部 SDF 使用 UV0（与材质 base_color 同一套）。
            const FVector2f UV = StaticBuffer.GetVertexUV(VertexIndex, 0);

            // 头部局部坐标（归一化到头部半径）。
            const FVector Offset = (FVector(Position) - Settings.HeadCenter) / Settings.HeadRadius;
            const float LocalForward = FVector::DotProduct(Offset, Forward);
            const float LocalRight = FVector::DotProduct(Offset, Right);
            const float LocalUp = FVector::DotProduct(Offset, Up);
            const float Distance = FMath::Sqrt(LocalRight * LocalRight + LocalForward * LocalForward + LocalUp * LocalUp);
            const float NormalDotForward = FVector::DotProduct(FVector(Normal), Forward);

            // 脸部顶点判定：法线朝前 + 在头部半径内 + 下颌以上 + 头顶以下。
            if (NormalDotForward <= Settings.FaceNormalThreshold)
                continue;
            if (Distance > 1.55f)
                continue;
            if (LocalUp <= Settings.ChinHeight || LocalUp >= Settings.CrownHeight)
                continue;

            // UV → texel。UV 环绕取小数部分，避免越界；V 向下与 UE 纹理存储一致。
            const float U = UV.X - FMath::FloorToFloat(UV.X);
            const float V = UV.Y - FMath::FloorToFloat(UV.Y);
            const int32 TexelX = FMath::Clamp(static_cast<int32>(U * Size), 0, Size - 1);
            const int32 TexelY = FMath::Clamp(static_cast<int32>(V * Size), 0, Size - 1);
            const int32 TexelIndex = TexelY * Size + TexelX;

            ++OutResult.ConsideredVertexCount;
            FaceTexel[TexelIndex] = 1;
            ++FaceVertexCount[TexelIndex];

            const FVector3f NormalF = Normal;
            for (int32 AngleIndex = 0; AngleIndex < LightAngleSamples; ++AngleIndex)
            {
                const FVector& Dir = LightDirections[AngleIndex];
                const FVector3f LightDir(static_cast<float>(Dir.X), static_cast<float>(Dir.Y), static_cast<float>(Dir.Z));
                if (FVector3f::DotProduct(NormalF, LightDir) > 0.0f)
                    LitAngleMask[TexelIndex] |= (1ull << AngleIndex);
            }
            // 正前方受光占比（B 通道）：轻俯角下的可见度。
            const FVector3f FrontDir(static_cast<float>(LightDirections[0].X),
                                     static_cast<float>(LightDirections[0].Y),
                                     static_cast<float>(LightDirections[0].Z));
            FrontLitSum[TexelIndex] += FMath::Clamp(FVector3f::DotProduct(NormalF, FrontDir), 0.0f, 1.0f);
        }

        // ---- 逐 texel 求阈值 ----
        OutResult.Pixels.SetNumZeroed(TexelCount * 4);
        for (int32 TexelIndex = 0; TexelIndex < TexelCount; ++TexelIndex)
        {
            const int32 ByteIndex = TexelIndex * 4;
            float Threshold = 0.0f;
            float LitFraction = 0.0f;
            float FaceMask = 0.0f;
            if (FaceTexel[TexelIndex] && FaceVertexCount[TexelIndex] > 0)
            {
                const uint64 Mask = LitAngleMask[TexelIndex];
                // 从 θ=0 起连续受光的最远角度：光照转到这里后该 texel 进入阴影。
                int32 LastLitAngle = 0;
                for (int32 AngleIndex = 0; AngleIndex < LightAngleSamples; ++AngleIndex)
                {
                    if ((Mask & (1ull << AngleIndex)) == 0ull)
                        break;
                    LastLitAngle = AngleIndex;
                }
                Threshold = static_cast<float>(LastLitAngle) / static_cast<float>(LightAngleSamples);
                LitFraction = FrontLitSum[TexelIndex] / static_cast<float>(FaceVertexCount[TexelIndex]);
                FaceMask = 1.0f;
                ++OutResult.FaceTexelCount;
                if (LastLitAngle > 0)
                    ++OutResult.LitTexelCount;
            }
            const u8 ThresholdByte = static_cast<u8>(FMath::Clamp(Threshold, 0.0f, 1.0f) * 255.0f);
            OutResult.Pixels[ByteIndex + 0] = ThresholdByte;
            OutResult.Pixels[ByteIndex + 1] = ThresholdByte;
            OutResult.Pixels[ByteIndex + 2] = static_cast<u8>(FMath::Clamp(LitFraction, 0.0f, 1.0f) * 255.0f);
            OutResult.Pixels[ByteIndex + 3] = static_cast<u8>(FaceMask * 255.0f);
        }

        OutResult.Size = Size;
        OutResult.bSuccess = true;
        return true;
    }

    /** 写出未压缩 32 位 TGA，便于在引擎外查看中间结果。 */
    bool WriteTga(const FBakeResult& Result, const FString& FilePath)
    {
        if (!Result.bSuccess || Result.Pixels.Num() == 0 || Result.Size <= 0)
            return false;
        const int32 Size = Result.Size;
        TArray<u8> File;
        File.Reserve(18 + Size * Size * 4);
        // TGA 头：无颜色表、未压缩真彩、32 位 BGRA。
        const u8 Header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                static_cast<u8>(Size & 0xFF), static_cast<u8>((Size >> 8) & 0xFF),
                                static_cast<u8>(Size & 0xFF), static_cast<u8>((Size >> 8) & 0xFF),
                                32, 0x28 };  // 0x28 = 8 位 alpha + 左上原点
        File.Append(Header, 18);
        for (int32 Index = 0; Index < Size * Size; ++Index)
        {
            const int32 ByteIndex = Index * 4;
            // TGA 为 BGRA 顺序。
            File.Add(Result.Pixels[ByteIndex + 2]);
            File.Add(Result.Pixels[ByteIndex + 1]);
            File.Add(Result.Pixels[ByteIndex + 0]);
            File.Add(Result.Pixels[ByteIndex + 3]);
        }
        return FFileHelper::SaveArrayToFile(File, *FilePath);
    }

    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError)
    {
        if (!Result.bSuccess || Result.Pixels.Num() == 0 || Result.Size <= 0)
        {
            OutError = TEXT("烘焙结果为空，拒绝写出贴图");
            return false;
        }
        const bool bExists = FPackageName::DoesPackageExist(PackageName);
        UTexture2D* Texture = bExists
            ? LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName))
            : nullptr;
        if (bExists && !Texture)
        {
            OutError = FString::Printf(TEXT("无法加载已存在的 SDF 贴图：%s"), *PackageName);
            return false;
        }
        if (!Texture)
        {
            Texture = NewObject<UTexture2D>(CreatePackage(*PackageName), *AssetName, RF_Public | RF_Standalone);
            if (!Texture)
            {
                OutError = FString::Printf(TEXT("无法创建 SDF 贴图：%s"), *PackageName);
                return false;
            }
            FAssetRegistryModule::AssetCreated(Texture);
        }
        // BGRA8 源数据；SDF 存的是角度阈值而非颜色，必须线性采样、不要 sRGB。
        Texture->Source.Init(Result.Size, Result.Size, 1, 1, TSF_BGRA8, Result.Pixels.GetData());
        Texture->SRGB = false;
        Texture->CompressionSettings = TC_VectorDisplacementmap;  // RGBA8 无损
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->Filter = TF_Bilinear;
        Texture->VirtualTextureStreaming = false;
        Texture->LODGroup = TEXTUREGROUP_Character;
        Texture->PostEditChange();

        const FString Filename = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        Texture->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Texture->GetOutermost(), Texture, *Filename, Args))
        {
            OutError = FString::Printf(TEXT("无法保存 SDF 贴图：%s"), *PackageName);
            return false;
        }
        return true;
    }
}
