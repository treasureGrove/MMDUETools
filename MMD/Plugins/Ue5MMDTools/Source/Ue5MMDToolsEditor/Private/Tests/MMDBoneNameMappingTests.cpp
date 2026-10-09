#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/Paths.h"
#include "TMMDMeshBuilder.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMMDBoneNameMappingTest, "Ue5MMDTools.Import.BoneNameMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

void FMMDBoneNameMappingTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    Names.Add(TEXT("LegacySanitizationCollision"));
    Commands.Add(TEXT("Legacy"));
    Names.Add(TEXT("ImportedSanitizationCollision"));
    Commands.Add(TEXT("Imported"));
}

bool FMMDBoneNameMappingTest::RunTest(const FString& Parameters)
{
    const bool Legacy = Parameters == TEXT("Legacy");
    TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>(GetTransientPackage()));
    TStrongObjectPtr<USkeleton> Skeleton(NewObject<USkeleton>(GetTransientPackage()));
    FReferenceSkeleton Reference;
    const FName PlusBone = Legacy ? FName(TEXT("+A")) : FName(TEXT("_A"));
    const FName UnderscoreBone = Legacy ? FName(TEXT("_A")) : FName(TEXT("_A_1"));
    {
        FReferenceSkeletonModifier Modifier(Reference, nullptr);
        Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
        Modifier.Add(FMeshBoneInfo(PlusBone, PlusBone.ToString(), 0), FTransform(FVector(1, 0, 0)));
        Modifier.Add(FMeshBoneInfo(UnderscoreBone, UnderscoreBone.ToString(), 0), FTransform(FVector(2, 0, 0)));
    }
    Mesh->SetRefSkeleton(Reference);
    Mesh->SetSkeleton(Skeleton.Get());
    if (!TestTrue(TEXT("构建临时碰撞骨架"), Skeleton->MergeAllBonesToBoneTree(Mesh.Get(), false))) return false;

    PMXDatas PMX{};
    PMX.ModelBones.AddDefaulted(2);
    PMX.ModelBoneCount = 2;
    PMX.ModelBones[0].NameJP = TEXT("+A");
    PMX.ModelBones[1].NameJP = TEXT("_A");
    const FString FixturePath = FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/Fixtures/BoneNameMapping.pmx");
    FMMDAnimationImportContext Context;
    FMMDAnimationImportReport Report;
    if (!TestTrue(TEXT("生产 API 构建动画导入上下文"), TMMDMeshBuilder::BuildAnimationImportContext(
        Mesh.Get(), &PMX, FixturePath, TEXT("Collision.vmd"), Context, &Report))) return false;
    if (!TestEqual(TEXT("上下文保留两条 PMX 骨映射"), Context.PMXBoneToSkeleton.Num(), 2)) return false;
    if (!TestEqual(TEXT("源 +A 保留独立骨索引"), Context.PMXBoneToSkeleton[0], 1)
        || !TestEqual(TEXT("源 _A 保留独立骨索引"), Context.PMXBoneToSkeleton[1], 2)) return false;

    VMDData Motion{};
    for (const FString SourceName : {FString(TEXT("+A")), FString(TEXT("_A"))})
    {
        VMDBoneKeyframe Key{};
        Key.BoneName = SourceName;
        Key.FrameNumber = 1;
        Key.Position = FVector::ZeroVector;
        Key.Rotation = FQuat::Identity;
        Motion.BoneKeyframes.Add(Key);
    }
    FMMDAnimationImportSettings Settings;
    Settings.bImportMorphCurves = false;
    if (!TestTrue(TEXT("生产 API 分析原名 VMD"), TMMDMeshBuilder::AnalyzeVMDAnimationImport(Motion, Context, Settings, Report))) return false;
    if (!TestEqual(TEXT("两个原名轨道都匹配"), Report.MatchedBoneTrackCount, 2)) return false;
    for (const FMMDResolvedBoneTrack& Track : Report.BoneTracks)
    {
        const bool IsPlus = Track.SourceBoneName == TEXT("+A");
        if (!TestEqual(TEXT("VMD 不合并清理后重名轨道"), Track.TargetBoneIndex, IsPlus ? 1 : 2)
            || !TestEqual(TEXT("VMD 指向真实目标骨名"), Track.TargetBoneName, IsPlus ? PlusBone : UnderscoreBone)) return false;
    }
    TestTrue(TEXT("临时测试没有创建可保存模型包"), Mesh->GetOutermost() == GetTransientPackage()
        && Skeleton->GetOutermost() == GetTransientPackage());
    return true;
}

#endif
