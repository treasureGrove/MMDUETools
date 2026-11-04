#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "UI/SMMDAnimatedButton.h"
#include "Animation/Skeleton.h"
#include "AssetCompilingManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDToolPanelExperienceTest, "Ue5MMDTools.UI.Experience",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	TSharedPtr<SWidget> FindExperienceWidget(const TSharedRef<SWidget>& Root, FName Tag)
	{
		if (Root->GetTag() == Tag) return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (auto Found = FindExperienceWidget(Children->GetChildAt(Index), Tag)) return Found;
		}
		return nullptr;
	}

	int32 CountMatchingStyleCards(const TSharedRef<SWidget>& Root)
	{
		Root->UpdateAllAttributes();
		int32 Count = Root->GetTag().ToString().StartsWith(TEXT("/Ue5MMDTools/Stages/Presets/"))
			&& Root->GetVisibility() != EVisibility::Collapsed ? 1 : 0;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index) Count += CountMatchingStyleCards(Children->GetChildAt(Index));
		return Count;
	}

	struct FExperienceSampleMount
	{
		FString Root, Directory;
		bool bOwned = false;
		~FExperienceSampleMount() { if (bOwned) FPackageName::UnRegisterMountPoint(Root, Directory); }
	};

	class FExperienceViewportWarmup : public IAutomationLatentCommand
	{
	public:
		explicit FExperienceViewportWarmup(TSharedPtr<FEditorViewportClient> InClient) : Client(InClient) {}
		bool Update() override
		{
			if (const TSharedPtr<FEditorViewportClient> Pinned = Client.Pin())
			{
				Pinned->Tick(1.0f / 60.0f);
				Pinned->Invalidate();
				if (Pinned->Viewport) Pinned->Viewport->Draw();
			}
			return --Frames <= 0;
		}
	private:
		TWeakPtr<FEditorViewportClient> Client;
		int32 Frames = 36;
	};

	class FExperienceCaptureFrames : public IAutomationLatentCommand
	{
	public:
		FExperienceCaptureFrames(FAutomationTestBase* InTest, TSharedRef<SWidget> InPanel,
			TSharedPtr<FEditorViewportClient> InClient, FString InOutput, TFunction<void(int32)> InAction)
			: Test(InTest), Panel(InPanel), Client(InClient), Output(MoveTemp(InOutput)), Action(MoveTemp(InAction)) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (LastCapture > 0 && Now - LastCapture < 1.0 / 30.0) return false;
			if (Frame == 0)
			{
				Started = Now;
				IFileManager::Get().MakeDirectory(*Output, true);
				Timings = TEXT("frame,elapsed_seconds\n");
			}
			Action(Frame);
			if (const auto Pinned = Client.Pin())
			{
				Pinned->Invalidate();
				if (Pinned->Viewport) Pinned->Viewport->Draw();
			}
			TArray<FColor> Pixels;
			FIntVector Size;
			if (!Test->TestTrue(TEXT("逐帧捕获真实 Slate 动效"), FSlateApplication::Get().TakeScreenshot(Panel, Pixels, Size))) return true;
			TArray64<uint8> PNG;
			FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
			if (!Test->TestTrue(TEXT("保存动效源帧"), FFileHelper::SaveArrayToFile(PNG,
				*(Output / FString::Printf(TEXT("frame_%04d.png"), Frame))))) return true;
			Timings += FString::Printf(TEXT("%d,%.6f\n"), Frame, Now - Started);
			LastCapture = Now;
			if (++Frame < 90) return false;
			FFileHelper::SaveStringToFile(Timings, *(Output / TEXT("capture_times.csv")));
			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedRef<SWidget> Panel;
		TWeakPtr<FEditorViewportClient> Client;
		FString Output, Timings;
		TFunction<void(int32)> Action;
		int32 Frame = 0;
		double Started = 0, LastCapture = 0;
	};
}

bool FMMDToolPanelExperienceTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate 可用于交互验收"), FSlateApplication::IsInitialized())) return false;
	const TSharedRef<MMDImportSetting> Panel = SNew(MMDImportSetting);
	Panel->SetUIAnimationsEnabled(false, false);
	Panel->SelectWorkflowPage(1);
	const TSharedPtr<SSearchBox> Search = StaticCastSharedPtr<SSearchBox>(FindExperienceWidget(Panel, TEXT("MMD.StyleSearch")));
	const TSharedPtr<STextBlock> Count = StaticCastSharedPtr<STextBlock>(FindExperienceWidget(Panel, TEXT("MMD.StyleSearchCount")));
	const TSharedPtr<SButton> Clear = StaticCastSharedPtr<SButton>(FindExperienceWidget(Panel, TEXT("MMD.StyleSearchClear")));
	const TSharedPtr<SExpandableArea> Reference = StaticCastSharedPtr<SExpandableArea>(FindExperienceWidget(Panel, TEXT("MMD.StyleSearchReference")));
	const TSharedPtr<SWidget> Empty = FindExperienceWidget(Panel, TEXT("MMD.StyleSearchEmpty"));
	if (!TestTrue(TEXT("搜索、计数、清空、兼容分组与空状态均有实际控件"), Search && Count && Clear && Reference && Empty)) return false;

	const TSharedRef<FExperienceSampleMount> Mount = MakeShared<FExperienceSampleMount>();
	FString SampleDirectory;
	TStrongObjectPtr<USkeletalMesh> Mesh;
	bool bMeshWasDirty = false, bSkeletonWasDirty = false;
	if (FParse::Value(FCommandLine::Get(), TEXT("MMDPanelSampleDir="), SampleDirectory))
	{
		SampleDirectory = FPaths::ConvertRelativePathToFull(SampleDirectory);
		FPaths::NormalizeDirectoryName(SampleDirectory);
		if (!TestTrue(TEXT("样本仅允许读取已有导入缓存"), FPaths::IsUnderDirectory(SampleDirectory,
			FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets")))) return false;
		const FString Name = FPaths::GetCleanFilename(SampleDirectory);
		Mount->Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
		Mount->Directory = SampleDirectory + TEXT("/");
		if (FPackageName::MountPointExists(Mount->Root))
		{
			FString Existing;
			FPackageName::TryConvertLongPackageNameToFilename(Mount->Root, Existing);
			if (!TestTrue(TEXT("已有样本挂载指向同一缓存"), FPaths::IsSamePath(Existing, Mount->Directory))) return false;
		}
		else
		{
			FPackageName::RegisterMountPoint(Mount->Root, Mount->Directory);
			Mount->bOwned = true;
		}
		Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *(Mount->Root + TEXT("Model/") + Name + TEXT(".") + Name)));
		if (!TestNotNull(TEXT("实际缓存 MMD 网格"), Mesh.Get())) return false;
		bMeshWasDirty = Mesh->GetOutermost()->IsDirty();
		bSkeletonWasDirty = Mesh->GetSkeleton() && Mesh->GetSkeleton()->GetOutermost()->IsDirty();
		Panel->ViewPanel->ShowImportedSkeletalMesh(Mesh.Get());
		Panel->ViewPanel->ResetPreviewCamera();
		Panel->SetSelectedModelTextUI(FText::FromString(TEXT("本地 MMD 验证角色")));
	}
	const auto InitialTargets = Panel->GetRenderPresetTargets();
	TArray<TObjectPtr<UMaterialInterface>> InitialOverrides;
	if (!InitialTargets.IsEmpty()) InitialOverrides = InitialTargets[0]->OverrideMaterials;
	if (!InitialTargets.IsEmpty())
	{
		const FString PresetPath(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
		Panel->ApplyRenderPreset(PresetPath);
		const auto FirstOverrides = InitialTargets[0]->OverrideMaterials;
		TestTrue(TEXT("实际加载角色可应用风格"), Panel->IsRenderPresetApplied(PresetPath));
		Panel->ApplyRenderPreset(PresetPath);
		TestTrue(TEXT("重复点击当前风格复用 MID"), InitialTargets[0]->OverrideMaterials == FirstOverrides);
		Panel->ApplyRenderPreset(PresetPath, true);
		TestTrue(TEXT("显式强制刷新重新创建材质实例"), InitialTargets[0]->OverrideMaterials != FirstOverrides);
		Panel->RestoreRenderPreset();
		TestTrue(TEXT("强制刷新后仍能恢复最初材质"), InitialTargets[0]->OverrideMaterials == InitialOverrides);
	}

	const TSharedRef<bool> ButtonMotion = MakeShared<bool>(true);
	const TSharedRef<SMMDAnimatedButton> Probe = SNew(SMMDAnimatedButton,
		TAttribute<bool>::CreateLambda([ButtonMotion]() { return *ButtonMotion; }))
		.ButtonStyle(MMDPanelStyle::Secondary()).Text(FText::FromString(TEXT("交互反馈验收")));
	const TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 交互体验验收")))
		.ClientSize(FVector2D(1280, 900))
		[SNew(SVerticalBox)
			+ SVerticalBox::Slot().FillHeight(1)[Panel]
			+ SVerticalBox::Slot().AutoHeight()[Probe]];
	FSlateApplication::Get().AddWindow(Window);

	auto CheckSearch = [this, Panel, Count, Empty](const TCHAR* Context, int32 Expected)
	{
		TestEqual(FString(Context) + TEXT("：实际匹配卡片数"), CountMatchingStyleCards(Panel), Expected);
		TestEqual(FString(Context) + TEXT("：空状态"), Empty->GetVisibility() == EVisibility::Visible, Expected == 0);
		TestTrue(FString(Context) + TEXT("：计数文本"), Count->GetText().ToString().Contains(FString::Printf(TEXT("%d / 10"), Expected))
			|| (Expected == 10 && Count->GetText().ToString().StartsWith(TEXT("10 种风格"))));
	};
	struct FSearchCase { const TCHAR* Query; const TCHAR* Name; int32 Expected; };
	for (const FSearchCase& Case : {
		FSearchCase{TEXT("  nIkKi  "), TEXT("英文忽略大小写及首尾空格"), 1},
		FSearchCase{TEXT("霓虹"), TEXT("中文名称"), 1},
		FSearchCase{TEXT("柔和 高光"), TEXT("描述与多关键词"), 1},
		FSearchCase{TEXT("Endfield"), TEXT("英文路径跨主风格和兼容风格"), 2},
		FSearchCase{TEXT("___no_match___"), TEXT("无匹配输入"), 0},
		FSearchCase{TEXT("   \t "), TEXT("纯空白输入"), 10}})
	{
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Search, Case]() { Search->SetText(FText::FromString(Case.Query)); }, 0.05f));
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, CheckSearch, Reference, Case]()
		{
			CheckSearch(Case.Name, Case.Expected);
			if (FString(Case.Query) == TEXT("霓虹")) TestTrue(TEXT("搜索命中兼容风格时自动展开"), Reference->IsExpanded());
		}, 0.05f));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Search, Reference]()
	{
		TestFalse(TEXT("退出搜索恢复原折叠状态"), Reference->IsExpanded());
		Reference->SetExpanded(true);
		Search->SetText(FText::FromString(TEXT("Nikki")));
	}, 0.05f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Clear]() { Clear->SimulateClick(); }, 0.05f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Search, Reference, CheckSearch, InitialTargets, InitialOverrides]()
	{
		TestTrue(TEXT("实际清空按钮清除文本"), Search->GetText().IsEmpty());
		CheckSearch(TEXT("清空搜索"), 10);
		TestTrue(TEXT("清空恢复用户原先展开状态"), Reference->IsExpanded());
		Reference->SetExpanded(false);
		TestTrue(TEXT("搜索不切换应用目标"), Panel->GetRenderPresetTargets() == InitialTargets);
		if (!InitialTargets.IsEmpty()) TestTrue(TEXT("搜索不应用材质"), InitialTargets[0]->OverrideMaterials == InitialOverrides);
		Panel->SelectWorkflowPage(0);
		Panel->SetUIAnimationsEnabled(true, false);
		Panel->SelectWorkflowPage(1);
		TestEqual(TEXT("动画启动后内容立即可用"), Panel->WorkflowSwitcher->GetActiveWidgetIndex(), 1);
	}, 0.05f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel]()
	{
		const float PreviousPosition = Panel->GetNavigationPosition();
		Panel->SelectWorkflowPage(2);
		TestEqual(TEXT("快速切页从当前指示条位置续接"), Panel->NavigationStart, PreviousPosition, 0.001f);
		const float Alpha = Panel->GetPageTransitionAlpha();
		Panel->SelectWorkflowPage(2);
		TestEqual(TEXT("重复点击当前页不重启动画"), Panel->GetPageTransitionAlpha(), Alpha, 0.001f);
		Panel->SelectWorkflowPage(0);
		Panel->SelectWorkflowPage(2);
		TestEqual(TEXT("连续点击最终内容正确"), Panel->WorkflowSwitcher->GetActiveWidgetIndex(), 2);
	}, 0.03f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel]()
	{
		TestEqual(TEXT("导航动效完成后落在最终标签"), Panel->GetNavigationPosition(), 2.0f, 0.001f);
		TestEqual(TEXT("内容恢复完全不透明"), Panel->GetPageTransitionAlpha(), 1.0f, 0.001f);
		Panel->SelectWorkflowPage(1);
		Panel->ShowImportProgress(TEXT("动效中关闭偏好"), EMMDMessageType::Success);
		Panel->SetUIAnimationsEnabled(false, false);
		TestEqual(TEXT("关闭动效立即归位导航"), Panel->GetNavigationPosition(), 1.0f, 0.001f);
		TestEqual(TEXT("关闭动效立即归位内容"), Panel->GetPageTransitionAlpha(), 1.0f, 0.001f);
		TestEqual(TEXT("关闭动效立即归位状态"), Panel->GetStatusTransitionAlpha(), 1.0f, 0.001f);
		for (const auto& Pair : {TPair<EMMDMessageType, FString>(EMMDMessageType::Success, TEXT("完成")),
			TPair<EMMDMessageType, FString>(EMMDMessageType::Warning, TEXT("注意")),
			TPair<EMMDMessageType, FString>(EMMDMessageType::Error, TEXT("失败"))})
		{
			const FString Message = TEXT("保留完整结果原文：") + Pair.Value;
			Panel->ShowImportProgress(Message, Pair.Key);
			TestEqual(TEXT("状态标签不只依赖颜色"), Panel->StatusTypeText->GetText().ToString().TrimStartAndEnd(), Pair.Value);
			TestEqual(TEXT("状态原文保持可读"), Panel->StatusText->GetText().ToString(), Message);
		}
	}, 0.3f));
	const TSharedRef<FVector2D> PreviewPosition = MakeShared<FVector2D>();
	const TSharedRef<FVector2D> PreviewSize = MakeShared<FVector2D>();
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, PreviewPosition, PreviewSize]()
	{
		*PreviewPosition = Panel->ViewPanel->GetCachedGeometry().GetAbsolutePosition();
		*PreviewSize = Panel->ViewPanel->GetCachedGeometry().GetLocalSize();
		Panel->ShowImportProgress(FString::ChrN(240, TCHAR('A')) + TEXT("：较长资产路径与中文结果也应完整保留。"), EMMDMessageType::Warning);
	}, 0.05f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, PreviewPosition, PreviewSize]()
	{
		TestTrue(TEXT("长消息不推动预览位置"), Panel->ViewPanel->GetCachedGeometry().GetAbsolutePosition().Equals(*PreviewPosition, 0.5f));
		TestTrue(TEXT("长消息不挤压预览尺寸"), Panel->ViewPanel->GetCachedGeometry().GetLocalSize().Equals(*PreviewSize, 0.5f));
		TestTrue(TEXT("省略显示仍保存完整原文"), Panel->StatusText->GetText().ToString().Len() > 240);
	}, 0.1f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Probe]()
	{
		TestFalse(TEXT("空闲按钮没有持续计时器"), Probe->HasActiveTimers());
		Probe->OnMouseEnter(Probe->GetCachedGeometry(), FPointerEvent());
		TestTrue(TEXT("悬停启动有限时长反馈"), Probe->HasActiveTimers());
	}, 0.05f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Probe]()
	{
		TestFalse(TEXT("悬停反馈完成后停止计时器"), Probe->HasActiveTimers());
		Probe->OnMouseLeave(FPointerEvent());
	}, 0.25f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Probe]()
	{
		TestFalse(TEXT("离开反馈完成后停止计时器"), Probe->HasActiveTimers());
		Probe->SetEnabled(false);
		Probe->OnMouseEnter(Probe->GetCachedGeometry(), FPointerEvent());
		TestFalse(TEXT("禁用按钮不启动动效"), Probe->HasActiveTimers());
		Probe->OnMouseLeave(FPointerEvent());
		Probe->SetEnabled(true);
		Probe->OnMouseEnter(Probe->GetCachedGeometry(), FPointerEvent());
	}, 0.25f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([ButtonMotion]() { *ButtonMotion = false; }, 0.02f));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Window, Probe]()
	{
		TestFalse(TEXT("关闭偏好后按钮停止活动计时器"), Probe->HasActiveTimers());
		Probe->OnMouseLeave(FPointerEvent());
		Panel->SetUIAnimationsEnabled(true, false);
		Panel->ShowImportProgress(TEXT("角色已加载，可搜索风格并切换场景。"), EMMDMessageType::Info);
		Window->SetContent(Panel);
	}, 0.1f));

	const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("MMDPanelExperienceCapture"));
	const bool bRecord = FParse::Param(FCommandLine::Get(), TEXT("MMDPanelExperienceRecord"));
	if (bCapture || bRecord)
	{
		FString Output = FPaths::ProjectSavedDir() / TEXT("UIValidation/Experience");
		FParse::Value(FCommandLine::Get(), TEXT("MMDPanelExperienceOutput="), Output);
		Output = FPaths::ConvertRelativePathToFull(Output);
		if (TestTrue(TEXT("截图仅写入 Saved"), FPaths::IsUnderDirectory(Output, FPaths::ProjectSavedDir())))
		{
			IFileManager::Get().MakeDirectory(*Output, true);
			for (int32 Shot = 0; Shot < (bCapture ? 3 : 0); ++Shot)
			{
				ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, Search, Shot]()
				{
					Panel->SelectWorkflowPage(Shot == 2 ? 2 : 1);
					Search->SetText(Shot == 1 ? FText::FromString(TEXT("Nikki")) : FText::GetEmpty());
					if (Shot == 0 && Panel->ViewPanel->HasPreviewModel())
						Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
					// 窗口真实布局完成后再取景，避免初建视口的默认宽高比导致裁切。
					if (Panel->ViewPanel->HasPreviewModel()) Panel->ViewPanel->SetPreviewView(EMMDPreviewView::FullBody, true);
					FAssetCompilingManager::Get().FinishAllCompilation();
					if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
				}, 0.05f));
				ADD_LATENT_AUTOMATION_COMMAND(FExperienceViewportWarmup(Panel->ViewPanel->GetViewportClient()));
				ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Output, Shot]()
				{
					TArray<FColor> Pixels;
					FIntVector Size;
					if (!TestTrue(TEXT("实际 Slate 面板截图"), FSlateApplication::Get().TakeScreenshot(Panel, Pixels, Size))) return;
					TArray64<uint8> PNG;
					FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
					TestTrue(TEXT("保存实际引擎面板图"), FFileHelper::SaveArrayToFile(PNG,
						*(Output / FString::Printf(TEXT("experience_%d.png"), Shot))));
				}, 0.1f));
			}
			if (bRecord)
			{
				ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel]()
				{
					if (Panel->ViewPanel->HasPreviewModel()) Panel->ViewPanel->SetPreviewView(EMMDPreviewView::FullBody, true);
				}, 0.05f));
				ADD_LATENT_AUTOMATION_COMMAND(FExperienceViewportWarmup(Panel->ViewPanel->GetViewportClient()));
				ADD_LATENT_AUTOMATION_COMMAND(FExperienceCaptureFrames(this, Panel, Panel->ViewPanel->GetViewportClient(), Output / TEXT("frames"),
					[Panel, Search](int32 Frame)
					{
						if (Frame == 0) { Search->SetText(FText::GetEmpty()); Panel->SelectWorkflowPage(0); }
						if (Frame == 15) Panel->SelectWorkflowPage(1);
						if (Frame == 30) Search->SetText(FText::FromString(TEXT("Nikki")));
						if (Frame == 45) { Search->SetText(FText::GetEmpty()); Panel->SelectWorkflowPage(2); }
						if (Frame == 60) Panel->SelectWorkflowPage(1);
						if (Frame == 75) Panel->ShowImportProgress(TEXT("风格已就绪，可继续调整角色与场景。"), EMMDMessageType::Success);
					}));
			}
		}
	}
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Window, Mount, Mesh, bMeshWasDirty, bSkeletonWasDirty]()
	{
		if (Mesh.IsValid())
		{
			TestEqual(TEXT("体验验收未改源网格脏标记"), Mesh->GetOutermost()->IsDirty(), bMeshWasDirty);
			if (Mesh->GetSkeleton()) TestEqual(TEXT("体验验收未改源骨架脏标记"), Mesh->GetSkeleton()->GetOutermost()->IsDirty(), bSkeletonWasDirty);
		}
		Window->RequestDestroyWindow();
	}, 0.1f));
	return true;
}

#endif
