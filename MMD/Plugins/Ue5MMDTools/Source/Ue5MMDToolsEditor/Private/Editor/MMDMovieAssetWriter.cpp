#include "MMDMovieAssetWriter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool SaveProductionAsset(UObject* Asset, const FString& Directory, FString& Error)
	{
		FString Filename;
		if (!Asset || !FPackageName::TryConvertLongPackageNameToFilename(Asset->GetOutermost()->GetName(),
			Filename, FPackageName::GetAssetPackageExtension())
			|| !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), Directory)
			|| IFileManager::Get().FileExists(*Filename))
		{
			Error = TEXT("作品资产保存路径无效或已经存在，拒绝覆盖。"); return false;
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
		{
			Error = TEXT("无法保存作品资产：") + Filename; return false;
		}
		FAssetRegistryModule::AssetCreated(Asset);
		return true;
	}

	bool AddSpawnTrack(UMovieScene* Scene, const FGuid& Guid, int32 Frames)
	{
		auto* Track = Scene->AddTrack<UMovieSceneSpawnTrack>(Guid);
		if (!Track) return false;
		Track->SetObjectId(Guid);
		UMovieSceneSection* Section = Track->CreateNewSection();
		if (!Section) return false;
		Track->AddSection(*Section);
		Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(Frames)));
		return true;
	}

	FGuid BindComponent(ULevelSequence* Sequence, UMovieScene* Scene, AActor* Actor,
		UActorComponent* Component, const FGuid& ActorGuid)
	{
		const FGuid Guid = Scene->AddPossessable(Component->GetName(), Component->GetClass());
		if (FMovieScenePossessable* Possessable = Scene->FindPossessable(Guid)) Possessable->SetParent(ActorGuid, Scene);
		if (FMovieSceneSpawnable* Spawnable = Scene->FindSpawnable(ActorGuid)) Spawnable->AddChildPossessable(Guid);
		Sequence->BindPossessableObject(Guid, *Component, Actor);
		return Guid;
	}
}

