#pragma once

#include "CoreMinimal.h"
#include "UI/MMDToolPanelWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

class MMDViewPanel;
class AActor;
class UAnimSequence;
class ULevelSequence;
class USkeletalMeshComponent;
struct FAssetData;

enum class EMMDMessageType : uint8
{
    Info,
    Warning,
    Error,
    Success  // 可以添加成功状态
};

class UE5MMDTOOLS_API MMDImportSetting : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(MMDImportSetting) {}
    SLATE_ARGUMENT(TSharedPtr<MMDViewPanel>, ViewPanel)
    SLATE_END_ARGS()

    void Construct(const FArguments &InArgs);

    void ShowImportProgress(const FString &Message,EMMDMessageType Type = EMMDMessageType::Info);

    static void ShowGlobalImportProgress(const FString &Message, EMMDMessageType Type = EMMDMessageType::Info);

    // 注册当前实例，避免在 Construct 中调用 SharedThis
    static void RegisterInstance(const TSharedRef<MMDImportSetting>& InstanceRef);

private:
    /** 导入模型按钮点击事件 */
    FReply OnImportModelClicked();

    FReply OnImportVMDClicked();

    FReply OnImportVMDCameraClicked();

    FReply OnOpenSequenceComposerClicked();

    FReply OnOpenPhysicsBakeClicked();

    /** 导入MMD模型文件 */
    void ImportMMDModel();

    /** 导入静态网格模型 */
    void ImportStaticMesh();

    void ImportVMDAnimation();

    void ImportVMDCameraAnimation();

    void OpenSequenceComposerWindow();

    void OpenPhysicsBakeWindow();

    FReply CaptureSequenceComposerActor();

    FReply CaptureSequenceComposerAnimation();

    FReply CaptureSequenceComposerCamera();

    void OnComposerActorChanged(const FAssetData& AssetData);

    void OnComposerAnimationChanged(const FAssetData& AssetData);

    void OnComposerCameraChanged(const FAssetData& AssetData);

    FString GetComposerActorPath() const;

    FString GetComposerAnimationPath() const;

    FString GetComposerCameraPath() const;

    FReply CreateComposedLevelSequence();

    void RefreshSequenceComposerLabels();

    FReply CapturePhysicsBakeActor();

    FReply CapturePhysicsBakeAnimation();

    void OnPhysicsBakeAnimationChanged(const FAssetData& AssetData);

    FString GetPhysicsBakeAnimationPath() const;

    TOptional<float> GetPhysicsBakeFrameRate() const;

    void OnPhysicsBakeFrameRateChanged(float NewValue);

    TOptional<int32> GetPhysicsBakeWarmupFrames() const;

    void OnPhysicsBakeWarmupFramesChanged(int32 NewValue);

    FReply BakePhysicsAnimation();

    void RefreshPhysicsBakeLabels();

   

private:
    /** 关联的视口面板 */
    TSharedPtr<MMDViewPanel> ViewPanel;

    /** 状态文本显示 */
    TSharedPtr<STextBlock> StatusText;
    TStrongObjectPtr<UMMDToolPanelWidget> ToolPanelWidget;

    TWeakObjectPtr<AActor> ComposerActor;
    TWeakObjectPtr<USkeletalMeshComponent> ComposerSkeletalMeshComponent;
    TWeakObjectPtr<UAnimSequence> ComposerAnimSequence;
    TWeakObjectPtr<ULevelSequence> ComposerCameraSequence;
    TSharedPtr<STextBlock> ComposerActorText;
    TSharedPtr<STextBlock> ComposerAnimText;
    TSharedPtr<STextBlock> ComposerCameraText;

    TWeakObjectPtr<AActor> PhysicsBakeActor;
    TWeakObjectPtr<USkeletalMeshComponent> PhysicsBakeSkeletalMeshComponent;
    TWeakObjectPtr<UAnimSequence> PhysicsBakeAnimSequence;
    TSharedPtr<STextBlock> PhysicsBakeActorText;
    TSharedPtr<STextBlock> PhysicsBakeAnimText;
    float PhysicsBakeFrameRate = 30.0f;
    int32 PhysicsBakeWarmupFrames = 0;
        
    // 🔧 添加静态成员，保存当前实例的弱引用
    static TWeakPtr<MMDImportSetting> CurrentInstance;
};
