#include "MMDViewPanel.h"
#include "Ue5MMDTools.h"
#include "EditorViewportClient.h"
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

class FMMDViewportClient : public FEditorViewportClient
{
public:
    FMMDViewportClient(FEditorModeTools* InModeTools, FPreviewScene *InPreviewScene, const TWeakPtr<SEditorViewport> &InEditorViewportWidget)
        : FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget), PreviewScene(InPreviewScene), SelectedActor(nullptr)
    {
        SetViewMode(VMI_Lit);
        SetRealtime(true);
        // 关键：启用编辑器/后处理/选择描边相关的 ShowFlag（保持兼容）
        EngineShowFlags.SetEditor(true);
        EngineShowFlags.SetPostProcessing(true);
		// 使用 UE 默认自动曝光，不在插件视口里锁定摄影参数。
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
        // 右键相关操作总是用于视角控制，不传递给Widget系统
        if (EventArgs.Key == EKeys::RightMouseButton)
        {
            return FEditorViewportClient::InputKey(EventArgs);
        }

        // Shift+滚轮控制摄像机速度
        if (EventArgs.Event == IE_Pressed && (EventArgs.Key == EKeys::MouseScrollUp || EventArgs.Key == EKeys::MouseScrollDown))
        {
            // 检查Shift键是否被按下 - 使用Viewport检查
            if (EventArgs.Viewport && EventArgs.Viewport->KeyState(EKeys::LeftShift) || EventArgs.Viewport->KeyState(EKeys::RightShift))
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
        if (EventArgs.Event == IE_Pressed && SelectedActor)
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
		.SetSkyBrightness(0.0f);
	PreviewScene = MakeShared<FPreviewScene>(PreviewCVS);

	if (UDirectionalLightComponent* DefaultLight = PreviewScene->DirectionalLight)
	{
		DefaultLight->SetVisibility(false);
	}
	if (USkyLightComponent* Sky = PreviewScene->SkyLight)
	{
		Sky->SetVisibility(false);
    }

    // 搭建影棚舞台（与光照环境 map 相同的 Cube 地面+墙），切场景时灯光/相机都会围绕它。
    BuildPreviewStage();

    // create local ModeTools instance as shared, required because FEditorViewportClient's ctor calls AsShared()
    LocalModeTools = MakeShared<FEditorModeTools>();

	// 首先调用父类的Construct来初始化视口
	SEditorViewport::Construct(SEditorViewport::FArguments());
	ApplyLightingEnvironment(CurrentLightingEnvironment);
}

MMDViewPanel::~MMDViewPanel()
{
	ClearLightingEnvironment();
	EndPhysicsBakePreview();
    LocalModeTools.Reset();
}

TSharedRef<FEditorViewportClient> MMDViewPanel::MakeEditorViewportClient()
{
	// SEditorViewport 已通过 SNew/SAssignNew 建立 shared 引用，此处 SharedThis(this) 安全（引擎标准做法）。
	CustomViewportClient = MakeShared<FMMDViewportClient>(LocalModeTools.Get(), PreviewScene.Get(), SharedThis(this));
	CustomViewportClient->ExposureSettings.bFixed = false;
	CustomViewportClient->EngineShowFlags.SetEyeAdaptation(true);
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
    // 创建工具栏
    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[SNew(SButton).Text(FText::FromString(TEXT("Move (W)"))).OnClicked_Lambda([this]()
                                                                                                                                                            {
                if (CustomViewportClient.IsValid())
                {
                    CustomViewportClient->SetWidgetMode(UE::Widget::WM_Translate);
                }
                return FReply::Handled(); })] +
           SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(2.0f)
                   [SNew(SButton)
                        .Text(FText::FromString(TEXT("Rotate (E)")))
                        .OnClicked_Lambda([this]()
                                          {
                if (CustomViewportClient.IsValid())
                {
                    CustomViewportClient->SetWidgetMode(UE::Widget::WM_Rotate);
                }
                return FReply::Handled(); })] +
           SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(2.0f)
                   [SNew(SButton)
                        .Text(FText::FromString(TEXT("Scale (R)")))
                        .OnClicked_Lambda([this]()
                                          {
                if (CustomViewportClient.IsValid())
                {
                    CustomViewportClient->SetWidgetMode(UE::Widget::WM_Scale);
                }
                return FReply::Handled(); })] +
           SHorizontalBox::Slot()
               .FillWidth(1.0f) // 占据剩余空间，将速度控制推到右边
           + SHorizontalBox::Slot()
                 .AutoWidth()
                 .Padding(5.0f, 2.0f)
                     [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f)[SNew(STextBlock).Text(FText::FromString(TEXT("Camera Speed:")))] + SHorizontalBox::Slot().AutoWidth().Padding(2.0f)[SNew(SSpinBox<float>).MinValue(0.1f).MaxValue(16.0f).Value_Lambda([this]() -> float
                                                                                                                                                                                                                                                                                                          {
                        if (CustomViewportClient.IsValid())
                        {
                            return static_cast<FMMDViewportClient*>(CustomViewportClient.Get())->GetCameraSpeed();
                        }
                        return 4.0f; })
                                                                                                                                                                                                                                            .OnValueChanged_Lambda([this](float NewValue)
                                                                                                                                                                                                                                                                   {
                        if (CustomViewportClient.IsValid())
                        {
                            static_cast<FMMDViewportClient*>(CustomViewportClient.Get())->SetCameraSpeed(NewValue);
                        } })
                                                                                                                                                                                                                                            .Delta(0.1f)
                                                                                                                                                                                                                                            .MinDesiredWidth(60.0f)]] +
           SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(10.0f, 2.0f)
                   [SNew(SButton)
                        .Text(FText::FromString(TEXT("Import Model")))
                        .OnClicked_Lambda([this]()
                                          {
                ImportModelClicked();
                return FReply::Handled(); })] +
           SHorizontalBox::Slot()
               .AutoWidth()
               .Padding(2.0f)
                   [SNew(SButton)
                        .Text(FText::FromString(TEXT("Add Test Cube")))
                        .OnClicked_Lambda([this]()
                                          {
                return FReply::Handled(); })];
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
	ApplyLightingEnvironment(CurrentLightingEnvironment);
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
	ApplyLightingEnvironment(CurrentLightingEnvironment);
}

