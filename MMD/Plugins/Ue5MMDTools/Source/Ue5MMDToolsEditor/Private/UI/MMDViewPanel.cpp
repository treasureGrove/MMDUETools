#include "MMDViewPanel.h"
#include "UI/MMDToolPanelStyle.h"
#include "UI/MMDPreviewFraming.h"
#include "Editor/MMDStudioDisplayScene.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Ue5MMDTools.h"
#include "EditorViewportClient.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "SEditorViewport.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "EditorModeTools.h" // include privately
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "TPMXParser.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "HitProxies.h"
#include "Templates/SharedPointer.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "UObject/UnrealType.h"
#include "Import/MMDModelDataAsset.h"
#include "TMMDMeshBuilder.h"
#include "TVMDParser.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SComboButton.h"

class FMMDViewportClient : public FEditorViewportClient
{
public:
	TWeakPtr<MMDViewPanel> OwnerPanel;
	void StopCameraTransition()
	{
		CameraElapsed = 1.0f;
		GetViewTransform().TransitionToLocation(GetViewLocation(), TWeakPtr<SWidget>(), true);
	}
	void FrameCamera(const FVector& Location, const FVector& Target, bool bInstant)
	{
		StopCameraTransition();
		CameraStart = GetViewLocation();
		CameraStartRotation = GetViewRotation().Quaternion();
		CameraEnd = Location;
		CameraEndRotation = (Target - Location).Rotation().Quaternion();
		SetLookAtLocation(Target);
		CameraElapsed = bInstant ? 1.0f : 0.0f;
		if (bInstant) { SetViewLocation(Location); SetViewRotation(CameraEndRotation.Rotator()); }
		Invalidate();
	}
    FMMDViewportClient(FEditorModeTools* InModeTools, FPreviewScene *InPreviewScene, const TWeakPtr<SEditorViewport> &InEditorViewportWidget)
        : FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget), PreviewScene(InPreviewScene), SelectedActor(nullptr)
    {
        SetViewMode(VMI_Lit);
        SetRealtime(true);
        // 关键：启用编辑器/后处理/选择描边相关的 ShowFlag（保持兼容）
        EngineShowFlags.SetEditor(true);
        EngineShowFlags.SetPostProcessing(true);
		// 曝光交给所选环境的后处理；默认影棚固定，舞台保留自己的配方。
		ExposureSettings.bFixed = false;
        // 设置默认视角
        SetViewLocation(FVector(300, 300, 300));
        SetViewRotation(FRotator(-25, 45, 0));

        // 启用标准编辑器视角控制
        bUsesDrawHelper = true;
        bAllowCinematicControl = false;
        bUsingOrbitCamera = true;

        // 设置轨道摄像机的焦点
        SetLookAtLocation(FVector::ZeroVector);

        // 启用鼠标控制
        bSetListenerPosition = false;
        bLockFlightCamera = false;

        // 启用选择高亮渲染（让UE5自动处理选择描边）
        EngineShowFlags.SetSelection(true);
        EngineShowFlags.SetSelectionOutline(true);
        EngineShowFlags.SetModeWidgets(true);
		EngineShowFlags.SetGrid(false);
        // 连接到编辑器选择系统
        USelection::SelectionChangedEvent.AddRaw(this, &FMMDViewportClient::OnActorSelectionChanged);
        
    }
    ~FMMDViewportClient()
    {
        USelection::SelectionChangedEvent.RemoveAll(this);
    }

	virtual void Tick(float DeltaSeconds) override
	{
		FEditorViewportClient::Tick(DeltaSeconds);
		const FIntPoint ViewportSize = Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		if (ViewportSize.X > 0 && ViewportSize.Y > 0 && ViewportSize != LastViewportSize)
		{
			LastViewportSize = ViewportSize;
			if (const TSharedPtr<MMDViewPanel> Panel = OwnerPanel.Pin())
			{
				const TOptional<EMMDPreviewView> SelectedView = Panel->GetPreviewView();
				// 只在有效视口尺寸改变时重新取景，手动机位与逐帧姿态不触发重置。
				if (SelectedView.IsSet() && (Panel->HasPreviewModel() || Panel->GetStagePreviewPreset()))
				{
					Panel->SetPreviewView(SelectedView.GetValue(), true);
				}
			}
		}
		if (CameraElapsed < 0.3f)
		{
			CameraElapsed += FMath::Max(DeltaSeconds, 0.0f);
			const float Alpha = FMath::SmoothStep(0.0f, 0.3f, CameraElapsed);
			SetViewLocation(FMath::Lerp(CameraStart, CameraEnd, Alpha));
			SetViewRotation(FQuat::Slerp(CameraStartRotation, CameraEndRotation, Alpha).Rotator());
			Invalidate();
		}
		if (UWorld* World = PreviewScene ? PreviewScene->GetWorld() : nullptr)
		{
			if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f)
				World->Tick(LEVELTICK_All, FMath::Min(DeltaSeconds, 1.0f / 15.0f));
			// 基础预览场景需要显式更新天空和反射捕获，否则切换布光后环境仍可能全黑。
			PreviewScene->UpdateCaptureContents();
		}
	}
	float CameraElapsed = 1.0f;
	FVector CameraStart, CameraEnd;
	FQuat CameraStartRotation, CameraEndRotation;

    // 从资源生成Actor
    bool SpawnActorFromAsset(UStaticMesh *StaticMesh, float MouseX, float MouseY)
    {
        if (!StaticMesh || !GetWorld())
            return false;

        // 简化的位置计算 - 在原点附近生成
        FVector SpawnLocation = FVector(0, 0, 0);

        // 生成StaticMeshActor
        AStaticMeshActor *NewActor = GetWorld()->SpawnActor<AStaticMeshActor>(SpawnLocation, FRotator::ZeroRotator);
        if (NewActor && NewActor->GetStaticMeshComponent())
        {
            NewActor->GetStaticMeshComponent()->SetStaticMesh(StaticMesh);
            NewActor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);

            // 设置碰撞
            NewActor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            NewActor->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
            NewActor->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);

            // 选中新生成的Actor
            SetSelectedActor(NewActor);

            return true;
        }

        return false;
    }

    // 选择Actor - 使用UE5原生选择系统
    void SetSelectedActor(AActor *Actor)
    {
        if (!GetWorld())
            return;

        // 使用UE5的原生选择系统
        USelection *SelectedActors = GEditor->GetSelectedActors();

        // 清除当前选择
        GEditor->SelectNone(false, true);

        // 选择新的Actor
        if (Actor && IsValid(Actor))
        {
            GEditor->SelectActor(Actor, true, true);
            SelectedActor = Actor;
            WidgetLocation = Actor->GetActorLocation();
            ShowWidget(true);
        }
        else
        {
            SelectedActor = nullptr;
            WidgetLocation = FVector::ZeroVector;
            ShowWidget(false);
        }

        Invalidate();
    }

    // 高亮选中的Actor - 让UE5原生系统处理
    void HighlightSelectedActor(AActor *Actor)
    {
        // UE5原生选择系统会自动处理高亮显示
        // 不需要手动设置CustomDepth等
    }

    // 清除Actor选择高亮 - 让UE5原生系统处理
    void ClearActorSelection(AActor *Actor)
    {
        // UE5原生选择系统会自动处理清除高亮
        // 不需要手动清除CustomDepth等
    }

    // 处理选择变化 - 同步UE5的选择状态
    void OnActorSelectionChanged(UObject *NewSelection)
    {
        // 检查当前UE5选择状态并同步本地状态
        USelection *SelectedActors = GEditor->GetSelectedActors();
        if (SelectedActors && SelectedActors->Num() > 0)
        {
            // 获取第一个选中的Actor
            AActor *FirstSelected = nullptr;
            for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
            {
                if (AActor *Actor = Cast<AActor>(*Iter))
                {
                    FirstSelected = Actor;
                    break;
                }
            }

            if (FirstSelected != SelectedActor)
            {
                SelectedActor = FirstSelected;
                WidgetLocation = SelectedActor ? SelectedActor->GetActorLocation() : FVector::ZeroVector;
                ShowWidget(SelectedActor != nullptr);
            }
        }
        else
        {
            SelectedActor = nullptr;
            WidgetLocation = FVector::ZeroVector;
            ShowWidget(false);
        }

        Invalidate();
    }

    // 重写Widget位置
    virtual FVector GetWidgetLocation() const override
    {
        return WidgetLocation;
    }

    // 重写Widget坐标系
    virtual FMatrix GetWidgetCoordSystem() const override
    {
        if (SelectedActor)
        {
            return FRotationMatrix(SelectedActor->GetActorRotation());
        }
        return FMatrix::Identity;
    }

    virtual void SetWidgetMode(UE::Widget::EWidgetMode NewMode) override
    {
        if (SelectedActor)
        {
            FEditorViewportClient::SetWidgetMode(NewMode);
        }
    }

    // 处理Widget变换 - 确保右键不被Widget拦截
    virtual bool InputWidgetDelta(FViewport *InViewport, EAxisList::Type CurrentAxis, FVector &Drag, FRotator &Rot, FVector &Scale) override
    {
        // 如果右键被按下，不处理Widget变换，让父类处理视角控制
        if (InViewport && InViewport->KeyState(EKeys::RightMouseButton))
        {
            return false; // 不处理，让父类的视角控制接管
        }

        if (SelectedActor)
        {
            switch (GetWidgetMode())
            {
            case UE::Widget::WM_Translate:
                SelectedActor->AddActorWorldOffset(Drag);
                WidgetLocation += Drag;
                break;
            case UE::Widget::WM_Rotate:
                SelectedActor->AddActorWorldRotation(Rot);
                WidgetLocation = SelectedActor->GetActorLocation();
                break;
            case UE::Widget::WM_Scale:
            {
                FVector CurrentScale = SelectedActor->GetActorScale3D();
                SelectedActor->SetActorScale3D(CurrentScale + Scale);
            }
            break;
            }

            Invalidate();
            return true;
        }
        return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
    }

    // 处理鼠标点击选择：使用屏幕射线选择
    virtual void ProcessClick(FSceneView &View, HHitProxy *HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override
    {
        // 先让父类处理（更新内部状态/Widget等）
        FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

        if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
        {
            // 屏幕射线
            if (GetWorld())
            {
                FVector WorldOrigin, WorldDirection;
                View.DeprojectFVector2D(FVector2D((float)HitX, (float)HitY), WorldOrigin, WorldDirection);

                const FVector TraceStart = WorldOrigin;
                const FVector TraceEnd = WorldOrigin + WorldDirection * 10000.0f;
                FHitResult HitResult;
                FCollisionQueryParams Params(SCENE_QUERY_STAT(ProcessClick), true);
                Params.bTraceComplex = true;

                if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, Params))
                {
                    if (AActor* HitActor = HitResult.GetActor())
                    {
                        SetSelectedActor(HitActor);
                        return;
                    }
                }
            }

            // 清空选择
            SetSelectedActor(nullptr);
        }
    }

    // 重写鼠标移动处理 - 确保右键拖拽用于视角控制
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
    virtual bool InputAxis(const FInputKeyEventArgs& Args) override
