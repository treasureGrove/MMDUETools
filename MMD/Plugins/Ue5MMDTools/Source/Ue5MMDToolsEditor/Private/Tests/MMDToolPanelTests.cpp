#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Stage/MMDStageMaterialState.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDShadowMapSubsystem.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "RenderUtils.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Tests/AutomationEditorCommon.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDToolPanelPresetTest, "Ue5MMDTools.UI.PresetWorkflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	class FPanelViewportWarmup : public IAutomationLatentCommand
	{
	public:
		explicit FPanelViewportWarmup(TSharedPtr<FEditorViewportClient> InClient) : Client(InClient) {}
		virtual bool Update() override
		{
			if (TSharedPtr<FEditorViewportClient> Pinned = Client.Pin(); Pinned && Pinned->Viewport)
			{
				Pinned->Invalidate();
				Pinned->Viewport->Draw();
			}
			return --Frames <= 0;
		}
	private:
		TWeakPtr<FEditorViewportClient> Client;
		int32 Frames = 48;
	};

	struct FPanelSampleMount
	{
		FString Root;
		FString Directory;
		~FPanelSampleMount() { if (!Root.IsEmpty()) FPackageName::UnRegisterMountPoint(Root, Directory); }
	};
}

bool FMMDToolPanelPresetTest::RunTest(const FString& Parameters)
{
	if (FParse::Param(FCommandLine::Get(), TEXT("MMDPanelVerifyStudioMap")))
	{
		UWorld* LoadedWorld = GEditor->GetEditorWorldContext().World();
		TestTrue(TEXT("只读复查默认影棚已保存关卡"), LoadedWorld && LoadedWorld->GetOutermost()->GetName() == TEXT("/Ue5MMDTools/Maps/LE_Studio3Point"));
		int32 Directions = 0, Mains = 0, LocalLights = 0, CalibratedPP = 0, Skies = 0;
		for (TObjectIterator<ULightComponent> It; It; ++It)
		{
			if (It->GetWorld() != LoadedWorld || !It->IsRegistered() || !It->IsVisible()) continue;
			if (const auto* Direction = Cast<UDirectionalLightComponent>(*It))
			{
				++Directions;
				if (Direction->ForwardShadingPriority > 0) ++Mains;
			}
			else ++LocalLights;
		}
		for (TActorIterator<APostProcessVolume> It(LoadedWorld); It; ++It)
			if (It->bUnbound && It->Settings.bOverride_AutoExposureMethod && It->Settings.AutoExposureMethod == AEM_Manual
				&& It->Settings.bOverride_AutoExposureBias && It->Settings.AutoExposureBias == 0.0f) ++CalibratedPP;
		for (TObjectIterator<USkyLightComponent> It; It; ++It)
			if (It->GetWorld() == LoadedWorld && It->IsRegistered() && It->IsVisible())
			{
				++Skies;
				TestEqual(TEXT("持久化影棚天光校准强度"), It->Intensity, 0.45f);
			}
		TestEqual(TEXT("持久化影棚三盏平行光"), Directions, 3);
		TestEqual(TEXT("持久化影棚唯一前向主光"), Mains, 1);
		TestEqual(TEXT("旧局部灯没有与新影棚叠加"), LocalLights, 0);
		TestEqual(TEXT("持久化影棚固定曝光"), CalibratedPP, 1);
		TestEqual(TEXT("持久化影棚没有重复叠加天光"), Skies, 1);
	}
	UWorld* World = FAutomationEditorCommonUtils::CreateNewMap();
	if (!TestNotNull(TEXT("测试关卡"), World)) return false;
	TSharedRef<MMDImportSetting> Panel = SNew(MMDImportSetting);
	TestTrue(TEXT("未加载角色时目标为空"), Panel->GetRenderPresetTargets().IsEmpty());
	TestEqual(TEXT("三个工作页签"), Panel->WorkflowSwitcher->GetNumWidgets(), 3);

	FString MeshPath;
	TSharedRef<FPanelSampleMount> SampleMount = MakeShared<FPanelSampleMount>();
	FString SampleDirectory;
	if (FParse::Value(FCommandLine::Get(), TEXT("MMDPanelSampleDir="), SampleDirectory))
	{
		SampleDirectory = FPaths::ConvertRelativePathToFull(SampleDirectory);
		FPaths::NormalizeDirectoryName(SampleDirectory);
		if (!TestTrue(TEXT("只读已缓存的本地导入样本"), FPaths::IsUnderDirectory(SampleDirectory, FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets")))) return false;
		const FString Name = FPaths::GetCleanFilename(SampleDirectory);
		SampleMount->Directory = SampleDirectory + TEXT("/");
		SampleMount->Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
		FPackageName::RegisterMountPoint(SampleMount->Root, SampleMount->Directory);
		MeshPath = SampleMount->Root + TEXT("Model/") + Name + TEXT(".") + Name;
	}
	FParse::Value(FCommandLine::Get(), TEXT("MMDPanelPreviewMesh="), MeshPath);
	if (MeshPath.IsEmpty()) MeshPath = TEXT("/Game/Characters/Mannequin_UE4/Meshes/SK_Mannequin.SK_Mannequin");
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!TestNotNull(TEXT("实际预览网格"), Mesh)) return false;
	Panel->ViewPanel->ShowImportedSkeletalMesh(Mesh);
	Panel->ViewPanel->ResetPreviewCamera();
	Panel->SetSelectedModelTextUI(FText::FromString(Mesh->GetName()));
	if (!SampleDirectory.IsEmpty())
	{
		Panel->ViewPanel->GetPreviewActor()->SetActorLabel(TEXT("本地 MMD 验证角色"));
		Panel->SetSelectedModelTextUI(FText::FromString(TEXT("本地 MMD 验证角色")));
	}
	const TArray<USkeletalMeshComponent*> PreviewTargets = Panel->GetRenderPresetTargets();
	if (!TestEqual(TEXT("预览目标"), PreviewTargets.Num(), 1)) return false;
	USkeletalMeshComponent* Preview = PreviewTargets[0];
	const auto OriginalOverrides = Preview->OverrideMaterials;
	const auto MeshMaterials = Mesh->GetMaterials();

	ASkeletalMeshActor* Selected = World->SpawnActor<ASkeletalMeshActor>();
	Selected->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);
	ASkeletalMeshActor* Unselected = World->SpawnActor<ASkeletalMeshActor>();
	Unselected->GetSkeletalMeshComponent()->SetSkeletalMesh(Mesh);
	const auto LevelOverrides = Selected->GetSkeletalMeshComponent()->OverrideMaterials;
	const TCHAR* Presets[] = {
		TEXT("EndfieldV2/DA_Render_EndfieldV2"), TEXT("DA_Render_InfinityNikki"), TEXT("DA_Render_OpenWorldToon"),
		TEXT("DA_Render_SilverCity"), TEXT("DA_Render_CinematicAnime"), TEXT("DA_Render_IndustrialAnime"),
		TEXT("DA_Render_NeonConcert"), TEXT("DA_Render_MoonMirror"), TEXT("DA_Render_SakuraTheater"), TEXT("Endfield/DA_Render_Endfield")
	};
	for (const TCHAR* Name : Presets)
	{
		const FString Path = FString(TEXT("/Ue5MMDTools/Stages/Presets/")) + Name;
		UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *Path);
		if (!TestNotNull(FString(TEXT("预设可加载：")) + Name, Preset)) continue;
		Panel->ApplyRenderPreset(Path);
		UMMDStageMaterialState* State = Preview->GetAssetUserData<UMMDStageMaterialState>();
		TestTrue(TEXT("确实应用所选预设"), State && State->AppliedPreset == Preset);
		TestTrue(TEXT("预览不改关卡实例"), Selected->GetSkeletalMeshComponent()->OverrideMaterials == LevelOverrides);
		const bool bNativePreset = FString(Name) == TEXT("EndfieldV2/DA_Render_EndfieldV2") || FString(Name) == TEXT("DA_Render_InfinityNikki");
		if (bNativePreset && Preset->CharacterToonMaterialOpaque)
		{
			TestTrue(TEXT("原生预设 Toon 父材质识别为受管原生材质"), MMDEndfieldNativeProfile::IsManagedMaterial(Preset->CharacterToonMaterialOpaque));
			TestFalse(TEXT("原生预设 Toon 父材质不计入旧 Unlit 管线"), MMDEndfieldNativeProfile::IsLegacyMMDMaterial(Preset->CharacterToonMaterialOpaque));
		}
		if (Preset->CharacterMaterialOpaque)
		{
			TestTrue(TEXT("兼容回退父材质识别为旧 Unlit MMD 材质"), MMDEndfieldNativeProfile::IsLegacyMMDMaterial(Preset->CharacterMaterialOpaque));
		}
		if (UWorld* PreviewWorld = Panel->ViewPanel->PreviewScene->GetWorld())
		{
			const bool bExpectLegacyActive = !(bNativePreset && Substrate::IsToonProfileEnabled());
			if (UMMDShadowMapSubsystem* ShadowSub = UMMDShadowMapSubsystem::Get())
			{
				TestEqual(FString(TEXT("阴影子系统按需休眠状态：")) + Name, ShadowSub->ShouldUpdateShadowForWorld(PreviewWorld), bExpectLegacyActive);
			}
			if (UMMDAnimeLightDataSubsystem* LightSub = UMMDAnimeLightDataSubsystem::Get())
			{
				TestEqual(FString(TEXT("灯光子系统按需休眠状态：")) + Name, LightSub->ShouldCollectLightsForWorld(PreviewWorld), bExpectLegacyActive);
			}
		}
	}
	Panel->RestoreRenderPreset();
	TestTrue(TEXT("跨预设切换后恢复最初覆盖"), Preview->OverrideMaterials == OriginalOverrides);
	for (int32 Index = 0; Index < MeshMaterials.Num(); ++Index)
		TestEqual(TEXT("源网格材质未变"), Mesh->GetMaterials()[Index].MaterialInterface, MeshMaterials[Index].MaterialInterface);
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(Selected, true, false);
	Panel->bApplyPresetToLevelSelection = true;
	Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
	TestNotNull(TEXT("只应用选中的关卡角色"), Selected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	Panel->SuspendLevelPresetPreviews(World);
	TestTrue(TEXT("保存前还原原始覆盖，不序列化临时 MID"), Selected->GetSkeletalMeshComponent()->OverrideMaterials == LevelOverrides);
	Panel->ResumeLevelPresetPreviews(World);
	TestNotNull(TEXT("保存后重新应用预览"), Selected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	TestNull(TEXT("未选中角色保持原样"), Unselected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	TestTrue(TEXT("关卡操作不改变面板预览"), Preview->OverrideMaterials == OriginalOverrides);
	GEditor->SelectActor(Unselected, true, false);
	TestEqual(TEXT("关卡多选目标"), Panel->GetRenderPresetTargets().Num(), 2);
	Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2"));
	TestNotNull(TEXT("多选第二角色也应用"), Unselected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	Panel->RestoreRenderPreset();
	TestTrue(TEXT("关卡目标可恢复"), Selected->GetSkeletalMeshComponent()->OverrideMaterials == LevelOverrides);
	TestNull(TEXT("恢复移除预设状态"), Unselected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	{
		TSharedPtr<MMDImportSetting> ClosingPanel = SNew(MMDImportSetting);
		ClosingPanel->bApplyPresetToLevelSelection = true;
		ClosingPanel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
		ClosingPanel.Reset();
		TestTrue(TEXT("关闭面板恢复关卡原始覆盖"), Selected->GetSkeletalMeshComponent()->OverrideMaterials == LevelOverrides);
		TestNull(TEXT("关闭面板清理临时状态"), Unselected->GetSkeletalMeshComponent()->GetAssetUserData<UMMDStageMaterialState>());
	}

	AMMDStageActor* UserStage = World->SpawnActor<AMMDStageActor>();
	const auto UserPreset = UserStage->RenderPreset;
	AMMDStageActor* FirstStage = nullptr;
	for (const TCHAR* Name : {TEXT("NeonConcert"), TEXT("MoonMirror"), TEXT("SakuraTheater")})
	{
		Panel->PlaceStagePreset(Name);
		int32 ToolStages = 0;
		AMMDStageActor* Current = nullptr;
		for (TActorIterator<AMMDStageActor> It(World); It; ++It)
			if (It->ActorHasTag(TEXT("MMDToolPanelStage"))) { ++ToolStages; Current = *It; }
		TestEqual(TEXT("反复切换只保留一个工具舞台"), ToolStages, 1);
		if (!FirstStage) FirstStage = Current;
		TestEqual(TEXT("原地复用同一个舞台"), Current, FirstStage);
		if (Current) TestTrue(TEXT("实际切换对应预设"), Current->RenderPreset && Current->RenderPreset->GetName() == FString(TEXT("DA_Render_")) + Name);
		TestEqual(TEXT("用户原有舞台不变"), UserStage->RenderPreset, UserPreset);
		AMMDStageActor* PreviewStage = Panel->ViewPanel->PreviewStageActor.Get();
		if (TestNotNull(TEXT("预览使用真实舞台 Actor"), PreviewStage) && Current)
		{
			TestEqual(TEXT("关卡和预览选用同一舞台配方"), PreviewStage->RenderPreset, Current->RenderPreset);
			TestEqual(TEXT("舞台主光强度与配方一致"), PreviewStage->KeyLight->Intensity, PreviewStage->RenderPreset->KeyLightIntensity);
			TestTrue(TEXT("前向通道明确选择主光"), PreviewStage->KeyLight->ForwardShadingPriority > PreviewStage->FillLight->ForwardShadingPriority
				&& PreviewStage->KeyLight->ForwardShadingPriority > PreviewStage->RimLight->ForwardShadingPriority);
			TestFalse(TEXT("舞台预览关闭中性台的后处理"), Panel->ViewPanel->PreviewPostProcessComponent->bEnabled);
			TestFalse(TEXT("舞台预览隐藏中性背景"), Panel->ViewPanel->PreviewBackdropComponent->IsVisible());
		}
	}
	auto* StagePreset = Panel->ViewPanel->GetStagePreviewPreset();
	Panel->ViewPanel->ShowImportedSkeletalMesh(Mesh);
	TestEqual(TEXT("换角色后保留所选舞台"), Panel->ViewPanel->GetStagePreviewPreset(), StagePreset);
	if (!SampleDirectory.IsEmpty()) Panel->ViewPanel->GetPreviewActor()->SetActorLabel(TEXT("本地 MMD 验证角色"));
	TestTrue(TEXT("预览世界允许 Lumen 跟踪"), Panel->ViewPanel->PreviewScene->GetWorld()->AllowLumenPrimitiveTrackingInPreviewWorld());
	const int32 ExpectedLightCounts[] = {4, 2, 2, 4, 3, 2, 3, 3};
	for (int32 Index = 1; Index <= 8; ++Index)
	{
		const auto Env = static_cast<EMMDLightingEnvironment>(Index);
		TestEqual(TEXT("每个环境实际生成正确灯数"), Panel->ViewPanel->ApplyLightingEnvironment(Env), ExpectedLightCounts[Index - 1]);
		TestNull(TEXT("检查光照时移除舞台灯光"), Panel->ViewPanel->GetStagePreviewPreset());
		TestTrue(TEXT("恢复中性检查台后处理"), Panel->ViewPanel->PreviewPostProcessComponent->bEnabled);
		const FPostProcessSettings& PP = Panel->ViewPanel->PreviewPostProcessComponent->Settings;
		if (Env == EMMDLightingEnvironment::Studio3Point)
		{
			TestTrue(TEXT("默认影棚固定曝光而非随构图测光"), PP.bOverride_AutoExposureMethod && PP.AutoExposureMethod == AEM_Manual
				&& PP.bOverride_AutoExposureBias && PP.AutoExposureBias == 0.0f
				&& PP.bOverride_AutoExposureApplyPhysicalCameraExposure && !PP.AutoExposureApplyPhysicalCameraExposure);
			int32 DirectionalCount = 0;
			int32 ForwardMainCount = 0;
			for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
			{
				if (It->GetWorld() != Panel->ViewPanel->PreviewScene->GetWorld() || !It->IsRegistered() || !It->IsVisible()) continue;
				++DirectionalCount;
				if (It->ForwardShadingPriority > 0) ++ForwardMainCount;
				TestEqual(TEXT("影棚只保留主光投影"), bool(It->CastShadows), It->ForwardShadingPriority > 0);
			}
			TestEqual(TEXT("默认影棚有实际主辅轮廓三盏平行光"), DirectionalCount, 3);
			TestEqual(TEXT("透明前向通道只有一个高优先主光"), ForwardMainCount, 1);
		}
		else TestFalse(TEXT("其他检查环境不会继承影棚固定曝光"), PP.bOverride_AutoExposureMethod || PP.bOverride_AutoExposureBias);
		FString SkyPath;
		float Rotation;
		float SkyIntensity;
		if (UMMDLightingEnvironmentLibrary::GetEnvironmentSky(Env, SkyPath, Rotation, &SkyIntensity))
		{
			for (TObjectIterator<USkyLightComponent> It; It; ++It)
			{
				if (It->GetWorld() == Panel->ViewPanel->PreviewScene->GetWorld() && It->IsRegistered() && It->IsVisible() && It->Cubemap)
				{
					TestEqual(TEXT("实际天光 Cubemap 来自所选环境"), It->Cubemap->GetPathName(), SkyPath);
					TestEqual(TEXT("实际天光旋转来自所选环境"), It->SourceCubemapAngle, Rotation);
					TestEqual(TEXT("实际天光强度来自所选环境"), It->Intensity, SkyIntensity);
				}
			}
			if (Panel->ViewPanel->PreviewSkyboxComponent && Panel->ViewPanel->PreviewSkyboxComponent->IsVisible())
			{
				const auto* Material = Cast<UMaterialInstanceDynamic>(Panel->ViewPanel->PreviewSkyboxComponent->GetMaterial(0));
				UTexture* Background = nullptr;
				float Angle = 0;
				TestTrue(TEXT("背景有真实 HDRI 参数"), Material && Material->GetTextureParameterValue(TEXT("HDRI_Map"), Background));
				if (Background) TestEqual(TEXT("背景 HDRI 和天光相同"), Background->GetPathName(), SkyPath);
				TestTrue(TEXT("背景有旋转参数"), Material && Material->GetScalarParameterValue(TEXT("HDRI_Angle"), Angle));
				TestEqual(TEXT("背景与天光旋转一致"), Angle, Rotation / 360.0f);
				float BackgroundIntensity = 0;
				TestTrue(TEXT("背景有强度参数"), Material && Material->GetScalarParameterValue(TEXT("Intensity"), BackgroundIntensity));
				TestEqual(TEXT("背景强度随所选环境同步"), BackgroundIntensity, SkyIntensity);
			}
		}
	}
	Panel->ViewPanel->ApplyLightingEnvironment(EMMDLightingEnvironment::Studio3Point);
	Panel->ViewPanel->ResetPreviewCamera();

	// 只在显式请求时捕获实际 Slate 面板，不将测试角色或关卡写入内容目录。
	if (FParse::Param(FCommandLine::Get(), TEXT("MMDPanelCapture")))
	{
		Panel->bApplyPresetToLevelSelection = false;
		Panel->ShowImportProgress(TEXT("角色已加载，可选择风格与场景。"), EMMDMessageType::Info);
		TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 工具")))
			.ClientSize(FVector2D(1280, 900))[Panel];
		FSlateApplication::Get().AddWindow(Window);
		const bool bLightingAudit = FParse::Param(FCommandLine::Get(), TEXT("MMDPanelLightingAudit"));
		TSharedRef<TArray<float>> LightingMeans = MakeShared<TArray<float>>();
		for (int32 Page = 0; Page < (bLightingAudit ? 8 : 5); ++Page)
		{
			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, Window, Page, bLightingAudit]()
			{
				Panel->WorkflowSwitcher->SetActiveWidgetIndex(Page >= 4 ? 1 : FMath::Min(Page, 2));
				if (Page == 1) Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
				if (Page == 2) Panel->ViewPanel->PreviewStageEnvironment(LoadObject<UMMDStageRenderPreset>(nullptr, TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater")));
				if (Page == 3)
				{
					Panel->ViewPanel->ApplyLightingEnvironment(EMMDLightingEnvironment::GoldenHour);
					StaticCastSharedPtr<SScrollBox>(Panel->WorkflowSwitcher->GetWidget(2))->ScrollToEnd();
				}
				if (Page == 4)
				{
					Window->Resize(FVector2D(960, 720));
					Panel->ViewPanel->ApplyLightingEnvironment(EMMDLightingEnvironment::Studio3Point);
					Panel->ViewPanel->ResetPreviewCamera();
				}
				if (Page == 5)
				{
					Panel->ViewPanel->ClearLightingEnvironment();
					if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
						Subsystem->SetPreviewLightOverride(UMMDLightingEnvironmentLibrary::PackEnvironmentLightData(EMMDLightingEnvironment::None));
				}
				if (Page == 6) Panel->ViewPanel->ApplyLightingEnvironment(EMMDLightingEnvironment::Studio3Point);
				if (bLightingAudit && Page >= 4 && Page <= 6)
				{
					// 灯光对照使用受光照的参照背景，避免把常亮展示背景计入关灯亮度。
					if (Panel->ViewPanel->StudioDisplayScene) Panel->ViewPanel->StudioDisplayScene->SetVisible(false);
					if (Panel->ViewPanel->PreviewFloorComponent) Panel->ViewPanel->PreviewFloorComponent->SetVisibility(true);
					if (Panel->ViewPanel->PreviewBackdropComponent) Panel->ViewPanel->PreviewBackdropComponent->SetVisibility(true);
				}
				if (Page == 7) Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2"));
				FAssetCompilingManager::Get().FinishAllCompilation();
				if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			}, 1.0f));
			// 按真实渲染帧预热；离屏 Slate 没有交互时，单纯等秒数可能仍读到缓存帧。
			ADD_LATENT_AUTOMATION_COMMAND(FPanelViewportWarmup(Panel->ViewPanel->CustomViewportClient));
			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Page, bLightingAudit, LightingMeans]()
			{
				TArray<FColor> Pixels;
				FIntVector Size;
				if (!TestTrue(TEXT("捕获实际面板"), FSlateApplication::Get().TakeScreenshot(Panel, Pixels, Size))) return;
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				const FString Path = FPaths::ProjectSavedDir() / TEXT("UIValidation") / FString::Printf(TEXT("panel_%d.png"), Page);
				TestTrue(TEXT("保存面板截图"), FFileHelper::SaveArrayToFile(PNG, *Path));
				if (bLightingAudit && Page >= 4 && Page <= 6)
				{
					TArray<FColor> ScenePixels;
					FViewport* Viewport = Panel->ViewPanel->CustomViewportClient->Viewport;
					if (!TestTrue(TEXT("读回实际视口光照对照"), Viewport && Viewport->ReadPixels(ScenePixels) && !ScenePixels.IsEmpty())) return;
					double Sum = 0;
					for (const FColor& Pixel : ScenePixels) Sum += FLinearColor(Pixel).GetLuminance();
					LightingMeans->Add(float(Sum / ScenePixels.Num()));
					if (Page == 6 && LightingMeans->Num() == 3)
					{
						const float Lit = (*LightingMeans)[0], Dark = (*LightingMeans)[1], Restored = (*LightingMeans)[2];
						TestTrue(TEXT("关灯显著降低真实场景亮度"), Lit > 0.01f && Dark < Lit * 0.65f);
						TestTrue(TEXT("恢复默认灯光后亮度可复现"), FMath::Abs(Restored - Lit) < Lit * 0.25f + 0.005f);
						FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"lit\":%.8f,\"lights_off\":%.8f,\"restored\":%.8f}\n"), Lit, Dark, Restored),
							*(FPaths::ProjectSavedDir() / TEXT("UIValidation/lighting_metrics.json")));
					}
				}
			}, 3.0f));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Window, SampleMount]() { Window->RequestDestroyWindow(); }, 0.2f));
	}
	return true;
}

#endif
