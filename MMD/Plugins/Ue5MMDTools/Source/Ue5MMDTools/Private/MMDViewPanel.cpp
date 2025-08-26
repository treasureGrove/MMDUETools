#include "MMDViewPanel.h"
#include "MMDModelImporter.h"
#include "Ue5MMDTools.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "SEditorViewport.h"
#include "UnrealWidget.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SSpinBox.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "EditorModeTools.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "HitProxies.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFilemanager.h"
#include "TPMXParser.h"

// 自定义的视窗客户端类 - 支持UE5编辑器功能
class FMMDViewportClient : public FEditorViewportClient
{
public:
    FMMDViewportClient(FPreviewScene *InPreviewScene, const TWeakPtr<SEditorViewport> &InEditorViewportWidget)
        : FEditorViewportClient(nullptr, InPreviewScene, InEditorViewportWidget), PreviewScene(InPreviewScene), SelectedActor(nullptr)
    {
        SetViewMode(VMI_Lit);
        SetRealtime(true);

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

    // 处理鼠标点击选择 - 让父类优先处理视角控制
    virtual void ProcessClick(FSceneView &View, HHitProxy *HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override
    {
        // 先让父类处理所有输入（包括视角控制）
        FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

        // 只有在左键释放时才处理选择（避免干扰拖拽）
        if (Key == EKeys::LeftMouseButton && Event == IE_Released)
        {
            // 使用射线检测进行选择
            if (GetWorld())
            {
                FVector WorldOrigin, WorldDirection;
                View.DeprojectFVector2D(FVector2D(HitX, HitY), WorldOrigin, WorldDirection);

                FVector TraceStart = WorldOrigin;
                FVector TraceEnd = WorldOrigin + WorldDirection * 10000.0f;

                FHitResult HitResult;
                FCollisionQueryParams Params(SCENE_QUERY_STAT(ProcessClick), true);

                if (GetWorld()->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, ECC_Visibility, Params))
                {
                    AActor *HitActor = HitResult.GetActor();
                    if (HitActor && IsValid(HitActor))
                    {
                        SetSelectedActor(HitActor);
                        return;
                    }
                }
            }

            // 如果没有找到对象，清除选择
            SetSelectedActor(nullptr);
        }
    }

    // 重写鼠标移动处理 - 确保右键拖拽用于视角控制
    virtual bool InputAxis(FViewport *InViewport, FInputDeviceId DeviceId, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad) override
    {
        // 右键相关的鼠标移动总是用于视角控制
        if (Key == EKeys::MouseX || Key == EKeys::MouseY)
        {
            // 检查右键是否被按下
            if (InViewport->KeyState(EKeys::RightMouseButton))
            {
                // 强制使用父类的视角控制，不传递给Widget系统
                return FEditorViewportClient::InputAxis(InViewport, DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
            }
        }

        // 其他轴输入正常处理
        return FEditorViewportClient::InputAxis(InViewport, DeviceId, Key, Delta, DeltaTime, NumSamples, bGamepad);
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

private:
    FPreviewScene *PreviewScene;
    AActor *SelectedActor;
    FVector WidgetLocation;
    float CameraSpeedSetting = 4.0f; // 默认摄像机速度
};

// MMDViewPanel 实现
void MMDViewPanel::Construct(const FArguments &InArgs)
{
    // 创建预览场景
    PreviewScene = MakeShareable(new FAdvancedPreviewScene(FPreviewScene::ConstructionValues()));

    // 首先调用父类的Construct来初始化视口
    SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> MMDViewPanel::MakeEditorViewportClient()
{
    CustomViewportClient = MakeShareable(new FMMDViewportClient(PreviewScene.Get(), SharedThis(this)));
    return CustomViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> MMDViewPanel::MakeViewportToolbar()
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
                CreatePreviewActor();
                return FReply::Handled(); })];
}

void MMDViewPanel::CreatePreviewActor()
{
    if (PreviewScene.IsValid() && PreviewScene->GetWorld())
    {
        // 加载立方体网格
        UStaticMesh *CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        if (CubeMesh)
        {
            // 生成立方体Actor
            FVector SpawnLocation = FVector(0, 0, 0);
            AStaticMeshActor *NewCube = PreviewScene->GetWorld()->SpawnActor<AStaticMeshActor>(SpawnLocation, FRotator::ZeroRotator);

            if (NewCube && NewCube->GetStaticMeshComponent())
            {
                NewCube->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
                NewCube->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);

                // 设置碰撞
                NewCube->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
                NewCube->GetStaticMeshComponent()->SetCollisionObjectType(ECC_WorldStatic);
                NewCube->GetStaticMeshComponent()->SetCollisionResponseToAllChannels(ECR_Block);

                // 选中新创建的立方体
                if (FMMDViewportClient *ViewportClient = static_cast<FMMDViewportClient *>(CustomViewportClient.Get()))
                {
                    ViewportClient->SetSelectedActor(NewCube);
                }
            }
        }
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

            // 显示导入成功的消息
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

            // 这里可以添加实际的模型导入逻辑
            // 比如使用UE5的FBX导入器来导入模型
            // ImportStaticMeshFromFile(SelectedFile);
        }
    }
}

void MMDViewPanel::LoadMMDModel(const FString &FilePath)
{
    UE_LOG(LogTemp, Warning, TEXT("LoadMMDModel called with file: %s"), *FilePath);
    // 检查是否为PMX文件
    FString FileExtension = FPaths::GetExtension(FilePath).ToLower();
}