#else
    virtual bool InputAxis(FViewport *InViewport, FInputDeviceId DeviceId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad) override
#endif
    {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
        FViewport* InViewport = Args.Viewport;
        const FKey Key = Args.Key;
#else
        // 右键相关的鼠标移动总是用于视角控制
#endif
        if (Key == EKeys::MouseX || Key == EKeys::MouseY)
        {
            // 检查右键是否被按下
            if (InViewport->KeyState(EKeys::RightMouseButton))
            {
                // 强制使用父类的视角控制，不传递给Widget系统
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
                return FEditorViewportClient::InputAxis(Args);
#else
                return FEditorViewportClient::InputAxis(InViewport, DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
#endif
            }
        }

        // 其他轴输入正常处理
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
        return FEditorViewportClient::InputAxis(Args);
#else
        return FEditorViewportClient::InputAxis(InViewport, DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
#endif
    }

    // 重写输入处理 - 确保右键总是用于视角控制，滚轮控制速度
    virtual bool InputKey(const FInputKeyEventArgs &EventArgs) override
    {
        const bool bCameraNavigation = EventArgs.Key == EKeys::RightMouseButton || EventArgs.Key == EKeys::MiddleMouseButton
            || EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown
            || (EventArgs.Key == EKeys::LeftMouseButton && IsAltPressed());
        // 播放快捷键不打断切镜头；仅手动相机操作接管当前过渡。
        if (bCameraNavigation && (EventArgs.Event == IE_Pressed || EventArgs.Event == IE_Repeat))
        {
            StopCameraTransition();
            if (auto Panel = OwnerPanel.Pin()) Panel->ClearPreviewViewSelection();
        }
		if (EventArgs.Event == IE_Pressed)
		{
			if (auto Panel = OwnerPanel.Pin())
			{
				const bool bMouseLooking = EventArgs.Viewport && EventArgs.Viewport->KeyState(EKeys::RightMouseButton);
				if (!bMouseLooking && !IsAltPressed() && !IsCtrlPressed() && !IsShiftPressed() && !IsCmdPressed()
					&& Panel->HandlePreviewShortcut(EventArgs.Key)) return true;
			}
		}
        // 右键相关操作总是用于视角控制，不传递给Widget系统
        if (EventArgs.Key == EKeys::RightMouseButton)
        {
            return FEditorViewportClient::InputKey(EventArgs);
        }

        // Shift+滚轮控制摄像机速度
        if (EventArgs.Event == IE_Pressed && (EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown))
        {
            // 检查Shift键是否被按下 - 使用Viewport检查
            if (EventArgs.Viewport && (EventArgs.Viewport->KeyState(EKeys::LeftShift) || EventArgs.Viewport->KeyState(EKeys::RightShift)))
            {
                if (EventArgs.Key == EKeys::MouseScrollUp)
                {
                    IncreaseCameraSpeed();
                }
                else
                {
                    DecreaseCameraSpeed();
                }
                return true;
            }
        }

        // 处理键盘快捷键
        bool bHandled = false;
        if (EventArgs.Event == IE_Pressed && SelectedActor
			&& !(EventArgs.Viewport && EventArgs.Viewport->KeyState(EKeys::RightMouseButton)))
        {
            if (EventArgs.Key == EKeys::W)
            {
                SetWidgetMode(UE::Widget::WM_Translate);
                bHandled = true;
            }
            else if (EventArgs.Key == EKeys::E)
            {
                SetWidgetMode(UE::Widget::WM_Rotate);
                bHandled = true;
            }
            else if (EventArgs.Key == EKeys::R)
            {
                SetWidgetMode(UE::Widget::WM_Scale);
                bHandled = true;
            }
            else if (EventArgs.Key == EKeys::Delete)
            {
                // 使用UE5原生的删除系统
                USelection *SelectedActors = GEditor->GetSelectedActors();
                if (SelectedActors && SelectedActors->Num() > 0)
                {
                    // 收集要删除的Actor
                    TArray<AActor *> ActorsToDelete;
                    for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
                    {
                        if (AActor *Actor = Cast<AActor>(*Iter))
                        {
                            ActorsToDelete.Add(Actor);
                        }
                    }

                    // 删除Actor
                    for (AActor *Actor : ActorsToDelete)
                    {
                        if (GetWorld())
                        {
                            GetWorld()->DestroyActor(Actor);
                        }
                    }

                    // 清除选择
                    GEditor->SelectNone(false, true);
                    SetSelectedActor(nullptr);
                    bHandled = true;
                }
            }
        }

        if (bHandled)
        {
            Invalidate();
            return true;
        }

        // 其他输入交给父类处理
        return FEditorViewportClient::InputKey(EventArgs);
    }

    AActor *GetSelectedActor() const { return SelectedActor; }

    // 摄像机速度控制
    float GetCameraSpeed() const { return CameraSpeedSetting; }
    void SetCameraSpeed(float NewSpeed)
    {
        CameraSpeedSetting = FMath::Clamp(NewSpeed, 0.1f, 16.0f);
        // 应用到编辑器视口客户端的速度设置
        SetCameraSpeedSetting(FMath::RoundToInt(CameraSpeedSetting));
    }
    void IncreaseCameraSpeed() { SetCameraSpeed(CameraSpeedSetting * 1.25f); }
    void DecreaseCameraSpeed() { SetCameraSpeed(CameraSpeedSetting * 0.8f); }

    // 声明该视口为关卡编辑器类型，启用相关编辑器特性
    virtual bool IsLevelEditorClient() const override { return true; }

private:
    FIntPoint LastViewportSize = FIntPoint::ZeroValue;
    FPreviewScene *PreviewScene;
    AActor *SelectedActor;
    FVector WidgetLocation;
    float CameraSpeedSetting = 4.0f; // 默认摄像机速度
};

// MMDViewPanel 实现
void MMDViewPanel::Construct(const FArguments &InArgs)
{
	CurrentLightingEnvironment = EMMDLightingEnvironment::Studio3Point;

	// 使用基础 PreviewScene，避免 AdvancedPreviewScene 隐式加载用户 Asset Viewer 的 HDRI、
	// 后处理和默认方向光。LookDev 的可见灯光全部由预设显式创建。
	FPreviewScene::ConstructionValues PreviewCVS;
	PreviewCVS.SetEditor(true)
		.SetCreateDefaultLighting(true)
		.SetLightBrightness(0.0f)
		.SetSkyBrightness(0.0f)
		.AllowLumenPrimitiveTrackingInPreviewWorld(true);
	PreviewScene = MakeShared<FPreviewScene>(PreviewCVS);

	if (UDirectionalLightComponent* DefaultLight = PreviewScene->DirectionalLight)
	{
		DefaultLight->SetVisibility(false);
	}
	if (USkyLightComponent* Sky = PreviewScene->SkyLight)
	{
		Sky->SetVisibility(false);
    }

	// 默认进入晴空露台；校准影棚仍可在场景页单独选择。
    BuildPreviewStage();

    // create local ModeTools instance as shared, required because FEditorViewportClient's ctor calls AsShared()
    LocalModeTools = MakeShared<FEditorModeTools>();

	// 首先调用父类的Construct来初始化视口
	SEditorViewport::Construct(SEditorViewport::FArguments());
	ApplyLightingEnvironment(CurrentLightingEnvironment);
}

MMDViewPanel::~MMDViewPanel()
{
	if (GEditor && IsValid(PreviewActor) && GEditor->GetSelectedActors()->IsSelected(PreviewActor))
		GEditor->SelectActor(PreviewActor, false, true);
	ClearLightingEnvironment();
	StudioDisplayScene.Reset();
	EndPhysicsBakePreview();
    LocalModeTools.Reset();
}

TSharedRef<FEditorViewportClient> MMDViewPanel::MakeEditorViewportClient()
{
	// SEditorViewport 已通过 SNew/SAssignNew 建立 shared 引用，此处 SharedThis(this) 安全（引擎标准做法）。
	CustomViewportClient = MakeShared<FMMDViewportClient>(LocalModeTools.Get(), PreviewScene.Get(), SharedThis(this));
	static_cast<FMMDViewportClient*>(CustomViewportClient.Get())->OwnerPanel = SharedThis(this);
	CustomViewportClient->ExposureSettings.bFixed = false;
	CustomViewportClient->EngineShowFlags.SetEyeAdaptation(true);
	CustomViewportClient->EngineShowFlags.SetSelectionOutline(false);
	CustomViewportClient->EngineShowFlags.SetBillboardSprites(false);
	CustomViewportClient->EngineShowFlags.SetLightRadius(false);
	CustomViewportClient->EngineShowFlags.SetVolumes(false);
	CustomViewportClient->ViewFOV = 35.0f;
	CustomViewportClient->FOVAngle = 35.0f;
	CustomViewportClient->SetWidgetMode(UE::Widget::WM_None);
	CustomViewportClient->EngineShowFlags.SetBloom(false);
	CustomViewportClient->EngineShowFlags.SetMotionBlur(false);
	CustomViewportClient->EngineShowFlags.SetDepthOfField(false);
	return CustomViewportClient.ToSharedRef();
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
TSharedPtr<SWidget> MMDViewPanel::BuildViewportToolbar()
#else
TSharedPtr<SWidget> MMDViewPanel::MakeViewportToolbar()
#endif
{
	TSharedRef<SWrapBox> Toolbar = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(3, 4));
	const TCHAR* Labels[] = { TEXT("全身"), TEXT("半身"), TEXT("面部"), TEXT("侧面"), TEXT("背面") };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const auto View = static_cast<EMMDPreviewView>(Index);
		Toolbar->AddSlot()
		[SNew(SButton).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).Text(FText::FromString(Labels[Index]))
			.ButtonColorAndOpacity_Lambda([this, View]() { return CurrentPreviewView.IsSet() && CurrentPreviewView.GetValue() == View ? MMDPanelStyle::AccentSoft : FLinearColor::White; })
			.ToolTipText(FText::FromString(FString::Printf(TEXT("%s机位 · 快捷键 %d"), Labels[Index], Index + 1)))
			.IsEnabled_Lambda([this]() { return GetPreviewMeshComponent() != nullptr; })
			.OnClicked_Lambda([this, View]() { SetPreviewView(View); return FReply::Handled(); })];
	}
	Toolbar->AddSlot().Padding(FMargin(5, 0, 0, 0))
	[SNew(SButton).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text).Text(FText::FromString(TEXT("场景机位")))
		.ButtonColorAndOpacity_Lambda([this]() { return CurrentPreviewView == EMMDPreviewView::Scene ? MMDPanelStyle::AccentSoft : FLinearColor::White; })
		.ToolTipText(FText::FromString(TEXT("恢复当前场景的默认构图")))
		.OnClicked_Lambda([this]() { ResetPreviewCamera(); return FReply::Handled(); })];
	Toolbar->AddSlot().Padding(FMargin(5, 0, 0, 0))
	[SNew(SComboButton).ButtonStyle(MMDPanelStyle::Secondary()).ForegroundColor(MMDPanelStyle::Text)
		.ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("编辑"))).ColorAndOpacity(MMDPanelStyle::Text)]
		.OnGetMenuContent_Lambda([this]()
		{
			FMenuBuilder Menu(true, nullptr);
			const TCHAR* Labels[] = { TEXT("隐藏手柄"), TEXT("移动 W"), TEXT("旋转 E"), TEXT("缩放 R") };
			const UE::Widget::EWidgetMode Modes[] = { UE::Widget::WM_None, UE::Widget::WM_Translate, UE::Widget::WM_Rotate, UE::Widget::WM_Scale };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const auto Mode = Modes[Index];
				Menu.AddMenuEntry(FText::FromString(Labels[Index]), FText(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Mode]()
				{
					if (CustomViewportClient.IsValid()) CustomViewportClient->SetWidgetMode(Mode);
				})));
			}
			Menu.AddMenuSeparator();
			Menu.AddMenuEntry(FText::FromString(TEXT("恢复原始姿态")), FText::FromString(TEXT("清除预览动作，停止动作和物理。可重新点预设恢复。")),
				FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { ClearPreviewAnimation(); })));
			return Menu.MakeWidget();
		})];
	return Toolbar;
}