void MMDViewPanel::SetPreviewAnimation(class UAnimSequence* AnimSequence)
{
	USkeletalMeshComponent* SkelComp = nullptr;
	if (IsValid(PreviewActor))
	{
		SkelComp = PreviewActor->FindComponentByClass<USkeletalMeshComponent>();
	}
	if (!SkelComp || !AnimSequence)
	{
		return;
	}

	SkelComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkelComp->SetAnimation(AnimSequence);
	SkelComp->Play(true);

	if (CustomViewportClient.IsValid())
	{
		CustomViewportClient->Invalidate();
	}
}

int32 MMDViewPanel::ApplyLightingEnvironment(EMMDLightingEnvironment Environment)
{
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

void MMDViewPanel::ResetPreviewCamera()
{
	if (!CustomViewportClient.IsValid())
	{
		return;
	}

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
		const float Radius = FMath::Max(ModelBounds.GetExtent().Size(), 1.0f);
		Dist = FMath::Max(Radius * 3.0f, 400.0f);
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
				IntensityProperty->SetPropertyValue_InContainer(PreviewHDRIBackdropActor, 1000.0f);
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
		UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(PreviewPostProcessComponent->Settings);
		PreviewScene->AddComponent(PreviewPostProcessComponent, FTransform::Identity);
	}

	SyncPreviewStageToEnvironment(CurrentLightingEnvironment);
	UE_LOG(LogTemp, Log, TEXT("[MMDViewPanel] Panel LookDev 已同步（18%% 灰地面、背景、Skybox、默认曝光）"));
}

void MMDViewPanel::SyncPreviewStageToEnvironment(EMMDLightingEnvironment Environment)
{
	const bool bUseHDRI = Environment == EMMDLightingEnvironment::Daylight ||
		Environment == EMMDLightingEnvironment::OvercastSoft ||
		Environment == EMMDLightingEnvironment::GoldenHour;

	if (PreviewFloorComponent)
	{
		PreviewFloorComponent->SetVisibility(true);
	}
	if (PreviewBackdropComponent)
	{
		PreviewBackdropComponent->SetVisibility(!bUseHDRI);
	}
	if (PreviewSkyboxComponent)
	{
		PreviewSkyboxComponent->SetHiddenInGame(!bUseHDRI, true);
		PreviewSkyboxComponent->SetVisibility(bUseHDRI, true);
	}
	if (PreviewPostProcessComponent)
	{
		UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(PreviewPostProcessComponent->Settings);
	}
}
