#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "UI/MMDPreviewMotionPresets.h"
#include "UI/MMDToolPanelStyle.h"
#include "UI/SMMDAnimatedButton.h"
#include "Editor/MMDPreviewMovieRequest.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Stage/MMDStageMaterialState.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "DynamicRHI.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "Widgets/SBoxPanel.h"

struct FMMDPreviewMovieJob
{
	FProcHandle Process;
	FString Output;
	bool bCancelRequested = false;
	bool bCancelSent = false;
	int32 ReportedFrames = -1;

	~FMMDPreviewMovieJob() { if (Process.IsValid()) FPlatformProcess::CloseProc(Process); }
};

namespace
{
	FString MovieFullPath(const FString& Path)
	{
		FString Full = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Full);
		return FPaths::CollapseRelativeDirectories(Full) ? Full : FString();
	}

	bool IsSavedProjectAsset(const UObject& Asset)
	{
		const UPackage* Package = Asset.GetOutermost();
		const FString Name = Package->GetName();
		FString Filename;
		return !Asset.HasAnyFlags(RF_Transient) && !Package->IsDirty() && Name.StartsWith(TEXT("/Game/"))
			&& Asset.GetName() == FPackageName::GetLongPackageAssetName(Name)
			&& FPackageName::TryConvertLongPackageNameToFilename(Name, Filename, FPackageName::GetAssetPackageExtension())
			&& FPaths::IsUnderDirectory(MovieFullPath(Filename), MovieFullPath(FPaths::ProjectContentDir()))
			&& IFileManager::Get().FileSize(*Filename) > 0;
	}

	FString MoviePresetId(const UMMDStageRenderPreset& Preset)
	{
		const FString Package = Preset.GetOutermost()->GetName();
		const FString Root = TEXT("/Ue5MMDTools/Stages/Presets/");
		for (const TCHAR* Id : { TEXT("InfinityNikki"), TEXT("SakuraTheater"), TEXT("MoonMirror"), TEXT("NeonConcert"),
			TEXT("OpenWorldToon"), TEXT("SilverCity"), TEXT("CinematicAnime"), TEXT("IndustrialAnime"), TEXT("Endfield"), TEXT("EndfieldV2") })
		{
			const FString Name(Id);
			const FString Folder = Name.StartsWith(TEXT("Endfield")) ? Name + TEXT("/") : FString();
			if (Package == Root + Folder + TEXT("DA_Render_") + Name) return Name;
		}
		return {};
	}

	TSharedPtr<FJsonObject> ReadMovieJson(const FString& Path)
	{
		FString Text;
		TSharedPtr<FJsonObject> Data;
		if (FFileHelper::LoadFileToString(Text, *Path))
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Data);
		return Data;
	}

	int32 MovieCVar(const TCHAR* Name, int32 Default)
	{
		const IConsoleVariable* Value = IConsoleManager::Get().FindConsoleVariable(Name);
		return Value ? Value->GetInt() : Default;
	}
}

