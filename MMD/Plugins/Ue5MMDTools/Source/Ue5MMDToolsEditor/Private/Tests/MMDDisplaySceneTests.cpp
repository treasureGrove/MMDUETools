#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDDisplaySceneTest, "Ue5MMDTools.UI.DisplayScenes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	struct FDisplaySampleMount
	{
		FString Root, Directory;
		~FDisplaySampleMount() { if (!Root.IsEmpty()) FPackageName::UnRegisterMountPoint(Root, Directory); }
	};
	TSharedPtr<SWidget> FindDisplayWidget(TSharedRef<SWidget> Root, FName Tag)
	{
		if (Root->GetTag() == Tag) return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (auto Found = FindDisplayWidget(Children->GetChildAt(Index), Tag)) return Found;
		return nullptr;
	}
	bool ClickDisplayWidget(TSharedRef<SWidget> Root, FName Tag)
	{
		const auto Widget = FindDisplayWidget(Root, Tag);
		if (!Widget || !Widget->IsEnabled() || Widget->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return false;
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.ProcessMouseMoveEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()), true);
		Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
		Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
		return true;
	}
	void PrepareDisplayCard(TSharedRef<SWidget> Root, TSharedRef<SScrollBox> Scroll, EMMDStudioDisplayStyle Style)
	{
		const bool bEarlyScene = Style == EMMDStudioDisplayStyle::SkyGlass || Style == EMMDStudioDisplayStyle::VioletStage
			|| Style == EMMDStudioDisplayStyle::WarmAtelier;
		if (const auto Area = FindDisplayWidget(Root, TEXT("MMDLegacyDisplayScenes")))
			StaticCastSharedPtr<SExpandableArea>(Area)->SetExpanded(bEarlyScene);
		const FName Tag(*(TEXT("MMDDisplayScene.") + FMMDStudioDisplayScene::GetId(Style)));
		Scroll->ScrollDescendantIntoView(FindDisplayWidget(Root, Tag), false, EDescendantScrollDestination::Center, 8.0f);
	}
	TSet<UStaticMeshComponent*> GetDisplayMeshes(UWorld* World)
	{
		TSet<UStaticMeshComponent*> Result;
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
			if (IsValid(*It) && It->GetWorld() == World && It->IsRegistered()) Result.Add(*It);
		return Result;
	}
	void CheckDisplayMeshWinding(FAutomationTestBase& Test, UStaticMesh* Mesh, const FString& Label)
	{
		const FMeshDescription* Source = Mesh->GetMeshDescription(0);
		if (!Test.TestNotNull(TEXT("生成布景保留可检查的网格描述：") + Label, Source)) return;
		FMeshDescription Computed = *Source;
		FStaticMeshAttributes Attributes(Computed);
		Attributes.RegisterTriangleNormalAndTangentAttributes();
		// 在副本上由引擎独立重算面法线；保留实际网格的顶点法线作比较。
		for (const FTriangleID Triangle : Computed.Triangles().GetElementIDs())
			Attributes.GetTriangleNormals()[Triangle] = FVector3f::ZeroVector;
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Computed);
		const FStaticMeshConstAttributes Result(Computed);
		const auto FaceNormals = Result.GetTriangleNormals();
		const auto VertexNormals = Result.GetVertexInstanceNormals();
		int32 InvalidNormals = 0, ReversedFaces = 0;
		for (const FTriangleID Triangle : Computed.Triangles().GetElementIDs())
		{
			FVector3f Average = FVector3f::ZeroVector;
			bool bValid = FaceNormals[Triangle].IsNormalized();
			for (const FVertexInstanceID Corner : Computed.GetTriangleVertexInstances(Triangle))
			{
				bValid &= VertexNormals[Corner].IsNormalized();
				Average += VertexNormals[Corner];
			}
			bValid &= !Average.IsNearlyZero() && !Average.ContainsNaN();
			if (!bValid) ++InvalidNormals;
			else if (FVector3f::DotProduct(FaceNormals[Triangle], Average.GetSafeNormal()) <= 0.0f) ++ReversedFaces;
		}
		Test.TestTrue(TEXT("生成布景具有三角面：") + Label, Computed.Triangles().Num() > 0);
		Test.TestEqual(TEXT("生成布景的几何与顶点法线有效：") + Label, InvalidNormals, 0);
		Test.TestEqual(TEXT("生成布景绕序与外向法线一致：") + Label, ReversedFaces, 0);
	}
	int32 CountDisplayStages(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AMMDStageActor> It(World); It; ++It)
			if (!It->IsActorBeingDestroyed()) ++Count;
		return Count;
	}
	class FDisplayCaptureWarmup : public IAutomationLatentCommand
	{
	public:
		explicit FDisplayCaptureWarmup(TSharedRef<FEditorViewportClient> InClient) : Client(InClient) {}
		bool Update() override
		{
			Client->Tick(1.0f / 60.0f);
			Client->Invalidate();
			if (Client->Viewport) Client->Viewport->Draw();
			return --Frames <= 0;
		}
	private:
		TSharedRef<FEditorViewportClient> Client;
		int32 Frames = 48;
	};
}

