#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Editor/MMDPreviewMovieRequest.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectIterator.h"
#include "AssetCompilingManager.h"
#include "ImageUtils.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationEditorCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPresetClickAuditTest, "Ue5MMDTools.UI.PresetClickAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPreviewMovieTest, "Ue5MMDTools.UI.PreviewMovie",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	bool IsAuditButton(const SWidget& Widget)
	{
		const FString Type = Widget.GetTypeAsString();
		return Type == TEXT("SButton") || Type == TEXT("SMMDAnimatedButton");
	}

	TSharedPtr<SWidget> FindAuditWidget(TSharedRef<SWidget> Root, TFunctionRef<bool(const SWidget&)> Match)
	{
		if (Match(*Root)) return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (auto Found = FindAuditWidget(Children->GetChildAt(Index), Match)) return Found;
		return nullptr;
	}

	TSharedPtr<SWidget> FindAuditTag(TSharedRef<SWidget> Root, FName Tag)
	{
		return FindAuditWidget(Root, [Tag](const SWidget& Widget) { return Widget.GetTag() == Tag; });
	}

	TSharedPtr<SWidget> FindAuditButton(TSharedRef<SWidget> Root, const FString& Text)
	{
		return FindAuditWidget(Root, [&Text](const SWidget& Widget)
		{
			if (!IsAuditButton(Widget)) return false;
			return FindAuditWidget(const_cast<SWidget&>(Widget).AsShared(), [&Text](const SWidget& Child)
			{
				return Child.GetTypeAsString() == TEXT("STextBlock") && static_cast<const STextBlock&>(Child).GetText().ToString() == Text;
			}).IsValid();
		});
	}

	bool FindAuditAncestors(TSharedRef<SWidget> Root, const SWidget* Target, TArray<TSharedRef<SWidget>>& Path)
	{
		Path.Add(Root);
		if (&Root.Get() == Target) return true;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (FindAuditAncestors(Children->GetChildAt(Index), Target, Path)) return true;
		Path.Pop();
		return false;
	}

	struct FClickAuditState
	{
		FAutomationTestBase* Test = nullptr;
		TSharedPtr<MMDImportSetting> Panel;
		TSharedPtr<MMDViewPanel> View;
		TSharedPtr<SWindow> Window;
		TStrongObjectPtr<USkeletalMesh> Mesh;
		TStrongObjectPtr<UBlueprint> ActorFixture;
		FString MountRoot, MountDirectory, OutputDirectory;
		bool bOwnMount = false;
		TArray<UMaterialInterface*> OriginalMaterials;
		TArray<TSharedPtr<FJsonValue>> Events;
		TSet<FString> ButtonLabels;
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		double Started = FPlatformTime::Seconds();
		int32 PrimaryClicks = 0, PreviewClicks = 0, VisibilityClicks = 0, WheelEvents = 0, LayoutRefreshes = 0;
		int32 ProductionValidationClicks = 0;

		~FClickAuditState()
		{
			if (bOwnMount) FPackageName::UnRegisterMountPoint(MountRoot, MountDirectory);
		}

		void RefreshLayout()
		{
			Window->SlatePrepass();
			FSlateApplication::Get().Tick();
			++LayoutRefreshes;
		}

		void Record(const TCHAR* Kind, const FString& Label, const FString& Phase, const FVector2D& Position, bool bPrimary)
		{
			auto Event = MakeShared<FJsonObject>();
			Event->SetNumberField(TEXT("event_index"), Events.Num() + 1);
			Event->SetStringField(TEXT("kind"), Kind);
			Event->SetStringField(TEXT("control"), Label);
			Event->SetStringField(TEXT("phase"), Phase);
			Event->SetBoolField(TEXT("primary_action"), bPrimary);
			Event->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - Started);
			Event->SetNumberField(TEXT("x"), Position.X);
			Event->SetNumberField(TEXT("y"), Position.Y);
			Events.Add(MakeShared<FJsonValueObject>(Event));
		}

		void CollectButtons(TSharedRef<SWidget> Root)
		{
			if (IsAuditButton(*Root))
			{
				FindAuditWidget(Root, [this](const SWidget& Child)
				{
					if (Child.GetTypeAsString() == TEXT("STextBlock"))
						ButtonLabels.Add(static_cast<const STextBlock&>(Child).GetText().ToString());
					return false;
				});
			}
			FChildren* Children = Root->GetChildren();
			for (int32 Index = 0; Index < Children->Num(); ++Index) CollectButtons(Children->GetChildAt(Index));
		}
	};

	class FClickAuditAction : public IAutomationLatentCommand
	{
	public:
		FClickAuditAction(TSharedRef<FClickAuditState> InState, FString InLabel, FString InPhase,
			TFunction<TSharedPtr<SWidget>()> InTarget, TSharedPtr<SScrollBox> InScroll, TFunction<bool()> InVerify)
			: State(InState), Label(MoveTemp(InLabel)), Phase(MoveTemp(InPhase)), Target(MoveTemp(InTarget)),
			Scroll(InScroll), Verify(MoveTemp(InVerify)) {}

		bool Update() override
		{
			if (State->Test->HasAnyErrors()) return true;
			const double Now = FPlatformTime::Seconds();
			if (Started == 0) { Started = Now; ReadyAt = Now + 0.25; }
			if (Now - Started > 15.0)
			{
				State->Test->AddError(TEXT("点击审计无法命中控件：") + Label);
				return true;
			}
			State->RefreshLayout();
			if (Now < ReadyAt) return false;
			if (bClicked)
			{
				State->Test->TestTrue(TEXT("真实点击产生预期状态：") + Label, Verify());
				State->CollectButtons(State->Panel.ToSharedRef());
				return true;
			}
			auto Widget = Target();
			if (!Widget) return false;
			TArray<TSharedRef<SWidget>> Ancestors;
			FindAuditAncestors(State->Panel.ToSharedRef(), Widget.Get(), Ancestors);
			bool bExpansion = false;
			for (const auto& Ancestor : Ancestors)
			{
				if (Ancestor->GetTypeAsString() == TEXT("SExpandableArea") && !StaticCastSharedRef<SExpandableArea>(Ancestor)->IsExpanded())
				{
					Widget = FindAuditWidget(Ancestor, [](const SWidget& Child) { return IsAuditButton(Child); });
					bExpansion = true;
					break;
				}
			}
			if (!Widget || !Widget->IsEnabled() || Widget->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return false;
			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
			if (Scroll)
			{
				const FSlateRect Visible = Scroll->GetCachedGeometry().GetLayoutBoundingRect();
				const FSlateRect Bounds = Geometry.GetLayoutBoundingRect();
				if (Bounds.Top < Visible.Top + 4 || Bounds.Bottom > Visible.Bottom - 4)
				{
					const FVector2D WheelPosition = Scroll->GetCachedGeometry().LocalToAbsolute(Scroll->GetCachedGeometry().GetLocalSize() * 0.5f);
					const float Delta = Bounds.Top < Visible.Top + 4 ? 1.0f : -1.0f;
					auto& Slate = FSlateApplication::Get();
					Slate.ProcessMouseMoveEvent(FPointerEvent(0, WheelPosition, WheelPosition, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()), true);
					Slate.ProcessMouseWheelOrGestureEvent(FPointerEvent(0, WheelPosition, WheelPosition, TSet<FKey>(), EKeys::Invalid, Delta, FModifierKeysState()), nullptr);
					State->Record(TEXT("mouse_wheel"), TEXT("滚动至：") + Label, Phase, WheelPosition, false);
					++State->WheelEvents;
					ReadyAt = Now + 0.12;
					return false;
				}
			}
			auto& Slate = FSlateApplication::Get();
			const FWidgetPath Hit = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows(), true);
			if (!Hit.ContainsWidget(Widget.Get())) return false;
			Slate.ProcessMouseMoveEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()), true);
			Slate.ProcessMouseButtonDownEvent(State->Window->GetNativeWindow(), FPointerEvent(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
			Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
			State->Record(TEXT("mouse_click"), bExpansion ? TEXT("展开控件所在分组：") + Label : Label, Phase, Position, !bExpansion);
			if (bExpansion) ++State->VisibilityClicks;
			else
			{
				++State->PrimaryClicks;
				if (Phase == TEXT("preview")) ++State->PreviewClicks;
					if (Phase == TEXT("production_validation")) ++State->ProductionValidationClicks;
				bClicked = true;
			}
			ReadyAt = Now + 0.3;
			return false;
		}
	private:
		TSharedRef<FClickAuditState> State;
		FString Label, Phase;
		TFunction<TSharedPtr<SWidget>()> Target;
		TSharedPtr<SScrollBox> Scroll;
		TFunction<bool()> Verify;
		double Started = 0, ReadyAt = 0;
		bool bClicked = false;
	};

	class FClickAuditWarmup : public IAutomationLatentCommand
	{
	public:
		explicit FClickAuditWarmup(TSharedRef<FClickAuditState> InState) : State(InState) {}
		bool Update() override
		{
			State->View->GetViewportClient()->Tick(1.0f / 60.0f);
			State->RefreshLayout();
			return --Frames <= 0;
		}
	private:
		TSharedRef<FClickAuditState> State;
		int32 Frames = 48;
	};

	class FPreviewMovieCompletion : public IAutomationLatentCommand
	{
	public:
		FPreviewMovieCompletion(FAutomationTestBase* InTest, TFunction<bool()> InPoll, TFunction<void()> InCancel)
			: Test(InTest), Poll(MoveTemp(InPoll)), Cancel(MoveTemp(InCancel)) {}
		bool Update() override
		{
			const double Now = FPlatformTime::Seconds();
			if (Started == 0) Started = Now;
			if (Poll()) return true;
			if (!bCancelRequested && Now - Started > 900.0)
			{
				Test->AddError(TEXT("后台制作超过 15 分钟，已请求取消。"));
				Cancel();
				bCancelRequested = true;
			}
			if (bCancelRequested && Now - Started > 1020.0)
			{
				Test->AddError(TEXT("请求取消后仍未退出；后台进程状态需检查。"));
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		TFunction<bool()> Poll;
		TFunction<void()> Cancel;
		double Started = 0;
		bool bCancelRequested = false;
	};
}

bool FMMDPresetClickAuditTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
	{
		AddWarning(TEXT("点击审计仅允许 -RenderOffscreen 后台运行；未创建窗口或发送输入。"));
		return true;
	}
	if (!TestTrue(TEXT("后台 Slate 已初始化"), FSlateApplication::IsInitialized())) return false;
	FString SampleDirectory;
	if (!TestTrue(TEXT("必须提供已有缓存 MMDPanelSampleDir，不通过文件对话框导入"), FParse::Value(FCommandLine::Get(), TEXT("MMDPanelSampleDir="), SampleDirectory))) return false;
	SampleDirectory = FPaths::ConvertRelativePathToFull(SampleDirectory);
	FPaths::NormalizeDirectoryName(SampleDirectory);
	if (!TestTrue(TEXT("只读已有模型缓存"), FPaths::IsUnderDirectory(SampleDirectory, FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets")))) return false;
	const auto State = MakeShared<FClickAuditState>();
	State->Test = this;
	const FString Name = FPaths::GetCleanFilename(SampleDirectory);
	State->MountRoot = TEXT("/Game/MMDModels/") + Name + TEXT("/");
	State->MountDirectory = SampleDirectory + TEXT("/");
	if (FPackageName::MountPointExists(State->MountRoot))
	{
		FString Existing;
		FPackageName::TryConvertLongPackageNameToFilename(State->MountRoot, Existing);
		if (!TestTrue(TEXT("已有挂载指向同一缓存"), FPaths::IsSamePath(Existing, State->MountDirectory))) return false;
	}
	else
	{
		FPackageName::RegisterMountPoint(State->MountRoot, State->MountDirectory);
		State->bOwnMount = true;
	}
	State->Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *(State->MountRoot + TEXT("Model/") + Name + TEXT(".") + Name)));
	if (!TestNotNull(TEXT("缓存模型 fixture"), State->Mesh.Get()) || !TestNotNull(TEXT("缓存骨架"), State->Mesh->GetSkeleton())) return false;
	const bool bMeshDirty = State->Mesh->GetOutermost()->IsDirty();
	const bool bSkeletonDirty = State->Mesh->GetSkeleton()->GetOutermost()->IsDirty();
	State->ActorFixture.Reset(FKismetEditorUtilities::CreateBlueprint(AMMDActor::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("MMDClickAuditFixture")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	if (!TestTrue(TEXT("仅创建内存中的角色蓝图 fixture"), State->ActorFixture.IsValid() && State->ActorFixture->GeneratedClass)) return false;
	State->ActorFixture->SetFlags(RF_Transient);
	auto* FixtureCDO = State->ActorFixture->GeneratedClass->GetDefaultObject<AMMDActor>();
	FixtureCDO->GetMeshComponent()->SetSkeletalMesh(State->Mesh.Get());
	State->View = SNew(MMDViewPanel);
	State->Panel = SNew(MMDImportSetting).ViewPanel(State->View);
	if (!TestTrue(TEXT("API 准备模型，明确不计作已点击导入"), State->Panel->SetCurrentMMDActorClass(State->ActorFixture->GeneratedClass, TEXT("后台点击审计 fixture")))) return false;
	auto* Component = State->View->GetPreviewActor()->FindComponentByClass<USkeletalMeshComponent>();
	if (!TestNotNull(TEXT("实际预览角色组件"), Component)) return false;
	for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot) State->OriginalMaterials.Add(Component->GetMaterial(Slot));
	TestEqual(TEXT("起点为角色页"), State->Panel->WorkflowSwitcher->GetActiveWidgetIndex(), 0);
	TestEqual(TEXT("加载后默认恬静待机"), State->View->GetBuiltinMotion(), FName(TEXT("Idle")));
	TestTrue(TEXT("默认纯白影棚"), State->View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::Studio);
	TestTrue(TEXT("默认实时物理与播放开启"), State->View->IsPreviewPhysicsEnabled() && State->View->IsPreviewPlaying());
	State->Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 后台点击审计"))).ClientSize(FVector2D(1280, 900))
		.AutoCenter(EAutoCenter::None).ScreenPosition(FVector2D(0, 0))[State->Panel.ToSharedRef()];
	FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
	State->OutputDirectory = FPaths::ProjectSavedDir() / TEXT("ClickAudit") / (FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H%M%S_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	State->Report->SetStringField(TEXT("test"), TEXT("Ue5MMDTools.UI.PresetClickAudit"));
	State->Report->SetStringField(TEXT("mode"), TEXT("RenderOffscreen / real Slate pointer events"));
	State->Report->SetStringField(TEXT("counting_rule"), TEXT("One left mouse down/up pair = one click. Wheel events and keyboard events are separate. Layout refreshes are not input."));
	State->Report->SetStringField(TEXT("start"), TEXT("Role page, cached mesh loaded by API into a transient Blueprint fixture; model file dialog and menu opening were NOT clicked."));
	State->Report->SetStringField(TEXT("model_cache"), SampleDirectory);
	State->Report->SetStringField(TEXT("setup_not_counted"), TEXT("Read-only cache mount, LoadObject, transient Blueprint CDO mesh assignment, SetCurrentMMDActorClass, offscreen SWindow creation, layout refresh/physics warmup."));
	State->Report->SetNumberField(TEXT("window_width"), 1280);
	State->Report->SetNumberField(TEXT("window_height"), 900);
	State->Report->SetNumberField(TEXT("menu_clicks_executed"), 0);
	State->Report->SetNumberField(TEXT("model_dialog_clicks_executed"), 0);
	State->Report->SetNumberField(TEXT("default_idle_white_preview_clicks_after_model_ready"), 0);
	const auto RoleScroll = StaticCastSharedRef<SScrollBox>(State->Panel->WorkflowSwitcher->GetWidget(0).ToSharedRef());
	const auto StyleScroll = StaticCastSharedRef<SScrollBox>(State->Panel->WorkflowSwitcher->GetWidget(1).ToSharedRef());
	const auto SceneScroll = StaticCastSharedRef<SScrollBox>(State->Panel->WorkflowSwitcher->GetWidget(2).ToSharedRef());
	const FString NikkiPath(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditWarmup(State));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("角色 / 轻柔呼吸"), TEXT("preview"),
		[State]() { return FindAuditButton(State->Panel.ToSharedRef(), TEXT("轻柔呼吸")); }, RoleScroll,
		[State]() { return State->View->GetBuiltinMotion() == TEXT("Breath"); }));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("风格页签"), TEXT("preview"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDWorkflowTab1")); }, nullptr,
		[State]() { return State->Panel->WorkflowSwitcher->GetActiveWidgetIndex() == 1; }));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("无限暖暖 / 预览风格"), TEXT("preview"),
		[State, NikkiPath]() -> TSharedPtr<SWidget>
		{
			const auto Card = FindAuditTag(State->Panel.ToSharedRef(), FName(*NikkiPath));
			return Card ? FindAuditButton(Card.ToSharedRef(), TEXT("预览风格 →")) : nullptr;
		}, StyleScroll, [State, NikkiPath]() { return State->Panel->IsRenderPresetApplied(NikkiPath); }));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, NikkiPath]()
	{
		const bool bWhiteCustomized = State->View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::Studio
			&& State->View->GetBuiltinMotion() == TEXT("Breath") && State->Panel->IsRenderPresetApplied(NikkiPath);
		TestTrue(TEXT("更换动作与暖暖风格后仍保留默认纯白影棚"), bWhiteCustomized);
		State->Report->SetBoolField(TEXT("white_default_customized_preview_verified"), bWhiteCustomized);
		State->Report->SetNumberField(TEXT("white_default_customized_preview_clicks"), State->PrimaryClicks + State->VisibilityClicks);
		State->Report->SetNumberField(TEXT("white_default_customized_preview_wheel_events"), State->WheelEvents);
	}, 0.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("场景页签"), TEXT("preview"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDWorkflowTab2")); }, nullptr,
		[State]() { return State->Panel->WorkflowSwitcher->GetActiveWidgetIndex() == 2; }));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("樱雾柔光 / 切换展示"), TEXT("preview"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDDisplayScene.RoseStudio")); }, SceneScroll,
		[State]() { return State->View->GetDisplaySceneStyle() == EMMDStudioDisplayStyle::RoseStudio; }));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditWarmup(State));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, NikkiPath]()
	{
		auto* PreviewMesh = State->View->GetPreviewActor()->FindComponentByClass<USkeletalMeshComponent>();
		auto* Playback = PreviewMesh ? Cast<UMMDPhysicsPlaybackAnimInstance>(PreviewMesh->GetAnimInstance()) : nullptr;
		TestTrue(TEXT("点击后保留同一缓存模型"), PreviewMesh && PreviewMesh->GetSkeletalMeshAsset() == State->Mesh.Get());
		TestTrue(TEXT("真实 MMD 物理已经初始化"), Playback && Playback->IsPhysicsEnabled() && Playback->IsPhysicsInitialized() && Playback->GetPhysicsRigidBodyCount() > 0);
		TestTrue(TEXT("动作与播放状态正确"), State->View->GetBuiltinMotion() == TEXT("Breath") && State->View->IsPreviewPlaying());
		TestTrue(TEXT("暖暖风格真实应用"), State->Panel->IsRenderPresetApplied(NikkiPath));
		bool bMaterialsChanged = false;
		for (int32 Slot = 0; PreviewMesh && Slot < State->OriginalMaterials.Num(); ++Slot)
			bMaterialsChanged |= PreviewMesh->GetMaterial(Slot) != State->OriginalMaterials[Slot];
		TestTrue(TEXT("实际材质槽发生替换"), bMaterialsChanged);
		TestTrue(TEXT("场景同步对应灯光"), State->View->GetLightingEnvironment() == EMMDLightingEnvironment::DisplayRoseStudio);
		UWorld* World = State->View->GetPreviewActor()->GetWorld();
		int32 Directional = 0, Sky = 0, Main = 0;
		bool bManualExposure = false;
		for (TObjectIterator<UDirectionalLightComponent> It; It; ++It)
			if (It->GetWorld() == World && It->IsRegistered() && It->IsVisible()) { ++Directional; Main += It->ForwardShadingPriority > 0; }
		for (TObjectIterator<USkyLightComponent> It; It; ++It)
			if (It->GetWorld() == World && It->IsRegistered() && It->IsVisible()) ++Sky;
		for (TObjectIterator<UPostProcessComponent> It; It; ++It)
			if (It->GetWorld() == World && It->IsRegistered())
			{
				const auto& Exposure = It->Settings;
				bManualExposure |= Exposure.bOverride_AutoExposureMethod && Exposure.AutoExposureMethod == AEM_Manual
					&& Exposure.bOverride_AutoExposureBias && Exposure.AutoExposureBias == 0.0f
					&& Exposure.bOverride_AutoExposureApplyPhysicalCameraExposure && !Exposure.AutoExposureApplyPhysicalCameraExposure;
			}
		TestTrue(TEXT("真实预览灯数与固定曝光正确"), Directional == 3 && Sky == 1 && Main == 1 && bManualExposure);
		State->Report->SetNumberField(TEXT("preview_primary_clicks"), State->PreviewClicks);
		State->Report->SetBoolField(TEXT("physics_initialized"), Playback && Playback->IsPhysicsInitialized());
		State->Report->SetNumberField(TEXT("physics_rigid_body_count"), Playback ? Playback->GetPhysicsRigidBodyCount() : 0);
		State->Report->SetStringField(TEXT("final_motion"), State->View->GetBuiltinMotion().ToString());
		State->Report->SetStringField(TEXT("final_style"), State->Panel->IsRenderPresetApplied(NikkiPath) ? NikkiPath : TEXT("Nikki not applied"));
		const auto ActualStyle = State->View->GetDisplaySceneStyle();
		State->Report->SetStringField(TEXT("final_scene"), ActualStyle.IsSet() ? FMMDStudioDisplayScene::GetId(ActualStyle.GetValue()) : TEXT("none"));
		State->Report->SetNumberField(TEXT("custom_scene_preview_clicks"), State->PrimaryClicks + State->VisibilityClicks);
		State->Report->SetBoolField(TEXT("material_slots_changed"), bMaterialsChanged);
		State->Report->SetBoolField(TEXT("fixed_exposure_verified"), bManualExposure);
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	}, 0.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("角色页签 / 制作检查"), TEXT("composer"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDWorkflowTab0")); }, nullptr,
		[State]() { return State->Panel->WorkflowSwitcher->GetActiveWidgetIndex() == 0; }));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("按当前预览制作 / 缓存来源校验"), TEXT("production_validation"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDProducePreviewMovie")); }, nullptr,
		[State]()
		{
			const bool bRejected = !State->Panel->PreviewMovieJob
				&& State->Panel->CurrentStatusType == EMMDMessageType::Warning
				&& State->Panel->CurrentStatusMessage.Contains(TEXT("Content"));
			State->Report->SetBoolField(TEXT("production_rejected_saved_cache_without_launch"), bRejected);
			return bRejected;
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("合成关卡序列 / 打开窗口"), TEXT("composer"),
		[State]() { return FindAuditButton(State->Panel.ToSharedRef(), TEXT("合成关卡序列")); }, RoleScroll,
		[]()
		{
			for (const auto& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows())
				if (Window->GetTitle().ToString() == TEXT("MMD Sequence Composer")) return true;
			return false;
		}));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, bMeshDirty, bSkeletonDirty]()
	{
		bool bComposerOpened = false;
		for (const auto& Window : FSlateApplication::Get().GetInteractiveTopLevelWindows())
			if (Window->GetTitle().ToString() == TEXT("MMD Sequence Composer"))
			{
				bComposerOpened = true;
				State->CollectButtons(Window);
				Window->RequestDestroyWindow();
			}
		TestTrue(TEXT("真实合成窗口已打开"), bComposerOpened);
		TestFalse(TEXT("内置临时动作没有自动进入制作动画槽"), State->Panel->ComposerAnimSequence.IsValid());
		TestFalse(TEXT("预览机位没有自动生成相机序列"), State->Panel->ComposerCameraSequence.IsValid());
		const auto MovieButton = FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDProducePreviewMovie"));
		const bool bMP4Entry = MovieButton && IsAuditButton(*MovieButton);
		TArray<FString> SortedLabels = State->ButtonLabels.Array();
		SortedLabels.Sort();
		TArray<TSharedPtr<FJsonValue>> Labels;
		for (const FString& Label : SortedLabels)
		{
			Labels.Add(MakeShared<FJsonValueString>(Label));
		}
		TestTrue(TEXT("面板已有按当前预览制作 MP4 的入口"), bMP4Entry);
		TestEqual(TEXT("只读缓存模型未变脏"), State->Mesh->GetOutermost()->IsDirty(), bMeshDirty);
		TestEqual(TEXT("只读缓存骨架未变脏"), State->Mesh->GetSkeleton()->GetOutermost()->IsDirty(), bSkeletonDirty);
		State->Report->SetBoolField(TEXT("composer_opened_by_click"), bComposerOpened);
		State->Report->SetBoolField(TEXT("composer_has_animation"), State->Panel->ComposerAnimSequence.IsValid());
		State->Report->SetBoolField(TEXT("composer_has_camera_sequence"), State->Panel->ComposerCameraSequence.IsValid());
		State->Report->SetBoolField(TEXT("mp4_button_observed"), bMP4Entry);
		State->Report->SetStringField(TEXT("export_boundary"), TEXT("The preview MP4 CTA exists and was clicked once; the read-only Saved cache fixture was rejected before launching a process. Composer was only opened; no sequence/assets/video were created. Preview 3/5-click checkpoints exclude this production validation click."));
		State->Report->SetNumberField(TEXT("production_validation_clicks"), State->ProductionValidationClicks);
		State->Report->SetNumberField(TEXT("primary_clicks_total"), State->PrimaryClicks);
		State->Report->SetNumberField(TEXT("ensure_visible_header_clicks"), State->VisibilityClicks);
		State->Report->SetNumberField(TEXT("mouse_down_up_pairs_total"), State->PrimaryClicks + State->VisibilityClicks);
		State->Report->SetNumberField(TEXT("mouse_wheel_events"), State->WheelEvents);
		State->Report->SetNumberField(TEXT("keyboard_events"), 0);
		State->Report->SetNumberField(TEXT("programmatic_scroll_into_view_calls"), 0);
		State->Report->SetNumberField(TEXT("layout_refreshes_not_input"), State->LayoutRefreshes);
		State->Report->SetArrayField(TEXT("events"), State->Events);
		State->Report->SetArrayField(TEXT("observed_button_labels"), Labels);
		State->Report->SetBoolField(TEXT("success"), !HasAnyErrors());
		FString Json;
		FJsonSerializer::Serialize(State->Report, TJsonWriterFactory<>::Create(&Json));
		IFileManager::Get().MakeDirectory(*State->OutputDirectory, true);
		const FString ReportPath = State->OutputDirectory / TEXT("preset-click-audit.json");
		TestTrue(TEXT("保存实际点击审计 JSON"), FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
		AddInfo(TEXT("Click audit report: ") + FPaths::ConvertRelativePathToFull(ReportPath));
		State->Window->RequestDestroyWindow();
	}, 0.0f));
	return true;
}