bool MMDViewPanel::CreatePreviewActor(UClass* InActorClass)
{
    // === 基础校验 ===
    if (!InActorClass || !InActorClass->IsChildOf<AActor>())
    {
        UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: Invalid ActorClass"));
        return false;
    }
    if (!PreviewScene.IsValid() || !PreviewScene->GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: PreviewScene is invalid"));
        return false;
    }

    UClass* ActorClass = InActorClass;
    const FString RawName = ActorClass->GetName();

    // === 处理 Blueprint 过渡类 (REINST_ / SKEL_)，需包含 Engine/Blueprint.h ===
    if (RawName.StartsWith(TEXT("REINST_")) || RawName.StartsWith(TEXT("SKEL_")))
    {
#if WITH_EDITOR
        UBlueprint* BP = Cast<UBlueprint>(ActorClass->ClassGeneratedBy);
        if (BP)
        {
            if (BP->GeneratedClass && BP->GeneratedClass != ActorClass)
            {
                UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: Transitional %s -> Use GeneratedClass %s"),
                    *RawName, *BP->GeneratedClass->GetName());
                ActorClass = BP->GeneratedClass;
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: Transitional %s but no final GeneratedClass yet (still compiling?)"), *RawName);
                return false; // 暂停，等待下一次再尝试
            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: Transitional class %s has no Blueprint origin"), *RawName);
            return false;
        }
#else
        UE_LOG(LogTemp, Warning, TEXT("CreatePreviewActor: Transitional class %s in non-editor build"), *RawName);
        return false;
#endif
    }

    // === 抽象 / 不可放置过滤 ===
    if (ActorClass->HasAnyClassFlags(CLASS_Abstract))
    {
        UE_LOG(LogTemp, Error, TEXT("CreatePreviewActor: Class %s is abstract"), *ActorClass->GetName());
        return false;
    }
    if (ActorClass->HasAnyClassFlags(CLASS_NotPlaceable))
    {
        UE_LOG(LogTemp, Error, TEXT("CreatePreviewActor: Class %s is not placeable (CLASS_NotPlaceable)"), *ActorClass->GetName());
        return false;
    }

    UWorld* World = PreviewScene->GetWorld();

    // === 如当前预览被选中，先清除再销毁 ===
    if (FMMDViewportClient* VC = static_cast<FMMDViewportClient*>(CustomViewportClient.Get()))
    {
        if (IsValid(PreviewActor) && VC->GetSelectedActor() == PreviewActor)
        {
            VC->SetSelectedActor(nullptr);
        }
    }
    if (IsValid(PreviewActor))
    {
        World->DestroyActor(PreviewActor);
        PreviewActor = nullptr;
    }

    // === Spawn 参数（不强制命名，减少重名/重复） ===
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.ObjectFlags |= RF_Transient; // 预览对象不参与保存
    // 如果你必须自定义名字再取消注释：
    // SpawnParams.Name = MakeUniqueObjectName(World, ActorClass, FName(TEXT("MMDPreviewActor")));

    UE_LOG(LogTemp, Verbose, TEXT("CreatePreviewActor: Spawning Class=%s Flags=0x%X"),
        *ActorClass->GetName(), (int32)ActorClass->GetClassFlags());

    AActor* NewActor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
    if (!IsValid(NewActor))
    {
        UE_LOG(LogTemp, Error, TEXT("CreatePreviewActor: SpawnActor failed for %s"), *ActorClass->GetName());
        return false;
    }

    PreviewActor = NewActor;
	ResetPreviewMotionState();
    {
        TArray<UPrimitiveComponent*> PrimComps;
        PreviewActor->GetComponents<UPrimitiveComponent>(PrimComps);
        for (UPrimitiveComponent* Comp : PrimComps)
        {
            if (!Comp) continue;
            Comp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Comp->SetCollisionResponseToAllChannels(ECR_Ignore);
            Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
            Comp->bTraceComplexOnMove = true; // 旧版标志可忽略，新版以 bTraceComplex 为准
            Comp->MarkRenderStateDirty();
        }
    }

    if (FMMDViewportClient* VC = static_cast<FMMDViewportClient*>(CustomViewportClient.Get()))
    {
        VC->SetSelectedActor(PreviewActor);
        const FBox Bounds = PreviewActor->GetComponentsBoundingBox(true);
        if (Bounds.IsValid)
        {
            VC->FocusViewportOnBox(Bounds);
        }
        else
        {
            // 你的版本可能没有 FocusViewportOnLocation，若无就忽略该情况
            UE_LOG(LogTemp, Verbose, TEXT("CreatePreviewActor: Bounds invalid, skip focus"));
        }
    }

	UE_LOG(LogTemp, Verbose, TEXT("CreatePreviewActor: Success %s"), *PreviewActor->GetName());
	RefreshPreviewEnvironment();
	FText MotionMessage;
	PlayBuiltinMotion(TEXT("Idle"), MotionMessage);
	return true;
}

