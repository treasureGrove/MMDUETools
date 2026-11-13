// V5：计算头投影、frontmost 外脸、连续左右角阈值；不修改网格 UV，不烘实际遮挡。
#include "Editor/MMDEndfieldFaceSdfBaker.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/MMDEndfieldHeadFrame.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshTypes.h"
#include "SkinnedAssetCompiler.h"
#include "Stage/MMDStageLibrary.h"
#include "TextureCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace MMDEnfieldFaceSdf
{
    static_assert(DirectionFieldVersion == 5, "V5 head-projected angular threshold encoding required");

    struct FV5FaceCpuGeometry
    {
        const FSkeletalMeshLODRenderData* LOD = nullptr;
        const FPositionVertexBuffer* Positions = nullptr;
        const FStaticMeshVertexBuffer* Vertices = nullptr;
        const FRawStaticIndexBuffer16or32Interface* Indices = nullptr;
        int32 VertexCount = 0;
    };

    static bool BuildHeadFrame(const FBakeSettings& Settings, FVector& Forward, FVector& Right, FVector& Up)
    {
        Forward = Settings.HeadForward.GetSafeNormal();
        Right = (Settings.HeadRight - Forward * FVector::DotProduct(Settings.HeadRight, Forward)).GetSafeNormal();
        Up = FVector::CrossProduct(Right, Forward).GetSafeNormal();
        return !Forward.ContainsNaN() && !Right.ContainsNaN() && !Up.ContainsNaN()
            && !Forward.IsNearlyZero() && !Right.IsNearlyZero() && !Up.IsNearlyZero();
    }

    static bool ReadV5FaceGeometry(const USkeletalMesh* Mesh, FV5FaceCpuGeometry& Geometry, FString& Error)
    {
        if (!IsValid(Mesh)) { Error = TEXT("骨骼网格为空"); return false; }
        USkinnedAsset* Pending[] = { const_cast<USkeletalMesh*>(Mesh) };
        FSkinnedAssetCompilingManager::Get().FinishCompilation(Pending);
        const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
        if (!Render || Render->LODRenderData.IsEmpty())
        { Error = TEXT("缺少 LOD0 渲染数据"); return false; }
        Geometry.LOD = &Render->LODRenderData[0];
        Geometry.Positions = &Geometry.LOD->StaticVertexBuffers.PositionVertexBuffer;
        Geometry.Vertices = &Geometry.LOD->StaticVertexBuffers.StaticMeshVertexBuffer;
        Geometry.VertexCount = static_cast<int32>(Geometry.Positions->GetNumVertices());
        if (Geometry.VertexCount <= 0 || Geometry.Vertices->GetNumVertices() != Geometry.Positions->GetNumVertices()
            || !Geometry.Positions->GetVertexData() || !Geometry.Vertices->GetTangentData())
        { Error = TEXT("LOD0 没有可读取的 CPU 位置/法线"); return false; }
        if (!Geometry.LOD->MultiSizeIndexContainer.IsIndexBufferValid())
        { Error = TEXT("LOD0 三角形索引为空"); return false; }
        Geometry.Indices = Geometry.LOD->MultiSizeIndexContainer.GetIndexBuffer();
        if (!Geometry.Indices || Geometry.Indices->Num() == 0 || Geometry.Indices->GetResourceDataSize() == 0)
        { Error = TEXT("LOD0 三角形索引缺少 CPU 数据"); return false; }
        // 只读位置/法线，不要求 UV0 或任何附加 UV 的 CPU 副本。
        return true;
    }

    static int32 V5SectionMaterial(const USkeletalMesh* Mesh, int32 SectionIndex, const FSkelMeshRenderSection& Section)
    {
        const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(0);
        return Info && Info->LODMaterialMap.IsValidIndex(SectionIndex) && Info->LODMaterialMap[SectionIndex] != INDEX_NONE
            ? Info->LODMaterialMap[SectionIndex] : static_cast<int32>(Section.MaterialIndex);
    }

    static bool CollectV5SkinMaterials(const USkeletalMesh* Mesh, const FBakeSettings& Settings,
        bool OpaqueOnly, TSet<int32>& Materials, TArray<int32>* SkippedTransparent, FString& Error)
    {
        TStrongObjectPtr<USkeletalMeshComponent> Classifier(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
        Classifier->SetSkeletalMesh(const_cast<USkeletalMesh*>(Mesh));
        TArray<int32> Candidates = Settings.MaterialIndices;
        if (Candidates.IsEmpty() && Settings.bAutoDetectFaceMaterials)
            for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot) Candidates.Add(Slot);
        for (const int32 Slot : Candidates)
        {
            if (!Mesh->GetMaterials().IsValidIndex(Slot))
            { Error = FString::Printf(TEXT("材质槽索引无效：%d"), Slot); return false; }
            if (UMMDStageLibrary::GetResolvedCharacterMaterialRole(Classifier.Get(), Slot) != EMMDMaterialRole::Skin) continue;
            const UMaterialInterface* Material = Mesh->GetMaterials()[Slot].MaterialInterface;
            if (OpaqueOnly && (!Material || (Material->GetBlendMode() != BLEND_Opaque && Material->GetBlendMode() != BLEND_Masked)))
            {
                if (SkippedTransparent) SkippedTransparent->AddUnique(Slot);
                continue;
            }
            Materials.Add(Slot);
        }
        if (Materials.IsEmpty())
        {
            Error = OpaqueOnly ? TEXT("没有可绑定的 opaque/masked Skin 槽；透明脸尚未接 V5，保持原路径")
                : TEXT("没有可靠 Skin 几何候选，使用纯骨骼框架");
            return false;
        }
        return true;
    }

    static double V5Quantile(const TArray<double>& Sorted, double Fraction)
    {
        const double Index = Fraction * (Sorted.Num() - 1);
        const int32 First = FMath::FloorToInt(Index);
        const int32 Last = FMath::Min(First + 1, Sorted.Num() - 1);
        return FMath::Lerp(Sorted[First], Sorted[Last], Index - First);
    }

    bool DeriveHeadFrame(const USkeletalMesh* Mesh, const FName& HeadBoneName,
        FBakeSettings& Settings, EHeadFrameSource& Source, FString& Error)
    {
        Source = EHeadFrameSource::Failed;
        Error.Reset();
        if (!IsValid(Mesh)) { Error = TEXT("骨骼网格为空"); return false; }
        USkinnedAsset* Pending[] = { const_cast<USkeletalMesh*>(Mesh) };
        FSkinnedAssetCompilingManager::Get().FinishCompilation(Pending);
        FMMDReferenceHeadFrame BoneFrame;
        if (!MMDEndfieldHeadFrame::BuildSkeletonFrame(Mesh, HeadBoneName, BoneFrame, Error)) return false;
        TArray<FTransform> Reference = Mesh->GetRefSkeleton().GetRefBonePose();
        for (int32 Index = 0; Index < Reference.Num(); ++Index)
        {
            const int32 Parent = Mesh->GetRefSkeleton().GetParentIndex(Index);
            if (Parent != INDEX_NONE) Reference[Index] *= Reference[Parent];
        }
        const FVector BoneCenter = Reference[BoneFrame.HeadIndex].GetLocation();
        const double BoneRadius = BoneFrame.Radius;
        Settings.HeadForward = BoneFrame.ForwardRef;
        Settings.HeadRight = BoneFrame.RightRef;
        Settings.HeadUp = BoneFrame.UpRef;
        Settings.HeadCenter = BoneCenter;
        Settings.HeadRadius = BoneFrame.Radius;
        Settings.UVChannel = -1;
        Source = EHeadFrameSource::HeadBone;

        FV5FaceCpuGeometry Geometry;
        TSet<int32> Materials;
        FString FitMessage;
        if (!ReadV5FaceGeometry(Mesh, Geometry, FitMessage)
            || !CollectV5SkinMaterials(Mesh, Settings, false, Materials, nullptr, FitMessage))
        { Error = TEXT("V5 使用头骨原点兜底：") + FitMessage; return true; }
        TBitArray<> Seen(false, Geometry.VertexCount);
        TArray<double> RightValues, ForwardValues, UpValues;
        for (int32 SectionIndex = 0; SectionIndex < Geometry.LOD->RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = Geometry.LOD->RenderSections[SectionIndex];
            if (Section.bDisabled || !Materials.Contains(V5SectionMaterial(Mesh, SectionIndex, Section))) continue;
            if (static_cast<uint64>(Section.BaseIndex) + static_cast<uint64>(Section.NumTriangles) * 3 > static_cast<uint64>(Geometry.Indices->Num()))
            { Error = TEXT("V5 拟合 section 索引越界，保留纯骨骼框架"); return true; }
            for (uint32 Corner = 0; Corner < Section.NumTriangles * 3; ++Corner)
            {
                const uint32 Index = Geometry.Indices->Get(Section.BaseIndex + Corner);
                if (Index >= static_cast<uint32>(Geometry.VertexCount) || Seen[Index]) continue;
                Seen[Index] = true;
                const FVector Position(Geometry.Positions->VertexPosition(Index));
                const FVector4f TangentZ = Geometry.Vertices->VertexTangentZ(Index);
                const FVector Normal = FVector(TangentZ.X, TangentZ.Y, TangentZ.Z).GetSafeNormal();
                const FVector Offset = Position - BoneCenter;
                const double X = FVector::DotProduct(Offset, BoneFrame.RightRef);
                const double Y = FVector::DotProduct(Offset, BoneFrame.ForwardRef);
                const double Z = FVector::DotProduct(Offset, BoneFrame.UpRef);
                if (Position.ContainsNaN() || Normal.ContainsNaN() || Normal.IsNearlyZero()
                    || FMath::Abs(X) >= 1.5 * BoneRadius || Z <= -0.65 * BoneRadius || Z >= 2.25 * BoneRadius
                    || Offset.SizeSquared() >= FMath::Square(2.5 * BoneRadius)
                    || FVector::DotProduct(Normal, BoneFrame.ForwardRef) <= 0.05) continue;
                RightValues.Add(X); ForwardValues.Add(Y); UpValues.Add(Z);
            }
        }
        if (RightValues.Num() < 32)
        { Error = TEXT("V5 头附近外脸候选不足，保留头骨原点与骨尺度"); return true; }
        RightValues.Sort(); ForwardValues.Sort(); UpValues.Sort();
        const double Right05 = V5Quantile(RightValues, 0.05), Right95 = V5Quantile(RightValues, 0.95);
        const double Forward05 = V5Quantile(ForwardValues, 0.05);
        const double Up05 = V5Quantile(UpValues, 0.05), Up95 = V5Quantile(UpValues, 0.95);
        const double RequestedRadius = FMath::Max3(BoneRadius, 0.6 * (Up95 - Up05), 0.6 * (Right95 - Right05));
        const double Radius = FMath::Min(RequestedRadius, 2.0 * BoneRadius);
        const FVector Center = BoneCenter + BoneFrame.ForwardRef * (Forward05 - 0.25 * BoneRadius)
            + BoneFrame.UpRef * ((Up05 + Up95) * 0.5);
        if (!FMath::IsFinite(RequestedRadius) || !FMath::IsFinite(Radius) || Center.ContainsNaN())
        { Error = TEXT("V5 几何拟合超出可靠骨尺度，保留纯骨骼框架"); return true; }
        // Right 沿头骨/双眼中线，不用顶点密度偏置的中位数移动脸部中线。
        Settings.HeadCenter = Center;
        Settings.HeadRadius = static_cast<float>(Radius);
        Error = FString::Printf(TEXT("V5 骨尺度 %.4f，%d 个外脸候选拟合半径 %.4f；未读取全模型 bounds"),
            BoneRadius, RightValues.Num(), Radius);
        return true;
    }

    static bool IsV5FacePoint(const FVector& Position, const FVector& Normal,
        const FBakeSettings& Settings, const FVector& Forward, const FVector& Right, const FVector& Up)
    {
        const FVector Offset = (Position - Settings.HeadCenter) / Settings.HeadRadius;
        const double X = FVector::DotProduct(Offset, Right), Z = FVector::DotProduct(Offset, Up);
        return !Position.ContainsNaN() && !Normal.ContainsNaN() && !Normal.IsNearlyZero()
            && FVector::DotProduct(Normal, Forward) > Settings.FaceNormalThreshold
            && FVector::DotProduct(Offset, Forward) > Settings.FaceForwardMinimum
            && FMath::Abs(X) <= 1.5 && Z > Settings.ChinHeight && Z < Settings.CrownHeight
            && Offset.SizeSquared() <= FMath::Square(Settings.MaximumHeadDistance);
    }

    static uint8 EncodeUnit(float Value)
    {
        return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f));
    }

    bool BakeFaceSdf(USkeletalMesh* Mesh, const FBakeSettings& Settings, FBakeResult& Result)
    {
        Result = FBakeResult();
        if (Settings.UVChannel != -1 || !FMath::IsFinite(Settings.HeadRadius) || Settings.HeadRadius <= KINDA_SMALL_NUMBER
            || Settings.HeadCenter.ContainsNaN() || !FMath::IsFinite(Settings.FaceNormalThreshold)
            || !FMath::IsFinite(Settings.FaceForwardMinimum) || !FMath::IsFinite(Settings.ChinHeight)
            || !FMath::IsFinite(Settings.CrownHeight) || Settings.ChinHeight >= Settings.CrownHeight
            || !FMath::IsFinite(Settings.MaximumHeadDistance) || Settings.MaximumHeadDistance <= 0.0f)
        { Result.Error = TEXT("V5 需要有效头部框架及 UVChannel=-1 计算投影"); return false; }
        FVector Forward, Right, Up;
        if (!BuildHeadFrame(Settings, Forward, Right, Up))
        { Result.Error = TEXT("头部参考轴退化"); return false; }
        FV5FaceCpuGeometry Geometry;
        if (!ReadV5FaceGeometry(Mesh, Geometry, Result.Error)) return false;
        TSet<int32> Materials;
        if (!CollectV5SkinMaterials(Mesh, Settings, true, Materials, &Result.SkippedTranslucentMaterialIndices, Result.Error)) return false;
        if (Materials.Num() != 1)
        { Result.Error = TEXT("V5 需要单 Skin 槽独立投影；自动多槽请由 BakeAndBind 逐槽调用，避免跨槽 frontmost 混用"); return false; }
        const int32 Size = FMath::Clamp(Settings.TextureSize, 64, 4096), Count = Size * Size;
        TArray<uint8> State;
        State.SetNumZeroed(Count);
        TArray<float> FrontDepth;
        FrontDepth.Init(-TNumericLimits<float>::Max(), Count);
        TArray<float> BackDepth;
        BackDepth.Init(TNumericLimits<float>::Max(), Count);
        TBitArray<> Considered(false, Geometry.VertexCount);
        Result.Pixels.SetNumZeroed(Count * 4);
        Result.GeometryNormalWeight = 0.0f;
        const float DepthTolerance = FMath::Max(Settings.HeadRadius * 0.0001f, UE_SMALL_NUMBER);
        for (int32 SectionIndex = 0; SectionIndex < Geometry.LOD->RenderSections.Num(); ++SectionIndex)
        {
            const FSkelMeshRenderSection& Section = Geometry.LOD->RenderSections[SectionIndex];
            const int32 Slot = V5SectionMaterial(Mesh, SectionIndex, Section);
            if (Section.bDisabled || !Materials.Contains(Slot)) continue;
            if (static_cast<uint64>(Section.BaseIndex) + static_cast<uint64>(Section.NumTriangles) * 3 > static_cast<uint64>(Geometry.Indices->Num()))
            { Result.Error = TEXT("LOD0 section 索引越界"); return false; }
            for (uint32 Triangle = 0; Triangle < Section.NumTriangles; ++Triangle)
            {
                FVector Positions[3], Normals[3]; FVector2f UV[3]; uint32 Indices[3];
                bool Valid = true;
                for (int32 Corner = 0; Corner < 3; ++Corner)
                {
                    const uint32 Index = Geometry.Indices->Get(Section.BaseIndex + Triangle * 3 + Corner);
                    Indices[Corner] = Index;
                    if (Index >= static_cast<uint32>(Geometry.VertexCount)) { Valid = false; break; }
                    Positions[Corner] = FVector(Geometry.Positions->VertexPosition(Index));
                    const FVector4f TangentZ = Geometry.Vertices->VertexTangentZ(Index);
                    Normals[Corner] = FVector(TangentZ.X, TangentZ.Y, TangentZ.Z).GetSafeNormal();
                    const FVector Offset = Positions[Corner] - Settings.HeadCenter;
                    UV[Corner] = FVector2f(static_cast<float>(0.5 + FVector::DotProduct(Offset, Right) / (3.0 * Settings.HeadRadius)),
                        static_cast<float>(0.5 - FVector::DotProduct(Offset, Up) / (3.0 * Settings.HeadRadius)));
                    if (Positions[Corner].ContainsNaN() || Normals[Corner].ContainsNaN()
                        || !FMath::IsFinite(UV[Corner].X) || !FMath::IsFinite(UV[Corner].Y)) Valid = false;
                }
                if (!Valid) { ++Result.RejectedTriangleCount; continue; }
                const FVector2f Edge1 = UV[1] - UV[0], Edge2 = UV[2] - UV[0];
                const float Determinant = Edge1.X * Edge2.Y - Edge1.Y * Edge2.X;
                if (!FMath::IsFinite(Determinant) || FMath::Abs(Determinant) <= 1.e-12f)
                { ++Result.RejectedTriangleCount; continue; }
                const float MinU = FMath::Min3(UV[0].X, UV[1].X, UV[2].X), MaxU = FMath::Max3(UV[0].X, UV[1].X, UV[2].X);
                const float MinV = FMath::Min3(UV[0].Y, UV[1].Y, UV[2].Y), MaxV = FMath::Max3(UV[0].Y, UV[1].Y, UV[2].Y);
                if (MinU < 0.0f || MaxU > 1.0f || MinV < 0.0f || MaxV > 1.0f) ++Result.ProjectedClippedTriangleCount;
                if (MaxU < 0.0f || MinU > 1.0f || MaxV < 0.0f || MinV > 1.0f) continue;
                const int32 X0 = FMath::Clamp(FMath::CeilToInt(FMath::Clamp(MinU, 0.0f, 1.0f) * Size - 0.5f), 0, Size - 1);
                const int32 X1 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(MaxU, 0.0f, 1.0f) * Size - 0.5f), 0, Size - 1);
                const int32 Y0 = FMath::Clamp(FMath::CeilToInt(FMath::Clamp(MinV, 0.0f, 1.0f) * Size - 0.5f), 0, Size - 1);
                const int32 Y1 = FMath::Clamp(FMath::FloorToInt(FMath::Clamp(MaxV, 0.0f, 1.0f) * Size - 0.5f), 0, Size - 1);
                bool Contributed = false;
                for (int32 Y = Y0; Y <= Y1; ++Y)
                for (int32 X = X0; X <= X1; ++X)
                {
                    const FVector2f Delta((X + 0.5f) / Size - UV[0].X, (Y + 0.5f) / Size - UV[0].Y);
                    const float W1 = (Delta.X * Edge2.Y - Delta.Y * Edge2.X) / Determinant;
                    const float W2 = (Edge1.X * Delta.Y - Edge1.Y * Delta.X) / Determinant, W0 = 1.0f - W1 - W2;
                    if (W0 < -1.e-5f || W1 < -1.e-5f || W2 < -1.e-5f) continue;
                    const FVector Position = Positions[0] * W0 + Positions[1] * W1 + Positions[2] * W2;
                    const FVector Normal = (Normals[0] * W0 + Normals[1] * W1 + Normals[2] * W2).GetSafeNormal();
                    if (!IsV5FacePoint(Position, Normal, Settings, Forward, Right, Up)) continue;
                    Contributed = true;
                    const int32 Pixel = Y * Size + X, Byte = Pixel * 4;
                    const FVector Offset = Position - Settings.HeadCenter;
                    const float Depth = static_cast<float>(FVector::DotProduct(Offset, Forward));
                    BackDepth[Pixel] = FMath::Min(BackDepth[Pixel], Depth);
                    if (State[Pixel] == 1 && Depth <= FrontDepth[Pixel]) continue;
                    FrontDepth[Pixel] = Depth;
                    const float PositionRight = static_cast<float>(FVector::DotProduct(Offset, Right));
                    const float Theta = FMath::Atan2(PositionRight, FMath::Max(Depth, Settings.HeadRadius * 0.6f));
                    Result.Pixels[Byte] = EncodeUnit(FMath::Clamp(0.5f + Theta / PI, 0.05f, 0.95f));
                    Result.Pixels[Byte + 1] = EncodeUnit(FMath::Clamp(0.5f - Theta / PI, 0.05f, 0.95f));
                    Result.Pixels[Byte + 2] = EncodeUnit(0.5f + Depth / (3.0f * Settings.HeadRadius));
                    Result.Pixels[Byte + 3] = 255;
                    State[Pixel] = 1;
                }
                if (Contributed)
                {
                    ++Result.ConsideredTriangleCount;
                    for (const uint32 Index : Indices) Considered[Index] = true;
                    Result.BakedMaterialIndices.AddUnique(Slot);
                }
            }
        }
        for (int32 Pixel = 0; Pixel < Count; ++Pixel)
        {
            if (State[Pixel] != 1) continue;
            ++Result.FaceTexelCount;
            if (FrontDepth[Pixel] - BackDepth[Pixel] > DepthTolerance) ++Result.ProjectedOccludedTexelCount;
            const float EncodedDepth = 0.5f + FrontDepth[Pixel] / (3.0f * Settings.HeadRadius);
            if (EncodedDepth < 0.0f || EncodedDepth > 1.0f) ++Result.DepthClampedTexelCount;
        }
        for (TConstSetBitIterator<> It(Considered); It; ++It) ++Result.ConsideredVertexCount;
        if (Result.FaceTexelCount == 0)
        { Result.Error = TEXT("没有有效头投影外脸覆盖，保持普通 Toon 回退"); return false; }
        // 两侧 RG 均严格大于零，因此正前 yaw=0 全亮；不是对场景阴影的统计。
        Result.LitTexelCount = Result.FaceTexelCount;
        for (int32 Pass = 0; Pass < FMath::Clamp(Settings.PaddingTexels, 0, 8); ++Pass)
        {
            TArray<int32> Destinations, Sources;
            for (int32 Y = 0; Y < Size; ++Y)
            for (int32 X = 0; X < Size; ++X)
            {
                const int32 Dest = Y * Size + X;
                if (State[Dest] != 0) continue;
                bool Found = false;
                for (int32 DY = -1; DY <= 1 && !Found; ++DY)
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    const int32 SX = X + DX, SY = Y + DY;
                    if (SX < 0 || SX >= Size || SY < 0 || SY >= Size) continue;
                    const int32 Source = SY * Size + SX;
                    if (State[Source] != 1 && State[Source] != 3) continue;
                    Destinations.Add(Dest); Sources.Add(Source); Found = true; break;
                }
            }
            for (int32 Index = 0; Index < Destinations.Num(); ++Index)
            {
                FMemory::Memcpy(Result.Pixels.GetData() + Destinations[Index] * 4, Result.Pixels.GetData() + Sources[Index] * 4, 3);
                State[Destinations[Index]] = 3;
                ++Result.PaddingTexelCount;
            }
        }
        Result.Size = Size;
        Result.bSuccess = true;
        return true;
    }

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
            OutError = TEXT("烘焙结果为空或不是 V5 几何角度阈值场，拒绝写出贴图");
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
        // 阈值、frontmost 深度和覆盖不做有损压缩或 mip 混合；读取端必须检查 A 和 B。
        Texture->MipGenSettings = TMGS_NoMipmaps;
        Texture->AddressX = TA_Clamp;
        Texture->AddressY = TA_Clamp;
        Texture->Filter = TF_Nearest;
        Texture->VirtualTextureStreaming = false;
        Texture->NeverStream = true;
        Texture->LODGroup = TEXTUREGROUP_Character;
        const FString VersionTag = FString::FromInt(DirectionFieldVersion);
        Texture->GetOutermost()->GetMetaData().SetValue(Texture, TEXT("MMDGeneratedFaceDirectionVersion"), *VersionTag);
        Texture->GetOutermost()->GetMetaData().SetValue(Texture, TEXT("MMDFaceDirectionEncoding"), TEXT("yaw-threshold-rg-front-depth-b-mask-a-head-planar-v5"));
        Texture->GetOutermost()->GetMetaData().SetValue(Texture, TEXT("MMDFaceProjection"), TEXT("uv=(.5+right/(3R),.5-up/(3R));uv-channel=-1;no-wrap"));
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
            OutError = TEXT("角度阈值场资产需要完整包名，且 AssetName 必须与包短名一致");
            return false;
        }
        const bool bExists = FPackageName::DoesPackageExist(PackageName);
        UTexture2D* Texture = bExists
            ? LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName))
            : nullptr;
        if (Texture) Texture->GetOutermost()->FullyLoad();
        if (bExists && !Texture)
        {
            OutError = FString::Printf(TEXT("无法加载已存在的角度阈值贴图：%s"), *PackageName);
            return false;
        }
        if (Texture && Texture->GetOutermost()->GetMetaData().GetValue(Texture, TEXT("MMDGeneratedFaceDirectionVersion"))
            != FString::FromInt(DirectionFieldVersion))
        {
            OutError = TEXT("已有贴图不是本工具 V5 角度阈值缓存，保留旧阈值图或用户资产：") + PackageName;
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
            OutError = FString::Printf(TEXT("无法保存角度阈值贴图：%s"), *PackageName);
            return false;
        }
        return true;
    }
}