FString FMMDPreviewMovieRequest::BuildArguments(const FString& Script, const FString& EngineRoot, const FString& Project, const FString& Output) const
{
	for (const FString* Value : { &Script, &EngineRoot, &Project, &Output, &ModelAsset, &AnimationAsset, &Motion, &Preset, &Stage })
		if (Value->Contains(TEXT("\"")) || Value->Contains(TEXT("\r")) || Value->Contains(TEXT("\n"))) return {};
	const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
	const bool bAsset = !AnimationAsset.IsEmpty();
	return FString::Printf(
		TEXT("-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\" ")
		TEXT("-EngineRoot \"%s\" -ProjectFile \"%s\" -OutputDirectory \"%s\" -SkipBuild ")
		TEXT("-ModelAsset \"%s\" %s \"%s\" -ImportVmdMorphs %d -Preset \"%s\" -Stage \"%s\" ")
		TEXT("-CameraMode Fixed -CameraLocation \"X=%.9f Y=%.9f Z=%.9f\" -CameraRotation \"P=%.9f Y=%.9f R=%.9f\" ")
		TEXT("-CameraFOV %.9f -Width %d -Height %d -Fps 30 -Physics %d -PlaybackRate %.6f ")
		TEXT("-MatchPreviewRendering -PreviewSubstrate %d -PreviewNativeMode %d -PreviewUnifiedDiffuse %d ")
		TEXT("-PreviewGBufferFormat %d -PreviewToonResolution %d -PreviewIndirectDiffuse %.6f -PreviewIndirectSpecular %.6f -PreviewVirtualShadows %d"),
		*Script, *EngineRoot, *Project, *Output, *ModelAsset, bAsset ? TEXT("-AnimationAsset") : TEXT("-Motion"),
		*(bAsset ? AnimationAsset : Motion), bAsset ? 1 : 0, *Preset, *Stage,
		Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll, HorizontalFOV,
		Size.X, Size.Y, bPhysics ? 1 : 0, PlaybackRate,
		MovieCVar(TEXT("r.Substrate"), 0) != 0, Backend.bActive ? Backend.Mode : -1,
		MovieCVar(TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"), 0) != 0,
		MovieCVar(TEXT("r.Substrate.ProjectGBufferFormat"), 0) != 0,
		MovieCVar(TEXT("r.Substrate.ToonProfile.Resolution"), 256),
		Backend.IndirectDiffuseScale, Backend.IndirectSpecularScale, MovieCVar(TEXT("r.Shadow.Virtual.Enable"), 0) != 0);
}

bool MMDImportSetting::BuildPreviewMovieRequest(FMMDPreviewMovieRequest& OutRequest, FString& OutError) const
{
	const auto Reject = [&OutError](const TCHAR* Message) { OutError = Message; return false; };
	AActor* Actor = ViewPanel ? ViewPanel->GetPreviewActor() : nullptr;
	auto* Component = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh || !Mesh->GetSkeleton() || !IsSavedProjectAsset(*Mesh) || !IsSavedProjectAsset(*Mesh->GetSkeleton()))
		return Reject(TEXT("请先导入角色并保存模型和骨架。后台制作读取当前工程 Content 中的资产。"));
	if (Actor->IsHidden()) return Reject(TEXT("请先结束烘焙预览，恢复角色播放后制作。"));
	if (!Component->GetComponentTransform().Equals(FTransform::Identity))
		return Reject(TEXT("当前角色已移动、旋转或缩放。请恢复原位置、朝向和缩放后制作。"));
	auto* Player = Component->GetSingleNodeInstance();
	const auto* Animation = Player ? Cast<UAnimSequence>(Player->GetCurrentAsset()) : nullptr;
	if (!Animation || Animation->GetSkeleton() != Mesh->GetSkeleton())
		return Reject(TEXT("请选择内置动作或当前角色骨架的已保存动画。"));
	if (ViewPanel->IsPreviewPoseHeld()) return Reject(TEXT("请先解除定格。制作会从动作第 0 秒开始播放。"));
	const auto Style = ViewPanel->GetDisplaySceneStyle();
	if (!Style.IsSet()) return Reject(TEXT("请先选择角色展示场景。完整舞台和独立 LookDev 暂不支持按当前预览制作。"));

	FMMDPreviewMovieRequest Request;
	Request.ModelAsset = Mesh->GetPathName();
	Request.Preset = TEXT("Original");
	Request.Stage = FMMDStudioDisplayScene::GetId(Style.GetValue());
	Request.PlaybackRate = ViewPanel->GetPreviewPlaybackSpeed();
	Request.bPhysics = ViewPanel->IsPreviewPhysicsEnabled() && ViewPanel->HasPreviewPhysics();
	if (const auto* Motion = MMDFindPreviewMotionPreset(ViewPanel->GetBuiltinMotion()))
	{
		const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		Request.Motion = Plugin ? MovieFullPath(Plugin->GetBaseDir() / TEXT("Resources/PreviewMotions") / Motion->RelativePath) : FString();
		if (!FPaths::FileExists(Request.Motion)) return Reject(TEXT("找不到当前内置 VMD，请检查插件资源。"));
	}
	else
	{
		if (!IsSavedProjectAsset(*Animation)) return Reject(TEXT("请先保存当前动画，后台才能读取。"));
		Request.AnimationAsset = Animation->GetPathName();
	}
	const auto* State = Component->GetAssetUserData<UMMDStageMaterialState>();
	if (State && State->AppliedPreset)
	{
		Request.Preset = MoviePresetId(*State->AppliedPreset);
		if (Request.Preset.IsEmpty() || State->AppliedPreset->GetOutermost()->IsDirty())
			return Reject(TEXT("当前风格是自定义或尚未保存的预设。请先选择已保存的内置风格。"));
		if (!State->RoleOverrides.IsEmpty())
			return Reject(TEXT("当前角色有自定义材质分类。请恢复默认分类后制作。"));
	}
	for (int32 Index = 0; Index < Mesh->GetMaterials().Num(); ++Index)
	{
		const UMaterialInterface* Source = Mesh->GetMaterials()[Index].MaterialInterface.Get();
		const UMaterialInterface* Expected = State && State->AppliedPreset && State->AppliedMaterials.IsValidIndex(Index)
			? State->AppliedMaterials[Index].Get() : Source;
		if (Component->GetMaterial(Index) != Expected
			|| (State && State->AppliedPreset && State->SourceMaterials.IsValidIndex(Index) && State->SourceMaterials[Index] != Source))
			return Reject(TEXT("角色存在未保存到网格的材质覆盖。请恢复模型原材质或重新应用内置风格。"));
	}
	if (!ViewPanel->GetProductionCamera(Request.Location, Request.Rotation, Request.HorizontalFOV, Request.Size))
		return Reject(TEXT("当前预览无法取得透视机位。请切换全身等透视机位，并让预览区有足够宽度。"));
	OutRequest = MoveTemp(Request);
	return true;
}

