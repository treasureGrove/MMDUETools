// -run=MMDCreateMovie (-ModelDirectory=<Saved 缓存> | -ModelAsset=/Game/...) (-Motion=<VMD> | -AnimationAsset=/Game/...)
// -Output=<Saved 内空目录> [-Duration=秒] [-Width=1280] [-Height=720] [-Fps=30]
// [-Preset=InfinityNikki] [-Stage=Studio] [-MaxFrames=帧数] [-Warmup=1]
// [-Physics=0|1] [-PlaybackRate=1] [-ImportVmdMorphs=0|1] [-CameraMode=Fixed -CameraLocation="X=0 Y=500 Z=100" -CameraRotation="P=0 Y=-90 R=0" -CameraFOV=35]
// 渲染进程需要 -AllowCommandletRendering -RenderOffscreen -d3d12，原生风格还需对应后端启动参数。
#include "MMDCreateMovieCommandlet.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "UI/MMDPreviewFraming.h"
#include "Editor/MMDMovieAssetWriter.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "Import/MMDModelDataAsset.h"
#include "Import/TMMDMeshBuilder.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PreviewScene.h"
#include "RenderGraphBuilder.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "RenderingThread.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	FString MMDMovieFullPath(const FString& Input)
	{
		FString Result = FPaths::ConvertRelativePathToFull(Input);
		FPaths::NormalizeFilename(Result);
		return FPaths::CollapseRelativeDirectories(Result) ? Result : FString();
	}

	bool MMDMovieHasNoLink(const FString& Path)
	{
		FString Current = Path;
		while (!Current.IsEmpty())
		{
			if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

	bool MMDMovieWriteReport(const TSharedRef<FJsonObject>& Report, const FString& Path)
	{
		FString Json;
		return FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	bool MMDMovieResolveProjectAsset(FString& ObjectPath, FString& Error)
	{
		const FString Package = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FString Canonical = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
		FString Filename;
		if (!Package.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Package)
			|| (ObjectPath != Package && ObjectPath != Canonical)
			|| !FPackageName::TryConvertLongPackageNameToFilename(Package, Filename, FPackageName::GetAssetPackageExtension())
			|| !FPaths::IsUnderDirectory(MMDMovieFullPath(Filename), MMDMovieFullPath(FPaths::ProjectContentDir()))
			|| !MMDMovieHasNoLink(MMDMovieFullPath(Filename)) || IFileManager::Get().FileSize(*Filename) <= 0)
		{
			Error = TEXT("资产须为当前工程 Content 中已经保存的 /Game 包：") + ObjectPath;
			return false;
		}
		ObjectPath = Canonical;
		return true;
	}

	bool MMDMovieReadCameraComponent(const FString& Text, const TCHAR* Key, double& Value)
	{
		FString Number;
		return FParse::Value(*Text, Key, Number) && FDefaultValueHelper::ParseDouble(Number, Value) && FMath::IsFinite(Value);
	}

	FString MMDMoviePresetPath(const FString& Id)
	{
		if (Id.StartsWith(TEXT("/Ue5MMDTools/Stages/Presets/"))) return Id;
		if (Id.Equals(TEXT("EndfieldV2"), ESearchCase::IgnoreCase))
			return TEXT("/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2.DA_Render_EndfieldV2");
		if (Id.Equals(TEXT("Endfield"), ESearchCase::IgnoreCase))
			return TEXT("/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield.DA_Render_Endfield");
		const TArray<FString> Names = { TEXT("InfinityNikki"), TEXT("SakuraTheater"), TEXT("MoonMirror"), TEXT("NeonConcert"),
			TEXT("OpenWorldToon"), TEXT("SilverCity"), TEXT("CinematicAnime"), TEXT("IndustrialAnime") };
		for (const FString& Name : Names)
			if (Id.Equals(Name, ESearchCase::IgnoreCase))
				return FString::Printf(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_%s.DA_Render_%s"), *Name, *Name);
		return FString();
	}

	struct FMMDMovieMount
	{
		FString Root, Directory;
		~FMMDMovieMount() { if (!Root.IsEmpty()) FPackageName::UnRegisterMountPoint(Root, Directory); }
	};

	// 相机六条曲线各占四字节，顺序为 x1、x2、y1、y2；控制点属于区间末端关键帧。
	double MMDMovieBezier(const uint8* Curve, double Alpha)
	{
		const double X1 = FMath::Clamp(double(Curve[0]) / 127.0, 0.0, 1.0);
		const double X2 = FMath::Clamp(double(Curve[1]) / 127.0, 0.0, 1.0);
		const double Y1 = FMath::Clamp(double(Curve[2]) / 127.0, 0.0, 1.0);
		const double Y2 = FMath::Clamp(double(Curve[3]) / 127.0, 0.0, 1.0);
		auto Evaluate = [](double T, double A, double B)
		{
			const double U = 1.0 - T;
			return 3.0 * U * U * T * A + 3.0 * U * T * T * B + T * T * T;
		};
		double Low = 0.0, High = 1.0;
		for (int32 Iteration = 0; Iteration < 24; ++Iteration)
		{
			const double Middle = (Low + High) * 0.5;
			if (Evaluate(Middle, X1, X2) < Alpha) Low = Middle; else High = Middle;
		}
		return Evaluate((Low + High) * 0.5, Y1, Y2);
	}

	struct FMMDMovieCamera
	{
		FTransform Transform;
		float HorizontalFOV = 45.0f;
		float OrthoWidth = 500.0f;
		bool bOrthographic = false;
		bool bCut = false;
	};

	FMMDMovieCamera MMDMovieEvaluateCamera(const TArray<VMDCameraKeyframe>& Keys, double Time, float Aspect,
		const FBoxSphereBounds& InitialBounds)
	{
		FMMDMovieCamera Result;
		if (Keys.IsEmpty())
		{
			// 无配套镜头时缓慢绕行，以真实包围盒投影保留头脚和长发边距。
			const float FOV = 38.0f;
			const double Angle = FMath::DegreesToRadians(10.0 * FMath::Sin(Time * 0.16));
			const FVector Direction = FVector(FMath::Sin(Angle), FMath::Cos(Angle), 0.06).GetSafeNormal();
			const double Distance = MMDGetBoundsFramingDistance(InitialBounds.GetBox(), Direction, Aspect, FOV);
			const FVector Target = InitialBounds.Origin;
			const FVector Location = Target + Direction * Distance;
			Result.Transform = FTransform((Target - Location).Rotation(), Location);
			Result.HorizontalFOV = FOV;
			Result.bCut = Time <= 0.0;
			return Result;
		}
		const double SourceFrame = Time * 30.0;
		int32 Left = 0, Right = Keys.Num();
		while (Left < Right)
		{
			const int32 Middle = (Left + Right) / 2;
			if (double(Keys[Middle].FrameNumber) <= SourceFrame + 0.00001) Left = Middle + 1; else Right = Middle;
		}
		const int32 Current = FMath::Clamp(Left - 1, 0, Keys.Num() - 1);
		const VMDCameraKeyframe& A = Keys[Current];
		const VMDCameraKeyframe& B = Keys[FMath::Min(Current + 1, Keys.Num() - 1)];
		FVector Interest = A.Interest;
		FVector Rotation = A.Rotation;
		double Distance = A.Distance;
		double VerticalFOV = A.ViewAngle;
		const int64 Span = int64(B.FrameNumber) - int64(A.FrameNumber);
		if (Span > 1 && SourceFrame > A.FrameNumber)
		{
			const double Alpha = FMath::Clamp((SourceFrame - A.FrameNumber) / double(Span), 0.0, 1.0);
			for (int32 Axis = 0; Axis < 3; ++Axis)
				Interest[Axis] = FMath::Lerp(A.Interest[Axis], B.Interest[Axis], MMDMovieBezier(B.Interpolation + Axis * 4, Alpha));
			Rotation = FMath::Lerp(A.Rotation, B.Rotation, MMDMovieBezier(B.Interpolation + 12, Alpha));
			Distance = FMath::Lerp(double(A.Distance), double(B.Distance), MMDMovieBezier(B.Interpolation + 16, Alpha));
			VerticalFOV = FMath::Lerp(double(A.ViewAngle), double(B.ViewAngle), MMDMovieBezier(B.Interpolation + 20, Alpha));
		}
		const FQuat Orbit = (FQuat(FVector(0, 0, 1), -Rotation.Z)
			* FQuat(FVector(1, 0, 0), -Rotation.X) * FQuat(FVector(0, 1, 0), -Rotation.Y)).GetNormalized();
		auto ConvertDirection = [](const FVector& V) { return FVector(V.X, -V.Z, V.Y); };
		const FVector Location = ConvertDirection(Interest + Orbit.RotateVector(FVector(0, 0, Distance))) * 8.0;
		const FVector Forward = ConvertDirection(Orbit.RotateVector(FVector(0, 0, 1))).GetSafeNormal();
		const FVector Up = ConvertDirection(Orbit.RotateVector(FVector(0, 1, 0))).GetSafeNormal();
		Result.Transform = FTransform(FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat(), Location);
		const double HalfVertical = FMath::DegreesToRadians(FMath::Clamp(VerticalFOV, 1.0, 150.0)) * 0.5;
		Result.HorizontalFOV = float(FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfVertical) * Aspect)));
		Result.bOrthographic = A.Perspective != 0;
		Result.OrthoWidth = float(FMath::Max(1.0, 2.0 * FMath::Abs(Distance) * 8.0 * FMath::Tan(HalfVertical) * Aspect));
		Result.bCut = Time <= 0.0 || (Current > 0 && A.FrameNumber - Keys[Current - 1].FrameNumber <= 1
			&& FMath::Abs(SourceFrame - double(A.FrameNumber)) < 0.51);
		return Result;
	}

	class FMMDMovieAntiAliasing final : public ISceneViewExtension
	{
	public:
		void SetupView(FSceneViewFamily& ViewFamily, FSceneView& View) override
		{
			if (!View.bIsSceneCapture) return;
			bViewStateAvailable = View.State != nullptr;
			bRealtimeView = ViewFamily.bRealtimeUpdate;
			View.AntiAliasingMethod = bViewStateAvailable && bRealtimeView ? AAM_TemporalAA : AAM_FXAA;
			// 原生输出分辨率的 TAA；仅作用于持有此扩展的捕获组件，不注册全局扩展。
			View.PrimaryScreenPercentageMethod = EPrimaryScreenPercentageMethod::SpatialUpscale;
			LastMethod = View.AntiAliasingMethod;
			bLastCameraCut = View.bCameraCut;
			++ConfiguredViews;
		}

		EAntiAliasingMethod LastMethod = AAM_None;
		bool bViewStateAvailable = false;
		bool bRealtimeView = false;
		bool bLastCameraCut = false;
		int32 ConfiguredViews = 0;
	};

	struct FMMDMovieRig
	{
		TUniquePtr<FPreviewScene> Scene;
		TUniquePtr<FMMDStudioDisplayScene> StudioDisplay;
		UWorld* World = nullptr;
		AMMDStageActor* Stage = nullptr;
		AActor* CharacterOwner = nullptr;
		AActor* BaselineOwner = nullptr;
		AActor* CaptureOwner = nullptr;
		TStrongObjectPtr<USkeletalMeshComponent> Character;
		TStrongObjectPtr<USkeletalMeshComponent> Baseline;
		TStrongObjectPtr<USceneCaptureComponent2D> Capture;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UStaticMeshComponent> Floor;
		TSharedPtr<FMMDMovieAntiAliasing, ESPMode::ThreadSafe> AntiAliasing;
		UMMDPhysicsPlaybackAnimInstance* Playback = nullptr;
		UMMDPhysicsPlaybackAnimInstance* BaselinePlayback = nullptr;
		FBoxSphereBounds InitialBounds;
		EMMDLightingEnvironment LightingEnvironment = EMMDLightingEnvironment::Studio3Point;
		TArray<int32> PhysicsBones;
		int64 PhysicsSteps = 0;
		int64 SimulationSteps = 0;
		bool bPhysicsEnabled = true;

		~FMMDMovieRig()
		{
			if (Scene)
			{
				StudioDisplay.Reset();
				UMMDLightingEnvironmentLibrary::ClearLightingEnvironmentFromPreview(Scene.Get());
				if (Capture.IsValid()) Scene->RemoveComponent(Capture.Get());
				if (Character.IsValid()) Scene->RemoveComponent(Character.Get());
				if (Baseline.IsValid()) Scene->RemoveComponent(Baseline.Get());
				if (Floor.IsValid()) Scene->RemoveComponent(Floor.Get());
				if (World)
				{
					for (AActor* Actor : { CharacterOwner, BaselineOwner, CaptureOwner, static_cast<AActor*>(Stage) })
						if (Actor) World->DestroyActor(Actor);
				}
			}
		}

		void Step(float DeltaSeconds)
		{
			FApp::SetDeltaTime(DeltaSeconds);
			World->Tick(LEVELTICK_All, DeltaSeconds);
			for (USkeletalMeshComponent* Component : { Character.Get(), Baseline.Get() })
			{
				Component->TickAnimation(DeltaSeconds, false);
				Component->RefreshBoneTransforms();
				Component->HandleExistingParallelEvaluationTask(true, true);
				Component->UpdateComponentToWorld();
				Component->MarkRenderDynamicDataDirty();
			}
			++SimulationSteps;
			if (bPhysicsEnabled) ++PhysicsSteps;
		}

		double PhysicsDifference() const
		{
			const TArray<FTransform>& Actual = Character->GetComponentSpaceTransforms();
			const TArray<FTransform>& Reference = Baseline->GetComponentSpaceTransforms();
			double Degrees = 0.0;
			for (int32 Index : PhysicsBones)
				if (Actual.IsValidIndex(Index) && Reference.IsValidIndex(Index))
					Degrees = FMath::Max(Degrees, FMath::RadiansToDegrees(Actual[Index].GetRotation().AngularDistance(Reference[Index].GetRotation())));
			return Degrees;
		}
	};

	FMMDMovieCamera MMDMovieShowcaseCamera(const FMMDMovieRig& Rig, double Time, float Aspect)
	{
		FBox Full(ForceInit), Upper(ForceInit);
		const TCHAR* FullBones[] = { TEXT("頭"), TEXT("首"), TEXT("上半身"), TEXT("上半身2"), TEXT("下半身"),
			TEXT("右手首"), TEXT("左手首"), TEXT("右ひじ"), TEXT("左ひじ"), TEXT("右足首"), TEXT("左足首"),
			TEXT("右つま先"), TEXT("左つま先") };
		const TCHAR* UpperBones[] = { TEXT("頭"), TEXT("首"), TEXT("上半身"), TEXT("上半身2"),
			TEXT("右手首"), TEXT("左手首"), TEXT("右ひじ"), TEXT("左ひじ") };
		for (const TCHAR* Name : FullBones)
			if (Rig.Character->GetBoneIndex(Name) != INDEX_NONE) Full += Rig.Character->GetBoneLocation(Name);
		for (const TCHAR* Name : UpperBones)
			if (Rig.Character->GetBoneIndex(Name) != INDEX_NONE) Upper += Rig.Character->GetBoneLocation(Name);
		if (!Full.IsValid) Full = Rig.InitialBounds.GetBox();
		if (!Upper.IsValid) Upper = Full;
		Full.Min -= FVector(26, 20, 8);
		Full.Max += FVector(26, 20, 30);
		Upper.Min -= FVector(20, 16, 12);
		Upper.Max += FVector(20, 16, 28);
		const int32 Segment = FMath::FloorToInt(Time / 6.0);
		auto MakeShot = [&](int32 Index)
		{
			const int32 ShotIndex = (Index % 4 + 4) % 4;
			const bool bUpper = ShotIndex == 1 || ShotIndex == 3;
			const FBox& Box = bUpper ? Upper : Full;
			const FVector Target = Box.GetCenter();
			const FVector Extent = Box.GetExtent();
			const double Fov = bUpper ? 40.0 : 44.0;
			const double Yaw = FMath::DegreesToRadians(ShotIndex == 2 ? 32.0 : ShotIndex == 3 ? -24.0 : 8.0);
			const double HalfH = FMath::DegreesToRadians(Fov * 0.5);
			const double HalfV = FMath::Atan(FMath::Tan(HalfH) / Aspect);
			const double HorizontalExtent = FMath::Abs(FMath::Cos(Yaw)) * Extent.X + FMath::Abs(FMath::Sin(Yaw)) * Extent.Y;
			const double Distance = FMath::Max(HorizontalExtent / FMath::Tan(HalfH), Extent.Z / FMath::Tan(HalfV)) * 1.13;
			const FVector Location = Target + FVector(FMath::Sin(Yaw), FMath::Cos(Yaw), 0.035) * FMath::Max(Distance, 180.0);
			FMMDMovieCamera Shot;
			Shot.Transform = FTransform((Target - Location).Rotation(), Location);
			Shot.HorizontalFOV = float(Fov);
			return Shot;
		};
		const FMMDMovieCamera Current = MakeShot(Segment);
		if (Segment == 0) return Current;
		const FMMDMovieCamera Previous = MakeShot(Segment - 1);
		const float Blend = FMath::SmoothStep(0.0f, 0.8f, float(Time - Segment * 6.0));
		FMMDMovieCamera Result;
		Result.Transform.Blend(Previous.Transform, Current.Transform, Blend);
		Result.HorizontalFOV = FMath::Lerp(Previous.HorizontalFOV, Current.HorizontalFOV, Blend);
		return Result;
	}

	void MMDMovieWaitForAssets(FMMDMovieRig& Rig)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		for (int32 Index = 0; Index < Rig.Character->GetNumMaterials(); ++Index)
			if (UMaterialInterface* Material = Rig.Character->GetMaterial(Index))
				Material->SetForceMipLevelsToBeResident(false, true, 600.0f, 0, true);
		IStreamingManager::Get().StreamAllResources(30.0f);
		Rig.Scene->UpdateCaptureContents();
		USkyLightComponent::UpdateSkyCaptureContents(Rig.World);
		Rig.World->SendAllEndOfFrameUpdates();
		FlushRenderingCommands();
	}

	bool MMDMovieBuildRig(FMMDMovieRig& Rig, USkeletalMesh* Mesh, const UMMDModelDataAsset* Model,
		UAnimSequence* Animation, UMMDStageRenderPreset* CharacterPreset, UMMDStageRenderPreset* StagePreset,
		EMMDStudioDisplayStyle DisplayStyle, int32 Width, int32 Height, bool bPhysicsEnabled, FString& Error)
	{
		Rig.bPhysicsEnabled = bPhysicsEnabled;
		FPreviewScene::ConstructionValues Values;
		Values.SetCreateDefaultLighting(false).SetEditor(true).SetCreatePhysicsScene(false).SetTransactional(false);
		Values.AllowLumenPrimitiveTrackingInPreviewWorld(true);
		Rig.Scene = MakeUnique<FPreviewScene>(Values);
		Rig.World = Rig.Scene->GetWorld();
		if (!Rig.World || !Rig.World->Scene || !Rig.World->Scene->GetSkeletalMeshUpdater())
		{
			Error = TEXT("预览世界没有有效渲染场景，请启用 AllowCommandletRendering 并使用真实 RHI。");
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags = RF_Transient;
		Rig.CharacterOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.BaselineOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.CaptureOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		if (!Rig.CharacterOwner || !Rig.BaselineOwner || !Rig.CaptureOwner)
		{
			Error = TEXT("无法创建独立渲染角色。"); return false;
		}
		Rig.Character.Reset(NewObject<USkeletalMeshComponent>(Rig.CharacterOwner, TEXT("MovieCharacter"), RF_Transient));
		Rig.Baseline.Reset(NewObject<USkeletalMeshComponent>(Rig.BaselineOwner, TEXT("MovieNoPhysicsReference"), RF_Transient));
		Rig.Capture.Reset(NewObject<USceneCaptureComponent2D>(Rig.CaptureOwner, TEXT("MovieCamera"), RF_Transient));
		Rig.Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
		Rig.CharacterOwner->SetRootComponent(Rig.Character.Get());
		Rig.CharacterOwner->AddInstanceComponent(Rig.Character.Get());
		Rig.BaselineOwner->SetRootComponent(Rig.Baseline.Get());
		Rig.BaselineOwner->AddInstanceComponent(Rig.Baseline.Get());
		Rig.CaptureOwner->SetRootComponent(Rig.Capture.Get());
		Rig.CaptureOwner->AddInstanceComponent(Rig.Capture.Get());
		for (USkeletalMeshComponent* Component : { Rig.Character.Get(), Rig.Baseline.Get() })
		{
			Component->SetSkeletalMesh(Mesh);
			Component->SetMobility(EComponentMobility::Movable);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Rig.Scene->AddComponent(Component, FTransform::Identity);
			Component->SetComponentTickEnabled(false);
		}
		Rig.Baseline->SetVisibility(false);
		Rig.Baseline->SetHiddenInGame(true);
		Rig.Baseline->SetRenderInMainPass(false);
		Rig.Baseline->SetCastShadow(false);
		Rig.Character->SetCastShadow(true);
		Rig.Character->SetUpdateAnimationInEditor(true);
		Rig.Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Rig.Character->SetAnimation(Animation);
		Rig.Character->TickAnimation(0.0f, false);
		Rig.Character->RefreshBoneTransforms();
		Rig.Character->UpdateBounds();
		Rig.InitialBounds = Rig.Character->Bounds;
		Rig.Character->SetBoundsScale(2.0f);
		if (Model && bPhysicsEnabled)
			for (const FMMDPhysicsRigidBodyData& Rigid : Model->Rigids)
				if (Rigid.PhysicsMode != 0 && Model->Bones.IsValidIndex(Rigid.RelatedBoneIndex))
				{
					const int32 Bone = Model->Bones[Rigid.RelatedBoneIndex].UEBoneIndex;
					if (Bone != INDEX_NONE) Rig.PhysicsBones.AddUnique(Bone);
				}
		if (CharacterPreset && !UMMDStageLibrary::ApplyCharacterRenderPreset(Rig.Character.Get(), CharacterPreset, true))
		{
			Error = TEXT("角色风格应用失败。"); return false;
		}
		UMMDOutlineLibrary::ApplyMMDOutline(Rig.Character.Get());
		if (FParse::Param(FCommandLine::Get(), TEXT("NoCaptureOutline")))
			UMMDOutlineLibrary::ApplyMMDOutline(Rig.Character.Get(), false);
		if (StagePreset)
		{
			Spawn.bDeferConstruction = true;
			Rig.Stage = Rig.World->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Spawn);
			if (!Rig.Stage) { Error = TEXT("舞台创建失败。"); return false; }
			Rig.Stage->RenderPreset = StagePreset;
			Rig.Stage->bPreviewAnimation = false;
			Rig.Stage->bUseManualTime = true;
			Rig.Stage->FinishSpawning(FTransform::Identity);
			TArray<ULightComponent*> StageLights;
			Rig.Stage->GetComponents<ULightComponent>(StageLights);
			for (ULightComponent* Light : StageLights) Light->SetVisibility(false);
			if (Rig.Stage->GardenSkyLight) Rig.Stage->GardenSkyLight->SetVisibility(false);
			if (Rig.Stage->PostProcess) Rig.Stage->PostProcess->bEnabled = false;
			Rig.Stage->EvaluateStageAtTime(0.0f);
		}
		else
		{
			Rig.StudioDisplay = MakeUnique<FMMDStudioDisplayScene>(*Rig.Scene, Rig.InitialBounds.GetBox(), DisplayStyle);
			if (!Rig.StudioDisplay->IsValid()) { Error = TEXT("默认展示场景创建失败。"); return false; }
		}
		Rig.LightingEnvironment = FMMDStudioDisplayScene::GetLightingEnvironment(DisplayStyle);
		UMMDLightingEnvironmentLibrary::ApplyLightingEnvironmentToPreviewScaled(Rig.Scene.Get(),
			Rig.LightingEnvironment, Rig.InitialBounds.Origin, 1.0f);
		Rig.Target->RenderTargetFormat = RTF_RGBA8;
		Rig.Target->bForceLinearGamma = true;
		Rig.Target->TargetGamma = 2.2f;
		Rig.Target->ClearColor = FLinearColor(0.075f, 0.09f, 0.14f, 1);
		Rig.Target->InitAutoFormat(Width, Height);
		Rig.Target->UpdateResourceImmediate(true);
		Rig.Capture->TextureTarget = Rig.Target.Get();
		Rig.Capture->CaptureSource = SCS_FinalColorLDR;
		Rig.Capture->bCaptureEveryFrame = false;
		Rig.Capture->bCaptureOnMovement = false;
		Rig.Capture->bAlwaysPersistRenderingState = true;
		Rig.Capture->PostProcessBlendWeight = 1.0f;
		UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(Rig.Capture->PostProcessSettings, Rig.LightingEnvironment);
		Rig.Capture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod = true;
		Rig.Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
		Rig.Capture->PostProcessSettings.bOverride_ReflectionMethod = true;
		Rig.Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::Lumen;
		if (FParse::Param(FCommandLine::Get(), TEXT("NoCaptureShadows")))
			Rig.Capture->ShowFlags.SetDynamicShadows(false);
		if (FParse::Param(FCommandLine::Get(), TEXT("NoCaptureIndirect")))
		{
			Rig.Capture->PostProcessSettings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
			Rig.Capture->PostProcessSettings.ReflectionMethod = EReflectionMethod::None;
		}
		Rig.Scene->AddComponent(Rig.Capture.Get(), FTransform::Identity);
		// 注册会还原默认 ShowFlags，诊断覆盖必须放在注册后。
		Rig.Capture->ShowFlags.SetAntiAliasing(true);
		Rig.Capture->ShowFlags.SetTemporalAA(true);
		Rig.AntiAliasing = MakeShared<FMMDMovieAntiAliasing, ESPMode::ThreadSafe>();
		Rig.Capture->SceneViewExtensions.Add(Rig.AntiAliasing);
		Rig.Capture->HideComponent(Rig.Baseline.Get());
		if (FParse::Param(FCommandLine::Get(), TEXT("NoCaptureShadows")))
			Rig.Capture->ShowFlags.SetDynamicShadows(false);
		MMDMovieWaitForAssets(Rig);
		Rig.Playback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(Rig.Character.Get(), Animation, false, bPhysicsEnabled);
		Rig.BaselinePlayback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(Rig.Baseline.Get(), Animation, false, false);
		if (!Rig.Playback || !Rig.BaselinePlayback || (bPhysicsEnabled && Rig.Playback->GetPhysicsRigidBodyCount() == 0))
		{
			Error = TEXT("真实 MMD 动作播放或 Bullet 刚体配置不可用。"); return false;
		}
		return true;
	}

	void MMDMovieApplyCamera(FMMDMovieRig& Rig, const FMMDMovieCamera& Camera)
	{
		Rig.Capture->SetWorldTransform(Camera.Transform);
		Rig.Capture->FOVAngle = Camera.HorizontalFOV;
		Rig.Capture->ProjectionType = Camera.bOrthographic ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective;
		Rig.Capture->OrthoWidth = Camera.OrthoWidth;
		Rig.Capture->bCameraCutThisFrame = Camera.bCut;
	}

	void MMDMovieCapture(FMMDMovieRig& Rig)
	{
		++GFrameCounter;
		UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
		Rig.World->SendAllEndOfFrameUpdates();
		if (UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get())
		{
			// SceneCapture 不进入普通主视图收集，兼容材质显式使用当前展示配方。
			Lights->SetPreviewLightOverride(UMMDLightingEnvironmentLibrary::PackEnvironmentLightDataScaled(
				Rig.LightingEnvironment, Rig.InitialBounds.Origin, 1.0f));
			const FVector Position = Rig.Capture->GetComponentLocation();
			Lights->CollectLightsForFrame(Rig.World, &Position);
			ENQUEUE_RENDER_COMMAND(MMDMovieLights)([Lights](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder Graph(RHICmdList, RDG_EVENT_NAME("MMDMovieLights"));
				Lights->WriteLightData_RenderThread(Graph);
				Graph.Execute();
			});
		}
		Rig.Capture->CaptureScene();
		FlushRenderingCommands();
		Rig.Capture->bCameraCutThisFrame = false;
	}

	bool MMDMovieSaveFrame(FMMDMovieRig& Rig, const FString& Filename, double& MeanLuma, FString& Error)
	{
		TArray<FColor> Pixels;
		FTextureRenderTargetResource* Resource = Rig.Target->GameThread_GetRenderTargetResource();
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Rig.Target->SizeX * Rig.Target->SizeY)
		{
			Error = TEXT("引擎渲染目标 GPU 读回失败。"); return false;
		}
		double Sum = 0.0;
		for (FColor& Pixel : Pixels)
		{
			Pixel.A = 255;
			Sum += 0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B;
		}
		MeanLuma = Sum / Pixels.Num();
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Rig.Target->SizeX, Rig.Target->SizeY, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Filename))
		{
			Error = TEXT("PNG 保存失败：") + Filename; return false;
		}
		return true;
	}
}