void MMDViewPanel::BeginPhysicsBakePreview(USkeletalMesh* SkeletalMesh)
{
    if (!PreviewScene.IsValid() || !PreviewScene->GetWorld() || !SkeletalMesh)
    {
        return;
    }

    EndPhysicsBakePreview();

    PhysicsBakePreviewComponent = NewObject<UPoseableMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
    if (!PhysicsBakePreviewComponent)
    {
        return;
    }

    PhysicsBakePreviewComponent->SetSkeletalMesh(SkeletalMesh);
    PhysicsBakePreviewComponent->SetMobility(EComponentMobility::Movable);
    PhysicsBakePreviewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PhysicsBakePreviewComponent->SetVisibility(true);
    PreviewScene->AddComponent(PhysicsBakePreviewComponent, FTransform::Identity);

    if (IsValid(PreviewActor))
    {
        PreviewActor->SetActorHiddenInGame(true);
        PreviewActor->SetIsTemporarilyHiddenInEditor(true);
    }

    if (FMMDViewportClient* VC = static_cast<FMMDViewportClient*>(CustomViewportClient.Get()))
    {
        const FBoxSphereBounds Bounds = SkeletalMesh->GetBounds();
        VC->FocusViewportOnBox(FBox::BuildAABB(Bounds.Origin, Bounds.BoxExtent));
        VC->Invalidate();
    }
}

void MMDViewPanel::PreviewPhysicsBakeFrame(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& ComponentTransforms)
{
    if (!PhysicsBakePreviewComponent)
    {
        return;
    }

    const int32 NumBones = FMath::Min(RefSkeleton.GetNum(), ComponentTransforms.Num());
    for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
    {
        PhysicsBakePreviewComponent->SetBoneTransformByName(
            RefSkeleton.GetBoneName(BoneIndex),
            ComponentTransforms[BoneIndex],
            EBoneSpaces::ComponentSpace);
    }

    PhysicsBakePreviewComponent->RefreshBoneTransforms();
    PhysicsBakePreviewComponent->MarkRenderDynamicDataDirty();

    if (CustomViewportClient.IsValid())
    {
        CustomViewportClient->Invalidate();
    }
}

