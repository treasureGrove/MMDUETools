#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSpawnable.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDProductionReloadTest, "Ue5MMDTools.Creation.ProductionReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	struct FProductionTestMount
	{
		FString Root;
		FString Directory;
		bool bOwned = false;

		~FProductionTestMount()
		{
			if (bOwned) FPackageName::UnRegisterMountPoint(Root, Directory + TEXT("/"));
		}

		bool Mount(const TSharedPtr<FJsonObject>& Report, const TCHAR* RootField,
			const TCHAR* DirectoryField, const TCHAR* Prefix)
		{
			if (!Report->TryGetStringField(RootField, Root) || !Report->TryGetStringField(DirectoryField, Directory)
				|| !Root.StartsWith(Prefix) || !Root.EndsWith(TEXT("/")) || Directory.IsEmpty()
				|| !FPackageName::IsValidLongPackageName(Root + TEXT("MMDReloadProbe"))) return false;
			Directory = FPaths::ConvertRelativePathToFull(Directory);
			FPaths::NormalizeDirectoryName(Directory);
			if (!FPaths::CollapseRelativeDirectories(Directory) || !IFileManager::Get().DirectoryExists(*Directory)) return false;
			if (FPackageName::MountPointExists(Root))
			{
				FString Probe;
				return FPackageName::TryConvertLongPackageNameToFilename(Root + TEXT("MMDReloadProbe"), Probe)
					&& FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(FPaths::GetPath(Probe)), Directory);
			}
			FPackageName::RegisterMountPoint(Root, Directory + TEXT("/"));
			bOwned = true;
			return true;
		}
	};
}

