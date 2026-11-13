#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/MMDImportSetting.h"
#include "AMMDActor.h"
#include "Animation/AnimSequence.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Import/MMDModelDataAsset.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelSequence.h"
#include "TMMDMeshBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDImportStateTest, "Ue5MMDTools.UI.ImportState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMMDImportStateTest::RunTest(const FString& Parameters)
{
	// 仅验证面板状态交接；不 Construct 视口、不打开窗口、不保存测试资产。
	TSharedRef<MMDImportSetting> Panel = MakeShared<MMDImportSetting>();
	Panel->bUIAnimationsEnabled = false;
	TStrongObjectPtr<USkeleton> SkeletonA(NewObject<USkeleton>());
	TStrongObjectPtr<USkeleton> SkeletonB(NewObject<USkeleton>());
	TArray<TStrongObjectPtr<USkeletalMesh>> Meshes;
	TArray<TStrongObjectPtr<UBlueprint>> Blueprints;
	TArray<TStrongObjectPtr<UAnimSequence>> Animations;
	const FString Prefix = TEXT("/Game/MMDModels/ImportState_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	auto MakeModel = [&](const FString& Folder, USkeleton* Skeleton) -> UClass*
	{
		UPackage* Package = CreatePackage(*(Folder / TEXT("Model/Mesh")));
		Package->SetFlags(RF_Transient);
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, TEXT("Mesh"), RF_Transient);
		Meshes.Emplace(Mesh);
		Mesh->SetSkeleton(Skeleton);
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AMMDActor::StaticClass(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("MMDImportStateFixture")),
			BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint || !Blueprint->GeneratedClass) return nullptr;
		Blueprint->SetFlags(RF_Transient);
		Blueprints.Emplace(Blueprint);
		Blueprint->GeneratedClass->GetDefaultObject<AMMDActor>()->GetMeshComponent()->SetSkeletalMesh(Mesh);
		return Blueprint->GeneratedClass;
	};
	auto MakeAnimation = [&](const FString& Folder, const TCHAR* Name, USkeleton* Skeleton) -> UAnimSequence*
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		Package->SetFlags(RF_Transient);
		UAnimSequence* Animation = NewObject<UAnimSequence>(Package, FName(Name), RF_Transient);
		Animations.Emplace(Animation);
		Animation->SetSkeleton(Skeleton);
		return Animation;
	};
	UClass* ModelA = MakeModel(Prefix + TEXT("A"), SkeletonA.Get());
	UClass* ModelB = MakeModel(Prefix + TEXT("B"), SkeletonB.Get());
	if (!TestNotNull(TEXT("角色 A fixture"), ModelA) || !TestNotNull(TEXT("角色 B fixture"), ModelB)) return false;
	UAnimSequence* AnimA = MakeAnimation(Prefix + TEXT("A/Animation"), TEXT("ActionA"), SkeletonA.Get());
	UAnimSequence* AnimB = MakeAnimation(Prefix + TEXT("B/Animation"), TEXT("ActionB"), SkeletonB.Get());
	UAnimSequence* BakedB = MakeAnimation(Prefix + TEXT("B/Animation"), TEXT("ActionB_Baked"), SkeletonB.Get());
	UAnimSequence* WrongFolderA = MakeAnimation(Prefix + TEXT("A/AnimationOther"), TEXT("Outside"), SkeletonA.Get());
	TStrongObjectPtr<ULevelSequence> Camera(NewObject<ULevelSequence>());
	Camera->Initialize();

	TestTrue(TEXT("选择 A"), Panel->SetCurrentMMDActorClass(ModelA, TEXT("A")));
	TestTrue(TEXT("合成器接受 A 的动作"), Panel->SelectProductionAnimation(AnimA, false));
	TestTrue(TEXT("烘焙接受 A 的动作"), Panel->SelectProductionAnimation(AnimA, true));
	TestFalse(TEXT("A 动作不会被当前筛选隐藏"), Panel->ShouldFilterAnimationForCurrentModel(FAssetData(AnimA)));
	TestTrue(TEXT("B 动作被 A 的筛选隐藏"), Panel->ShouldFilterAnimationForCurrentModel(FAssetData(AnimB)));
	TestTrue(TEXT("相同前缀的旁支目录不能冒充 Animation 目录"), Panel->ShouldFilterAnimationForCurrentModel(FAssetData(WrongFolderA)));
	TestTrue(TEXT("重选同骨架角色"), Panel->SetCurrentMMDActorClass(ModelA, TEXT("A")));
	TestTrue(TEXT("兼容的制作动画保留"), Panel->ComposerAnimSequence == AnimA && Panel->PhysicsBakeAnimSequence == AnimA);

	const uint64 OldImport = Panel->BeginModelImport(nullptr);
	TestTrue(TEXT("导入未完成时允许选择 B"), Panel->SetCurrentMMDActorClass(ModelB, TEXT("B")));
	TestFalse(TEXT("迟到的 A 导入不抢 B"), Panel->AdoptImportedActorClass(OldImport, ModelA, TEXT("迟到的 A")));
	TestTrue(TEXT("当前仍是 B"), Panel->LastLoadedMMDActorClass == ModelB);
	TestFalse(TEXT("旧合成动画已清除"), Panel->ComposerAnimSequence.IsValid());
	TestFalse(TEXT("旧烘焙动画已清除"), Panel->PhysicsBakeAnimSequence.IsValid());
	TestFalse(TEXT("切角色后筛选立即允许 B"), Panel->ShouldFilterAnimationForCurrentModel(FAssetData(AnimB)));
	TestTrue(TEXT("切角色后筛选立即排除 A"), Panel->ShouldFilterAnimationForCurrentModel(FAssetData(AnimA)));
	Panel->OnComposerAnimationChanged(FAssetData(AnimB));
	Panel->OnPhysicsBakeAnimationChanged(FAssetData(AnimB));
	Panel->OnComposerAnimationChanged(FAssetData(AnimA));
	Panel->OnPhysicsBakeAnimationChanged(FAssetData(AnimA));
	TestTrue(TEXT("无效新选择不会抹掉有效 B 动作"), Panel->ComposerAnimSequence == AnimB && Panel->PhysicsBakeAnimSequence == AnimB);

	const uint64 FirstImport = Panel->BeginModelImport(nullptr);
	const uint64 LatestImport = Panel->BeginModelImport(nullptr);
	TestFalse(TEXT("新导入启动后旧完成失效"), Panel->AdoptImportedActorClass(FirstImport, ModelA, TEXT("A")));
	TestTrue(TEXT("最新导入正常接管"), Panel->AdoptImportedActorClass(LatestImport, ModelB, TEXT("B")));
	TestFalse(TEXT("重复完成同一导入不会重建预览"), Panel->AdoptImportedActorClass(LatestImport, ModelB, TEXT("B")));

	Panel->OnComposerCameraChanged(FAssetData(Camera.Get()));
	TestTrue(TEXT("相机结果回填合成器"), Panel->ComposerCameraSequence == Camera.Get());
	const uint64 BakeRevision = Panel->ModelSelectionRevision;
	TestTrue(TEXT("烘焙完成回填合成动画"), Panel->UseBakedAnimationResult(BakedB, BakeRevision));
	TestTrue(TEXT("合成使用烘焙结果，烘焙仍保留原始输入"), Panel->ComposerAnimSequence == BakedB && Panel->PhysicsBakeAnimSequence == AnimB);
	Panel->SetCurrentMMDActorClass(ModelA, TEXT("A"));
	Panel->SelectProductionAnimation(AnimA, false);
	TestFalse(TEXT("迟到烘焙不改新角色制作动画"), Panel->UseBakedAnimationResult(BakedB, BakeRevision));
	TestTrue(TEXT("新角色动画保留"), Panel->ComposerAnimSequence == AnimA);

	// 模拟绕过选择器的旧状态，执行入口必须在创建资产或初始化物理前拦截。
	Panel->ComposerAnimSequence = AnimB;
	Panel->CreateComposedLevelSequence();
	TestTrue(TEXT("合成入口拦截不兼容旧动画"), Panel->CurrentStatusType == EMMDMessageType::Error && Panel->CurrentStatusMessage.Contains(TEXT("不兼容")));
	Panel->PhysicsBakeAnimSequence = AnimB;
	Panel->BakePhysicsAnimation();
	TestTrue(TEXT("烘焙入口拦截不兼容旧动画"), Panel->CurrentStatusType == EMMDMessageType::Error && Panel->CurrentStatusMessage.Contains(TEXT("不兼容")));

	// 验证 VMD 全半角骨骼／表情归一、标准表情同义名回退及 UMMDModelDataAsset 持久化组合表情（无 PMXData）
	{
		TStrongObjectPtr<USkeletalMesh> MorphMesh(NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient));
		TStrongObjectPtr<USkeleton> MorphSkeleton(NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Transient));
		FReferenceSkeleton RefSkel;
		{
			FReferenceSkeletonModifier Modifier(RefSkel, nullptr);
			Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("右足IK"), TEXT("右足IK"), 0), FTransform::Identity);
			Modifier.Add(FMeshBoneInfo(TEXT("上半身2"), TEXT("上半身2"), 0), FTransform::Identity);
		}
		MorphMesh->SetRefSkeleton(RefSkel);
		MorphMesh->SetSkeleton(MorphSkeleton.Get());
		TestTrue(TEXT("合并全半角测试骨架"), MorphSkeleton->MergeAllBonesToBoneTree(MorphMesh.Get(), false));

		for (const TCHAR* MorphName : { TEXT("まばたき"), TEXT("笑い"), TEXT("ウィンク2"), TEXT("あ") })
		{
			UMorphTarget* MT = NewObject<UMorphTarget>(MorphMesh.Get(), FName(MorphName), RF_Transient);
			FMorphTargetLODModel& LODModel = MT->GetMorphLODModels().AddDefaulted_GetRef();
			FMorphTargetDelta& Delta = LODModel.Vertices.AddDefaulted_GetRef();
			Delta.PositionDelta = FVector3f(1.0f, 0.0f, 0.0f);
			Delta.SourceIdx = 0;
			LODModel.NumBaseMeshVerts = 1;
			MorphMesh->RegisterMorphTarget(MT, false);
		}

		UMMDModelDataAsset* ModelData = NewObject<UMMDModelDataAsset>(GetTransientPackage(), NAME_None, RF_Transient);
		{
			FMMDModelMorphData& BlinkMorph = ModelData->Morphs.AddDefaulted_GetRef();
			BlinkMorph.NameJP = TEXT("まばたき");
			BlinkMorph.NameEN = TEXT("Blink");
			BlinkMorph.MorphType = 1;

			FMMDModelMorphData& AhMorph = ModelData->Morphs.AddDefaulted_GetRef();
			AhMorph.NameJP = TEXT("あ");
			AhMorph.NameEN = TEXT("Ah");
			AhMorph.MorphType = 1;

			FMMDModelMorphData& GroupMorph = ModelData->Morphs.AddDefaulted_GetRef();
			GroupMorph.NameJP = TEXT("びっくり");
			GroupMorph.NameEN = TEXT("SurpriseGroup");
			GroupMorph.MorphType = 0;
			FMMDModelMorphChildData& Child0 = GroupMorph.Children.AddDefaulted_GetRef();
			Child0.MorphIndex = 0;
			Child0.Weight = 0.8f;
			FMMDModelMorphChildData& Child1 = GroupMorph.Children.AddDefaulted_GetRef();
			Child1.MorphIndex = 1;
			Child1.Weight = 0.5f;
		}
		UMMDSkeletalMeshUserData* UserData = NewObject<UMMDSkeletalMeshUserData>(MorphMesh.Get(), UMMDSkeletalMeshUserData::StaticClass(), NAME_None, RF_Transient);
		UserData->ModelDataAsset = ModelData;
		MorphMesh->AddAssetUserData(UserData);

		FMMDAnimationImportContext ImportCtx;
		FMMDAnimationImportReport ImportRep;
		TestTrue(TEXT("无 PMXData 构建导入上下文"), TMMDMeshBuilder::BuildAnimationImportContext(
			MorphMesh.Get(), nullptr, FString(), TEXT("NormalizedAndSynonym.vmd"), ImportCtx, &ImportRep));
		TestTrue(TEXT("上下文自动绑定 ModelDataAsset"), ImportCtx.ModelDataAsset == ModelData);

		VMDData Vmd{};
		for (const TCHAR* BoneName : { TEXT("右足ＩＫ"), TEXT("上半身２") })
		{
			VMDBoneKeyframe BK{};
			BK.BoneName = BoneName;
			BK.FrameNumber = 1;
			BK.Position = FVector::ZeroVector;
			BK.Rotation = FQuat::Identity;
			Vmd.BoneKeyframes.Add(BK);
		}
		for (const TCHAR* MorphName : { TEXT("瞬き"), TEXT("笑顔"), TEXT("ウインク２"), TEXT("驚き") })
		{
			VMDMorphKeyframe MK{};
			MK.MorphName = MorphName;
			MK.FrameNumber = 1;
			MK.Weight = 1.0f;
			Vmd.MorphKeyframes.Add(MK);
		}

		FMMDAnimationImportSettings ImportSettings;
		ImportSettings.bImportBoneTracks = true;
		ImportSettings.bImportMorphCurves = true;
		TestTrue(TEXT("分析含全半角与同义名的 VMD"), TMMDMeshBuilder::AnalyzeVMDAnimationImport(Vmd, ImportCtx, ImportSettings, ImportRep));
		TestEqual(TEXT("全半角骨骼轨道全部匹配（右足ＩＫ->右足IK，上半身２->上半身2）"), ImportRep.MatchedBoneTrackCount, 2);
		TestEqual(TEXT("同义表情、片假名小字与无 PMX 组合表情全部匹配"), ImportRep.MatchedMorphTrackCount, 4);
	}

	return !HasAnyErrors();
}

#endif