void MMDViewPanel::EndPhysicsBakePreview()
{
    if (PhysicsBakePreviewComponent)
    {
        if (PreviewScene.IsValid())
        {
            PreviewScene->RemoveComponent(PhysicsBakePreviewComponent);
        }
        PhysicsBakePreviewComponent->DestroyComponent();
        PhysicsBakePreviewComponent = nullptr;
    }

    if (IsValid(PreviewActor))
    {
        PreviewActor->SetActorHiddenInGame(false);
        PreviewActor->SetIsTemporarilyHiddenInEditor(false);
    }

    if (CustomViewportClient.IsValid())
    {
        CustomViewportClient->Invalidate();
    }
}

void MMDViewPanel::ImportModelClicked()
{
    // 使用文件对话框选择模型文件
    IDesktopPlatform *DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform)
    {
        TArray<FString> OpenedFiles;
        const FString FileTypes = TEXT("Static Mesh Files (*.fbx;*.obj;*.3ds)|*.fbx;*.obj;*.3ds|FBX Files (*.fbx)|*.fbx|OBJ Files (*.obj)|*.obj|All Files (*.*)|*.*");

        bool bOpened = DesktopPlatform->OpenFileDialog(
            FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
            TEXT("Import Model"),
            TEXT(""), // 默认路径
            TEXT(""), // 默认文件名
            FileTypes,
            EFileDialogFlags::None,
            OpenedFiles);

        if (bOpened && OpenedFiles.Num() > 0)
        {
            FString SelectedFile = OpenedFiles[0];

            if (GEngine)
            {
                GEngine->AddOnScreenDebugMessage(
                    -1,
                    5.0f,
                    FColor::Green,
                    FString::Printf(TEXT("Selected file: %s"), *SelectedFile));

                GEngine->AddOnScreenDebugMessage(
                    -1,
                    5.0f,
                    FColor::Yellow,
                    TEXT("Model import functionality ready - you can implement FBX/OBJ import here"));
            }

        }
    }
}

void MMDViewPanel::LoadMMDModel(const FString &FilePath)
{
    UE_LOG(LogTemp, Warning, TEXT("LoadMMDModel called with file: %s"), *FilePath);
    // 检查是否为PMX文件
    FString FileExtension = FPaths::GetExtension(FilePath).ToLower();
}
void MMDViewPanel::ShowImportedSkeletalMesh(class USkeletalMesh* SkeletalMesh)
{
	if (!PreviewScene.IsValid() || !PreviewScene->GetWorld() || !SkeletalMesh)
	{
		return;
	}

	EndPhysicsBakePreview();

	// 清理旧预览 actor
	if (IsValid(PreviewActor))
	{
		PreviewScene->GetWorld()->DestroyActor(PreviewActor);
		PreviewActor = nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	ASkeletalMeshActor* SkelActor = PreviewScene->GetWorld()->SpawnActor<ASkeletalMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!SkelActor)
	{
		return;
	}

	PreviewActor = SkelActor;
	ResetPreviewMotionState();
	SkelActor->SetActorLabel(SkeletalMesh->GetName());

	if (USkeletalMeshComponent* SkelComp = SkelActor->GetSkeletalMeshComponent())
	{
		SkelComp->SetSkeletalMesh(SkeletalMesh);
		SkelComp->SetMobility(EComponentMobility::Movable);
		SkelComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SkelComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		SkelComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	}

	if (FMMDViewportClient* VC = static_cast<FMMDViewportClient*>(CustomViewportClient.Get()))
	{
		VC->SetSelectedActor(SkelActor);
		const FBoxSphereBounds Bounds = SkeletalMesh->GetBounds();
		VC->FocusViewportOnBox(FBox::BuildAABB(Bounds.Origin, Bounds.BoxExtent));
		VC->Invalidate();
	}
	RefreshPreviewEnvironment();
	FText MotionMessage;
	PlayBuiltinMotion(TEXT("Idle"), MotionMessage);
}

bool MMDViewPanel::SetPreviewAnimation(class UAnimSequence* AnimSequence)
{
	USkeletalMeshComponent* SkelComp = nullptr;
	if (IsValid(PreviewActor))
	{
		SkelComp = PreviewActor->FindComponentByClass<USkeletalMeshComponent>();
	}
	if (!SkelComp || !AnimSequence)
	{
		return false;
	}

	const bool bHadAnimation = HasPreviewAnimation();
	const bool bWasPlaying = !bHadAnimation || (bPreviewScrubbing ? bPreviewScrubWasPlaying : IsPreviewPlaying());
	const float RelativeTime = GetPreviewDuration() > 0 ? GetPreviewTime() / GetPreviewDuration() : 0;
	auto* Playback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(SkelComp, AnimSequence, true, bPreviewPhysicsEnabled);
	if (!Playback) return false;
	bPreviewScrubbing = false;
	Playback->SetPlayRate(bPreviewPoseHeld ? 0.0f : PreviewPlaybackSpeed);
	if (bPreviewPoseHeld && bHadAnimation) Playback->SeekMMDAnimation(RelativeTime * Playback->GetLength());
	Playback->SetPlaying(bWasPlaying);
	CurrentBuiltinMotion = NAME_None;

	if (CustomViewportClient.IsValid())
	{
		CustomViewportClient->Invalidate();
	}
	return true;
}

void MMDViewPanel::SetPreviewPlaying(bool bPlaying)
{
	USkeletalMeshComponent* Component = IsValid(PreviewActor) ? PreviewActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (UAnimSingleNodeInstance* Instance = Component ? Component->GetSingleNodeInstance() : nullptr) Instance->SetPlaying(bPlaying);
}

void MMDViewPanel::SetPreviewPhysicsEnabled(bool bEnabled)
{
	bPreviewPhysicsEnabled = bEnabled;
	USkeletalMeshComponent* Component = IsValid(PreviewActor) ? PreviewActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (UMMDPhysicsPlaybackAnimInstance* Instance = Component ? Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()) : nullptr)
		Instance->SetPhysicsEnabled(bEnabled);
}

void MMDViewPanel::ResetPreviewPhysics()
{
	USkeletalMeshComponent* Component = IsValid(PreviewActor) ? PreviewActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (UMMDPhysicsPlaybackAnimInstance* Instance = Component ? Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()) : nullptr)
		Instance->ResetPhysics();
}

int32 MMDViewPanel::ApplyLightingEnvironment(EMMDLightingEnvironment Environment)
{
	const bool bWasStagePreview = PreviewStageActor.IsValid();
	DestroyStagePreview();
	CurrentLightingEnvironment = Environment;
	SyncPreviewStageToEnvironment(Environment);
	int32 Count = 0;
	if (PreviewScene.IsValid())
	{
		// 以当前预览模型的实际包围盒为灯光基准：Center=模型中心，Scale=模型半径/标准半径(100cm)，
		// 保证灯光聚在模型身上，而不是固定在原点附近。
		FVector Center = FVector::ZeroVector;
		float Scale = 1.0f;
		FBox ModelBounds(ForceInit);
		if (PreviewActor)
		{
			ModelBounds = PreviewActor->GetComponentsBoundingBox(true);
		}
		if (ModelBounds.IsValid)
		{
			Center = ModelBounds.GetCenter();
			const float Radius = FMath::Max(ModelBounds.GetExtent().Size(), 1.0f);
			Scale = Radius / 100.0f; // 标准 MMD 模型半径约 100cm
		}
		if (StudioDisplayScene) StudioDisplayScene->UpdateBounds(ModelBounds);

		Count = UMMDLightingEnvironmentLibrary::ApplyLightingEnvironmentToPreviewScaled(
			PreviewScene.Get(), Environment, Center, Scale);

		// 预览场景的灯是孤儿组件（无 owner actor），GWorld 扫描收集不到，
		// 所以把打包好的灯光数据推给子系统覆盖 LightDataRT，材质才能读到预览灯光。
		if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
		{
			const TArray<FVector4f> Packed = UMMDLightingEnvironmentLibrary::PackEnvironmentLightDataScaled(
				Environment, Center, Scale);
			Subsystem->SetPreviewLightOverride(Packed);
		}

		if (CustomViewportClient.IsValid())
		{
			// SEditorViewport 会在客户端构造后恢复部分编辑器显示标志；环境切换末尾再次锁定 LookDev 视口。
			CustomViewportClient->EngineShowFlags.SetGrid(false);
			CustomViewportClient->Invalidate();
		}
	}
	if (bWasStagePreview) ResetPreviewCamera();
	return Count;
}