bool FMMDProductionReloadTest::RunTest(const FString& Parameters)
{
	FString Output;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MMDProductionDirectory="), Output))
	{
		AddWarning(TEXT("未提供 -MMDProductionDirectory=<样片目录>，跳过实际作品资产读回。"));
		return true;
	}
	FString Json;
	TSharedPtr<FJsonObject> Report;
	if (!FFileHelper::LoadFileToString(Json, *(Output / TEXT("report.json")))
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Report) || !Report.IsValid())
	{
		AddError(TEXT("无法读取作品 report.json。")); return false;
	}
	bool bSuccess = false, bEditable = false;
	Report->TryGetBoolField(TEXT("success"), bSuccess);
	Report->TryGetBoolField(TEXT("saved_editable_assets"), bEditable);
	if (!TestTrue(TEXT("作品报告制作成功且保存了可编辑资产"), bSuccess && bEditable)) return false;
	FProductionTestMount ModelMount, AssetsMount;
	FString ModelSourceType;
	Report->TryGetStringField(TEXT("model_source_type"), ModelSourceType);
	const bool bProjectModel = ModelSourceType == TEXT("project_asset");
	if (!bProjectModel && !TestTrue(TEXT("挂载真实模型缓存目录"),
		ModelMount.Mount(Report, TEXT("model_mount_root"), TEXT("model_directory"), TEXT("/Game/MMDModels/")))) return false;
	if (!TestTrue(TEXT("挂载真实作品资产目录"),
		AssetsMount.Mount(Report, TEXT("assets_mount_root"), TEXT("assets_directory"), TEXT("/Game/MMDProductions/")))) return false;
	FString SequencePath, AnimationPath, MeshPath;
	double FramesValue = 0, FpsValue = 0;
	if (!Report->TryGetStringField(TEXT("sequence"), SequencePath) || !SequencePath.StartsWith(AssetsMount.Root)
		|| !Report->TryGetStringField(TEXT("baked_animation"), AnimationPath) || !AnimationPath.StartsWith(AssetsMount.Root)
		|| !Report->TryGetStringField(TEXT("mesh"), MeshPath) || !MeshPath.StartsWith(bProjectModel ? TEXT("/Game/") : ModelMount.Root)
		|| !Report->TryGetNumberField(TEXT("frames"), FramesValue) || FramesValue < 1
		|| !Report->TryGetNumberField(TEXT("fps"), FpsValue) || FpsValue < 1)
	{
		AddError(TEXT("作品报告的对象路径、帧数或采样率无效。")); return false;
	}
	if (bProjectModel)
	{
		FString ModelAssetPath, MeshFilename;
		Report->TryGetStringField(TEXT("model_asset"), ModelAssetPath);
		const FString Package = FPackageName::ObjectPathToPackageName(MeshPath);
		if (!TestEqual(TEXT("作品引用原工程模型资产"), ModelAssetPath, MeshPath)
			|| !TestTrue(TEXT("模型资产包路径有效"), FPackageName::IsValidLongPackageName(Package))
			|| !FPackageName::TryConvertLongPackageNameToFilename(Package, MeshFilename, FPackageName::GetAssetPackageExtension())
			|| !TestTrue(TEXT("模型实际保存在工程 Content"),
				FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(MeshFilename), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()))
				&& IFileManager::Get().FileSize(*MeshFilename) > 0)) return false;
		bool bSourcesUnchanged = false;
		Report->TryGetBoolField(TEXT("source_asset_dirty_flags_unchanged"), bSourcesUnchanged);
		TestTrue(TEXT("制作后源模型、Skeleton 和动画未被修改"), bSourcesUnchanged);
	}
	const int32 Frames = FMath::RoundToInt(FramesValue), Fps = FMath::RoundToInt(FpsValue);
	TStrongObjectPtr<ULevelSequence> Sequence(LoadObject<ULevelSequence>(nullptr, *SequencePath));
	TStrongObjectPtr<UAnimSequence> Animation(LoadObject<UAnimSequence>(nullptr, *AnimationPath));
	TStrongObjectPtr<USkeletalMesh> Mesh(LoadObject<USkeletalMesh>(nullptr, *MeshPath));
	if (!TestNotNull(TEXT("从磁盘加载作品 LevelSequence"), Sequence.Get())
		|| !TestNotNull(TEXT("从磁盘加载已烘焙动画"), Animation.Get())
		|| !TestNotNull(TEXT("解析序列引用的原模型网格"), Mesh.Get())) return false;
	Animation->WaitOnExistingCompression();
	const IAnimationDataModel* Model = Animation->GetDataModel();
	if (!TestNotNull(TEXT("已保存动画的数据模型"), Model)) return false;
	TestEqual(TEXT("动画完整覆盖视频帧数"), Model->GetNumberOfFrames(), Frames);
	TestEqual(TEXT("动画额外保存末帧保持键"), Model->GetNumberOfKeys(), Frames + 1);
	TestTrue(TEXT("动画采样率"), Model->GetFrameRate() == FFrameRate(Fps, 1));
	TestTrue(TEXT("动画长度与视频一致且末帧不回环"), FMath::IsNearlyEqual(Animation->GetPlayLength(), float(Frames) / Fps, 0.0001f));
	TestEqual(TEXT("动画保存全部实际骨骼轨道"), Model->GetNumBoneTracks(), Mesh->GetRefSkeleton().GetNum());
	TestTrue(TEXT("动画引用原缓存 Skeleton"), Animation->GetSkeleton() == Mesh->GetSkeleton());
	TestTrue(TEXT("已保存动画具有有效骨骼压缩数据"), Animation->IsBoneCompressedDataValid());
	FString SourceAnimationPath;
	if (Report->TryGetStringField(TEXT("source_animation_asset"), SourceAnimationPath) && !SourceAnimationPath.IsEmpty())
	{
		TStrongObjectPtr<UAnimSequence> SourceAnimation(LoadObject<UAnimSequence>(nullptr, *SourceAnimationPath));
		if (!TestNotNull(TEXT("新进程读回原保存动画"), SourceAnimation.Get())
			|| !TestNotNull(TEXT("原动画保留可编辑数据"), SourceAnimation->GetDataModel())) return false;
		TestTrue(TEXT("原保存动画与作品骨架相同"), SourceAnimation->GetSkeleton() == Mesh->GetSkeleton());
		double PlaybackRate = 1.0;
		Report->TryGetNumberField(TEXT("playback_rate"), PlaybackRate);
		for (const FFloatCurve& SourceCurve : SourceAnimation->GetDataModel()->GetFloatCurves())
		{
			const FFloatCurve* SavedCurve = nullptr;
			for (const FFloatCurve& Curve : Model->GetFloatCurves())
				if (Curve.GetName() == SourceCurve.GetName()) { SavedCurve = &Curve; break; }
			if (!TestNotNull(TEXT("作品保存源动画的表情曲线"), SavedCurve)) return false;
			for (int32 Sample : { 0, Frames / 2, Frames - 1 })
			{
				const float Time = float(Sample) / Fps;
				TestTrue(TEXT("表情曲线按倍速与实际输出帧一致"), FMath::IsNearlyEqual(
					SavedCurve->FloatCurve.Eval(Time), SourceCurve.FloatCurve.Eval(float(Time * PlaybackRate)), 0.0001f));
			}
		}
	}
	double MatchedMorphs = 0;
	Report->TryGetNumberField(TEXT("matched_morph_tracks"), MatchedMorphs);
	if (MatchedMorphs > 0) TestTrue(TEXT("源动作表情曲线随物理结果一并保存"), Model->GetFloatCurves().Num() > 0);
	AddInfo(FString::Printf(TEXT("读回动画：%d 骨骼轨道，%d 表情曲线，%d 帧，%d fps，%.3f 秒。"),
		Model->GetNumBoneTracks(), Model->GetFloatCurves().Num(), Frames, Fps, Animation->GetPlayLength()));
	UMovieScene* Scene = Sequence->GetMovieScene();
	if (!TestNotNull(TEXT("序列包含 MovieScene"), Scene)) return false;
	TestTrue(TEXT("序列显示采样率"), Scene->GetDisplayRate() == FFrameRate(Fps, 1));
	TestTrue(TEXT("序列时钟采样率"), Scene->GetTickResolution() == FFrameRate(Fps, 1));
	TestEqual(TEXT("序列起始帧"), Scene->GetPlaybackRange().GetLowerBoundValue().Value, 0);
	TestEqual(TEXT("序列结束帧"), Scene->GetPlaybackRange().GetUpperBoundValue().Value, Frames);
	FGuid CharacterGuid, CameraGuid, AnimationBindingGuid;
	for (int32 Index = 0; Index < Scene->GetSpawnableCount(); ++Index)
	{
		FMovieSceneSpawnable& Spawnable = Scene->GetSpawnable(Index);
		if (ASkeletalMeshActor* Template = Cast<ASkeletalMeshActor>(Spawnable.GetObjectTemplate()))
		{
			CharacterGuid = Spawnable.GetGuid();
			TestTrue(TEXT("保存的角色模板指向原缓存网格"), Template->GetSkeletalMeshComponent()->GetSkeletalMeshAsset() == Mesh.Get());
			TestEqual(TEXT("角色模板使用单节点动画"), Template->GetSkeletalMeshComponent()->GetAnimationMode(), EAnimationMode::AnimationSingleNode);
			TestTrue(TEXT("已烘焙角色关闭后处理动画，避免物理重复"), Template->GetSkeletalMeshComponent()->GetDisablePostProcessBlueprint());
		}
		if (Cast<ACameraActor>(Spawnable.GetObjectTemplate())) CameraGuid = Spawnable.GetGuid();
	}
	for (const FMovieSceneBinding& Binding : AsConst(*Scene).GetBindings())
		for (UMovieSceneTrack* Track : Binding.GetTracks())
			if (auto* AnimationTrack = Cast<UMovieSceneSkeletalAnimationTrack>(Track))
				for (UMovieSceneSection* Section : AnimationTrack->GetAllSections())
					if (auto* AnimationSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
					{
						AnimationBindingGuid = Binding.GetObjectGuid();
						TestTrue(TEXT("序列动画片段引用持久化的 Bullet 烘焙动画"), AnimationSection->Params.Animation == Animation.Get());
					}
	if (!TestTrue(TEXT("序列包含角色、相机和动画绑定"), CharacterGuid.IsValid() && CameraGuid.IsValid() && AnimationBindingGuid.IsValid())) return false;
	auto* CutTrack = Cast<UMovieSceneCameraCutTrack>(Scene->GetCameraCutTrack());
	if (!TestNotNull(TEXT("序列保存镜头切换轨道"), CutTrack) || !TestTrue(TEXT("镜头切换轨道包含片段"), !CutTrack->GetAllSections().IsEmpty())) return false;
	const auto* CutSection = Cast<UMovieSceneCameraCutSection>(CutTrack->GetAllSections()[0]);
	TestTrue(TEXT("镜头切换引用已保存的相机"), CutSection && CutSection->GetCameraBindingID().GetGuid() == CameraGuid);

	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("创建读回验证的临时关卡"), World)) return false;
	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bDisableCameraCuts = false;
	Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
	ALevelSequenceActor* SequenceActor = nullptr;
	ULevelSequencePlayer* Player = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence.Get(), Settings, SequenceActor);
	if (!TestNotNull(TEXT("创建真实序列播放器"), Player)) return false;
	ON_SCOPE_EXIT { Player->Stop(); if (SequenceActor) SequenceActor->Destroy(); };
	TArray<int32> Samples;
	Samples.Add(0); Samples.AddUnique(Frames / 2); Samples.AddUnique(Frames - 1);
	const TArray<TSharedPtr<FJsonValue>>* FrameData = nullptr;
	Report->TryGetArrayField(TEXT("frame_data"), FrameData);
	for (int32 Frame : Samples)
	{
		Player->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(FFrameTime(Frame), EUpdatePositionMethod::Jump));
		const FMovieSceneObjectBindingID CharacterId{UE::MovieScene::FRelativeObjectBindingID(CharacterGuid)};
		const FMovieSceneObjectBindingID CameraId{UE::MovieScene::FRelativeObjectBindingID(CameraGuid)};
		ASkeletalMeshActor* SpawnedCharacter = nullptr;
		ACameraActor* SpawnedCamera = nullptr;
		for (UObject* Object : Player->GetBoundObjects(CharacterId)) if (auto* Actor = Cast<ASkeletalMeshActor>(Object)) SpawnedCharacter = Actor;
		for (UObject* Object : Player->GetBoundObjects(CameraId)) if (auto* Actor = Cast<ACameraActor>(Object)) SpawnedCamera = Actor;
		if (!TestNotNull(*FString::Printf(TEXT("第%d帧实际生成角色"), Frame), SpawnedCharacter)
			|| !TestNotNull(*FString::Printf(TEXT("第%d帧实际生成镜头"), Frame), SpawnedCamera)) return false;
		USkeletalMeshComponent* Component = SpawnedCharacter->GetSkeletalMeshComponent();
		TestTrue(TEXT("实际角色绑定缓存网格"), Component->GetSkeletalMeshAsset() == Mesh.Get());
		const FMovieSceneObjectBindingID AnimationId{UE::MovieScene::FRelativeObjectBindingID(AnimationBindingGuid)};
		TestTrue(TEXT("动画轨道的组件绑定能够实际解析"), Player->GetBoundObjects(AnimationId).Contains(Component));
		TestTrue(TEXT("实际切镜激活保存的相机组件"), Player->GetActiveCameraComponent() == SpawnedCamera->GetCameraComponent());
		Component->TickAnimation(0.0f, false);
		Component->RefreshBoneTransforms();
		Component->HandleExistingParallelEvaluationTask(true, true);
		TestNotNull(TEXT("序列为角色建立了动画实例"), Component->GetAnimInstance());
		const TArray<FTransform>& ActualPose = Component->GetBoneSpaceTransforms();
		TestEqual(TEXT("序列实际评估出全部局部骨骼"), ActualPose.Num(), Mesh->GetRefSkeleton().GetNum());
		int32 Mismatched = 0;
		for (int32 Bone = 0; Bone < ActualPose.Num(); ++Bone)
		{
			const FTransform Expected = Model->EvaluateBoneTrackTransform(Mesh->GetRefSkeleton().GetBoneName(Bone),
				FFrameTime(Frame), EAnimInterpolationType::Linear);
			if (!ActualPose[Bone].Equals(Expected, 0.05)) ++Mismatched;
		}
		TestEqual(*FString::Printf(TEXT("第%d帧实际姿态匹配磁盘中的烘焙轨道"), Frame), Mismatched, 0);
		if (FrameData && FrameData->IsValidIndex(Frame) && (*FrameData)[Frame].IsValid())
		{
			const TSharedPtr<FJsonObject> Shot = (*FrameData)[Frame]->AsObject();
			FString LocationString;
			double Fov = 0;
			FVector Location;
			if (Shot.IsValid() && Shot->TryGetStringField(TEXT("camera_location"), LocationString) && Location.InitFromString(LocationString))
				TestTrue(TEXT("读回镜头位置匹配实际成片记录"), SpawnedCamera->GetActorLocation().Equals(Location, 0.02));
			if (Shot.IsValid() && Shot->TryGetNumberField(TEXT("horizontal_fov"), Fov))
				TestTrue(TEXT("读回镜头视角匹配实际成片记录"), FMath::IsNearlyEqual(SpawnedCamera->GetCameraComponent()->FieldOfView, float(Fov), 0.001f));
		}
	}
	return !HasAnyErrors();
}

#endif