bool FMMDDisplaySceneTest::RunTest(const FString& Parameters)
{
	FAutomationEditorCommonUtils::CreateNewMap();
	const TSharedRef<MMDViewPanel> View = SNew(MMDViewPanel);
	const TSharedRef<MMDImportSetting> Panel = SNew(MMDImportSetting).ViewPanel(View);
	const TSharedRef<SScrollBox> SceneScroll = StaticCastSharedRef<SScrollBox>(Panel->WorkflowSwitcher->GetWidget(2).ToSharedRef());
	Panel->WorkflowSwitcher->SetActiveWidgetIndex(2);
	TestTrue(TEXT("场景页提供可选 Fab 入口"), FindDisplayWidget(Panel, TEXT("MMDOpenFabLibrary")).IsValid());
	TestTrue(TEXT("默认选择最初的纯白影棚"), View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::Studio);
	const auto LegacyArea = StaticCastSharedPtr<SExpandableArea>(FindDisplayWidget(Panel, TEXT("MMDLegacyDisplayScenes")));
	if (TestTrue(TEXT("早期云空场景收纳入口存在"), LegacyArea.IsValid()))
		TestFalse(TEXT("早期云空场景默认折叠"), LegacyArea->IsExpanded());
	const auto DisplayStyles = FMMDStudioDisplayScene::GetStyles();
	TestEqual(TEXT("提供原影棚、四套新展示与三套早期云空"), DisplayStyles.Num(), 8);
	TSet<FString> DisplayIds;
	for (const auto Style : DisplayStyles)
	{
		const FString Id = FMMDStudioDisplayScene::GetId(Style);
		TestFalse(TEXT("展示 ID 唯一：") + Id, DisplayIds.Contains(Id));
		DisplayIds.Add(Id);
		EMMDStudioDisplayStyle Parsed = EMMDStudioDisplayStyle::Studio;
		TestTrue(TEXT("展示 ID 可用于离线命令并忽略首尾空白及大小写：") + Id,
			FMMDStudioDisplayScene::TryParse(TEXT(" ") + Id.ToLower() + TEXT(" "), Parsed) && Parsed == Style);
		TestTrue(TEXT("展示有对应的 UI 卡片：") + Id, FindDisplayWidget(Panel, FName(*(TEXT("MMDDisplayScene.") + Id))).IsValid());
	}
	for (const TCHAR* Id : { TEXT("Studio"), TEXT("RoseStudio"), TEXT("SlateGallery"), TEXT("PearlAtrium"),
		TEXT("LilacStudio"), TEXT("SkyGlass"), TEXT("VioletStage"), TEXT("WarmAtelier") })
		TestTrue(TEXT("约定展示 ID 保留：") + FString(Id), DisplayIds.Contains(Id));
	if (!TestTrue(TEXT("展示布景可用"), View->StudioDisplayScene && View->StudioDisplayScene->IsValid())) return false;
	TestFalse(TEXT("无效展示枚举不改变场景"), View->PreviewDisplayScene(static_cast<EMMDStudioDisplayStyle>(255)));
	UWorld* PreviewWorld = View->PreviewScene->GetWorld();
	const TSet<UStaticMeshComponent*> InitialMeshes = GetDisplayMeshes(PreviewWorld);
	TSet<UStaticMesh*> CheckedGeneratedMeshes;
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		for (const auto Style : DisplayStyles)
		{
			const FString Label = FMMDStudioDisplayScene::GetId(Style);
			TestTrue(TEXT("展示可切换：") + Label, View->PreviewDisplayScene(Style));
			TestTrue(TEXT("当前展示选择匹配：") + Label, View->GetDisplaySceneStyle() == Style);
			TestEqual(TEXT("布景实际样式匹配：") + Label, View->StudioDisplayScene->GetStyle(), Style);
			TestEqual(TEXT("对应光照配方匹配：") + Label, View->GetLightingEnvironment(), FMMDStudioDisplayScene::GetLightingEnvironment(Style));
			TestEqual(TEXT("循环切换不积累已注册静态组件"), GetDisplayMeshes(PreviewWorld).Num(), InitialMeshes.Num());
			for (UStaticMeshComponent* Component : InitialMeshes) TestTrue(TEXT("循环切换复用原展示组件"), IsValid(Component) && Component->IsRegistered());
			if (Cycle == 0 && (Style == EMMDStudioDisplayStyle::RoseStudio || Style == EMMDStudioDisplayStyle::SlateGallery
				|| Style == EMMDStudioDisplayStyle::PearlAtrium))
			{
				bool bFoundNewGeometry = false;
				for (UStaticMeshComponent* Component : InitialMeshes)
				{
					UStaticMesh* Mesh = Component->GetStaticMesh();
					if (!Component->IsVisible() || !Mesh || !Mesh->HasAnyFlags(RF_Transient) || CheckedGeneratedMeshes.Contains(Mesh)) continue;
					CheckedGeneratedMeshes.Add(Mesh);
					bFoundNewGeometry = true;
					CheckDisplayMeshWinding(*this, Mesh, Label);
				}
				TestTrue(TEXT("圆润台面、圆角板、弧墙分别使用生成网格：") + Label, bFoundNewGeometry);
			}
			int32 Directions = 0, MainLights = 0, Skies = 0;
			for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
				if (It->GetWorld() == PreviewWorld && It->IsRegistered() && It->IsVisible())
				{ ++Directions; if (It->ForwardShadingPriority > 0) ++MainLights; }
			for (TObjectIterator<USkyLightComponent> It; It; ++It)
				if (It->GetWorld() == PreviewWorld && It->IsRegistered() && It->IsVisible()) ++Skies;
			TestEqual(TEXT("展示光照没有叠加旧方向光"), Directions, 3);
			TestEqual(TEXT("展示只有一个主光"), MainLights, 1);
			TestEqual(TEXT("展示没有叠加旧天光"), Skies, 1);
			TestTrue(TEXT("展示固定手动曝光"), View->PreviewPostProcessComponent->Settings.AutoExposureMethod == AEM_Manual);
			TestEqual(TEXT("展示曝光 EV0"), View->PreviewPostProcessComponent->Settings.AutoExposureBias, 0.0f);
			const auto& Exposure = View->PreviewPostProcessComponent->Settings;
			TestTrue(TEXT("展示曝光覆盖标记实际启用"), Exposure.bOverride_AutoExposureMethod && Exposure.bOverride_AutoExposureBias
				&& Exposure.bOverride_AutoExposureApplyPhysicalCameraExposure && !Exposure.AutoExposureApplyPhysicalCameraExposure);
		}
	}
	for (int32 Environment = 1; Environment <= 8; ++Environment)
	{
		const auto Env = static_cast<EMMDLightingEnvironment>(Environment);
		View->ApplyLightingEnvironment(Env);
		TestEqual(TEXT("原有八种 LookDev 保留各自环境"), View->GetLightingEnvironment(), Env);
		TestEqual(TEXT("普通 LookDev 退出展示选中，影棚恢复默认展示"), View->GetDisplaySceneStyle().IsSet(), Env == EMMDLightingEnvironment::Studio3Point);
	}
	for (const TCHAR* Name : { TEXT("NeonConcert"), TEXT("MoonMirror"), TEXT("SakuraTheater") })
	{
		auto* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *(FString(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_")) + Name));
		if (!TestNotNull(TEXT("原完整舞台仍可用"), Preset)) continue;
		TestTrue(TEXT("展示切换到完整舞台"), View->PreviewStageEnvironment(Preset));
		TestFalse(TEXT("完整舞台退出展示选中"), View->GetDisplaySceneStyle().IsSet());
		TestEqual(TEXT("只有一个预览完整舞台"), CountDisplayStages(PreviewWorld), 1);
		TestTrue(TEXT("完整舞台切回展示"), View->PreviewDisplayScene(EMMDStudioDisplayStyle::SkyGlass));
		TestNull(TEXT("展示清除完整舞台配置"), View->GetStagePreviewPreset());
		TestEqual(TEXT("展示销毁旧完整舞台"), CountDisplayStages(PreviewWorld), 0);
		TestEqual(TEXT("往返舞台不积累展示几何"), GetDisplayMeshes(PreviewWorld).Num(), InitialMeshes.Num());
	}
	FString SampleDirectory;
	const bool bHasSample = FParse::Value(FCommandLine::Get(), TEXT("MMDPanelSampleDir="), SampleDirectory);
	const TSharedRef<FDisplaySampleMount> Mount = MakeShared<FDisplaySampleMount>();
	if (!bHasSample)
	{
		AddWarning(TEXT("已检查场景与灯光切换；未提供 MMDPanelSampleDir，跳过角色状态及截图验收。"));
	}
	else
	{
		SampleDirectory = FPaths::ConvertRelativePathToFull(SampleDirectory);
		FPaths::NormalizeDirectoryName(SampleDirectory);
		if (!TestTrue(TEXT("只读本地缓存的 MMD 样本"), FPaths::IsUnderDirectory(SampleDirectory, FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets")))) return false;
		const FString Name = FPaths::GetCleanFilename(SampleDirectory);
		Mount->Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
		Mount->Directory = SampleDirectory + TEXT("/");
		FPackageName::RegisterMountPoint(Mount->Root, Mount->Directory);
		auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Mount->Root + TEXT("Model/") + Name + TEXT(".") + Name));
		if (!TestNotNull(TEXT("展示验证角色"), Mesh)) return false;
		const bool bMeshDirty = Mesh->GetOutermost()->IsDirty();
		const bool bSkeletonDirty = Mesh->GetSkeleton()->GetOutermost()->IsDirty();
		View->PreviewDisplayScene(EMMDStudioDisplayStyle::WarmAtelier);
		View->ShowImportedSkeletalMesh(Mesh);
		TestTrue(TEXT("加载角色保留展示风格"), View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::WarmAtelier);
		View->ShowImportedSkeletalMesh(Mesh);
		TestTrue(TEXT("替换角色也保留展示风格"), View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::WarmAtelier);
		Panel->SetSelectedModelTextUI(FText::FromString(TEXT("展示验收 · YYB 16th")));
		USkeletalMeshComponent* Component = View->GetPreviewMeshComponent();
		auto* Nikki = LoadObject<UMMDStageRenderPreset>(nullptr, TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
		if (!TestTrue(TEXT("角色使用暖暖材质验证展示"), Nikki && UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Nikki))) return false;
		UAnimSingleNodeInstance* Playback = Component->GetSingleNodeInstance();
		if (!TestNotNull(TEXT("展示保留真实动作播放实例"), Playback)) return false;
		UAnimationAsset* Animation = Playback->GetCurrentAsset();
		const auto Materials = Component->OverrideMaterials;
		View->SeekPreviewAnimation(0.65f);
		View->SetPreviewPlaybackSpeed(0.5f);
		View->SetPreviewPoseHeld(true);
		for (int32 Cycle = 0; Cycle < 2; ++Cycle)
		{
			const bool bPlaying = Cycle == 0;
			View->SetPreviewPlaying(bPlaying);
			for (const auto Style : DisplayStyles)
			{
				View->PreviewDisplayScene(Style);
				TestEqual(TEXT("切展示不重建角色播放实例"), Component->GetSingleNodeInstance(), Playback);
				TestEqual(TEXT("切展示不替换动作"), Playback->GetCurrentAsset(), Animation);
				TestEqual(TEXT("切展示不重置动作时间"), View->GetPreviewTime(), 0.65f);
				TestEqual(TEXT("切展示保留播放或暂停状态"), View->IsPreviewPlaying(), bPlaying);
				TestTrue(TEXT("切展示保留定格与物理选择"), View->IsPreviewPoseHeld() && View->IsPreviewPhysicsEnabled());
				TestEqual(TEXT("切展示保留倍速"), View->GetPreviewPlaybackSpeed(), 0.5f);
				TestEqual(TEXT("切展示保留材质槽数"), Component->OverrideMaterials.Num(), Materials.Num());
				for (int32 Index = 0; Index < Materials.Num(); ++Index)
					TestEqual(TEXT("切展示不替换角色材质"), Component->OverrideMaterials[Index], Materials[Index]);
			}
		}
		TestEqual(TEXT("展示操作不写角色资产"), Mesh->GetOutermost()->IsDirty(), bMeshDirty);
		TestEqual(TEXT("展示操作不写骨架资产"), Mesh->GetSkeleton()->GetOutermost()->IsDirty(), bSkeletonDirty);
	}
	const bool bCapture = bHasSample && FParse::Param(FCommandLine::Get(), TEXT("MMDPanelDisplayCapture"));
	Panel->WorkflowSwitcher->SetActiveWidgetIndex(0);
	View->SetPreviewView(EMMDPreviewView::FullBody, true);
	const TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 展示场景验收"))).ClientSize(FVector2D(1280, 900))[Panel];
	FSlateApplication::Get().AddWindow(Window);
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel]()
	{
		TestTrue(TEXT("真实点击场景页签"), ClickDisplayWidget(Panel, TEXT("MMDWorkflowTab2")));
	}, 0.2f));
	for (const auto Style : DisplayStyles)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, SceneScroll, Style]()
		{
			PrepareDisplayCard(Panel, SceneScroll, Style);
		}, 0.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, View, Style]()
		{
			const FName Tag(*(TEXT("MMDDisplayScene.") + FMMDStudioDisplayScene::GetId(Style)));
			TestTrue(TEXT("真实点击展示卡片"), ClickDisplayWidget(Panel, Tag));
			TestTrue(TEXT("展示卡片实际选中对应布景"), View->GetDisplaySceneStyle() == Style);
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		}, 0.25f));
		ADD_LATENT_AUTOMATION_COMMAND(FDisplayCaptureWarmup(View->CustomViewportClient.ToSharedRef()));
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, View, Style, bCapture]()
		{
			const auto OpenMap = FindDisplayWidget(Panel, TEXT("MMDOpenPreviewMap"));
			if (TestTrue(TEXT("实际场景页的打开关卡入口存在"), OpenMap.IsValid()))
				TestEqual(TEXT("界面刷新后影棚启用参考关卡，新增展示禁用不存在的关卡：") + FMMDStudioDisplayScene::GetId(Style),
					OpenMap->IsEnabled(), Style == EMMDStudioDisplayStyle::Studio);
			if (!bCapture) return;
			TArray<FColor> Pixels;
			FIntVector Size;
			if (!TestTrue(TEXT("实际引擎展示截图"), FSlateApplication::Get().TakeScreenshot(Panel, Pixels, Size))) return;
			TArray64<uint8> PNG;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("UIValidation");
			IFileManager::Get().MakeDirectory(*Directory, true);
			TestTrue(TEXT("保存真实展示截图"), FFileHelper::SaveArrayToFile(PNG, *(Directory / (TEXT("display_") + FMMDStudioDisplayScene::GetId(Style) + TEXT(".png")))));
			const FIntPoint ViewportSize = View->CustomViewportClient->Viewport->GetSizeXY();
			TArray<FColor> ViewportPixels;
			if (TestTrue(TEXT("读取实际展示视口"), View->CustomViewportClient->Viewport->ReadPixels(ViewportPixels)))
			{
				TArray64<uint8> ViewportPNG;
				FImageUtils::PNGCompressImageArray(ViewportSize.X, ViewportSize.Y, ViewportPixels, ViewportPNG);
				TestTrue(TEXT("保存无界面展示截图"), FFileHelper::SaveArrayToFile(ViewportPNG,
					*(Directory / (TEXT("display_") + FMMDStudioDisplayScene::GetId(Style) + TEXT("_viewport.png")))));
			}
		}, 0.1f));
	}
	if (bCapture)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, SceneScroll]()
		{
			PrepareDisplayCard(Panel, SceneScroll, EMMDStudioDisplayStyle::SkyGlass);
		}, 0.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, View]()
		{
			TestTrue(TEXT("侧视实拍切回晴空露台"), ClickDisplayWidget(Panel, TEXT("MMDDisplayScene.SkyGlass")));
			View->SetPreviewView(EMMDPreviewView::Side, true);
		}, 0.25f));
		ADD_LATENT_AUTOMATION_COMMAND(FDisplayCaptureWarmup(View->CustomViewportClient.ToSharedRef()));
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, View]()
		{
			const FIntPoint Size = View->CustomViewportClient->Viewport->GetSizeXY();
			TArray<FColor> Pixels;
			if (!TestTrue(TEXT("读取晴空露台侧面视口"), View->CustomViewportClient->Viewport->ReadPixels(Pixels))) return;
			TArray64<uint8> PNG;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			TestTrue(TEXT("保存晴空露台侧面实拍"), FFileHelper::SaveArrayToFile(PNG,
				*(FPaths::ProjectSavedDir() / TEXT("UIValidation/display_SkyGlass_side_viewport.png"))));
		}, 0.1f));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Window, Mount]()
	{
		GEditor->SelectNone(false, true);
		Window->RequestDestroyWindow();
	}, 0.1f));
	return true;
}
#endif