TSharedRef<SWidget> MMDImportSetting::BuildPreviewMovieControls()
{
	const auto Motion = TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled);
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[SNew(SMMDAnimatedButton, Motion).Tag(TEXT("MMDProducePreviewMovie"))
			.ButtonStyle(MMDPanelStyle::Primary()).ForegroundColor(MMDPanelStyle::PrimaryText)
			.ContentPadding(FMargin(10, 6))
			.Text_Lambda([this]() { return FText::FromString(PreviewMovieJob ? TEXT("后台制作中…") : TEXT("按当前预览制作")); })
			.IsEnabled_Lambda([this]() { return !PreviewMovieJob && ViewPanel && ViewPanel->HasPreviewAnimation(); })
			.ToolTipText(FText::FromString(TEXT("沿用当前角色、动作、风格、展示场景和透视机位，从动作第 0 秒输出 MP4。30 fps，画幅随预览区，文件保存在 Saved/MMDProduction。需要 FFmpeg。")))
			.OnClicked(this, &MMDImportSetting::StartPreviewMovie)]
		+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
		[SNew(SMMDAnimatedButton, Motion).ButtonStyle(MMDPanelStyle::Secondary())
			.Visibility_Lambda([this]() { return PreviewMovieJob ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsEnabled_Lambda([this]() { return PreviewMovieJob && !PreviewMovieJob->bCancelRequested; })
			.Text(FText::FromString(TEXT("取消"))).OnClicked(this, &MMDImportSetting::CancelPreviewMovie)];
}

FReply MMDImportSetting::StartPreviewMovie()
{
	if (PreviewMovieJob) return FReply::Handled();
	FMMDPreviewMovieRequest Request;
	FString Error;
	if (!BuildPreviewMovieRequest(Request, Error))
	{
		ShowImportProgress(Error, EMMDMessageType::Warning);
		return FReply::Handled();
	}
	if (!GDynamicRHI || FCString::Stricmp(GDynamicRHI->GetName(), TEXT("D3D12")) != 0 || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM6)
	{
		ShowImportProgress(TEXT("按当前预览制作需要 Windows D3D12 SM6，请检查工程渲染设置。"), EMMDMessageType::Warning);
		return FReply::Handled();
	}
	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
	const FString Script = Plugin ? MovieFullPath(Plugin->GetBaseDir() / TEXT("Scripts/CreateMMDMovieBackground.ps1")) : FString();
	const FString EngineRoot = MovieFullPath(FPaths::EngineDir() / TEXT(".."));
	const FString Project = MovieFullPath(FPaths::GetProjectFilePath());
	const FString PowerShell = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot")) / TEXT("System32/WindowsPowerShell/v1.0/powershell.exe");
	if (!FPaths::FileExists(Script) || !FPaths::FileExists(Project) || !FPaths::FileExists(PowerShell) || EngineRoot.IsEmpty())
	{
		ShowImportProgress(TEXT("未找到后台制作脚本、工程文件或 Windows PowerShell。"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	const FString Output = MovieFullPath(FPaths::ProjectSavedDir() / TEXT("MMDProduction") /
		(FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H%M%S_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
	const FString Arguments = Request.BuildArguments(Script, EngineRoot, Project, Output);
	if (Arguments.IsEmpty())
	{
		ShowImportProgress(TEXT("制作路径含有无法传递给后台的字符。"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	const auto Job = MakeShared<FMMDPreviewMovieJob>();
	Job->Output = Output;
	Job->Process = FPlatformProcess::CreateProc(*PowerShell, *Arguments, true, true, true,
		nullptr, 0, *FPaths::GetPath(Project), nullptr);
	if (!Job->Process.IsValid())
	{
		ShowImportProgress(TEXT("无法启动隐藏的后台制作进程。"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	PreviewMovieJob = Job;
	ShowImportProgress(TEXT("已开始后台制作，从动作第 0 秒输出：") + Output);
	const TWeakPtr<MMDImportSetting> WeakPanel = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPanel, Job](float)
	{
		const auto Panel = WeakPanel.Pin();
		if (FPlatformProcess::IsProcRunning(Job->Process))
		{
			if (Job->bCancelRequested && !Job->bCancelSent && IFileManager::Get().DirectoryExists(*Job->Output))
				Job->bCancelSent = FFileHelper::SaveStringToFile(TEXT("cancel"), *(Job->Output / TEXT("cancel.request")));
			if (Panel && !Job->bCancelRequested)
				if (const auto Report = ReadMovieJson(Job->Output / TEXT("report.json")))
				{
					double Frames = 0, Planned = 0;
					if (Report->TryGetNumberField(TEXT("frames"), Frames) && Report->TryGetNumberField(TEXT("planned_frames"), Planned)
						&& int32(Frames) != Job->ReportedFrames)
					{
						Job->ReportedFrames = int32(Frames);
						Panel->ShowImportProgress(FString::Printf(TEXT("后台制作：%d / %d 帧 · %s"), int32(Frames), int32(Planned), *Job->Output));
					}
				}
			return true;
		}
		int32 ExitCode = -1;
		FPlatformProcess::GetProcReturnCode(Job->Process, &ExitCode);
		if (Panel && Panel->PreviewMovieJob == Job)
		{
			Panel->PreviewMovieJob.Reset();
			const auto Success = ReadMovieJson(Job->Output / TEXT("production.json"));
			bool bSuccess = false;
			const FString Movie = Job->Output / TEXT("mmd_movie.mp4");
			if (ExitCode == 0 && Success && Success->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess
				&& IFileManager::Get().FileSize(*Movie) > 0)
				Panel->ShowImportProgress(TEXT("后台制作完成：") + Movie, EMMDMessageType::Success);
			else
			{
				const auto Failed = ReadMovieJson(Job->Output / TEXT("production_failed.json"));
				FString Failure;
				if (Failed) Failed->TryGetStringField(TEXT("error"), Failure);
				if (ExitCode == 2) Panel->ShowImportProgress(TEXT("已取消后台制作，保留已有结果：") + Job->Output, EMMDMessageType::Warning);
				else Panel->ShowImportProgress(FString::Printf(TEXT("后台制作失败（%d）：%s · %s"), ExitCode, *Failure, *Job->Output), EMMDMessageType::Error);
			}
		}
		return false;
	}), 0.5f);
	return FReply::Handled();
}

FReply MMDImportSetting::CancelPreviewMovie()
{
	if (PreviewMovieJob)
	{
		PreviewMovieJob->bCancelRequested = true;
		ShowImportProgress(TEXT("正在取消后台制作，已输出的文件会保留。"));
	}
	return FReply::Handled();
}