bool FMMDPreviewMovieTest::RunTest(const FString& Parameters)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")))
	{
		AddWarning(TEXT("预览制作回归仅允许 -RenderOffscreen；未创建窗口或启动进程。"));
		return true;
	}
	FString ModelAsset;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MMDPreviewMovieModelAsset="), ModelAsset))
	{
		AddWarning(TEXT("未提供 MMDPreviewMovieModelAsset，跳过已保存工程模型的正向快照；测试不会导入或保存资产。"));
		return true;
	}
	if (!TestTrue(TEXT("后台 Slate 已初始化"), FSlateApplication::IsInitialized())) return false;
	const auto State = MakeShared<FClickAuditState>();
	State->Test = this;
	State->Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *ModelAsset));
	if (!TestNotNull(TEXT("明确指定的已保存测试模型"), State->Mesh.Get()) || !TestNotNull(TEXT("测试模型骨架"), State->Mesh->GetSkeleton())) return false;
	const bool bMeshDirty = State->Mesh->GetOutermost()->IsDirty();
	const bool bSkeletonDirty = State->Mesh->GetSkeleton()->GetOutermost()->IsDirty();
	State->ActorFixture.Reset(FKismetEditorUtilities::CreateBlueprint(AMMDActor::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("MMDPreviewMovieFixture")), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
	if (!TestTrue(TEXT("仅创建 transient 角色 fixture"), State->ActorFixture.IsValid() && State->ActorFixture->GeneratedClass)) return false;
	State->ActorFixture->SetFlags(RF_Transient);
	AMMDActor* Defaults = State->ActorFixture->GeneratedClass->GetDefaultObject<AMMDActor>();
	Defaults->GetMeshComponent()->SetSkeletalMesh(State->Mesh.Get());
	if (auto* Capsule = Defaults->FindComponentByClass<UCapsuleComponent>()) Capsule->SetVisibility(false);
	State->View = SNew(MMDViewPanel);
	State->Panel = SNew(MMDImportSetting).ViewPanel(State->View);
	if (!TestTrue(TEXT("准备真实面板模型"), State->Panel->SetCurrentMMDActorClass(State->ActorFixture->GeneratedClass, TEXT("本地测试角色")))) return false;
	State->Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 后台制作快照回归"))).ClientSize(FVector2D(1280, 900))
		.AutoCenter(EAutoCenter::None).ScreenPosition(FVector2D(0, 0))[State->Panel.ToSharedRef()];
	FSlateApplication::Get().AddWindow(State->Window.ToSharedRef());
	FText MotionMessage;
	TestTrue(TEXT("选择轻柔呼吸"), State->View->PlayBuiltinMotion(TEXT("Breath"), MotionMessage));
	State->View->SetPreviewPlaying(false);
	State->View->SetPreviewPlaybackSpeed(1.5f);
	State->View->PreviewDisplayScene(EMMDStudioDisplayStyle::RoseStudio);
	State->View->SetPreviewView(EMMDPreviewView::FullBody, true);
	const FString NikkiPath(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
	State->Panel->ApplyRenderPreset(NikkiPath);
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditWarmup(State));
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, ModelAsset]()
	{
		const auto Button = FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDProducePreviewMovie"));
		TestTrue(TEXT("真实 Slate 制作按钮存在且已启用"), Button && IsAuditButton(*Button) && Button->IsEnabled());
		FMMDPreviewMovieRequest Snapshot;
		FString Error;
		const bool bSnapshotReady = State->Panel->BuildPreviewMovieRequest(Snapshot, Error);
		if (TestTrue(TEXT("已保存工程模型能够生成制作快照：") + Error, bSnapshotReady))
		{
			TestEqual(TEXT("快照使用当前模型资产"), Snapshot.ModelAsset, ModelAsset);
			TestTrue(TEXT("快照指向轻柔呼吸原始 VMD"), Snapshot.Motion.EndsWith(TEXT("MMDAgent/mei_breath/mei_breath.vmd")) && Snapshot.AnimationAsset.IsEmpty());
			TestEqual(TEXT("快照继承暖暖风格"), Snapshot.Preset, FString(TEXT("InfinityNikki")));
			TestEqual(TEXT("快照继承樱雾展示场景"), Snapshot.Stage, FString(TEXT("RoseStudio")));
			TestEqual(TEXT("快照继承 1.5 倍速"), Snapshot.PlaybackRate, 1.5f);
			TestTrue(TEXT("带刚体模型继承启用物理"), State->View->HasPreviewPhysics() && Snapshot.bPhysics);
			const auto Client = State->View->GetViewportClient();
			TestTrue(TEXT("快照记录实际机位"), Snapshot.Location.Equals(Client->GetViewLocation(), 0.001) && Snapshot.Rotation.Equals(Client->GetViewRotation(), 0.001));
			const FIntPoint ViewportSize = Client->Viewport->GetSizeXY();
			TestTrue(TEXT("输出画幅跟随实际视口且为偶数尺寸"), Snapshot.Size.X > 0 && Snapshot.Size.Y > 0 && Snapshot.Size.X % 2 == 0 && Snapshot.Size.Y % 2 == 0
				&& FMath::Abs(double(Snapshot.Size.X) / Snapshot.Size.Y / (double(ViewportSize.X) / ViewportSize.Y) - 1.0) < 0.01);
			TestTrue(TEXT("快照取得有效透视 FOV"), Snapshot.HorizontalFOV >= 1.0 && Snapshot.HorizontalFOV <= 170.0);
			const FString Arguments = Snapshot.BuildArguments(TEXT("D:/Fixture Path/create.ps1"), TEXT("D:/UE"), TEXT("D:/Project/MMD.uproject"), TEXT("D:/Output"));
			TestTrue(TEXT("后台参数选择当前模型、动作、固定机位和倍速"), Arguments.Contains(FString(TEXT("-ModelAsset \"")) + ModelAsset + TEXT("\""))
				&& Arguments.Contains(TEXT("-Motion \"")) && !Arguments.Contains(TEXT("-AnimationAsset"))
				&& Arguments.Contains(TEXT("-CameraMode Fixed")) && Arguments.Contains(TEXT("-PlaybackRate 1.500000")));
			if (FParse::Param(FCommandLine::Get(), TEXT("MMDPreviewMovieCapture")))
			{
				TArray<FColor> Pixels;
				FIntVector ImageSize;
				if (TestTrue(TEXT("捕获包含新制作入口的真实面板"), FSlateApplication::Get().TakeScreenshot(State->Panel.ToSharedRef(), Pixels, ImageSize)))
				{
					TArray64<uint8> PNG;
					FImageUtils::PNGCompressImageArray(ImageSize.X, ImageSize.Y, Pixels, PNG);
					const FString Directory = FPaths::ProjectSavedDir() / TEXT("UIExperience") / (FDateTime::Now().ToString(TEXT("%Y-%m-%d")) + TEXT("-preview-movie"));
					IFileManager::Get().MakeDirectory(*Directory, true);
					const FString ImagePath = Directory / TEXT("preview_movie_cta.png");
					TestTrue(TEXT("保存制作入口实拍"), FFileHelper::SaveArrayToFile(PNG, *ImagePath));
					AddInfo(TEXT("Preview movie CTA screenshot: ") + FPaths::ConvertRelativePathToFull(ImagePath));
				}
			}
			State->View->SetPreviewPhysicsEnabled(false);
			TestTrue(TEXT("关闭物理后快照即时更新"), State->Panel->BuildPreviewMovieRequest(Snapshot, Error) && !Snapshot.bPhysics);
			State->View->SetPreviewPhysicsEnabled(true);
		}
		else AddError(Error);
		// 用实际的 transient 内置动画模拟未保存的外部动画，不改任何源资产。
		auto* Component = State->View->GetPreviewActor()->FindComponentByClass<USkeletalMeshComponent>();
		UAnimSequence* TransientMotion = Component && Component->GetSingleNodeInstance() ? Cast<UAnimSequence>(Component->GetSingleNodeInstance()->GetCurrentAsset()) : nullptr;
		State->View->SetPreviewAnimation(TransientMotion);
		TestTrue(TEXT("未保存的外部动画不能提交后台"), !State->Panel->BuildPreviewMovieRequest(Snapshot, Error) && Error.Contains(TEXT("保存当前动画")));
		FText Message;
		State->View->PlayBuiltinMotion(TEXT("Breath"), Message);
		State->View->SetPreviewPoseHeld(true);
	}, 0.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("制作按钮 / 定格校验"), TEXT("production_validation"),
		[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDProducePreviewMovie")); }, nullptr,
		[State]() { return !State->Panel->PreviewMovieJob && State->Panel->CurrentStatusType == EMMDMessageType::Warning && State->Panel->CurrentStatusMessage.Contains(TEXT("解除定格")); }));
	if (FParse::Param(FCommandLine::Get(), TEXT("MMDPreviewMovieRun")))
	{
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State]()
		{
			if (HasAnyErrors()) return;
			State->View->SetPreviewPoseHeld(false);
			FMMDPreviewMovieRequest Request;
			FString Error;
			TestTrue(TEXT("显式开启端到端测试后恢复有效快照"), State->Panel->BuildPreviewMovieRequest(Request, Error));
			AddInfo(TEXT("MMDPreviewMovieRun：将通过真实 Slate 按钮启动后台 MP4 制作。"));
		}, 0.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FClickAuditAction(State, TEXT("按当前预览制作 / 真实后台输出"), TEXT("production_run"),
			[State]() { return FindAuditTag(State->Panel.ToSharedRef(), TEXT("MMDProducePreviewMovie")); }, nullptr,
			[State]() { return State->Panel->PreviewMovieJob.IsValid(); }));
		ADD_LATENT_AUTOMATION_COMMAND(FPreviewMovieCompletion(this, [this, State]()
		{
			if (State->Panel->PreviewMovieJob.IsValid()) return false;
			if (HasAnyErrors()) return true;
			const FString Prefix(TEXT("后台制作完成："));
			const FString Status = State->Panel->CurrentStatusMessage;
			if (TestTrue(TEXT("真实后台制作成功并回到完成状态：") + Status,
				State->Panel->CurrentStatusType == EMMDMessageType::Success && Status.StartsWith(Prefix)))
			{
				const FString Movie = Status.RightChop(Prefix.Len());
				TestTrue(TEXT("成功状态指向本次制作目录下实际非空 MP4"),
					FPaths::IsUnderDirectory(Movie, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MMDProduction")))
					&& FPaths::GetCleanFilename(Movie) == TEXT("mmd_movie.mp4") && IFileManager::Get().FileSize(*Movie) > 0);
				AddInfo(TEXT("Preview movie E2E output: ") + Movie);
			}
			return true;
		}, [State]() { State->Panel->CancelPreviewMovie(); }));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, State, bMeshDirty, bSkeletonDirty]()
	{
		TestEqual(TEXT("定格负向校验的实际 CTA 点击只执行一次"), State->ProductionValidationClicks, 1);
		TestFalse(TEXT("测试结束没有未完成的后台制作 job"), State->Panel->PreviewMovieJob.IsValid());
		TestEqual(TEXT("测试没有改变模型包状态"), State->Mesh->GetOutermost()->IsDirty(), bMeshDirty);
		TestEqual(TEXT("测试没有改变骨架包状态"), State->Mesh->GetSkeleton()->GetOutermost()->IsDirty(), bSkeletonDirty);
		State->Window->RequestDestroyWindow();
	}, 0.0f));
	return true;
}
#endif
