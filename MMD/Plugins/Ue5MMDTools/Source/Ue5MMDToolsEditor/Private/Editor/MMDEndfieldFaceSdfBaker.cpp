// 将 LOD0 三角形栅格化到原 UV，保存完整头空间漫反射方向；不新增 UV 或烘焙实际遮挡。
#include "Editor/MMDEndfieldFaceSdfBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Import/MMDModelDataAsset.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "SkeletalMeshTypes.h"
#include "SkinnedAssetCompiler.h"
#include "Stage/MMDStageLibrary.h"
#include "TextureCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MMDEnfieldFaceSdf
{
    static_assert(DirectionFieldVersion == UMMDEndfieldFaceData::CurrentVersion, "Face direction schema version mismatch");
    static bool BuildHeadFrame(const FBakeSettings& Settings,
                        FVector& OutForward, FVector& OutRight, FVector& OutUp)
    {
        OutForward = Settings.HeadForward.GetSafeNormal();
        if (OutForward.IsNearlyZero() || OutForward.ContainsNaN()) return false;
        FVector Right = Settings.HeadRight - OutForward * FVector::DotProduct(Settings.HeadRight, OutForward);
        Right = Right.GetSafeNormal();
        if (Right.IsNearlyZero())
            return false;
        OutRight = Right;
        OutUp = FVector::CrossProduct(OutRight, OutForward).GetSafeNormal();
        return !OutUp.IsNearlyZero() && !OutRight.ContainsNaN() && !OutUp.ContainsNaN();
    }

    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
                         FBakeSettings& OutSettings, EHeadFrameSource& OutSource, FString& OutError)
    {
        OutSource = EHeadFrameSource::Failed;
        OutError.Reset();
        if (!Mesh)
        {
            OutError = TEXT("骨骼网格为空");
            return false;
        }

        USkinnedAsset* PendingMeshes[] = { const_cast<USkeletalMesh*>(Mesh) };
        FSkinnedAssetCompilingManager::Get().FinishCompilation(PendingMeshes);
        const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
        int32 HeadBoneIndex = HeadBoneName.IsNone() ? INDEX_NONE : RefSkeleton.FindBoneIndex(HeadBoneName);
        if (HeadBoneIndex == INDEX_NONE)
        {
            for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") })
            {
                HeadBoneIndex = RefSkeleton.FindBoneIndex(FName(Name));
                if (HeadBoneIndex != INDEX_NONE) break;
            }
        }
        if (HeadBoneIndex != INDEX_NONE)
        {
            // 与运行时相同：先合成组件空间参考变换，再使用参考姿势中的角色方向。
            TArray<FTransform> ReferenceTransforms = RefSkeleton.GetRefBonePose();
            if (!ReferenceTransforms.IsValidIndex(HeadBoneIndex))
            {
                OutError = TEXT("头骨没有有效参考姿势，拒绝生成 SDF");
                return false;
            }
            for (int32 Index = 0; Index < ReferenceTransforms.Num(); ++Index)
            {
                const int32 Parent = RefSkeleton.GetParentIndex(Index);
                if (Parent != INDEX_NONE && (Parent < 0 || Parent >= Index))
                {
                    OutError = TEXT("骨骼参考姿势层级无效，拒绝生成 SDF");
                    return false;
                }
                if (Parent != INDEX_NONE) ReferenceTransforms[Index] *= ReferenceTransforms[Parent];
            }
            const FTransform& HeadTransform = ReferenceTransforms[HeadBoneIndex];
            const bool bMMD = const_cast<USkeletalMesh*>(Mesh)->GetAssetUserData<UMMDSkeletalMeshUserData>() != nullptr
                || RefSkeleton.GetBoneName(HeadBoneIndex) == FName(TEXT("頭"));
            OutSettings.HeadForward = bMMD ? FVector::YAxisVector : FVector::XAxisVector;
            OutSettings.HeadUp = FVector::UpVector;
            OutSettings.HeadRight = FVector::CrossProduct(OutSettings.HeadForward, OutSettings.HeadUp).GetSafeNormal();
            OutSettings.HeadRadius = Mesh->GetBounds().BoxExtent.Z * 0.14f;
            int32 LeftEye = INDEX_NONE;
            int32 RightEye = INDEX_NONE;
            for (const TCHAR* Name : { TEXT("左目"), TEXT("左眼"), TEXT("LeftEye"), TEXT("eye_l"), TEXT("Eye_L"), TEXT("eye.L"), TEXT("EyeLeft") })
            {
                LeftEye = RefSkeleton.FindBoneIndex(FName(Name));
                if (LeftEye != INDEX_NONE) break;
            }
            for (const TCHAR* Name : { TEXT("右目"), TEXT("右眼"), TEXT("RightEye"), TEXT("eye_r"), TEXT("Eye_R"), TEXT("eye.R"), TEXT("EyeRight") })
            {
                RightEye = RefSkeleton.FindBoneIndex(FName(Name));
                if (RightEye != INDEX_NONE) break;
            }
            OutSettings.HeadCenter = LeftEye != INDEX_NONE && RightEye != INDEX_NONE
                ? (ReferenceTransforms[LeftEye].GetLocation() + ReferenceTransforms[RightEye].GetLocation()) * 0.5
                    - OutSettings.HeadUp * (OutSettings.HeadRadius * 0.18)
                    - OutSettings.HeadForward * (OutSettings.HeadRadius * 0.10)
                : HeadTransform.GetLocation() + OutSettings.HeadUp * (OutSettings.HeadRadius * 0.45);
            if (!OutSettings.HeadCenter.ContainsNaN() && FMath::IsFinite(OutSettings.HeadRadius)
                && OutSettings.HeadRadius > KINDA_SMALL_NUMBER)
            {
                OutSource = EHeadFrameSource::HeadBone;
                return true;
            }
            OutError = TEXT("头骨参考姿势或网格尺度无效，拒绝生成 SDF");
            return false;
        }
        if (!OutSettings.bAllowBoundsFallback)
        {
            OutError = TEXT("未找到有效头骨，SDF 保持禁用；可指定真实头骨名后重烘");
            return false;
        }
        const FBoxSphereBounds Bounds = Mesh->GetBounds();
        const FVector Size = Bounds.BoxExtent * 2.0;
        OutSettings.HeadCenter = Bounds.Origin + FVector(0.0, 0.0, Bounds.BoxExtent.Z - Size.Z * 0.10);
        OutSettings.HeadForward = FVector(0.0, 1.0, 0.0);
        OutSettings.HeadRight = FVector(1.0, 0.0, 0.0);
        OutSettings.HeadUp = FVector(0.0, 0.0, 1.0);
        OutSettings.HeadRadius = static_cast<float>(Size.Z) * 0.10f;
        if (!FMath::IsFinite(OutSettings.HeadRadius) || OutSettings.HeadRadius <= KINDA_SMALL_NUMBER
            || OutSettings.HeadCenter.ContainsNaN())
        {
            OutError = TEXT("包围盒过小，无法推导有效头部半径");
            return false;
        }
        OutSource = EHeadFrameSource::BoundsFallback;
        OutError = TEXT("未找到 Head 骨，已用包围盒兜底推导头部基；结果偏差较大，请人工核对");
        return true;
    }

    static float SideThreshold(const FVector& Normal, const FVector& Forward,
                               const FVector& Right, const FVector& Up, float Side, bool& bNonContinuous)
    {
        const float ForwardTerm = FVector::DotProduct(Normal, Forward) * FMath::Cos(KeyLightPitch);
        const float RightTerm = FVector::DotProduct(Normal, Right) * FMath::Cos(KeyLightPitch) * Side;
        const float UpTerm = FVector::DotProduct(Normal, Up) * FMath::Sin(KeyLightPitch);
        float PreviousLight = ForwardTerm + UpTerm;
        bNonContinuous = false;
        if (PreviousLight <= 0.0f) return 0.0f;
        float Threshold = 1.0f;
        bool bCrossed = false;
        for (int32 Index = 1; Index <= LightAngleSamples; ++Index)
        {
            const float Angle = PI * static_cast<float>(Index) / LightAngleSamples;
            const float Light = ForwardTerm * FMath::Cos(Angle) + RightTerm * FMath::Sin(Angle) + UpTerm;
            if (Light <= 0.0f && !bCrossed)
            {
                const float Crossing = PreviousLight / FMath::Max(PreviousLight - Light, UE_SMALL_NUMBER);
                Threshold = (static_cast<float>(Index - 1) + Crossing) / LightAngleSamples;
                bCrossed = true;
            }
            else if (Light > 0.0f && bCrossed) bNonContinuous = true;
            PreviousLight = Light;
        }
        return Threshold;
    }

    static bool IsFacePoint(const FVector& Position, const FVector& Normal,
                            const FBakeSettings& Settings, const FVector& Forward, const FVector& Up)
    {
        const FVector Offset = (Position - Settings.HeadCenter) / Settings.HeadRadius;
        const float Height = FVector::DotProduct(Offset, Up);
        return !Position.ContainsNaN() && !Normal.ContainsNaN() && !Normal.IsNearlyZero()
            && FVector::DotProduct(Normal, Forward) > Settings.FaceNormalThreshold
            && FVector::DotProduct(Offset, Forward) > Settings.FaceForwardMinimum
            && Offset.SizeSquared() <= FMath::Square(Settings.MaximumHeadDistance)
            && Height > Settings.ChinHeight && Height < Settings.CrownHeight;
    }

    static uint8 EncodeUnit(float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    static void EncodeOctDirection(const FVector3f& Direction, uint8* Pixel)
    {
        const float Denominator = FMath::Abs(Direction.X) + FMath::Abs(Direction.Y) + FMath::Abs(Direction.Z);
        FVector3f Oct = Direction / FMath::Max(Denominator, UE_SMALL_NUMBER);
        if (Oct.Z < 0.0f)
        {
            const float PreviousX = Oct.X;
            Oct.X = (1.0f - FMath::Abs(Oct.Y)) * (PreviousX >= 0.0f ? 1.0f : -1.0f);
            Oct.Y = (1.0f - FMath::Abs(PreviousX)) * (Oct.Y >= 0.0f ? 1.0f : -1.0f);
        }
        Pixel[0] = EncodeUnit(Oct.X * 0.5f + 0.5f);
        Pixel[1] = EncodeUnit(Oct.Y * 0.5f + 0.5f);
    }

    static bool MergeFaceTexel(uint8* Pixel, FVector3f& StoredPosition, FVector3f& StoredDirection,
                               bool bCovered, const FVector3f& Position, const FVector3f& Direction,
                               float NormalToleranceCos, float PositionTolerance, float SideMinimum)
    {
        if (!bCovered)
        {
            EncodeOctDirection(Direction, Pixel);
            Pixel[2] = 0;
            StoredPosition = Position;
            StoredDirection = Direction;
            return true;
        }
        const bool bReliableSides = FMath::Abs(StoredPosition.X) >= SideMinimum && FMath::Abs(Position.X) >= SideMinimum;
        const bool bMirrorGeometry = bReliableSides && StoredPosition.X * Position.X < 0.0f
            && FMath::Abs(StoredPosition.X + Position.X) <= PositionTolerance
            && FMath::Abs(StoredPosition.Y - Position.Y) <= PositionTolerance
            && FMath::Abs(StoredPosition.Z - Position.Z) <= PositionTolerance;
        const bool bSameGeometry = FMath::Abs(StoredPosition.X - Position.X) <= PositionTolerance
            && FMath::Abs(StoredPosition.Y - Position.Y) <= PositionTolerance
            && FMath::Abs(StoredPosition.Z - Position.Z) <= PositionTolerance;
        if (Pixel[2] == 255)
        {
            // 第三次覆盖恢复当前几何侧方向；不能把任何相似法线都接受为同一 UV。
            FVector3f Expected = StoredDirection;
            if (Position.X < 0.0f) Expected.X = -Expected.X;
            return bReliableSides && ((bSameGeometry && StoredPosition.X * Position.X > 0.0f) || bMirrorGeometry)
                && FVector3f::DotProduct(Expected, Direction) >= NormalToleranceCos;
        }
        FVector3f MirroredStored = StoredDirection;
        MirroredStored.X = -MirroredStored.X;
        if (bMirrorGeometry && FVector3f::DotProduct(MirroredStored, Direction) >= NormalToleranceCos)
        {
            // canonical 是正 Right 几何侧，x 可指向内侧；禁止 abs(x) 抹掉折面方向。
            if (StoredPosition.X < 0.0f) StoredDirection.X = -StoredDirection.X;
            EncodeOctDirection(StoredDirection, Pixel);
            Pixel[2] = 255;
            return true;
        }
        // 方向相似但几何无关的 UV 重叠仍回退；保留第一次方向，避免重复平均积累误差。
        return bSameGeometry && FVector3f::DotProduct(StoredDirection, Direction) >= NormalToleranceCos;
    }

    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& OutResult)
    {
        OutResult = FBakeResult();
        if (!Mesh)
        {
            OutResult.Error = TEXT("骨骼网格为空");
            return false;
        }
        if (!FMath::IsFinite(Settings.HeadRadius) || Settings.HeadRadius <= KINDA_SMALL_NUMBER
            || Settings.HeadCenter.ContainsNaN() || !FMath::IsFinite(Settings.MaximumHeadDistance)
            || Settings.MaximumHeadDistance <= 0.0f || !FMath::IsFinite(Settings.FaceNormalThreshold)
            || !FMath::IsFinite(Settings.FaceForwardMinimum) || !FMath::IsFinite(Settings.ChinHeight)
            || !FMath::IsFinite(Settings.CrownHeight) || Settings.ChinHeight >= Settings.CrownHeight
            || !FMath::IsFinite(Settings.UVOverlapThresholdTolerance)
            || !FMath::IsFinite(Settings.DirectionNormalAngleToleranceDegrees)
            || Settings.DirectionNormalAngleToleranceDegrees < 0.0f || Settings.DirectionNormalAngleToleranceDegrees > 45.0f
            || !FMath::IsFinite(Settings.GeometryNormalWeight)
            || !FMath::IsFinite(Settings.MirrorPositionTolerance) || Settings.MirrorPositionTolerance <= 0.0f
            || !FMath::IsFinite(Settings.MirrorSideMinimum) || Settings.MirrorSideMinimum <= 0.0f)
        {
            OutResult.Error = TEXT("头部基或脸部判定参数无效，拒绝生成 SDF");
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
        USkinnedAsset* PendingMeshes[] = { Mesh };
        FSkinnedAssetCompilingManager::Get().FinishCompilation(PendingMeshes);
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
        if (VertexCount == 0 || StaticBuffer.GetNumVertices() != PositionBuffer.GetNumVertices()
            || !PositionBuffer.GetVertexData() || !StaticBuffer.GetTangentData())
        {
            OutResult.Error = TEXT("LOD0 没有可读取的 CPU 顶点或法线数据");
            return false;
        }
        if (Settings.UVChannel < 0 || Settings.UVChannel >= static_cast<int32>(StaticBuffer.GetNumTexCoords())
            || !StaticBuffer.GetTexCoordData())
        {
            OutResult.Error = TEXT("LOD0 缺少指定的 UV 通道，保持普通 toon 回退");
            return false;
        }
        if (!LODData.MultiSizeIndexContainer.IsIndexBufferValid())
        {
            OutResult.Error = TEXT("LOD0 三角形索引缓冲为空");
            return false;
        }
        const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
        if (IndexBuffer->Num() == 0 || IndexBuffer->GetResourceDataSize() == 0)
        {
            OutResult.Error = TEXT("LOD0 三角形索引没有 CPU 数据，无法安全烘焙");
            return false;
        }

        TSet<int32> MaterialIndices;
        for (const int32 MaterialIndex : Settings.MaterialIndices)
        {
            if (!Mesh->GetMaterials().IsValidIndex(MaterialIndex))
            {
                OutResult.Error = FString::Printf(TEXT("材质槽索引无效：%d"), MaterialIndex);
                return false;
            }
            MaterialIndices.Add(MaterialIndex);
        }
        if (MaterialIndices.IsEmpty() && Settings.bAutoDetectFaceMaterials)
        {
            USkeletalMeshComponent* Classifier = NewObject<USkeletalMeshComponent>(GetTransientPackage());
            Classifier->SetSkeletalMesh(Mesh);
            for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetMaterials().Num(); ++MaterialIndex)
                if (UMMDStageLibrary::GetResolvedCharacterMaterialRole(Classifier, MaterialIndex) == EMMDMaterialRole::Skin)
                    MaterialIndices.Add(MaterialIndex);
            if (MaterialIndices.IsEmpty())
            {
                OutResult.Error = TEXT("未识别到 Skin 材质槽；可明确设置 MaterialIndices，禁止把头发烘进脸部 SDF");
                return false;
            }
        }

        const int32 Size = FMath::Clamp(Settings.TextureSize, 64, 4096);
        const int32 TexelCount = Size * Size;

        // 0=未覆盖，1=有效，2=UV 冲突，3=只有阈值边缘扩展；冲突不再被后续三角形覆盖。
        TArray<uint8> TexelState;
        TexelState.SetNumZeroed(TexelCount);
        TArray<FVector3f> TexelLocalPosition;
        TexelLocalPosition.SetNumZeroed(TexelCount);
        TArray<FVector3f> TexelLocalDirection;
        TexelLocalDirection.SetNumZeroed(TexelCount);
        TBitArray<> ConsideredVertices(false, VertexCount);
        OutResult.GeometryNormalWeight = FMath::Clamp(Settings.GeometryNormalWeight, 0.0f, 1.0f);
        OutResult.Pixels.SetNumZeroed(TexelCount * 4);
        const FVector FrontLight = Forward * FMath::Cos(KeyLightPitch) + Up * FMath::Sin(KeyLightPitch);
        const float NormalToleranceCos = FMath::Cos(FMath::DegreesToRadians(Settings.DirectionNormalAngleToleranceDegrees));

        const FSkeletalMeshLODInfo* LODInfo = Mesh->GetLODInfo(0);
        for (int32 SectionIndex = 0; SectionIndex < LODData.RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
            const int32 MaterialIndex = LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex)
                && LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE
                ? LODInfo->LODMaterialMap[SectionIndex] : static_cast<int32>(Section.MaterialIndex);
            if (Section.bDisabled || (MaterialIndices.Num() > 0 && !MaterialIndices.Contains(MaterialIndex))) continue;
            if (static_cast<uint64>(Section.BaseIndex) + static_cast<uint64>(Section.NumTriangles) * 3 > static_cast<uint64>(IndexBuffer->Num()))
            {
                OutResult.Error = TEXT("LOD0 section 三角形索引范围无效");
                return false;
            }
            for (uint32 TriangleIndex = 0; TriangleIndex < Section.NumTriangles; ++TriangleIndex)
            {
                FVector Positions[3];
                FVector Normals[3];
                FVector2f UVs[3];
                uint32 Indices[3];
                bool bValid = true;
                for (int32 Corner = 0; Corner < 3; ++Corner)
                {
                    Indices[Corner] = IndexBuffer->Get(Section.BaseIndex + TriangleIndex * 3 + Corner);
                    if (Indices[Corner] >= static_cast<uint32>(VertexCount)) { bValid = false; break; }
                    Positions[Corner] = FVector(PositionBuffer.VertexPosition(Indices[Corner]));
                    const FVector4f TangentZ = StaticBuffer.VertexTangentZ(Indices[Corner]);
                    Normals[Corner] = FVector(TangentZ.X, TangentZ.Y, TangentZ.Z).GetSafeNormal();
                    UVs[Corner] = StaticBuffer.GetVertexUV(Indices[Corner], Settings.UVChannel);
                    if (Positions[Corner].ContainsNaN() || Normals[Corner].ContainsNaN()
                        || !FMath::IsFinite(UVs[Corner].X) || !FMath::IsFinite(UVs[Corner].Y)) bValid = false;
                }
                if (!bValid)
                {
                    ++OutResult.RejectedTriangleCount;
                    continue;
                }
                const FVector2f Edge1 = UVs[1] - UVs[0];
                const FVector2f Edge2 = UVs[2] - UVs[0];
                const float Determinant = Edge1.X * Edge2.Y - Edge1.Y * Edge2.X;
                if (!FMath::IsFinite(Determinant) || FMath::Abs(Determinant) <= 1.e-12f)
                {
                    ++OutResult.RejectedTriangleCount;
                    continue;
                }
                // 无论顶点是否落在脸区，均在三角形内部插值后判断，保留下颌等边界覆盖。
                const float MinU = FMath::Min3(UVs[0].X, UVs[1].X, UVs[2].X);
                const float MaxU = FMath::Max3(UVs[0].X, UVs[1].X, UVs[2].X);
                const float MinV = FMath::Min3(UVs[0].Y, UVs[1].Y, UVs[2].Y);
                const float MaxV = FMath::Max3(UVs[0].Y, UVs[1].Y, UVs[2].Y);
                if (MaxU - MinU > 8.0f || MaxV - MinV > 8.0f
                    || FMath::Max(FMath::Abs(MinU), FMath::Abs(MaxU)) > 1.e6f
                    || FMath::Max(FMath::Abs(MinV), FMath::Abs(MaxV)) > 1.e6f)
                {
                    ++OutResult.RejectedTriangleCount;
                    continue;
                }
                bool bTriangleContributed = false;
                // 对原始 UV 三角形逐 tile 栅格化，不能先对三个顶点分别 frac，否则会破坏跨边界三角形。
                for (int32 TileY = FMath::FloorToInt(MinV); TileY <= FMath::FloorToInt(MaxV); ++TileY)
                for (int32 TileX = FMath::FloorToInt(MinU); TileX <= FMath::FloorToInt(MaxU); ++TileX)
                {
                    const int32 X0 = FMath::Clamp(FMath::CeilToInt((MinU - TileX) * Size - 0.5f), 0, Size - 1);
                    const int32 X1 = FMath::Clamp(FMath::FloorToInt((MaxU - TileX) * Size - 0.5f), 0, Size - 1);
                    const int32 Y0 = FMath::Clamp(FMath::CeilToInt((MinV - TileY) * Size - 0.5f), 0, Size - 1);
                    const int32 Y1 = FMath::Clamp(FMath::FloorToInt((MaxV - TileY) * Size - 0.5f), 0, Size - 1);
                    for (int32 Y = Y0; Y <= Y1; ++Y)
                    for (int32 X = X0; X <= X1; ++X)
                    {
                        const FVector2f Delta((X + 0.5f) / Size + TileX - UVs[0].X,
                                              (Y + 0.5f) / Size + TileY - UVs[0].Y);
                        const float W1 = (Delta.X * Edge2.Y - Delta.Y * Edge2.X) / Determinant;
                        const float W2 = (Edge1.X * Delta.Y - Edge1.Y * Delta.X) / Determinant;
                        const float W0 = 1.0f - W1 - W2;
                        if (W0 < -1.e-5f || W1 < -1.e-5f || W2 < -1.e-5f) continue;
                        const FVector Position = Positions[0] * W0 + Positions[1] * W1 + Positions[2] * W2;
                        const FVector Normal = (Normals[0] * W0 + Normals[1] * W1 + Normals[2] * W2).GetSafeNormal();
                        if (!IsFacePoint(Position, Normal, Settings, Forward, Up)) continue;
                        bTriangleContributed = true;
                        const int32 TexelIndex = Y * Size + X;
                        const int32 ByteIndex = TexelIndex * 4;
                        if (TexelState[TexelIndex] == 2) continue;
                        const FVector SphereNormal = (Position - Settings.HeadCenter).GetSafeNormal();
                        const FVector BlendedNormal = (SphereNormal * (1.0f - OutResult.GeometryNormalWeight)
                            + Normal * OutResult.GeometryNormalWeight).GetSafeNormal();
                        const FVector DirectionNormal = BlendedNormal.IsNearlyZero() ? Normal : BlendedNormal;
                        const FVector LocalOffset = (Position - Settings.HeadCenter) / Settings.HeadRadius;
                        const FVector3f LocalPosition(static_cast<float>(FVector::DotProduct(LocalOffset, Right)),
                            static_cast<float>(FVector::DotProduct(LocalOffset, Forward)), static_cast<float>(FVector::DotProduct(LocalOffset, Up)));
                        const FVector3f LocalDirection = FVector3f(
                            static_cast<float>(FVector::DotProduct(DirectionNormal, Right)),
                            static_cast<float>(FVector::DotProduct(DirectionNormal, Forward)),
                            static_cast<float>(FVector::DotProduct(DirectionNormal, Up))).GetSafeNormal();
                        if (!MergeFaceTexel(OutResult.Pixels.GetData() + ByteIndex, TexelLocalPosition[TexelIndex],
                            TexelLocalDirection[TexelIndex], TexelState[TexelIndex] == 1, LocalPosition, LocalDirection,
                            NormalToleranceCos, Settings.MirrorPositionTolerance, Settings.MirrorSideMinimum))
                        {
                            TexelState[TexelIndex] = 2;
                            FMemory::Memzero(OutResult.Pixels.GetData() + ByteIndex, 4);
                            ++OutResult.ConflictingTexelCount;
                            continue;
                        }
                        TexelState[TexelIndex] = 1;
                        OutResult.Pixels[ByteIndex + 3] = 255;
                    }
                }
                if (bTriangleContributed)
                {
                    ++OutResult.ConsideredTriangleCount;
                    for (const uint32 Index : Indices) ConsideredVertices[Index] = true;
                    OutResult.BakedMaterialIndices.AddUnique(MaterialIndex);
                }
            }
        }
        for (int32 TexelIndex = 0; TexelIndex < TexelCount; ++TexelIndex)
        {
            if (TexelState[TexelIndex] == 1)
            {
                ++OutResult.FaceTexelCount;
                if (OutResult.Pixels[TexelIndex * 4 + 2] == 255) ++OutResult.MirroredTexelCount;
                const FVector3f Local = TexelLocalDirection[TexelIndex];
                const FVector Direction = Right * Local.X + Forward * Local.Y + Up * Local.Z;
                const bool bFrontDark = FVector::DotProduct(Direction, FrontLight) <= 0.0f;
                if (bFrontDark) ++OutResult.FrontDarkTexelCount;
                else ++OutResult.LitTexelCount;
                // 旧固定仰角仅作已保留方向的诊断，绝不参与 V4 编码或有效覆盖判定。
                bool bRightNonContinuous = false;
                bool bLeftNonContinuous = false;
                SideThreshold(Direction, Forward, Right, Up, 1.0f, bRightNonContinuous);
                SideThreshold(Direction, Forward, Right, Up, -1.0f, bLeftNonContinuous);
                if (bRightNonContinuous || bLeftNonContinuous) ++OutResult.NonContinuousTexelCount;
            }
        }
        for (TConstSetBitIterator<> It(ConsideredVertices); It; ++It) ++OutResult.ConsideredVertexCount;
        if (OutResult.FaceTexelCount == 0)
        {
            OutResult.Error = TEXT("没有有效脸部三角形覆盖，保持普通 toon 回退");
            return false;
        }

        // 只扩展方向与镜像类别，不扩大有效遮罩；冲突像素永远不作扩展来源。
        for (int32 Pass = 0; Pass < FMath::Clamp(Settings.PaddingTexels, 0, 8); ++Pass)
        {
            TArray<int32> PendingDestinations;
            TArray<int32> PendingSources;
            for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
            {
                const int32 Dest = Y * Size + X;
                if (TexelState[Dest] != 0) continue;
                for (int32 DY = -1; DY <= 1; ++DY)
                {
                    bool bFound = false;
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const int32 SX = X + DX;
                        const int32 SY = Y + DY;
                        if (SX < 0 || SX >= Size || SY < 0 || SY >= Size) continue;
                        const int32 Source = SY * Size + SX;
                        if (TexelState[Source] != 1 && TexelState[Source] != 3) continue;
                        PendingDestinations.Add(Dest);
                        PendingSources.Add(Source);
                        bFound = true;
                        break;
                    }
                    if (bFound) break;
                }
            }
            for (int32 Index = 0; Index < PendingDestinations.Num(); ++Index)
            {
                const int32 Dest = PendingDestinations[Index];
                const int32 Source = PendingSources[Index];
                FMemory::Memcpy(OutResult.Pixels.GetData() + Dest * 4, OutResult.Pixels.GetData() + Source * 4, 3);
                TexelState[Dest] = 3;
                ++OutResult.PaddingTexelCount;
            }
        }

        OutResult.Size = Size;
        OutResult.bSuccess = true;
        return true;
    }

    /** 写出未压缩 32 位 TGA，便于在引擎外查看中间结果。 */
    bool WriteTga(const FBakeResult& Result, const FString& FilePath)
    {
        if (!Result.bSuccess || Result.Size <= 0 || Result.Size > 4096
            || Result.Pixels.Num() != Result.Size * Result.Size * 4)
            return false;
        const int32 Size = Result.Size;
        TArray<uint8> File;
        File.Reserve(18 + Size * Size * 4);
        // TGA 头：无颜色表、未压缩真彩、32 位 BGRA。
        const uint8 Header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                static_cast<uint8>(Size & 0xFF), static_cast<uint8>((Size >> 8) & 0xFF),
                                static_cast<uint8>(Size & 0xFF), static_cast<uint8>((Size >> 8) & 0xFF),
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
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
        return FFileHelper::SaveArrayToFile(File, *FilePath);
    }

    static bool InitializeTexture(const FBakeResult& Result, UTexture2D* Texture, FString& OutError)
    {
        if (!Texture || !Result.bSuccess || Result.Size <= 0 || Result.Size > 4096
            || Result.DataVersion != DirectionFieldVersion
            || Result.Pixels.Num() != Result.Size * Result.Size * 4 || Result.FaceTexelCount <= 0)
        {
            OutError = TEXT("烘焙结果为空或不是 V4 几何方向场，拒绝写出贴图");
            return false;
        }
        // Result 为 RGBA；UE 的 TSF_BGRA8 必须显式交换 R/B，不能直接复制源数组。
        TArray<uint8> SourceBGRA;
        SourceBGRA.SetNumUninitialized(Result.Pixels.Num());
        for (int32 ByteIndex = 0; ByteIndex < Result.Pixels.Num(); ByteIndex += 4)
        {
            SourceBGRA[ByteIndex] = Result.Pixels[ByteIndex + 2];
            SourceBGRA[ByteIndex + 1] = Result.Pixels[ByteIndex + 1];
            SourceBGRA[ByteIndex + 2] = Result.Pixels[ByteIndex];
            SourceBGRA[ByteIndex + 3] = Result.Pixels[ByteIndex + 3];
        }
        Texture->Source.Init(Result.Size, Result.Size, 1, 1, TSF_BGRA8, SourceBGRA.GetData());
        Texture->SRGB = false;
        Texture->CompressionSettings = TC_VectorDisplacementmap;
        // oct 和镜像类别不做有损压缩或 mip 混合；shader 四 tap 分别解码后插值。
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->Filter = TF_Nearest;
        Texture->VirtualTextureStreaming = false;
        Texture->NeverStream = true;
        Texture->LODGroup = TEXTUREGROUP_Character;
        const FString VersionTag = FString::FromInt(UMMDEndfieldFaceData::CurrentVersion);
        Texture->GetOutermost()->GetMetaData().SetValue(Texture, TEXT("MMDGeneratedFaceDirectionVersion"), *VersionTag);
        Texture->GetOutermost()->GetMetaData().SetValue(Texture, TEXT("MMDFaceDirectionEncoding"), TEXT("oct-unorm8-right-forward-up-mirror-positive-position-side"));
        Texture->PostEditChange();
        UTexture* PendingTextures[] = { Texture };
        FTextureCompilingManager::Get().FinishCompilation(PendingTextures);
        Texture->UpdateResource();
        OutError.Reset();
        return true;
    }

    UTexture2D* CreateTexture(const FBakeResult& Result, UObject* Outer, FName Name,
                             EObjectFlags Flags, FString& OutError)
    {
        UTexture2D* Texture = NewObject<UTexture2D>(Outer ? Outer : GetTransientPackage(), Name, Flags);
        return InitializeTexture(Result, Texture, OutError) ? Texture : nullptr;
    }

    bool SaveAsTexture(const FBakeResult& Result, const FString& PackageName,
                       const FString& AssetName, FString& OutError)
    {
        if (!FPackageName::IsValidLongPackageName(PackageName) || AssetName.IsEmpty()
            || FPackageName::GetShortName(PackageName) != AssetName)
        {
            OutError = TEXT("方向场资产需要完整包名，且 AssetName 必须与包短名一致");
            return false;
        }
        const bool bExists = FPackageName::DoesPackageExist(PackageName);
        UTexture2D* Texture = bExists
            ? LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName))
            : nullptr;
        if (Texture) Texture->GetOutermost()->FullyLoad();
        if (bExists && !Texture)
        {
            OutError = FString::Printf(TEXT("无法加载已存在的方向贴图：%s"), *PackageName);
            return false;
        }
        if (Texture && Texture->GetOutermost()->GetMetaData().GetValue(Texture, TEXT("MMDGeneratedFaceDirectionVersion"))
            != FString::FromInt(DirectionFieldVersion))
        {
            OutError = TEXT("已有贴图不是本工具 V4 方向缓存，保留旧阈值图或用户资产：") + PackageName;
            return false;
        }
        if (!Texture)
        {
            Texture = CreateTexture(Result, CreatePackage(*PackageName), FName(*AssetName), RF_Public | RF_Standalone, OutError);
            if (!Texture)
            {
                return false;
            }
            FAssetRegistryModule::AssetCreated(Texture);
        }
        else if (!InitializeTexture(Result, Texture, OutError)) return false;

        const FString Filename = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        Texture->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Texture->GetOutermost(), Texture, *Filename, Args))
        {
            OutError = FString::Printf(TEXT("无法保存方向贴图：%s"), *PackageName);
            return false;
        }
        return true;
    }
}