void MMDViewPanel::ClearLightingEnvironment()
{
	if (PreviewScene.IsValid())
	{
		UMMDLightingEnvironmentLibrary::ClearLightingEnvironmentFromPreview(PreviewScene.Get());

		// 同步清除预览灯光覆盖，恢复主世界灯光收集。
		if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
		{
			Subsystem->ClearPreviewLightOverride();
		}

		if (CustomViewportClient.IsValid())
		{
			CustomViewportClient->Invalidate();
		}
	}
}

namespace
{
	double GetPreviewHorizontalFov(const FEditorViewportClient& Client, double Aspect)
	{
		const auto Constraint = GetDefault<ULevelEditorViewportSettings>()->AspectRatioAxisConstraint;
		const bool bMaintainX = Constraint == AspectRatio_MaintainXFOV || (Constraint == AspectRatio_MajorAxisFOV && Aspect > 1.0);
		if (bMaintainX) return Client.ViewFOV;
		// 与编辑器保持纵向 FOV 时的投影一致，不改用户的全局视口偏好。
		const double ReferenceAspect = Client.AspectRatio > 0.0f ? Client.AspectRatio : 1.0;
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(Client.ViewFOV * 0.5)) * Aspect / ReferenceAspect));
	}
}

bool MMDViewPanel::GetProductionCamera(FVector& OutLocation, FRotator& OutRotation, double& OutHorizontalFOV, FIntPoint& OutSize) const
{
	if (!CustomViewportClient.IsValid() || !CustomViewportClient->Viewport || !CustomViewportClient->IsPerspective()) return false;
	const FIntPoint Size = CustomViewportClient->Viewport->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0) return false;
	const double Aspect = double(Size.X) / Size.Y;
	const int32 Height = FMath::Clamp(FMath::RoundToInt(FMath::Min(720.0, 3840.0 / Aspect) * 0.5) * 2, 180, 2160);
	const int32 Width = FMath::Clamp(FMath::RoundToInt(Height * Aspect * 0.5) * 2, 320, 3840);
	if (FMath::Abs(double(Width) / Height / Aspect - 1.0) > 0.01) return false;
	OutSize = FIntPoint(Width, Height);
	OutLocation = CustomViewportClient->GetViewLocation();
	OutRotation = CustomViewportClient->GetViewRotation();
	OutHorizontalFOV = GetPreviewHorizontalFov(*CustomViewportClient, Aspect);
	return !OutLocation.ContainsNaN() && !OutRotation.ContainsNaN() && FMath::IsFinite(OutHorizontalFOV)
		&& OutHorizontalFOV >= 1.0 && OutHorizontalFOV <= 170.0;
}

void MMDViewPanel::SetPreviewView(EMMDPreviewView View, bool bInstant)
{
	if (View == EMMDPreviewView::Scene) { ResetPreviewCamera(); return; }
	USkeletalMeshComponent* Component = GetPreviewMeshComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh || !CustomViewportClient.IsValid()) return;
	CurrentPreviewView = View;
	const FBoxSphereBounds Bounds = Mesh->GetBounds().TransformBy(Component->GetComponentTransform());
	const float Height = FMath::Max(Bounds.BoxExtent.Z * 2.0f, 1.0f);
	FVector Target = Bounds.Origin;
	float Radius = Bounds.SphereRadius;
	FVector Direction(0, 1, 0);
	if (View == EMMDPreviewView::UpperBody)
	{
		Target.Z += Height * 0.22f;
		Radius = Height * 0.33f;
	}
	if (View == EMMDPreviewView::Face)
	{
		Target.Z += Height * 0.38f;
		Radius = Height * 0.105f;
		FName Head;
		if (const auto* Data = Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>(); Data && Data->ModelDataAsset)
			for (const auto& Bone : Data->ModelDataAsset->Bones)
				if (Bone.NameJP == TEXT("頭") || Bone.NameEN.Equals(TEXT("head"), ESearchCase::IgnoreCase)) { Head = Bone.UEBoneName; break; }
		if (Head.IsNone())
			for (FName Candidate : { FName(TEXT("頭")), FName(TEXT("head")) })
				if (Component->GetBoneIndex(Candidate) != INDEX_NONE) { Head = Candidate; break; }
		if (!Head.IsNone() && Component->GetBoneIndex(Head) != INDEX_NONE)
			Target = Component->GetBoneLocation(Head) + FVector(0, 0, Height * 0.045f);
	}
	if (View == EMMDPreviewView::Side) Direction = FVector(1, 0.12f, 0).GetSafeNormal();
	if (View == EMMDPreviewView::Back) Direction = FVector(0, -1, 0);
	Direction = Component->GetComponentTransform().TransformVectorNoScale(Direction).GetSafeNormal();
	const FIntPoint Size = CustomViewportClient->Viewport ? CustomViewportClient->Viewport->GetSizeXY() : FIntPoint(1280, 720);
	const float Aspect = Size.X > 0 && Size.Y > 0 ? float(Size.X) / Size.Y : 16.0f / 9.0f;
	CustomViewportClient->ViewFOV = CustomViewportClient->FOVAngle = 35.0f;
	const float HalfFov = FMath::Atan(FMath::Tan(FMath::DegreesToRadians(17.5f)) / FMath::Max(Aspect, 1.0f));
	const bool bWholeBody = View == EMMDPreviewView::FullBody || View == EMMDPreviewView::Side || View == EMMDPreviewView::Back;
	const double Distance = bWholeBody
		? MMDGetBoundsFramingDistance(FBox::BuildAABB(Bounds.Origin, Bounds.BoxExtent), Direction, Aspect,
			GetPreviewHorizontalFov(*CustomViewportClient, Aspect), 1.18, CustomViewportClient->GetNearClipPlane())
		: Radius / FMath::Sin(HalfFov) * 1.08f;
	CustomViewportClient->SetWidgetMode(UE::Widget::WM_None);
	static_cast<FMMDViewportClient*>(CustomViewportClient.Get())->FrameCamera(Target + Direction * Distance, Target, bInstant);
}