UMMDCreateMovieCommandlet::UMMDCreateMovieCommandlet()
{
	IsClient = true;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UMMDCreateMovieCommandlet::Main(const FString& Params)
{
	const double Start = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("schema"), TEXT("mmd.movie.v1"));
	Report->SetBoolField(TEXT("success"), false);
	Report->SetStringField(TEXT("error"), TEXT("尚未完成"));
	Report->SetNumberField(TEXT("frames"), 0);
	FString ModelDirectory, ModelAssetPath, AnimationAssetPath, MotionPath, FacePath, CameraPath, Output;
	FString PresetId = TEXT("InfinityNikki"), StageId = TEXT("Studio"), CameraMode = TEXT("Vmd");
	FString CameraLocation, CameraRotation, PhysicsOption = TEXT("1"), ImportVmdMorphsOption = TEXT("1");
	FString Error, ReportPath;
	int32 Width = 1280, Height = 720, Fps = 30, MaxFrames = 0;
	float Duration = 0.0f, Warmup = 1.0f, PlaybackRate = 1.0f;
	FMMDMovieCamera FixedCamera;
	FParse::Value(*Params, TEXT("ModelDirectory="), ModelDirectory);
	FParse::Value(*Params, TEXT("ModelAsset="), ModelAssetPath);
	FParse::Value(*Params, TEXT("AnimationAsset="), AnimationAssetPath);
	FParse::Value(*Params, TEXT("Motion="), MotionPath);
	FParse::Value(*Params, TEXT("Face="), FacePath);
	FParse::Value(*Params, TEXT("Camera="), CameraPath);
	FParse::Value(*Params, TEXT("Output="), Output);
	FParse::Value(*Params, TEXT("Preset="), PresetId);
	FParse::Value(*Params, TEXT("Stage="), StageId);
	FParse::Value(*Params, TEXT("CameraMode="), CameraMode);
	FParse::Value(*Params, TEXT("CameraLocation="), CameraLocation);
	FParse::Value(*Params, TEXT("CameraRotation="), CameraRotation);
	const bool bHasCameraFOV = FParse::Value(*Params, TEXT("CameraFOV="), FixedCamera.HorizontalFOV);
	FParse::Value(*Params, TEXT("Physics="), PhysicsOption);
	FParse::Value(*Params, TEXT("ImportVmdMorphs="), ImportVmdMorphsOption);
	const bool bPhysicsEnabled = PhysicsOption != TEXT("0") && !FParse::Param(*Params, TEXT("NoPhysics"));
	const bool bImportVmdMorphs = ImportVmdMorphsOption != TEXT("0");
	FParse::Value(*Params, TEXT("PlaybackRate="), PlaybackRate);
	FParse::Value(*Params, TEXT("Width="), Width);
	FParse::Value(*Params, TEXT("Height="), Height);
	FParse::Value(*Params, TEXT("Fps="), Fps);
	FParse::Value(*Params, TEXT("MaxFrames="), MaxFrames);
	FParse::Value(*Params, TEXT("Duration="), Duration);
	FParse::Value(*Params, TEXT("Warmup="), Warmup);
	float ShadowNormalBias = -1.0f;
	FParse::Value(*Params, TEXT("ShadowNormalBias="), ShadowNormalBias);
	if (ShadowNormalBias >= 0.0f && ShadowNormalBias <= 10.0f)
		if (IConsoleVariable* Bias = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.NormalBias")))
			Bias->Set(ShadowNormalBias, ECVF_SetByCode);
	if (FParse::Param(*Params, TEXT("ConventionalShadows")))
		if (IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))
			Shadows->Set(0, ECVF_SetByCode);
	if (IConsoleVariable* Bias = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.NormalBias")))
		Report->SetNumberField(TEXT("shadow_normal_bias"), Bias->GetFloat());
	if (IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))
		Report->SetBoolField(TEXT("virtual_shadows"), Shadows->GetInt() != 0);
	Report->SetNumberField(TEXT("width"), Width);
	Report->SetNumberField(TEXT("height"), Height);
	Report->SetNumberField(TEXT("fps"), Fps);
	Report->SetNumberField(TEXT("duration_seconds"), 0);
	Report->SetBoolField(TEXT("cancelled"), false);
	Report->SetBoolField(TEXT("physics_enabled"), bPhysicsEnabled);
	Report->SetBoolField(TEXT("import_vmd_morphs"), bImportVmdMorphs);
	Report->SetNumberField(TEXT("playback_rate"), PlaybackRate);
	Report->SetNumberField(TEXT("start_time_seconds"), 0);
	ON_SCOPE_EXIT
	{
		if (UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get()) Lights->ClearPreviewLightOverride();
		Report->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Start);
		if (!Error.IsEmpty())
		{
			Report->SetBoolField(TEXT("success"), false);
			Report->SetStringField(TEXT("error"), Error);
			UE_LOG(LogTemp, Error, TEXT("[MMDCreateMovie] %s"), *Error);
		}
		if (!ReportPath.IsEmpty() && !MMDMovieWriteReport(Report, ReportPath))
			UE_LOG(LogTemp, Error, TEXT("[MMDCreateMovie] 无法保存报告：%s"), *ReportPath);
	};
	const FString SavedRoot = MMDMovieFullPath(FPaths::ProjectSavedDir());
	if (Output.IsEmpty() || (ModelDirectory.IsEmpty() == ModelAssetPath.IsEmpty())
		|| (MotionPath.IsEmpty() == AnimationAssetPath.IsEmpty()))
	{
		Error = TEXT("必须提供 Output；ModelDirectory/ModelAsset 和 Motion/AnimationAsset 分别只能提供一个。"); return 1;
	}
	const bool bModelAsset = !ModelAssetPath.IsEmpty();
	const bool bAnimationAsset = !AnimationAssetPath.IsEmpty();
	Output = MMDMovieFullPath(Output);
	if (!ModelDirectory.IsEmpty()) ModelDirectory = MMDMovieFullPath(ModelDirectory);
	if (!FPaths::IsUnderDirectory(Output, SavedRoot) || !MMDMovieHasNoLink(Output))
	{
		Error = TEXT("输出必须位于本工程 Saved，路径不能包含链接。"); return 1;
	}
	if (!bModelAsset && (!FPaths::IsUnderDirectory(ModelDirectory, SavedRoot / TEXT("BackgroundValidation/ImportedAssets"))
		|| !MMDMovieHasNoLink(ModelDirectory) || !IFileManager::Get().DirectoryExists(*ModelDirectory)
		|| FPaths::IsUnderDirectory(Output, ModelDirectory) || FPaths::IsUnderDirectory(ModelDirectory, Output)))
	{
		Error = TEXT("ModelDirectory 须为 ImportedAssets 缓存，与输出不能重叠或包含链接。"); return 1;
	}
	TArray<FString> Existing;
	IFileManager::Get().FindFilesRecursive(Existing, *Output, TEXT("*"), true, false);
	const TSet<FString> RunnerFiles = { TEXT("request.json"), TEXT("engine.log"), TEXT("engine.stdout.log"), TEXT("engine.stderr.log"), TEXT("engine.pid"), TEXT("build.log"), TEXT("cancel.request") };
	for (const FString& ExistingFile : Existing)
	{
		const FString FullExisting = MMDMovieFullPath(ExistingFile);
		if (!FPaths::IsSamePath(FPaths::GetPath(FullExisting), Output) || !RunnerFiles.Contains(FPaths::GetCleanFilename(FullExisting)))
		{
			Error = TEXT("输出目录已有图像、报告或未知文件，拒绝覆盖：") + ExistingFile; return 1;
		}
	}
	if (!IFileManager::Get().MakeDirectory(*(Output / TEXT("frames")), true))
	{
		Error = TEXT("无法创建图像序列输出目录。"); return 1;
	}
	ReportPath = Output / TEXT("report.json");
	Report->SetStringField(TEXT("output"), Output);
	Report->SetStringField(TEXT("model_directory"), ModelDirectory);
	Report->SetStringField(TEXT("model_source_type"), bModelAsset ? TEXT("project_asset") : TEXT("cache"));
	Report->SetStringField(TEXT("animation_source_type"), bAnimationAsset ? TEXT("asset") : TEXT("vmd"));
	Report->SetStringField(TEXT("frame_pattern"), TEXT("frames/frame_%06d.png"));
	if (Width < 128 || Width > 4096 || Height < 128 || Height > 4096 || Fps < 1 || Fps > 60 || 60 % Fps != 0
		|| MaxFrames < 0 || !FMath::IsFinite(Duration) || Duration < 0 || !FMath::IsFinite(Warmup) || Warmup < 0 || Warmup > 10
		|| !FMath::IsFinite(PlaybackRate) || PlaybackRate < 0.1f || PlaybackRate > 4.0f)
	{
		Error = TEXT("分辨率须为128..4096；Fps须整除60；时长非负，Warmup为0..10秒，PlaybackRate为0.1..4。"); return 1;
	}
	if (PhysicsOption != TEXT("0") && PhysicsOption != TEXT("1"))
	{
		Error = TEXT("Physics 须为0或1。"); return 1;
	}
	if (ImportVmdMorphsOption != TEXT("0") && ImportVmdMorphsOption != TEXT("1"))
	{
		Error = TEXT("ImportVmdMorphs 须为0或1。"); return 1;
	}
	Report->SetNumberField(TEXT("camera_aspect_ratio"), double(Width) / Height);
	if (bAnimationAsset && !FacePath.IsEmpty())
	{
		Error = TEXT("AnimationAsset 使用已保存的表情曲线；请先在编辑器追加表情并保存，或改用 Motion+Face。"); return 1;
	}
	const bool bFixedCamera = CameraMode.Equals(TEXT("Fixed"), ESearchCase::IgnoreCase);
	if (!bFixedCamera && !CameraMode.Equals(TEXT("Vmd"), ESearchCase::IgnoreCase)
		&& !CameraMode.Equals(TEXT("Showcase"), ESearchCase::IgnoreCase))
	{
		Error = TEXT("CameraMode 须为 Vmd、Showcase 或 Fixed。"); return 1;
	}
	if (bFixedCamera)
	{
		double X = 0, Y = 0, Z = 0, Pitch = 0, Yaw = 0, Roll = 0;
		if (!MMDMovieReadCameraComponent(CameraLocation, TEXT("X="), X)
			|| !MMDMovieReadCameraComponent(CameraLocation, TEXT("Y="), Y)
			|| !MMDMovieReadCameraComponent(CameraLocation, TEXT("Z="), Z)
			|| !MMDMovieReadCameraComponent(CameraRotation, TEXT("P="), Pitch)
			|| !MMDMovieReadCameraComponent(CameraRotation, TEXT("Y="), Yaw)
			|| !MMDMovieReadCameraComponent(CameraRotation, TEXT("R="), Roll) || !bHasCameraFOV
			|| !FMath::IsFinite(FixedCamera.HorizontalFOV) || FixedCamera.HorizontalFOV < 1 || FixedCamera.HorizontalFOV > 170
			|| !CameraPath.IsEmpty())
		{
			Error = TEXT("Fixed 镜头须有有限的 CameraLocation(X/Y/Z)、CameraRotation(P/Y/R) 与 CameraFOV(1..170)，不能同时指定 Camera VMD。"); return 1;
		}
		const FVector Location(X, Y, Z);
		const FRotator Rotation(Pitch, Yaw, Roll);
		FixedCamera.Transform = FTransform(Rotation, Location);
		Report->SetStringField(TEXT("fixed_camera_location"), Location.ToString());
		Report->SetStringField(TEXT("fixed_camera_rotation"), Rotation.ToString());
		Report->SetNumberField(TEXT("fixed_camera_horizontal_fov"), FixedCamera.HorizontalFOV);
	}
	else if (!CameraLocation.IsEmpty() || !CameraRotation.IsEmpty() || bHasCameraFOV)
	{
		Error = TEXT("显式机位参数需要 CameraMode=Fixed。"); return 1;
	}
	auto IsCancelled = [&]()
	{
		if (!IFileManager::Get().FileExists(*(Output / TEXT("cancel.request")))) return false;
		Report->SetBoolField(TEXT("cancelled"), true);
		Error = TEXT("已取消制作；已生成文件保留在本次输出目录，源资产未保存。");
		return true;
	};
	if (IsCancelled()) return 2;
	auto ParseVMD = [&Error](FString& Path, TVMDParser& Parser)
	{
		Path = MMDMovieFullPath(Path);
		if (!FPaths::GetExtension(Path).Equals(TEXT("vmd"), ESearchCase::IgnoreCase) || !IFileManager::Get().FileExists(*Path)
			|| !Parser.ParseVMDFile(Path))
		{
			Error = TEXT("VMD 不存在或解析失败：") + Path; return false;
		}
		return true;
	};
	TVMDParser Motion, Face, Camera;
	if (!bAnimationAsset && (!ParseVMD(MotionPath, Motion) || Motion.VMDInfo.BoneKeyframes.IsEmpty()))
	{
		if (Error.IsEmpty()) Error = TEXT("动作 VMD 没有骨骼关键帧。"); return 1;
	}
	// 内置预览关闭动作自带表情；显式追加的 Face 仍由调用者独立选择。
	if (!bImportVmdMorphs) Motion.VMDInfo.MorphKeyframes.Reset();
	if (!FacePath.IsEmpty())
	{
		if (!ParseVMD(FacePath, Face)) return 1;
		// 相同名称和时刻以追加表情为准，保留其余原动作表情。
		TSet<FString> FaceNames;
		for (const VMDMorphKeyframe& Key : Face.VMDInfo.MorphKeyframes) FaceNames.Add(Key.MorphName);
		Motion.VMDInfo.MorphKeyframes.RemoveAll([&FaceNames](const VMDMorphKeyframe& Key) { return FaceNames.Contains(Key.MorphName); });
		Motion.VMDInfo.MorphKeyframes.Append(Face.VMDInfo.MorphKeyframes);
	}
	TArray<VMDCameraKeyframe> CameraKeys = Motion.VMDInfo.CameraKeyframes;
	if (!CameraPath.IsEmpty())
	{
		if (!ParseVMD(CameraPath, Camera)) return 1;
		CameraKeys = MoveTemp(Camera.VMDInfo.CameraKeyframes);
		if (CameraKeys.IsEmpty()) { Error = TEXT("指定的相机 VMD 没有相机轨道。"); return 1; }
	}
	CameraKeys.Sort([](const VMDCameraKeyframe& A, const VMDCameraKeyframe& B) { return A.FrameNumber < B.FrameNumber; });
	Report->SetStringField(TEXT("source_motion"), MotionPath);
	Report->SetStringField(TEXT("source_face"), FacePath);
	Report->SetStringField(TEXT("source_camera"), CameraPath);
	Report->SetNumberField(TEXT("source_bone_keys"), Motion.VMDInfo.BoneKeyframes.Num());
	Report->SetNumberField(TEXT("source_morph_keys"), Motion.VMDInfo.MorphKeyframes.Num());
	Report->SetBoolField(TEXT("vmd_morph_import_enabled"), !bAnimationAsset && (bImportVmdMorphs || !FacePath.IsEmpty()));
	Report->SetNumberField(TEXT("camera_keys"), CameraKeys.Num());
	Report->SetStringField(TEXT("camera_mode"), bFixedCamera ? TEXT("fixed_preview")
		: CameraMode.Equals(TEXT("Showcase"), ESearchCase::IgnoreCase) ? TEXT("showcase_follow_pose")
		: CameraKeys.IsEmpty() ? TEXT("auto_full_body_orbit") : TEXT("vmd_bezier"));
	FMMDMovieMount Mount;
	FString MeshPath = ModelAssetPath;
	TSharedPtr<FJsonObject> Manifest;
	if (bModelAsset)
	{
		if (!MMDMovieResolveProjectAsset(MeshPath, Error)) return 1;
	}
	else
	{
		const FString Name = FPaths::GetCleanFilename(ModelDirectory);
		Mount.Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
		Mount.Directory = ModelDirectory + TEXT("/");
		if (!FPackageName::IsValidLongPackageName(Mount.Root + TEXT("Model")) || FPackageName::MountPointExists(Mount.Root))
		{
			Mount.Root.Reset();
			Error = TEXT("模型缓存挂载名称无效或已经占用。"); return 1;
		}
		FPackageName::RegisterMountPoint(Mount.Root, Mount.Directory);
		MeshPath = Mount.Root + TEXT("Model/") + Name + TEXT(".") + Name;
		FString ManifestJson;
		if (FFileHelper::LoadFileToString(ManifestJson, *(ModelDirectory / TEXT("manifest.json")))
			&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestJson), Manifest) && Manifest.IsValid())
		{
			FString ExplicitMesh;
			if (Manifest->TryGetStringField(TEXT("mesh"), ExplicitMesh) && ExplicitMesh.StartsWith(Mount.Root)) MeshPath = ExplicitMesh;
		}
	}
	Report->SetStringField(TEXT("model_mount_root"), Mount.Root);
	Report->SetStringField(TEXT("model_asset"), bModelAsset ? MeshPath : FString());
	TStrongObjectPtr<USkeletalMesh> Mesh(LoadObject<USkeletalMesh>(nullptr, *MeshPath));
	if (!Mesh.IsValid() || !Mesh->GetSkeleton() || Mesh->GetPathName() != MeshPath)
	{
		Error = TEXT("找不到已保存且具有骨架的网格，重定向资产请先重新保存：") + MeshPath; return 1;
	}
	const auto* UserData = Cast<UMMDSkeletalMeshUserData>(Mesh->GetAssetUserDataOfClass(UMMDSkeletalMeshUserData::StaticClass()));
	const UMMDModelDataAsset* Model = UserData ? UserData->ModelDataAsset.Get() : nullptr;
	if (bPhysicsEnabled && (!Model || Model->Rigids.IsEmpty()))
	{
		Error = TEXT("开启物理需要模型保存 MMD 刚体数据；请重新导入，或使用 Physics=0。"); return 1;
	}
	TMap<UPackage*, bool> SourceDirtyFlags;
	auto TrackSource = [&](const UObject* Asset)
	{
		if (Asset) SourceDirtyFlags.FindOrAdd(Asset->GetOutermost(), Asset->GetOutermost()->IsDirty());
	};
	auto AreSourceFlagsUnchanged = [&]()
	{
		for (const auto& Entry : SourceDirtyFlags) if (Entry.Key->IsDirty() != Entry.Value) return false;
		return true;
	};
	TrackSource(Mesh.Get());
	TrackSource(Mesh->GetSkeleton());
	TrackSource(Model);
	Report->SetStringField(TEXT("mesh"), MeshPath);
	Report->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum());
	Report->SetNumberField(TEXT("rigid_bodies"), Model ? Model->Rigids.Num() : 0);
	Report->SetNumberField(TEXT("joints"), Model ? Model->Joints.Num() : 0);
	TPMXParser PMX;
	const PMXDatas* PMXData = nullptr;
	FString SourcePMX;