bool MMDMovieAssetWriter::Write(const FString& OutputDir, USkeletalMesh* Mesh, UAnimSequence* SourceAnimation,
	const TArray<TArray<FTransform>>& BoneLocalTracks, const TArray<FTransform>& CameraTransforms,
	const TArray<float>& HorizontalFovs, const TArray<bool>& CameraCuts, int32 Fps,
	FResult& OutResult, FString& OutError, float PlaybackRate)
{
	OutResult = FResult{};
	const int32 Frames = CameraTransforms.Num();
	if (!Mesh || !Mesh->GetSkeleton() || !SourceAnimation || !SourceAnimation->GetDataModel() || Fps <= 0
		|| !FMath::IsFinite(PlaybackRate) || PlaybackRate <= 0
		|| Frames < 1 || HorizontalFovs.Num() != Frames || (!CameraCuts.IsEmpty() && CameraCuts.Num() != Frames)
		|| BoneLocalTracks.Num() != Mesh->GetRefSkeleton().GetNum())
	{
		OutError = TEXT("作品资产缺少有效网格、源动画或逐帧数据。"); return false;
	}
	for (const TArray<FTransform>& Track : BoneLocalTracks)
	{
		if (Track.Num() != Frames) { OutError = TEXT("作品骨骼轨道的帧数不一致。"); return false; }
		for (const FTransform& Transform : Track)
			if (Transform.ContainsNaN()) { OutError = TEXT("作品骨骼轨道包含无效变换。"); return false; }
	}
	for (int32 Frame = 0; Frame < Frames; ++Frame)
		if (CameraTransforms[Frame].ContainsNaN() || !FMath::IsFinite(HorizontalFovs[Frame])
			|| HorizontalFovs[Frame] <= 0 || HorizontalFovs[Frame] >= 180)
		{
			OutError = TEXT("作品镜头包含无效变换或视角。"); return false;
		}
	FString Output = FPaths::ConvertRelativePathToFull(OutputDir);
	FPaths::NormalizeDirectoryName(Output);
	FPaths::CollapseRelativeDirectories(Output);
	FString SessionName = FPaths::GetCleanFilename(Output);
	for (TCHAR& Character : SessionName) if (!FChar::IsAlnum(Character) && Character != TEXT('_')) Character = TEXT('_');
	const FString Directory = Output / TEXT("Assets");
	const FString Root = TEXT("/Game/MMDProductions/") + SessionName + TEXT("/");
	if (SessionName.IsEmpty() || !FPackageName::IsValidLongPackageName(Root + TEXT("LS_MMDProduction"))
		|| FPackageName::MountPointExists(Root))
	{
		OutError = TEXT("作品资产挂载名称无效或已占用，请使用新的输出目录名。"); return false;
	}
	TArray<FString> Existing;
	IFileManager::Get().FindFilesRecursive(Existing, *Directory, TEXT("*"), true, false);
	if (!Existing.IsEmpty() || !IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("作品 Assets 目录不为空或无法创建。"); return false;
	}
	FPackageName::RegisterMountPoint(Root, Directory + TEXT("/"));
	const FString AnimPackage = Root + TEXT("A_MMDProduction");
	const FString SequencePackage = Root + TEXT("LS_MMDProduction");
	if (FindPackage(nullptr, *AnimPackage) || FindPackage(nullptr, *SequencePackage))
	{
		OutError = TEXT("作品包名称已在内存中占用，拒绝替换。"); return false;
	}
	TStrongObjectPtr<UAnimSequence> Animation(NewObject<UAnimSequence>(CreatePackage(*AnimPackage),
		TEXT("A_MMDProduction"), RF_Public | RF_Standalone));
	if (!Animation.IsValid()) { OutError = TEXT("无法创建作品动画。"); return false; }
	Animation->SetSkeleton(Mesh->GetSkeleton());
	Animation->SetPreviewMesh(Mesh);
	IAnimationDataController& Controller = Animation->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("保存 MMD 实际动画姿态")), false);
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(Fps, 1), false);
	// 末尾保持一帧，避免 Sequencer 在最后一个输出采样处循环回动画开头。
	const int32 AnimationFrames = Frames;
	const int32 Keys = AnimationFrames + 1;
	Controller.SetNumberOfFrames(FFrameNumber(AnimationFrames), false);
	bool bTracksWritten = true;
	for (int32 Bone = 0; Bone < BoneLocalTracks.Num(); ++Bone)
	{
		TArray<FVector3f> Positions, Scales;
		TArray<FQuat4f> Rotations;
		Positions.Reserve(Keys); Rotations.Reserve(Keys); Scales.Reserve(Keys);
		for (int32 Frame = 0; Frame < Keys; ++Frame)
		{
			const FTransform& Transform = BoneLocalTracks[Bone][FMath::Min(Frame, Frames - 1)];
			Positions.Add(FVector3f(Transform.GetTranslation()));
			Rotations.Add(FQuat4f(Transform.GetRotation().GetNormalized()));
			Scales.Add(FVector3f(Transform.GetScale3D()));
		}
		const FName Name = Mesh->GetRefSkeleton().GetBoneName(Bone);
		bTracksWritten &= Controller.AddBoneCurve(Name, false);
		bTracksWritten &= Controller.SetBoneTrackKeys(Name, Positions, Rotations, Scales, false);
	}
	const float AnimationLength = float(AnimationFrames) / Fps;
	const float LastSampleTime = float(Frames - 1) / Fps;
	for (const FFloatCurve& Curve : SourceAnimation->GetDataModel()->GetFloatCurves())
	{
		if (Curve.GetName().IsNone()) continue;
		const FAnimationCurveIdentifier Id(Curve.GetName(), ERawCurveTrackTypes::RCT_Float);
		TArray<FRichCurveKey> CurveKeys;
		if (FMath::IsNearlyEqual(PlaybackRate, 1.0f))
		{
			for (const FRichCurveKey& Key : Curve.FloatCurve.GetConstRefOfKeys())
				if (Key.Time >= 0 && Key.Time < LastSampleTime) CurveKeys.Add(Key);
			if (CurveKeys.IsEmpty() || CurveKeys[0].Time > 0) CurveKeys.Insert(FRichCurveKey(0, Curve.FloatCurve.Eval(0)), 0);
			if (LastSampleTime > 0) CurveKeys.Add(FRichCurveKey(LastSampleTime, Curve.FloatCurve.Eval(LastSampleTime)));
		}
		else
		{
			// 与实际输出骨骼按同一时间采样，表情不再沿用原速时间轴。
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				const float Time = float(Frame) / Fps;
				FRichCurveKey Key(Time, Curve.FloatCurve.Eval(Time * PlaybackRate));
				Key.InterpMode = RCIM_Linear;
				CurveKeys.Add(Key);
			}
		}
		CurveKeys.Add(FRichCurveKey(AnimationLength, Curve.FloatCurve.Eval(LastSampleTime * PlaybackRate)));
		bTracksWritten &= Controller.AddCurve(Id, Curve.GetCurveTypeFlags(), false);
		bTracksWritten &= Controller.SetCurveKeys(Id, CurveKeys, false);
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	if (!bTracksWritten) { OutError = TEXT("无法写入完整的作品动画轨道。"); return false; }
	Animation->PostEditChange();
	Animation->WaitOnExistingCompression();
	if (!SaveProductionAsset(Animation.Get(), Directory, OutError)) return false;

	TStrongObjectPtr<ULevelSequence> Sequence(NewObject<ULevelSequence>(CreatePackage(*SequencePackage),
		TEXT("LS_MMDProduction"), RF_Public | RF_Standalone));
	if (!Sequence.IsValid()) { OutError = TEXT("无法创建作品序列。"); return false; }
	Sequence->Initialize();
	UMovieScene* Scene = Sequence->GetMovieScene();
	Scene->SetDisplayRate(FFrameRate(Fps, 1));
	Scene->SetTickResolutionDirectly(FFrameRate(Fps, 1));
	Scene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
	Scene->SetPlaybackRange(FFrameNumber(0), Frames);
	ASkeletalMeshActor* Actor = NewObject<ASkeletalMeshActor>(Scene, TEXT("MMDProductionCharacter"), RF_Transactional);
	USkeletalMeshComponent* Component = Actor->GetSkeletalMeshComponent();
	Component->SetSkeletalMeshAsset(Mesh);
	Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Component->SetDisablePostProcessBlueprint(true);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const FGuid ActorGuid = Scene->AddSpawnable(TEXT("MMD 角色 · 已烘焙动画"), *Actor);
	if (!ActorGuid.IsValid() || !AddSpawnTrack(Scene, ActorGuid, Frames))
	{
		OutError = TEXT("无法创建作品角色绑定。"); return false;
	}
	const FGuid ComponentGuid = BindComponent(Sequence.Get(), Scene, Actor, Component, ActorGuid);
	auto* AnimTrack = Scene->AddTrack<UMovieSceneSkeletalAnimationTrack>(ComponentGuid);
	UMovieSceneSection* AnimSection = AnimTrack ? AnimTrack->AddNewAnimation(FFrameNumber(0), Animation.Get()) : nullptr;
	if (!AnimSection) { OutError = TEXT("无法创建作品动画片段。"); return false; }
	AnimSection->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(Frames)));
	ACameraActor* Camera = NewObject<ACameraActor>(Scene, TEXT("MMDProductionCamera"), RF_Transactional);
	Camera->SetActorTransform(CameraTransforms[0]);
	Camera->GetCameraComponent()->SetFieldOfView(HorizontalFovs[0]);
	const FGuid CameraGuid = Scene->AddSpawnable(TEXT("MMD 镜头"), *Camera);
	if (!CameraGuid.IsValid() || !AddSpawnTrack(Scene, CameraGuid, Frames))
	{
		OutError = TEXT("无法创建作品镜头绑定。"); return false;
	}
	auto* TransformTrack = Scene->AddTrack<UMovieScene3DTransformTrack>(CameraGuid);
	auto* TransformSection = TransformTrack ? Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection()) : nullptr;
	const FGuid CameraComponentGuid = BindComponent(Sequence.Get(), Scene, Camera, Camera->GetCameraComponent(), CameraGuid);
	auto* FovTrack = Scene->AddTrack<UMovieSceneFloatTrack>(CameraComponentGuid);
	auto* FovSection = FovTrack ? Cast<UMovieSceneFloatSection>(FovTrack->CreateNewSection()) : nullptr;
	auto* CutTrack = Cast<UMovieSceneCameraCutTrack>(Scene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass()));
	if (!TransformSection || !FovSection || !CutTrack)
	{
		OutError = TEXT("无法创建作品镜头轨道。"); return false;
	}
	TransformTrack->SetPropertyNameAndPath(TEXT("Transform"), TEXT("Transform"));
	TransformTrack->AddSection(*TransformSection);
	FovTrack->SetPropertyNameAndPath(TEXT("FieldOfView"), TEXT("FieldOfView"));
	FovTrack->AddSection(*FovSection);
	const TRange<FFrameNumber> Range(FFrameNumber(0), FFrameNumber(Frames));
	TransformSection->SetRange(Range); FovSection->SetRange(Range);
	FVector PreviousEuler = FVector::ZeroVector;
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		const FVector Location = CameraTransforms[Frame].GetLocation();
		const FRotator Rotation = CameraTransforms[Frame].Rotator();
		FVector Euler(Rotation.Roll, Rotation.Pitch, Rotation.Yaw);
		if (Frame > 0 && (CameraCuts.IsEmpty() || !CameraCuts[Frame]))
			for (int32 Axis = 0; Axis < 3; ++Axis) Euler[Axis] = PreviousEuler[Axis] + FMath::FindDeltaAngleDegrees(PreviousEuler[Axis], Euler[Axis]);
		PreviousEuler = Euler;
		const bool bCutAfter = Frame + 1 < Frames && !CameraCuts.IsEmpty() && CameraCuts[Frame + 1];
		const double Values[] = {Location.X, Location.Y, Location.Z, Euler.X, Euler.Y, Euler.Z, 1, 1, 1};
		const TCHAR* Names[] = {TEXT("Location.X"), TEXT("Location.Y"), TEXT("Location.Z"), TEXT("Rotation.X"), TEXT("Rotation.Y"), TEXT("Rotation.Z"), TEXT("Scale.X"), TEXT("Scale.Y"), TEXT("Scale.Z")};
		for (int32 ChannelIndex = 0; ChannelIndex < 9; ++ChannelIndex)
		{
			auto* Channel = TransformSection->GetChannelProxy().GetChannelByName<FMovieSceneDoubleChannel>(Names[ChannelIndex]).Get();
			if (!Channel) { OutError = TEXT("作品镜头缺少变换通道。"); return false; }
			if (bCutAfter) Channel->AddConstantKey(FFrameNumber(Frame), Values[ChannelIndex]);
			else Channel->AddLinearKey(FFrameNumber(Frame), Values[ChannelIndex]);
		}
		if (bCutAfter) FovSection->GetChannel().AddConstantKey(FFrameNumber(Frame), HorizontalFovs[Frame]);
		else FovSection->GetChannel().AddLinearKey(FFrameNumber(Frame), HorizontalFovs[Frame]);
	}
	auto* CutSection = CutTrack->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraGuid), FFrameNumber(0));
	if (!CutSection) { OutError = TEXT("无法创建作品镜头切换片段。"); return false; }
	CutSection->SetRange(Range);
	if (!SaveProductionAsset(Sequence.Get(), Directory, OutError)) return false;
	OutResult.AssetsDirectory = Directory;
	OutResult.MountRoot = Root;
	OutResult.SequenceObjectPath = Sequence->GetPathName();
	OutResult.BakedAnimationObjectPath = Animation->GetPathName();
	return true;
}