void MMDViewPanel::ResetPreviewCamera()
{
	CurrentPreviewView = EMMDPreviewView::Scene;
	if (CustomViewportClient.IsValid()) static_cast<FMMDViewportClient*>(CustomViewportClient.Get())->StopCameraTransition();
	if (!CustomViewportClient.IsValid())
	{
		return;
	}
	if (const AMMDStageActor* Stage = PreviewStageActor.Get())
	{
		// 与 MMDStagePresetsCommandlet 保存的舞台主镜头一致，避开庭院立柱等前景。
		const bool bGarden = Stage->Theme == EMMDStageTheme::SakuraTheater;
		const FVector Location = Stage->GetActorTransform().TransformPosition(bGarden ? FVector(65, -440, 145) : FVector(0, -900, 285));
		const FVector Target = Stage->GetActorTransform().TransformPosition(bGarden ? FVector(0, -5, 75) : FVector(0, 150, 170));
		CustomViewportClient->ViewFOV = CustomViewportClient->FOVAngle = bGarden ? 50.0f : 55.0f;
		CustomViewportClient->SetViewLocation(Location);
		CustomViewportClient->SetViewRotation((Target - Location).Rotation());
		CustomViewportClient->Invalidate();
		return;
	}
	CustomViewportClient->ViewFOV = CustomViewportClient->FOVAngle = 35.0f;

	// 以当前预览模型包围盒为取景基准；无模型时看向舞台中央（原点）。
	FBox ModelBounds(ForceInit);
	if (PreviewActor)
	{
		ModelBounds = PreviewActor->GetComponentsBoundingBox(true);
	}

	// 标准机位（配套设计，不是 aim 追模型）：
	//   模型在原点，标准 MMD 身高约 160cm（中心 Z≈80cm）。
	//   相机在模型正前方（Y+ 方向）2.4m、躯干高度 1.1m，forward 朝 -Y 平视模型。
	//   无模型时同样用这套机位看舞台中央，保证任何时刻都有合理构图。
	FVector LookAt = FVector(0.0f, 0.0f, 110.0f);
	float Dist = 240.0f;
	if (ModelBounds.IsValid)
	{
		LookAt = ModelBounds.GetCenter();
		const FIntPoint Size = CustomViewportClient->Viewport ? CustomViewportClient->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
		const double Aspect = Size.X > 0 && Size.Y > 0 ? double(Size.X) / Size.Y : 16.0 / 9.0;
		Dist = MMDGetBoundsFramingDistance(ModelBounds, FVector(0, 1, 0), Aspect,
			GetPreviewHorizontalFov(*CustomViewportClient, Aspect), 1.18, CustomViewportClient->GetNearClipPlane());
	}

	// 相机在模型正前方（Y+ 方向，UE 中 Y=前），forward 朝 -Y 平视模型。
	const FVector CamLoc = LookAt + FVector(0.0f, Dist, 0.0f);
	const FRotator CamRot = (LookAt - CamLoc).Rotation();

	CustomViewportClient->SetViewLocation(CamLoc);
	CustomViewportClient->SetViewRotation(CamRot);
	CustomViewportClient->Invalidate();

	UE_LOG(LogTemp, Log, TEXT("[MMDViewPanel] 预览相机已归位（LookAt=%s Dist=%.0f）"), *LookAt.ToString(), Dist);
}

void MMDViewPanel::BuildPreviewStage()
{
	if (!PreviewScene.IsValid())
	{
		return;
	}

	// 与 Content LookDev map 相同的中性灰开放舞台。
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!CubeMesh)
	{
		return;
	}

	UMaterialInterface* NeutralMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Ue5MMDTools/LookDev/Materials/MI_LookDevGray18.MI_LookDevGray18"));
	UMaterialInterface* ReferenceMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Ue5MMDTools/LookDev/Materials/M_LookDevReference.M_LookDevReference"));
	if (!NeutralMaterial)
	{
		NeutralMaterial = ReferenceMaterial;
	}
	// 这些材质只由 Panel 动态引用；创建 scene proxy 前等待其 shader map，避免 WorldGridMaterial 回退。
	if (ReferenceMaterial)
	{
		ReferenceMaterial->EnsureIsComplete();
	}
	if (NeutralMaterial && NeutralMaterial != ReferenceMaterial)
	{
		NeutralMaterial->EnsureIsComplete();
	}

	auto CreateStageComponent = [this, CubeMesh, NeutralMaterial, ReferenceMaterial](const FVector& Location, const FVector& Scale)
	{
		UStaticMeshComponent* CubeComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		if (!CubeComp)
		{
			return static_cast<UStaticMeshComponent*>(nullptr);
		}
		CubeComp->SetStaticMesh(CubeMesh);
		CubeComp->SetMobility(EComponentMobility::Static);
		CubeComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CubeComp->SetCastShadow(true);
		if (ReferenceMaterial)
		{
			UMaterialInstanceDynamic* GrayMaterial = UMaterialInstanceDynamic::Create(ReferenceMaterial, CubeComp);
			GrayMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.18f, 0.18f, 0.18f));
			GrayMaterial->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
			GrayMaterial->SetScalarParameterValue(TEXT("Roughness"), 0.5f);
			CubeComp->SetMaterial(0, GrayMaterial);
		}
		else if (NeutralMaterial)
		{
			CubeComp->SetMaterial(0, NeutralMaterial);
		}
		PreviewScene->AddComponent(CubeComp, FTransform(FRotator::ZeroRotator, Location, Scale));
		return CubeComp;
	};

	PreviewFloorComponent = CreateStageComponent(
		FVector(0.0f, 0.0f, -5.0f), FVector(12.0f, 12.0f, 0.1f));
	PreviewBackdropComponent = CreateStageComponent(
		FVector(0.0f, -420.0f, 220.0f), FVector(12.0f, 0.1f, 4.5f));
	StudioDisplayScene = MakeShared<FMMDStudioDisplayScene>(*PreviewScene, FBox(ForceInit));

	// 与 Content LookDev map 使用同一个 HDRIBackdrop Blueprint；关闭其 Skylight，只保留可见穹顶。
	UClass* HDRIBackdropClass = LoadClass<AActor>(nullptr,
		TEXT("/HDRIBackdrop/Blueprints/HDRIBackdrop.HDRIBackdrop_C"));
	UTextureCube* SkyCubemap = LoadObject<UTextureCube>(nullptr,
		TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
	if (HDRIBackdropClass && SkyCubemap && PreviewScene->GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		PreviewHDRIBackdropActor = PreviewScene->GetWorld()->SpawnActor<AActor>(
			HDRIBackdropClass, FVector(0.0f, 0.0f, -75.0f), FRotator::ZeroRotator, Params);
		if (PreviewHDRIBackdropActor)
		{
			if (FObjectPropertyBase* CubemapProperty = FindFProperty<FObjectPropertyBase>(HDRIBackdropClass, TEXT("Cubemap")))
			{
				CubemapProperty->SetObjectPropertyValue_InContainer(PreviewHDRIBackdropActor, SkyCubemap);
			}
			if (FFloatProperty* IntensityProperty = FindFProperty<FFloatProperty>(HDRIBackdropClass, TEXT("Intensity")))
			{
				IntensityProperty->SetPropertyValue_InContainer(PreviewHDRIBackdropActor, 1.0f);
			}
			if (FFloatProperty* SizeProperty = FindFProperty<FFloatProperty>(HDRIBackdropClass, TEXT("Size")))
			{
				SizeProperty->SetPropertyValue_InContainer(PreviewHDRIBackdropActor, 100.0f);
			}
			if (FStructProperty* ProjectionProperty = FindFProperty<FStructProperty>(HDRIBackdropClass, TEXT("ProjectionCenter")))
			{
				*ProjectionProperty->ContainerPtrToValuePtr<FVector>(PreviewHDRIBackdropActor) = FVector(0.0f, 0.0f, 165.0f);
			}
			PreviewHDRIBackdropActor->RerunConstructionScripts();
			if (FObjectPropertyBase* GeometryProperty = FindFProperty<FObjectPropertyBase>(HDRIBackdropClass, TEXT("Geometry")))
			{
				PreviewSkyboxComponent = Cast<UStaticMeshComponent>(
					GeometryProperty->GetObjectPropertyValue_InContainer(PreviewHDRIBackdropActor));
				if (PreviewSkyboxComponent)
				{
					const FTransform GeometryTransform = PreviewSkyboxComponent->GetComponentTransform();
					PreviewSkyboxComponent->UnregisterComponent();
					PreviewScene->AddComponent(PreviewSkyboxComponent, GeometryTransform);
				}
			}
			if (USkyLightComponent* BackdropSky = PreviewHDRIBackdropActor->FindComponentByClass<USkyLightComponent>())
			{
				BackdropSky->SetVisibility(false);
				BackdropSky->SetIntensity(0.0f);
			}
		}
	}

	PreviewPostProcessComponent = NewObject<UPostProcessComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	if (PreviewPostProcessComponent)
	{
		PreviewPostProcessComponent->bUnbound = true;
		PreviewPostProcessComponent->Priority = 100.0f;
		PreviewPostProcessComponent->BlendWeight = 1.0f;
		UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(PreviewPostProcessComponent->Settings, CurrentLightingEnvironment);
		PreviewScene->AddComponent(PreviewPostProcessComponent, FTransform::Identity);
	}

	SyncPreviewStageToEnvironment(CurrentLightingEnvironment);
	UE_LOG(LogTemp, Log, TEXT("[MMDViewPanel] 默认浅色展示台与 LookDev 环境已同步"));
}