#if WITH_EDITORONLY_DATA
	if (Model) SourcePMX = Model->SourcePMXFilePath;
#endif
	if ((SourcePMX.IsEmpty() || !IFileManager::Get().FileExists(*SourcePMX)) && Manifest.IsValid())
		Manifest->TryGetStringField(TEXT("source_pmx"), SourcePMX);
	if (!bAnimationAsset && !SourcePMX.IsEmpty() && IFileManager::Get().FileExists(*SourcePMX) && PMX.ParsePMXFile(SourcePMX)) PMXData = &PMX.PMXInfo;
	Report->SetBoolField(TEXT("source_pmx_available_for_morph_groups"), PMXData != nullptr);
	FMMDAnimationImportReport ImportReport;
	TStrongObjectPtr<UAnimSequence> SourceAnimation;
	TStrongObjectPtr<UAnimSequence> Animation;
	if (bAnimationAsset)
	{
		if (!MMDMovieResolveProjectAsset(AnimationAssetPath, Error)) return 1;
		SourceAnimation.Reset(LoadObject<UAnimSequence>(nullptr, *AnimationAssetPath));
		if (!SourceAnimation.IsValid() || SourceAnimation->GetPathName() != AnimationAssetPath
			|| SourceAnimation->GetSkeleton() != Mesh->GetSkeleton() || !SourceAnimation->GetDataModel()
			|| SourceAnimation->GetDataModel()->GetNumBoneTracks() == 0)
		{
			Error = TEXT("AnimationAsset 必须为已经保存、有骨骼轨道且与当前模型使用同一 Skeleton 的 AnimSequence。"); return 1;
		}
		TrackSource(SourceAnimation.Get());
		// 压缩和播放只作用于工作副本；后台进程不编辑或保存源动画。
		Animation.Reset(DuplicateObject<UAnimSequence>(SourceAnimation.Get(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), TEXT("MMDMovieWorkingAnimation"))));
		if (Animation.IsValid())
		{
			Animation->ClearFlags(RF_Public | RF_Standalone);
			Animation->SetFlags(RF_Transient);
			ImportReport.MatchedBoneTrackCount = Animation->GetDataModel()->GetNumBoneTracks();
			ImportReport.MatchedMorphTrackCount = Animation->GetDataModel()->GetFloatCurves().Num();
		}
	}
	else
	{
		FMMDAnimationImportContext Context;
		FMMDAnimationImportSettings Settings;
		Settings.bTransientPreview = true;
		Settings.bImportMorphCurves = bImportVmdMorphs || !FacePath.IsEmpty();
		if (!TMMDMeshBuilder::BuildAnimationImportContext(Mesh.Get(), PMXData, SourcePMX, MotionPath, Context, &ImportReport))
		{
			Error = TEXT("无法构建当前模型的动作导入上下文。"); return 1;
		}
		Animation.Reset(TMMDMeshBuilder::BuildVMDAnimation(Motion.VMDInfo, Context, Settings, &ImportReport));
	}
	if (!Animation.IsValid() || ImportReport.HasErrors())
	{
		Error = ImportReport.Errors.IsEmpty() ? TEXT("无法创建制作所需的临时动作。") : FString::Join(ImportReport.Errors, TEXT("; ")); return 1;
	}
	Report->SetStringField(TEXT("source_animation_asset"), bAnimationAsset ? AnimationAssetPath : FString());
	Report->SetBoolField(TEXT("animation_uses_transient_copy"), bAnimationAsset);
	Animation->WaitOnExistingCompression();
	const float SourceLength = Animation->GetPlayLength();
	if (SourceLength <= 0.0f) { Error = TEXT("动作时长为零。"); return 1; }
	const float PlaybackLength = SourceLength / PlaybackRate;
	const float RequestedDuration = Duration > 0.0f ? FMath::Min(Duration, PlaybackLength) : PlaybackLength;
	const int32 PlannedFrames = FMath::Max(1, FMath::CeilToInt(RequestedDuration * Fps));
	const int32 OutputFrames = MaxFrames > 0 ? FMath::Min(PlannedFrames, MaxFrames) : PlannedFrames;
	Report->SetNumberField(TEXT("animation_length_seconds"), SourceLength);
	Report->SetNumberField(TEXT("playback_length_seconds"), PlaybackLength);
	Report->SetNumberField(TEXT("requested_duration_seconds"), RequestedDuration);
	Report->SetNumberField(TEXT("planned_frames"), OutputFrames);
	Report->SetNumberField(TEXT("matched_bone_tracks"), ImportReport.MatchedBoneTrackCount);
	Report->SetNumberField(TEXT("matched_morph_tracks"), ImportReport.MatchedMorphTrackCount);
	TArray<TSharedPtr<FJsonValue>> Warnings;
	for (const FString& Warning : ImportReport.Warnings) Warnings.Add(MakeShared<FJsonValueString>(Warning));
	Report->SetArrayField(TEXT("import_warnings"), Warnings);
	TStrongObjectPtr<UMMDStageRenderPreset> CharacterPreset;
	TStrongObjectPtr<UMMDStageRenderPreset> StagePreset;
	if (!PresetId.Equals(TEXT("Original"), ESearchCase::IgnoreCase) && !PresetId.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		const FString Path = MMDMoviePresetPath(PresetId);
		CharacterPreset.Reset(Path.IsEmpty() ? nullptr : LoadObject<UMMDStageRenderPreset>(nullptr, *Path));
		if (!CharacterPreset.IsValid()) { Error = TEXT("角色预设不存在：") + PresetId; return 1; }
	}
	EMMDStudioDisplayStyle DisplayStyle = EMMDStudioDisplayStyle::Studio;
	const bool bDisplayScene = FMMDStudioDisplayScene::TryParse(StageId, DisplayStyle)
		|| StageId.Equals(TEXT("None"), ESearchCase::IgnoreCase);
	if (!bDisplayScene)
	{
		const FString Path = MMDMoviePresetPath(StageId);
		StagePreset.Reset(Path.IsEmpty() ? nullptr : LoadObject<UMMDStageRenderPreset>(nullptr, *Path));
		if (!StagePreset.IsValid()) { Error = TEXT("舞台预设不存在：") + StageId; return 1; }
	}
	const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
	Report->SetBoolField(TEXT("native_backend_active"), Backend.bActive);
	Report->SetBoolField(TEXT("native_profile_marker_allowed"), Backend.bProfileMarkerAllowed);
	Report->SetStringField(TEXT("native_backend_signature"), Backend.Signature);
	Report->SetStringField(TEXT("character_preset"), CharacterPreset.IsValid() ? CharacterPreset->GetPathName() : TEXT("Original"));
	Report->SetStringField(TEXT("stage_preset"), StagePreset.IsValid() ? StagePreset->GetPathName() : FMMDStudioDisplayScene::GetId(DisplayStyle));
	Report->SetStringField(TEXT("lighting"), StaticEnum<EMMDLightingEnvironment>()->GetNameStringByValue(
		static_cast<int64>(FMMDStudioDisplayScene::GetLightingEnvironment(DisplayStyle))));
	Report->SetStringField(TEXT("exposure"), TEXT("Manual EV0; physical camera exposure disabled"));
	Report->SetStringField(TEXT("scene_lighting_scope"), StagePreset.IsValid()
		? TEXT("Stage geometry with Studio3Point lighting override") : TEXT("Shared display scene and lighting recipe with tool preview"));
	Report->SetBoolField(TEXT("diagnostic_no_shadows"), FParse::Param(FCommandLine::Get(), TEXT("NoCaptureShadows")));
	Report->SetBoolField(TEXT("diagnostic_no_indirect"), FParse::Param(FCommandLine::Get(), TEXT("NoCaptureIndirect")));
	Report->SetBoolField(TEXT("diagnostic_no_outline"), FParse::Param(FCommandLine::Get(), TEXT("NoCaptureOutline")));
	FMMDMovieRig Rig;
	if (IsCancelled()) return 2;
	if (!MMDMovieBuildRig(Rig, Mesh.Get(), Model, Animation.Get(), CharacterPreset.Get(), StagePreset.Get(), DisplayStyle,
		Width, Height, bPhysicsEnabled, Error)) return 1;
	Report->SetBoolField(TEXT("capture_dynamic_shadows"), Rig.Capture->ShowFlags.DynamicShadows);
	Report->SetBoolField(TEXT("capture_excludes_physics_reference"), Rig.Capture->HiddenComponents.Contains(Rig.Baseline.Get()));
	TArray<TSharedPtr<FJsonValue>> Materials;
	for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
	{
		const auto* Instance = Cast<UMaterialInstance>(Rig.Character->GetMaterial(Slot));
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("slot"), Slot);
		Item->SetStringField(TEXT("material"), Rig.Character->GetMaterial(Slot) ? Rig.Character->GetMaterial(Slot)->GetPathName() : FString());
		Item->SetStringField(TEXT("parent"), Instance && Instance->Parent ? Instance->Parent->GetPathName() : FString());
		if (Instance)
		{
			UTexture* BaseTexture = nullptr;
			FLinearColor BaseTint = FLinearColor::White;
			Instance->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), BaseTexture);
			Instance->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), BaseTint);
			Item->SetStringField(TEXT("base_color_texture"), BaseTexture ? BaseTexture->GetPathName() : FString());
			Item->SetStringField(TEXT("base_color_tint"), BaseTint.ToString());
		}
		Materials.Add(MakeShared<FJsonValueObject>(Item));
	}
	Report->SetArrayField(TEXT("materials"), Materials);
	Rig.Playback->SetPlayRate(0.0f);
	Rig.BaselinePlayback->SetPlayRate(0.0f);
	for (int32 Step = 0; Step < FMath::CeilToInt(Warmup * 60.0f); ++Step)
	{
		if (IsCancelled()) return 2;
		Rig.Step(1.0f / 60.0f);
	}
	if (bPhysicsEnabled && !Rig.Playback->IsPhysicsInitialized()) { Error = TEXT("Bullet 未能完成初始化。"); return 1; }
	Rig.Playback->SetPlayRate(PlaybackRate);
	Rig.BaselinePlayback->SetPlayRate(PlaybackRate);
	const float Aspect = float(Width) / Height;
	auto EvaluateMovieCamera = [&](double Time)
	{
		if (bFixedCamera)
		{
			FMMDMovieCamera Shot = FixedCamera;
			Shot.bCut = Time <= 0.0;
			return Shot;
		}
		return CameraMode.Equals(TEXT("Showcase"), ESearchCase::IgnoreCase) ? MMDMovieShowcaseCamera(Rig, Time, Aspect)
			: MMDMovieEvaluateCamera(CameraKeys, Time * PlaybackRate, Aspect, Rig.InitialBounds);
	};
	const FMMDMovieCamera WarmupCamera = EvaluateMovieCamera(0.0);
	MMDMovieApplyCamera(Rig, WarmupCamera);
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		if (IsCancelled()) return 2;
		MMDMovieCapture(Rig);
	}
	auto ReportAntiAliasing = [&]()
	{
		Report->SetStringField(TEXT("capture_antialiasing"), Rig.AntiAliasing->LastMethod == AAM_TemporalAA ? TEXT("TAA")
			: Rig.AntiAliasing->LastMethod == AAM_FXAA ? TEXT("FXAA fallback") : TEXT("None"));
		Report->SetNumberField(TEXT("capture_antialiasing_method"), static_cast<int32>(Rig.AntiAliasing->LastMethod));
		Report->SetStringField(TEXT("capture_antialiasing_scope"), TEXT("Component-local view extension; native output resolution"));
		Report->SetBoolField(TEXT("capture_antialiasing_show_flag"), Rig.Capture->ShowFlags.AntiAliasing);
		Report->SetBoolField(TEXT("capture_temporal_aa_show_flag"), Rig.Capture->ShowFlags.TemporalAA);
		Report->SetBoolField(TEXT("capture_post_processing_show_flag"), Rig.Capture->ShowFlags.PostProcessing);
		Report->SetBoolField(TEXT("capture_persistent_view_state"), Rig.Capture->bAlwaysPersistRenderingState);
		Report->SetBoolField(TEXT("capture_view_state_available"), Rig.AntiAliasing->bViewStateAvailable);
		Report->SetBoolField(TEXT("capture_realtime_view"), Rig.AntiAliasing->bRealtimeView);
		Report->SetNumberField(TEXT("capture_antialiasing_configured_views"), Rig.AntiAliasing->ConfiguredViews);
	};
	ReportAntiAliasing();
	Report->SetNumberField(TEXT("capture_history_warmup_frames"), 12);
	Report->SetBoolField(TEXT("physics_initialized"), Rig.Playback->IsPhysicsInitialized());
	Report->SetNumberField(TEXT("physics_fixed_step_seconds"), 1.0 / 60.0);
	Report->SetNumberField(TEXT("physics_compared_bones"), Rig.PhysicsBones.Num());
	Report->SetNumberField(TEXT("warmup_seconds"), Warmup);
	Report->SetStringField(TEXT("error"), TEXT("正在输出"));
	if (!MMDMovieWriteReport(Report, ReportPath)) { Error = TEXT("无法写入开始报告。"); return 1; }
	TArray<TSharedPtr<FJsonValue>> FrameData;
	TArray<TArray<FTransform>> BoneLocalTracks;
	BoneLocalTracks.SetNum(Mesh->GetRefSkeleton().GetNum());
	for (TArray<FTransform>& Track : BoneLocalTracks) Track.Reserve(OutputFrames);
	TArray<FTransform> CameraTransforms;
	TArray<float> HorizontalFovs;
	TArray<bool> CameraCuts;
	CameraTransforms.Reserve(OutputFrames);
	HorizontalFovs.Reserve(OutputFrames);
	CameraCuts.Reserve(OutputFrames);
	double MaxPhysicsDifference = 0.0;
	const int32 StepsPerFrame = 60 / Fps;
	for (int32 Frame = 0; Frame < OutputFrames; ++Frame)
	{
		if (IsCancelled()) return 2;
		if (Frame > 0)
			for (int32 Step = 0; Step < StepsPerFrame; ++Step) Rig.Step(1.0f / 60.0f);
		const double Time = double(Frame) / Fps;
		if (Rig.Stage) Rig.Stage->EvaluateStageAtTime(float(Time));
		const FMMDMovieCamera Shot = EvaluateMovieCamera(Time);
		MMDMovieApplyCamera(Rig, Shot);
		const bool bReuseWarmupHistory = Frame == 0 && Shot.Transform.Equals(WarmupCamera.Transform)
			&& FMath::IsNearlyEqual(Shot.HorizontalFOV, WarmupCamera.HorizontalFOV)
			&& Shot.bOrthographic == WarmupCamera.bOrthographic && FMath::IsNearlyEqual(Shot.OrthoWidth, WarmupCamera.OrthoWidth);
		// 同一首镜头已经完成预热；后续真正换镜仍按源镜头的 Cut 重置历史。
		if (bReuseWarmupHistory) Rig.Capture->bCameraCutThisFrame = false;
		MMDMovieCapture(Rig);
		if (Frame == 0) Report->SetBoolField(TEXT("capture_initial_history_reused"), bReuseWarmupHistory
			&& Rig.AntiAliasing->LastMethod == AAM_TemporalAA && Rig.AntiAliasing->bViewStateAvailable && !Rig.AntiAliasing->bLastCameraCut);
		double MeanLuma = 0.0;
		const FString Filename = Output / TEXT("frames") / FString::Printf(TEXT("frame_%06d.png"), Frame);
		if (!MMDMovieSaveFrame(Rig, Filename, MeanLuma, Error)) return 1;
		const TArray<FTransform> LocalPose = Rig.Character->GetBoneSpaceTransforms();
		if (LocalPose.Num() != BoneLocalTracks.Num())
		{
			Error = TEXT("成片骨骼数与完整骨架不一致，不能保存可编辑物理动画。"); return 1;
		}
		for (int32 Bone = 0; Bone < LocalPose.Num(); ++Bone) BoneLocalTracks[Bone].Add(LocalPose[Bone]);
		CameraTransforms.Add(Shot.Transform);
		HorizontalFovs.Add(Shot.HorizontalFOV);
		CameraCuts.Add(Shot.bCut);
		const double PhysicsDifference = Rig.PhysicsDifference();
		MaxPhysicsDifference = FMath::Max(MaxPhysicsDifference, PhysicsDifference);
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("frame"), Frame);
		Item->SetNumberField(TEXT("time_seconds"), Time);
		Item->SetNumberField(TEXT("animation_time_seconds"), Rig.Playback->GetCurrentTime());
		Item->SetNumberField(TEXT("physics_difference_degrees"), PhysicsDifference);
		Item->SetNumberField(TEXT("mean_luma"), MeanLuma);
		Item->SetStringField(TEXT("camera_location"), Shot.Transform.GetLocation().ToString());
		Item->SetStringField(TEXT("camera_rotation"), Shot.Transform.Rotator().ToString());
		Item->SetNumberField(TEXT("horizontal_fov"), Shot.HorizontalFOV);
		Item->SetBoolField(TEXT("camera_cut"), Shot.bCut);
		Item->SetBoolField(TEXT("capture_camera_cut"), Rig.AntiAliasing->bLastCameraCut);
		FrameData.Add(MakeShared<FJsonValueObject>(Item));
		Report->SetNumberField(TEXT("frames"), Frame + 1);
		Report->SetNumberField(TEXT("duration_seconds"), double(Frame + 1) / Fps);
		Report->SetNumberField(TEXT("maximum_physics_difference_degrees"), MaxPhysicsDifference);
		Report->SetNumberField(TEXT("physics_steps"), double(Rig.PhysicsSteps));
		Report->SetNumberField(TEXT("simulation_steps"), double(Rig.SimulationSteps));
		if (Frame % 60 == 0 || Frame + 1 == OutputFrames)
		{
			ReportAntiAliasing();
			Report->SetArrayField(TEXT("frame_data"), FrameData);
			MMDMovieWriteReport(Report, ReportPath);
			UE_LOG(LogTemp, Display, TEXT("[MMDCreateMovie] %d/%d frames; time=%.3f; Bullet=%d; bone difference=%.3f deg"),
				Frame + 1, OutputFrames, Time, Rig.Playback->GetPhysicsRigidBodyCount(), PhysicsDifference);
		}
		FPlatformProcess::Sleep(0.0f);
	}
	const bool SourcesUnchanged = AreSourceFlagsUnchanged();
	Report->SetBoolField(TEXT("source_asset_dirty_flags_unchanged"), SourcesUnchanged);
	Report->SetBoolField(TEXT("saved_editable_assets"), false);
	Report->SetArrayField(TEXT("frame_data"), FrameData);
	if (!SourcesUnchanged) { Error = TEXT("源模型、Skeleton、ModelData 或动画的 dirty 状态出现意外变化；没有保存源资产。"); return 1; }
	Report->SetBoolField(TEXT("physics_motion_verified"), bPhysicsEnabled && !Rig.PhysicsBones.IsEmpty() && MaxPhysicsDifference >= 0.01);
	if (bPhysicsEnabled && (Rig.PhysicsBones.IsEmpty() || MaxPhysicsDifference < 0.01))
	{
		if (!bModelAsset)
		{
			Error = TEXT("输出完成，但未验证到动态刚体骨骼相对于无物理动画的变化。"); return 1;
		}
		Report->SetStringField(TEXT("physics_validation_note"), TEXT("Bullet 已初始化；本段未观察到超过0.01度的动态骨骼变化，静止姿态仍可正常输出。"));
	}
	MMDMovieAssetWriter::FResult Editable;
	if (IsCancelled()) return 2;
	if (!MMDMovieAssetWriter::Write(Output, Mesh.Get(), Animation.Get(), BoneLocalTracks, CameraTransforms, HorizontalFovs,
		CameraCuts, Fps, Editable, Error, PlaybackRate)) return 1;
	Report->SetStringField(TEXT("assets_directory"), Editable.AssetsDirectory);
	Report->SetStringField(TEXT("assets_mount_root"), Editable.MountRoot);
	Report->SetStringField(TEXT("sequence"), Editable.SequenceObjectPath);
	Report->SetStringField(TEXT("baked_animation"), Editable.BakedAnimationObjectPath);
	Report->SetBoolField(TEXT("saved_editable_assets"), true);
	Report->SetNumberField(TEXT("baked_tail_hold_frames"), 1);
	Report->SetStringField(TEXT("editable_scope"), TEXT("Baked skeletal animation (physics according to physics_enabled), facial curves and camera; source materials; stage and preview material overrides are recorded separately."));
	if (!AreSourceFlagsUnchanged())
	{
		Report->SetBoolField(TEXT("source_asset_dirty_flags_unchanged"), false);
		Error = TEXT("保存作品后源模型、Skeleton、ModelData 或动画的 dirty 状态出现意外变化；未保存源资产。"); return 1;
	}
	Report->SetBoolField(TEXT("success"), true);
	Report->SetStringField(TEXT("error"), FString());
	UE_LOG(LogTemp, Display, TEXT("[MMDCreateMovie] Done: %d frames, %.3fs, %s"), OutputFrames, double(OutputFrames) / Fps, *ReportPath);
	return 0;
}
