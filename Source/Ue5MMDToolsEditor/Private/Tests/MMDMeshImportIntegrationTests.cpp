#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "IImageWrapperModule.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MMDModelDataAsset.h"
#include "MMDPhysicsSimulator.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TMMDMeshBuilder.h"
#include "TVMDParser.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace MMDMeshImportIntegrationTests
{
    struct FSandboxMount
    {
        FString Root;
        FString Directory;

        FSandboxMount(const FString& InRoot, const FString& InDirectory)
            : Root(InRoot), Directory(InDirectory)
        {
            FPackageName::RegisterMountPoint(Root, Directory);
        }

        ~FSandboxMount()
        {
            FPackageName::UnRegisterMountPoint(Root, Directory);
        }
    };

    struct FStandardMaterialScope
    {
        bool Previous = TMMDMeshBuilder::IsUseStandardMaterials();

        explicit FStandardMaterialScope(bool UseStandard) { TMMDMeshBuilder::SetUseStandardMaterials(UseStandard); }
        ~FStandardMaterialScope() { TMMDMeshBuilder::SetUseStandardMaterials(Previous); }
    };

    bool IsFinite(const FVector3f& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    TMap<int32, FVector3f> GatherCollapsedNormalExpectations(const PMXDatas& Data)
    {
        TMap<int32, FVector3f> Expected;
        TSet<int32> HasOpposingGeometry;
        for (int32 Index = 0; Index + 2 < Data.ModelIndices.Num(); Index += 3)
        {
            const int32 Indices[] = { Data.ModelIndices[Index], Data.ModelIndices[Index + 1], Data.ModelIndices[Index + 2] };
            if (Indices[0] == Indices[1] || Indices[1] == Indices[2] || Indices[0] == Indices[2]) continue;
            FVector3f Positions[3];
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const FVector& Source = Data.ModelVertices[Indices[Corner]].Position;
                Positions[Corner] = FVector3f(Source.X * 8.0f, -Source.Z * 8.0f, Source.Y * 8.0f);
            }
            const FVector3f Cross = FVector3f::CrossProduct(Positions[1] - Positions[0], Positions[2] - Positions[0]);
            const float AreaSquared = Cross.SizeSquared();
            const bool HasGeometry = FMath::IsFinite(AreaSquared) && AreaSquared > KINDA_SMALL_NUMBER;
            for (const int32 SourceIndex : Indices)
            {
                const FVector& Source = Data.ModelVertices[SourceIndex].Normal;
                const FVector3f Normal(Source.X, -Source.Z, Source.Y);
                const float LengthSquared = Normal.SizeSquared();
                if (!FMath::IsFinite(LengthSquared) || LengthSquared <= KINDA_SMALL_NUMBER) continue;
                const FVector3f UnitNormal = Normal * FMath::InvSqrt(LengthSquared);
                if (HasGeometry && FVector3f::DotProduct(UnitNormal, Cross * FMath::InvSqrt(AreaSquared)) < -0.25f)
                    HasOpposingGeometry.Add(SourceIndex);
                // 这些源法线曾被伪 +Z 回退翻转；只检查没有其他合法反向几何证据的顶点。
                else if (!HasGeometry && UnitNormal.Z < -0.25f) Expected.Add(SourceIndex, UnitNormal);
            }
        }
        for (const int32 SourceIndex : HasOpposingGeometry) Expected.Remove(SourceIndex);
        return Expected;
    }

    struct FUsedAlphaVertexSamples
    {
        int32 Count = 0;
        int32 NonOpaque = 0;
        int32 Soft = 0;
        uint8 Minimum = 255;
        uint8 Maximum = 0;
    };

    FUsedAlphaVertexSamples InspectUsedAlphaVertices(UTexture2D* Texture, const PMXDatas& Data,
        int64 FirstIndex, int32 IndexCount, TMap<UTexture2D*, TArray64<uint8>>& MipCache)
    {
        FUsedAlphaVertexSamples Samples;
        if (!Texture || !Texture->Source.IsValid() || Texture->Source.GetFormat() != TSF_BGRA8) return Samples;
        TArray64<uint8>* Pixels = MipCache.Find(Texture);
        if (!Pixels)
        {
            Pixels = &MipCache.Add(Texture);
            IImageWrapperModule& ImageWrapper = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
            if (!Texture->Source.GetMipData(*Pixels, 0, &ImageWrapper)) Pixels->Empty();
        }
        const int32 Width = Texture->Source.GetSizeX();
        const int32 Height = Texture->Source.GetSizeY();
        if (Width <= 0 || Height <= 0 || Pixels->Num() < int64(Width) * Height * 4) return Samples;
        const int64 LastIndex = FMath::Min(FirstIndex + FMath::Max(IndexCount, 0), int64(Data.ModelIndices.Num()));
        for (int64 Index = FirstIndex; Index < LastIndex; ++Index)
        {
            const int32 VertexIndex = Data.ModelIndices[static_cast<int32>(Index)];
            if (!Data.ModelVertices.IsValidIndex(VertexIndex)) continue;
            const FVector2D& UV = Data.ModelVertices[VertexIndex].UV;
            if (!FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y)) continue;
            // 独立读取真实样本的原始 texel，验证透明源数据仍在，不复制生产判定的插值逻辑。
            const int32 X = FMath::FloorToInt((UV.X - FMath::FloorToDouble(UV.X)) * Width);
            const int32 Y = FMath::FloorToInt((UV.Y - FMath::FloorToDouble(UV.Y)) * Height);
            const uint8 Alpha = (*Pixels)[(int64(FMath::Clamp(Y, 0, Height - 1)) * Width
                + FMath::Clamp(X, 0, Width - 1)) * 4 + 3];
            ++Samples.Count;
            Samples.NonOpaque += Alpha < 255;
            Samples.Soft += Alpha > 1 && Alpha < 254;
            Samples.Minimum = FMath::Min(Samples.Minimum, Alpha);
            Samples.Maximum = FMath::Max(Samples.Maximum, Alpha);
        }
        return Samples;
    }

    bool ValidatePhysics(const PMXDatas& Data, USkeletalMesh* Mesh)
    {
        TArray<FMMDPhysicsRigidBodyData> Rigids;
        TArray<FMMDPhysicsJointData> Joints;
        for (const FPMXRigid& Source : Data.ModelRigids)
        {
            if (Source.PhysicsMode > 2 || Source.ShapeType > 2 || Source.Group > 15
                || Source.Size.ContainsNaN() || Source.Position.ContainsNaN() || Source.Rotation.ContainsNaN())
            {
                return false;
            }
            FMMDPhysicsRigidBodyData& Rigid = Rigids.AddDefaulted_GetRef();
            Rigid.Name = Source.NameJP;
            Rigid.RelatedBoneIndex = Source.RelatedBoneIndex;
            Rigid.ShapeType = Source.ShapeType;
            Rigid.ShapeSize = Source.Size;
            Rigid.ShapePosition = Source.Position;
            Rigid.ShapeRotation = Source.Rotation;
            Rigid.Mass = Source.Mass;
            Rigid.Friction = Source.Friction;
            Rigid.Restitution = Source.Restitution;
            Rigid.LinearDamping = Source.LinearDamping;
            Rigid.AngularDamping = Source.AngularDamping;
            Rigid.CollisionGroup = Source.Group;
            Rigid.CollisionMask = Source.CollisionMask;
            Rigid.PhysicsMode = Source.PhysicsMode;
        }
        for (const FPMXJoint& Source : Data.ModelJoints)
        {
            // 无效索引不送入仍含 checkf 的旧关节构建路径。
            if (!Rigids.IsValidIndex(Source.RigidA) || !Rigids.IsValidIndex(Source.RigidB))
            {
                return false;
            }
            FMMDPhysicsJointData& Joint = Joints.AddDefaulted_GetRef();
            Joint.Name = Source.NameJP;
            Joint.JointType = Source.JointType;
            Joint.RigidBodyIndexA = Source.RigidA;
            Joint.RigidBodyIndexB = Source.RigidB;
            Joint.Position = Source.Position;
            Joint.Rotation = Source.Rotation;
            Joint.LimitPositionMin = Source.LimitPosLower;
            Joint.LimitPositionMax = Source.LimitPosUpper;
            Joint.LimitRotationMin = Source.LimitRotLower;
            Joint.LimitRotationMax = Source.LimitRotUpper;
            Joint.SpringPosition = Source.SpringPos;
            Joint.SpringRotation = Source.SpringRot;
        }

        TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
        Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Component->SetSkeletalMesh(Mesh);
        FMMDPhysicsSimulator Simulator;
        const bool Initialized = Simulator.InitializeFromPMX(Rigids, Joints, Component.Get());
        Simulator.StepSimulationMMD(1.0f / 60.0f);
        Simulator.Shutdown();
        return Initialized && !Simulator.IsInitialized();
    }

    bool ValidateVMDAnimation(FAutomationTestBase& Test, USkeletalMesh* Mesh, const PMXDatas& PMX,
        const FString& LogicalPMXPath, const FString& VirtualRoot, const FString& SandboxDirectory, FJsonObject& Report)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
        if (!Plugin.IsValid()) return false;
        TArray<FString> Sources;
        IFileManager::Get().FindFilesRecursive(Sources, *(Plugin->GetBaseDir() / TEXT("Resources")), TEXT("CH4NGE.vmd"), true, false);
        if (!Test.TestEqual(TEXT("真实动作样本唯一"), Sources.Num(), 1)) return false;
        TVMDParser Parser;
        if (!Test.TestTrue(TEXT("真实 VMD 解析"), Parser.ParseVMDFile(Sources[0]))) return false;
        VMDData& Motion = Parser.VMDInfo;
        // 使用真实动作前两秒覆盖插值、IK 与 Morph，限制集成测试的烘焙开销。
        constexpr uint32 LastFrame = 60;
        Motion.BoneKeyframes.RemoveAll([](const VMDBoneKeyframe& Key) { return Key.FrameNumber > LastFrame; });
        Motion.MorphKeyframes.RemoveAll([](const VMDMorphKeyframe& Key) { return Key.FrameNumber > LastFrame; });
        Motion.CameraKeyframes.RemoveAll([](const VMDCameraKeyframe& Key) { return Key.FrameNumber > LastFrame; });
        Motion.LightKeyframes.RemoveAll([](const VMDLightKeyframe& Key) { return Key.FrameNumber > LastFrame; });
        Motion.ShadowKeyframes.RemoveAll([](const VMDShadowKeyframe& Key) { return Key.FrameNumber > LastFrame; });
        Motion.IKKeyframes.RemoveAll([](const VMDIKKeyframe& Key) { return Key.FrameNumber > LastFrame; });

        FMMDAnimationImportContext Context;
        FMMDAnimationImportReport ImportReport;
        if (!Test.TestTrue(TEXT("构建真实 VMD 动画上下文"), TMMDMeshBuilder::BuildAnimationImportContext(
            Mesh, &PMX, LogicalPMXPath, Sources[0], Context, &ImportReport))) return false;
        FMMDAnimationImportSettings Settings;
        if (!Test.TestTrue(TEXT("动作轨道匹配"), TMMDMeshBuilder::AnalyzeVMDAnimationImport(Motion, Context, Settings, ImportReport))) return false;
        FString ProposedFilename;
        if (!Test.TestTrue(TEXT("动画目录映射到 Saved"), ImportReport.PackagePath.StartsWith(VirtualRoot)
            && FPackageName::TryConvertLongPackageNameToFilename(ImportReport.PackagePath / ImportReport.AssetName,
                ProposedFilename, FPackageName::GetAssetPackageExtension())
            && FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(ProposedFilename), SandboxDirectory))) return false;
        TStrongObjectPtr<UAnimSequence> Animation(TMMDMeshBuilder::BuildVMDAnimation(Motion, Context, Settings, &ImportReport));
        if (!Test.TestNotNull(TEXT("真实 AnimSequence 生成"), Animation.Get())
            || !Test.TestFalse(TEXT("动画导入无 Error"), ImportReport.HasErrors())) return false;
        Animation->WaitOnExistingCompression();
        if (!Test.TestTrue(TEXT("骨骼动画压缩完成"), Animation->IsBoneCompressedDataValid())) return false;
        const IAnimationDataModel* Model = Animation->GetDataModel();
        if (!Test.TestNotNull(TEXT("AnimSequence 数据模型"), Model)) return false;
        Test.TestEqual(TEXT("动作帧数保持真实帧窗"), Model->GetNumberOfFrames(), int32(LastFrame));
        Test.TestTrue(TEXT("动作时长两秒"), FMath::IsNearlyEqual(Model->GetPlayLength(), 2.0, 1.e-6));
        if (!Test.TestTrue(TEXT("含匹配骨骼轨道"), Model->GetNumBoneTracks() > 0)
            || !Test.TestTrue(TEXT("含真实 Morph 曲线"), Model->GetNumberOfFloatCurves() > 0)) return false;

        TArray<FName> TrackNames;
        Model->GetBoneTrackNames(TrackNames);
        int32 VaryingTracks = 0;
        int32 SampleCount = 0;
        for (const FName Track : TrackNames)
        {
            const int32 BoneIndex = Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Track);
            if (BoneIndex == INDEX_NONE)
            {
                TArray<FString> BoneNames;
                for (const FMeshBoneInfo& Bone : Mesh->GetSkeleton()->GetReferenceSkeleton().GetRefBoneInfo())
                    BoneNames.Add(Bone.Name.ToString());
                Test.AddError(FString::Printf(TEXT("动画轨道骨骼索引无效：Track='%s' SkeletonNum=%d MeshNum=%d Bones=[%s]"),
                    *Track.ToString(), Mesh->GetSkeleton()->GetReferenceSkeleton().GetNum(), Mesh->GetRefSkeleton().GetNum(),
                    *FString::Join(BoneNames, TEXT(","))));
                return false;
            }
            FTransform First = FTransform::Identity;
            bool Varies = false;
            for (const double Time : {0.0, 0.733333333, 1.966666667})
            {
                const FTransform DataTransform = Model->EvaluateBoneTrackTransform(Track, Model->GetFrameRate().AsFrameTime(Time), EAnimInterpolationType::Linear);
                FTransform RawTransform = FTransform::Identity;
                FTransform CompressedTransform = FTransform::Identity;
                FAnimExtractContext Extract(Time);
                Animation->GetBoneTransform(RawTransform, FSkeletonPoseBoneIndex(BoneIndex), Extract, true);
                Extract.bEnforceCompressedDataSampling = true;
                Animation->GetBoneTransform(CompressedTransform, FSkeletonPoseBoneIndex(BoneIndex), Extract, false);
                if (!DataTransform.IsValid() || !RawTransform.IsValid() || !CompressedTransform.IsValid()
                    || !DataTransform.Equals(RawTransform, 1.e-4))
                {
                    Test.AddError(FString::Printf(TEXT("动画采样无效或 raw/data-model 不一致：%s @ %.3f"), *Track.ToString(), Time));
                    return false;
                }
                const double PositionError = FVector::Distance(RawTransform.GetTranslation(), CompressedTransform.GetTranslation());
                const double RotationError = FMath::RadiansToDegrees(RawTransform.GetRotation().AngularDistance(CompressedTransform.GetRotation()));
                const double ScaleError = FVector::Distance(RawTransform.GetScale3D(), CompressedTransform.GetScale3D());
                if (PositionError > 0.1 || RotationError > 0.2 || ScaleError > 1.e-3)
                {
                    Test.AddError(FString::Printf(TEXT("压缩动画采样偏离原轨道：%s @ %.3f Position=%.6fcm Rotation=%.6fdeg Scale=%.6f"),
                        *Track.ToString(), Time, PositionError, RotationError, ScaleError));
                    return false;
                }
                if (Time == 0.0) First = RawTransform;
                else Varies |= !First.Equals(RawTransform, 1.e-3);
                ++SampleCount;
            }
            VaryingTracks += Varies ? 1 : 0;
        }
        if (!Test.TestTrue(TEXT("真实动作采样包含变化"), VaryingTracks > 0)) return false;
        for (const FFloatCurve& Curve : Model->GetFloatCurves())
        {
            for (const float Time : {0.0f, 0.733333333f, 1.966666667f})
            {
                if (!FMath::IsFinite(Curve.Evaluate(Time)))
                {
                    Test.AddError(TEXT("生成 Morph 曲线含非有限采样值"));
                    return false;
                }
            }
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(Animation->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!Test.TestTrue(TEXT("动画文件已保存在 Saved"), FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), SandboxDirectory)
            && FPaths::FileExists(Filename))) return false;
        Report.SetStringField(TEXT("source_vmd"), Sources[0]);
        Report.SetStringField(TEXT("animation"), Animation->GetPathName());
        Report.SetStringField(TEXT("animation_file"), FPaths::ConvertRelativePathToFull(Filename));
        Report.SetNumberField(TEXT("animation_frames"), Model->GetNumberOfFrames());
        Report.SetNumberField(TEXT("animation_bone_tracks"), Model->GetNumBoneTracks());
        Report.SetNumberField(TEXT("animation_morph_curves"), Model->GetNumberOfFloatCurves());
        Report.SetNumberField(TEXT("animation_sampled_transforms"), SampleCount);
        Report.SetNumberField(TEXT("animation_varying_tracks"), VaryingTracks);
        return true;
    }
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMMDMeshImportIntegrationTest, "Ue5MMDTools.Import.MeshIntegration",
    EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

void FMMDMeshImportIntegrationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
    if (!Plugin.IsValid())
    {
        return;
    }
    const FString Directory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/YYB Assistant Gen. Works Librarian Miku"));
    Names.Add(TEXT("AccessorySword"));
    Commands.Add(FPaths::Combine(Directory, TEXT("Librarian's Sword.pmx")));
    Names.Add(TEXT("CharacterNoSword"));
    Commands.Add(FPaths::Combine(Directory, TEXT("YYB_AssistantGenWorks_Miku_NoSword.pmx")));
}

bool FMMDMeshImportIntegrationTest::RunTest(const FString& Parameters)
{
    if (!IsRunningCommandlet())
    {
        AddError(TEXT("实际导入沙盒只允许在 Commandlet 中运行"));
        return false;
    }

    TPMXParser Parser;
    if (!TestTrue(TEXT("集成样本 PMX 解析"), Parser.ParsePMXFile(Parameters)))
    {
        return false;
    }

    const FString UniqueName = TEXT("MMDBackgroundValidation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString VirtualRoot = TEXT("/Game/MMDModels/") + UniqueName + TEXT("/");
    const FString SandboxDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/ImportedAssets"), UniqueName)) + TEXT("/");
    if (!TestTrue(TEXT("创建导入沙盒"), IFileManager::Get().MakeDirectory(*SandboxDirectory, true)))
    {
        return false;
    }
    MMDMeshImportIntegrationTests::FSandboxMount Mount(VirtualRoot, SandboxDirectory);
    const FString MeshPackageName = VirtualRoot + TEXT("Model/") + UniqueName;
    FString MeshFile;
    if (!TestTrue(TEXT("验证产物路径映射"), FPackageName::TryConvertLongPackageNameToFilename(
        MeshPackageName, MeshFile, FPackageName::GetAssetPackageExtension()))
        || !TestTrue(TEXT("产物保存严格限制在 Saved"), FPaths::IsUnderDirectory(
            FPaths::ConvertRelativePathToFull(MeshFile), SandboxDirectory)))
    {
        return false;
    }
    if (FindPackage(nullptr, *MeshPackageName) != nullptr || FPaths::FileExists(MeshFile))
    {
        AddError(TEXT("沙盒 GUID 发生资源冲突"));
        return false;
    }

    // 仅改变逻辑文件名以隔离产物；原始目录保持不变，贴图仍从真实 PMX 目录读取。
    const FString LogicalSourcePath = FPaths::Combine(FPaths::GetPath(Parameters), UniqueName + TEXT(".pmx"));
    const bool UseStandard = FPaths::GetCleanFilename(Parameters) != TEXT("Librarian's Sword.pmx");
    MMDMeshImportIntegrationTests::FStandardMaterialScope StandardMaterials(UseStandard);
    TStrongObjectPtr<USkeletalMesh> Mesh(TMMDMeshBuilder::BuildSkeletalMeshFromPMX(
        Parser.PMXInfo, VirtualRoot, UniqueName, LogicalSourcePath));
    if (!TestNotNull(TEXT("真实 SkeletalMesh 构建"), Mesh.Get()))
    {
        return false;
    }
    TestEqual(TEXT("网格位于唯一沙盒包"), Mesh->GetOutermost()->GetName(), MeshPackageName);
    TestNotNull(TEXT("导入 Skeleton"), Mesh->GetSkeleton());
    TestEqual(TEXT("全部 PMX 骨骼加 UE Root"), Mesh->GetRefSkeleton().GetNum(), Parser.PMXInfo.ModelBoneCount + 1);
    if (!TestEqual(TEXT("Skeleton 保存全部网格骨骼"), Mesh->GetSkeleton()->GetReferenceSkeleton().GetNum(), Mesh->GetRefSkeleton().GetNum())) return false;
    for (int32 BoneIndex = 0; BoneIndex < Mesh->GetRefSkeleton().GetNum(); ++BoneIndex)
    {
        const FReferenceSkeleton& SkeletonBones = Mesh->GetSkeleton()->GetReferenceSkeleton();
        if (!TestEqual(TEXT("Skeleton/Mesh 骨名一致"), SkeletonBones.GetBoneName(BoneIndex), Mesh->GetRefSkeleton().GetBoneName(BoneIndex))
            || !TestEqual(TEXT("Skeleton/Mesh 父骨骼一致"), SkeletonBones.GetParentIndex(BoneIndex), Mesh->GetRefSkeleton().GetParentIndex(BoneIndex))) return false;
    }
    if (!TestEqual(TEXT("材质槽覆盖 PMX 材质"), Mesh->GetMaterials().Num(), Parser.PMXInfo.ModelMaterialCount)) return false;
    TestTrue(TEXT("导入包已保存"), FPaths::FileExists(MeshFile));

    const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
    if (ImportedModel == nullptr || ImportedModel->LODModels.IsEmpty())
    {
        AddError(TEXT("导入网格缺少 LOD0"));
        return false;
    }
    const FSkeletalMeshLODModel& LOD = ImportedModel->LODModels[0];
    TestTrue(TEXT("LOD0 有顶点和三角面"), LOD.NumVertices > 0 && LOD.IndexBuffer.Num() > 0 && !LOD.Sections.IsEmpty());
    TestTrue(TEXT("顶点颜色保留在构建网格"), Mesh->GetHasVertexColors());
    if (!TestEqual(TEXT("LOD 原始顶点映射完整"), LOD.MeshToImportVertexMap.Num(), int32(LOD.NumVertices))) return false;
    const TMap<int32, FVector3f> CollapsedNormals = MMDMeshImportIntegrationTests::GatherCollapsedNormalExpectations(Parser.PMXInfo);
    int32 CollapsedNormalsChecked = 0;
    for (const uint32 Index : LOD.IndexBuffer)
    {
        if (Index >= static_cast<uint32>(LOD.NumVertices))
        {
            AddError(TEXT("构建后的 LOD 索引越界"));
            return false;
        }
    }
    int32 TriangleCount = 0;
    FBox GeometryBounds(ForceInit);
    for (const FSkelMeshSection& Section : LOD.Sections)
    {
        if (!Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex))
        {
            AddError(TEXT("Section 材质索引越界"));
            return false;
        }
        TriangleCount += Section.NumTriangles;
        for (int32 LocalVertex = 0; LocalVertex < Section.SoftVertices.Num(); ++LocalVertex)
        {
            const FSoftSkinVertex& Vertex = Section.SoftVertices[LocalVertex];
            if (!MMDMeshImportIntegrationTests::IsFinite(Vertex.Position))
            {
                AddError(TEXT("构建后的顶点位置含非有限值"));
                return false;
            }
            GeometryBounds += FVector(Vertex.Position);
            const int32 ImportVertex = LOD.MeshToImportVertexMap[Section.BaseVertexIndex + LocalVertex];
            if (!Parser.PMXInfo.ModelVertices.IsValidIndex(ImportVertex))
            {
                AddError(TEXT("LOD 原始顶点索引越界"));
                return false;
            }
            if (const FVector3f* ExpectedNormal = CollapsedNormals.Find(ImportVertex))
            {
                const FVector3f BuiltNormal(Vertex.TangentZ.X, Vertex.TangentZ.Y, Vertex.TangentZ.Z);
                if (!MMDMeshImportIntegrationTests::IsFinite(BuiltNormal)
                    || FVector3f::DotProduct(BuiltNormal.GetSafeNormal(), *ExpectedNormal) < 0.99f)
                {
                    AddError(FString::Printf(TEXT("折叠表情三角篡改有效源法线：SourceVertex=%d"), ImportVertex));
                    return false;
                }
                ++CollapsedNormalsChecked;
            }
            const float SourceEdge = Parser.PMXInfo.ModelVertices[ImportVertex].Weight.EdgeScale;
            const float Edge = FMath::IsFinite(SourceEdge) && SourceEdge > 0 ? SourceEdge : 0.0f;
            const uint8 Encoded = uint8(FMath::RoundToInt(255.0f * FMath::Clamp(Edge / (1.0f + Edge), 0.0f, 1.0f)));
            if (Vertex.Color != FColor(255, 255, 255, Encoded))
            {
                AddError(TEXT("真实 PMX EdgeScale 没有保留在顶点 Alpha"));
                return false;
            }
        }
    }
    TestTrue(TEXT("导入三角面计数有效"), TriangleCount > 0 && TriangleCount <= Parser.PMXInfo.ModelIndicesCount / 3);
    if (!CollapsedNormals.IsEmpty() && !TestTrue(TEXT("真实折叠表情法线回归覆盖生成 LOD"), CollapsedNormalsChecked > 0)) return false;
    TArray<TSharedPtr<FJsonValue>> MaterialAlphaSlots;
    TMap<UTexture2D*, TArray64<uint8>> AlphaMipCache;
    int64 MaterialFirstIndex = 0;
    int32 HairShadowChecked = 0;
    int32 OpaqueSkinChecked = 0;
    for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetMaterials().Num(); ++MaterialIndex)
    {
        const FSkeletalMaterial& Material = Mesh->GetMaterials()[MaterialIndex];
        const FPMXMaterial& Source = Parser.PMXInfo.ModelMaterials[MaterialIndex];
        UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Material.MaterialInterface);
        if (Instance == nullptr || !Instance->GetOutermost()->GetName().StartsWith(VirtualRoot))
        {
            AddError(TEXT("导入材质实例未隔离在沙盒"));
            return false;
        }
        UTexture* BaseTexture = nullptr;
        Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColorMap")), BaseTexture);
        if (!BaseTexture) Instance->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColorTexture")), BaseTexture);
        UTexture2D* Texture = Cast<UTexture2D>(BaseTexture);
        const MMDMeshImportIntegrationTests::FUsedAlphaVertexSamples Alpha = MMDMeshImportIntegrationTests::InspectUsedAlphaVertices(
            Texture, Parser.PMXInfo, MaterialFirstIndex, Source.FaceIndexCount, AlphaMipCache);
        MaterialFirstIndex += FMath::Max(Source.FaceIndexCount, 0);
        float ScalarOpacity = 1.0f;
        const bool HasScalarOpacity = Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")), ScalarOpacity);
        const TSharedRef<FJsonObject> Slot = MakeShared<FJsonObject>();
        Slot->SetNumberField(TEXT("slot"), MaterialIndex);
        Slot->SetStringField(TEXT("name"), Source.NameEN.IsEmpty() ? Source.NameJP : Source.NameEN);
        Slot->SetStringField(TEXT("material"), Instance->GetPathName());
        Slot->SetStringField(TEXT("texture"), BaseTexture ? BaseTexture->GetPathName() : FString());
        Slot->SetNumberField(TEXT("blend_mode"), int32(Instance->GetBlendMode()));
        Slot->SetNumberField(TEXT("diffuse_alpha"), Source.DiffuseColor.W);
        Slot->SetBoolField(TEXT("has_scalar_opacity"), HasScalarOpacity);
        if (HasScalarOpacity) Slot->SetNumberField(TEXT("scalar_opacity"), ScalarOpacity);
        Slot->SetNumberField(TEXT("used_alpha_vertex_samples"), Alpha.Count);
        Slot->SetNumberField(TEXT("used_alpha_non_opaque_vertex_samples"), Alpha.NonOpaque);
        Slot->SetNumberField(TEXT("used_alpha_soft_vertex_samples"), Alpha.Soft);
        if (Alpha.Count > 0)
        {
            Slot->SetNumberField(TEXT("used_alpha_min_byte"), Alpha.Minimum);
            Slot->SetNumberField(TEXT("used_alpha_max_byte"), Alpha.Maximum);
        }
        MaterialAlphaSlots.Add(MakeShared<FJsonValueObject>(Slot));
        if (Source.NameEN.Equals(TEXT("Hairshadow"), ESearchCase::IgnoreCase))
        {
            if (!TestEqual(TEXT("真实头发阴影保持半透明混合"), Instance->GetBlendMode(), BLEND_Translucent)
                || !TestTrue(TEXT("头发阴影使用区保留软 alpha"), Alpha.Count == Source.FaceIndexCount
                    && Alpha.Count > 0 && Alpha.NonOpaque == Alpha.Count && Alpha.Soft == Alpha.Count
                    && Alpha.Minimum > 1 && Alpha.Maximum < 254)) return false;
            ++HairShadowChecked;
        }
        if (Source.NameEN.Equals(TEXT("face01"), ESearchCase::IgnoreCase)
            || Source.NameEN.Equals(TEXT("head"), ESearchCase::IgnoreCase))
        {
            if (!TestEqual(TEXT("实心脸和头皮不受图集透明区域影响"), Instance->GetBlendMode(), BLEND_Opaque)
                || !TestTrue(TEXT("实心皮肤实际使用区全部不透明"), Alpha.Count > 0 && Alpha.NonOpaque == 0)) return false;
            ++OpaqueSkinChecked;
        }
        if (Source.DiffuseColor.W < 0.999f)
        {
            if (!TestEqual(TEXT("PMX diffuse alpha 保留透明混合"), Instance->GetBlendMode(), BLEND_Translucent)
                || !TestTrue(TEXT("PMX diffuse alpha 保留 Opacity 参数"), HasScalarOpacity
                    && FMath::IsNearlyEqual(ScalarOpacity, FMath::Clamp(float(Source.DiffuseColor.W), 0.0f, 1.0f), 1.e-6f))) return false;
        }
    }
    if (UseStandard && (!TestEqual(TEXT("真实头发阴影回归有覆盖"), HairShadowChecked, 1)
        || !TestEqual(TEXT("实心脸和头皮回归有覆盖"), OpaqueSkinChecked, 2))) return false;
    const UMMDSkeletalMeshUserData* UserData = Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>();
    TestTrue(TEXT("模型骨骼数据关联完整"), UserData != nullptr && UserData->ModelDataAsset != nullptr
        && UserData->ModelDataAsset->Bones.Num() == Parser.PMXInfo.ModelBoneCount);
    if (!TestTrue(TEXT("描边材质槽完整"), UserData != nullptr && UserData->OutlineMaterials.Num() == Parser.PMXInfo.ModelMaterials.Num())) return false;
    int32 OutlineCount = 0;
    for (int32 Index = 0; Index < Parser.PMXInfo.ModelMaterials.Num(); ++Index)
    {
        const FPMXMaterial& Source = Parser.PMXInfo.ModelMaterials[Index];
        const bool Enabled = (Source.DrawFlags & 0x10) != 0 && Source.EdgeSize > 0 && Source.EdgeColor.W > 0;
        UMaterialInstanceConstant* Outline = Cast<UMaterialInstanceConstant>(UserData->OutlineMaterials[Index]);
        if (!Enabled)
        {
            if (!TestNull(TEXT("未启用 PMX 描边的槽保持为空"), UserData->OutlineMaterials[Index].Get())) return false;
            continue;
        }
        if (!TestNotNull(TEXT("已启用 PMX 描边的槽产生 MI"), Outline)
            || !TestTrue(TEXT("描边 MI 位于导入沙盒"), Outline->GetOutermost()->GetName().StartsWith(VirtualRoot))) return false;
        FLinearColor Color;
        float Size = 0;
        if (!TestTrue(TEXT("描边颜色参数准确"), Outline->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EdgeColor")), Color)
            && Color.Equals(FLinearColor(Source.EdgeColor.X, Source.EdgeColor.Y, Source.EdgeColor.Z, Source.EdgeColor.W), 1.e-6f))
            || !TestTrue(TEXT("描边宽度参数准确"), Outline->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EdgeSize")), Size)
                && FMath::IsNearlyEqual(Size, Source.EdgeSize, 1.e-6f))) return false;
        ++OutlineCount;
    }
    const FVector OutlinePadding = OutlineCount > 0 ? FVector(10.0) : FVector::ZeroVector;
    const FBox ImportedBounds = Mesh->GetImportedBounds().GetBox().ExpandBy(1.e-3);
    TestTrue(TEXT("导入 bounds 覆盖几何与最大描边扩张"), ImportedBounds.IsInsideOrOn(GeometryBounds.Min - OutlinePadding)
        && ImportedBounds.IsInsideOrOn(GeometryBounds.Max + OutlinePadding));
    TestTrue(TEXT("真实 PMX 刚体/关节初始化和清理"), MMDMeshImportIntegrationTests::ValidatePhysics(Parser.PMXInfo, Mesh.Get()));

    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    if (UseStandard && !MMDMeshImportIntegrationTests::ValidateVMDAnimation(*this, Mesh.Get(), Parser.PMXInfo,
        LogicalSourcePath, VirtualRoot, SandboxDirectory, *Report)) return false;

    TArray<TStrongObjectPtr<UPackage>> Packages;
    for (TObjectIterator<UPackage> It; It; ++It)
    {
        if (It->GetName().StartsWith(VirtualRoot))
        {
            Packages.Emplace(*It);
        }
    }
    TArray<TSharedPtr<FJsonValue>> AssetFiles;
    for (const TStrongObjectPtr<UPackage>& Package : Packages)
    {
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension())
            || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), SandboxDirectory))
        {
            AddError(TEXT("保存前发现导入包路径超出沙盒"));
            return false;
        }
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.Error = GError;
        SaveArgs.bWarnOfLongFilename = false;
        if (!TestTrue(TEXT("保存沙盒导入资源"), UPackage::SavePackage(Package.Get(), nullptr, *Filename, SaveArgs)))
        {
            return false;
        }
        AssetFiles.Add(MakeShared<FJsonValueString>(FPaths::ConvertRelativePathToFull(Filename)));
    }
    TestTrue(TEXT("保存网格、Skeleton、模型数据及材质"), Packages.Num() >= Parser.PMXInfo.ModelMaterialCount + 3);

    Report->SetStringField(TEXT("source_pmx"), Parameters);
	Report->SetStringField(TEXT("material_mode"), UseStandard ? TEXT("standard") : TEXT("legacy"));
    Report->SetStringField(TEXT("output_directory"), SandboxDirectory);
    Report->SetNumberField(TEXT("asset_count"), AssetFiles.Num());
    Report->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum());
    Report->SetNumberField(TEXT("vertices"), LOD.NumVertices);
    Report->SetNumberField(TEXT("triangles"), TriangleCount);
    Report->SetNumberField(TEXT("materials"), Mesh->GetMaterials().Num());
    Report->SetArrayField(TEXT("material_alpha_slots"), MaterialAlphaSlots);
    Report->SetNumberField(TEXT("collapsed_source_normal_candidates"), CollapsedNormals.Num());
    Report->SetNumberField(TEXT("collapsed_source_normals_verified"), CollapsedNormalsChecked);
    Report->SetNumberField(TEXT("outline_materials"), OutlineCount);
    Report->SetNumberField(TEXT("rigid_bodies"), Parser.PMXInfo.ModelRigids.Num());
    Report->SetNumberField(TEXT("joints"), Parser.PMXInfo.ModelJoints.Num());
    Report->SetArrayField(TEXT("asset_files"), AssetFiles);
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    TestTrue(TEXT("保存集成导入清单"), FJsonSerializer::Serialize(Report, Writer)
        && FFileHelper::SaveStringToFile(Json, *(SandboxDirectory + TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
    UE_LOG(LogTemp, Display, TEXT("[MMDMeshIntegration] Assets=%d Bones=%d Vertices=%d Triangles=%d Output=%s"),
        AssetFiles.Num(), Mesh->GetRefSkeleton().GetNum(), LOD.NumVertices, TriangleCount, *SandboxDirectory);
    return true;
}

#endif