void MMDViewPanel::DestroyStagePreview()
{
	if (AMMDStageActor* Stage = PreviewStageActor.Get()) Stage->Destroy();
	PreviewStageActor.Reset();
}

UMMDStageRenderPreset* MMDViewPanel::GetStagePreviewPreset() const
{
	const AMMDStageActor* Stage = PreviewStageActor.Get();
	return Stage ? Stage->RenderPreset.Get() : nullptr;
}

EMMDLightingEnvironment MMDViewPanel::GetLightingEnvironment() const
{
	return PreviewStageActor.IsValid() ? EMMDLightingEnvironment::None : CurrentLightingEnvironment;
}

TOptional<EMMDStudioDisplayStyle> MMDViewPanel::GetDisplaySceneStyle() const
{
	if (PreviewStageActor.IsValid()) return {};
	for (const auto Style : FMMDStudioDisplayScene::GetStyles())
	{
		if (FMMDStudioDisplayScene::GetLightingEnvironment(Style) == CurrentLightingEnvironment) return Style;
	}
	return {};
}

bool MMDViewPanel::PreviewDisplayScene(EMMDStudioDisplayStyle Style)
{
	bool bKnownStyle = false;
	for (const auto Candidate : FMMDStudioDisplayScene::GetStyles()) bKnownStyle |= Candidate == Style;
	if (!bKnownStyle || !PreviewScene.IsValid() || !StudioDisplayScene || !StudioDisplayScene->IsValid()) return false;
	if (GetDisplaySceneStyle() == Style && StudioDisplayScene->GetStyle() == Style) return true;
	return ApplyLightingEnvironment(FMMDStudioDisplayScene::GetLightingEnvironment(Style)) > 0;
}

FText MMDViewPanel::GetLookDevDescription() const
{
	if (const UMMDStageRenderPreset* Preset = GetStagePreviewPreset())
		return FText::FromString(TEXT("LOOKDEV  /  ") + Preset->DisplayName.ToString() + TEXT(" · 舞台原配灯光 / 后处理"));
	if (const TOptional<EMMDStudioDisplayStyle> Style = GetDisplaySceneStyle(); Style.IsSet())
		return FText::FromString(TEXT("展示场景  /  ") + FMMDStudioDisplayScene::GetDisplayName(Style.GetValue()) + TEXT(" · 配套光照 / 固定曝光"));
	return FText::FromString(TEXT("LOOKDEV  /  ") + UMMDLightingEnvironmentLibrary::GetEnvironmentDisplayName(CurrentLightingEnvironment)
		+ (CurrentLightingEnvironment == EMMDLightingEnvironment::Studio3Point ? TEXT(" · 中性影棚 / 固定曝光") : TEXT(" · 环境布光 / 自动曝光")));
}

bool MMDViewPanel::PreviewStageEnvironment(UMMDStageRenderPreset* Preset)
{
	if (!IsValid(Preset) || !PreviewScene.IsValid() || !PreviewScene->GetWorld()) return false;
	AMMDStageActor* Stage = PreviewStageActor.Get();
	if (!Stage)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.bDeferConstruction = true;
		Stage = PreviewScene->GetWorld()->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Params);
		if (!Stage) return false;
		Stage->RenderPreset = Preset;
		Stage->bPreviewAnimation = false;
		Stage->bAnimate = false;
		Stage->FinishSpawning(FTransform::Identity);
		PreviewStageActor = Stage;
	}
	else Stage->ApplyStageRenderPreset(Preset);
	UMMDLightingEnvironmentLibrary::ClearLightingEnvironmentFromPreview(PreviewScene.Get());
	ResetPreviewCamera();
	RefreshPreviewEnvironment();
	return true;
}

void MMDViewPanel::RefreshPreviewEnvironment()
{
	if (!PreviewStageActor.IsValid())
	{
		ApplyLightingEnvironment(CurrentLightingEnvironment);
		return;
	}
	SyncPreviewStageToEnvironment(EMMDLightingEnvironment::None);
	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		const FVector Camera = CustomViewportClient->GetViewLocation();
		Subsystem->SetPreviewLightOverrideFromWorld(PreviewScene->GetWorld(), &Camera);
	}
	PreviewScene->UpdateCaptureContents();
	if (CustomViewportClient.IsValid())
	{
		CustomViewportClient->EngineShowFlags.SetBloom(true);
		CustomViewportClient->Invalidate();
	}
}

void MMDViewPanel::SyncPreviewStageToEnvironment(EMMDLightingEnvironment Environment)
{
	const bool bStage = PreviewStageActor.IsValid();
	if (CustomViewportClient.IsValid()) CustomViewportClient->EngineShowFlags.SetBloom(bStage);
	const bool bUseHDRI = !bStage && (Environment == EMMDLightingEnvironment::Daylight ||
		Environment == EMMDLightingEnvironment::OvercastSoft || Environment == EMMDLightingEnvironment::GoldenHour);
	const TOptional<EMMDStudioDisplayStyle> DisplayStyle = GetDisplaySceneStyle();
	const bool bStudioDisplay = !bStage && DisplayStyle.IsSet() && StudioDisplayScene && StudioDisplayScene->IsValid();
	if (StudioDisplayScene)
	{
		if (bStudioDisplay) StudioDisplayScene->SetStyle(DisplayStyle.GetValue());
		StudioDisplayScene->SetVisible(bStudioDisplay);
	}
	if (PreviewFloorComponent) PreviewFloorComponent->SetVisibility(!bStage && !bStudioDisplay);
	if (PreviewBackdropComponent) PreviewBackdropComponent->SetVisibility(!bStage && !bUseHDRI && !bStudioDisplay);
	if (PreviewPostProcessComponent)
	{
		PreviewPostProcessComponent->bEnabled = !bStage;
		if (!bStage) UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(PreviewPostProcessComponent->Settings, Environment);
	}
	if (PreviewSkyboxComponent)
	{
		PreviewSkyboxComponent->SetHiddenInGame(!bUseHDRI, true);
		PreviewSkyboxComponent->SetVisibility(bUseHDRI, true);
		FString CubemapPath;
		float Rotation = 0;
		float Intensity = 0;
		if (bUseHDRI && UMMDLightingEnvironmentLibrary::GetEnvironmentSky(Environment, CubemapPath, Rotation, &Intensity))
		{
			for (int32 Index = 0; Index < PreviewSkyboxComponent->GetNumMaterials(); ++Index)
			{
				if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(PreviewSkyboxComponent->GetMaterial(Index)))
				{
					Material->SetTextureParameterValue(TEXT("HDRI_Map"), LoadObject<UTextureCube>(nullptr, *CubemapPath));
					Material->SetScalarParameterValue(TEXT("HDRI_Angle"), Rotation / 360.0f);
					Material->SetScalarParameterValue(TEXT("Intensity"), Intensity);
				}
			}
		}
	}
}
