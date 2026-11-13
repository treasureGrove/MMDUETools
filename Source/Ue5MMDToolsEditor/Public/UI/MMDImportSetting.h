#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "UI/MMDToolPanelWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

class MMDViewPanel;
class AActor;
class UAnimSequence;
class ULevelSequence;
class UMaterialInterface;
class UMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMMDStageRenderPreset;
class SWidgetSwitcher;
struct FAssetData;
struct FMMDPreviewMovieRequest;
struct FMMDPreviewMovieJob;

struct FMMDMaterialPresetUndoEntry
{
    TWeakObjectPtr<UMeshComponent> Component;
    TArray<TWeakObjectPtr<UMaterialInterface>> Materials;
};

enum class EMMDMessageType : uint8
{
    Info,
    Warning,
    Error,
    Success  // 可以添加成功状态
};

class UE5MMDTOOLSEDITOR_API MMDImportSetting : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(MMDImportSetting) {}
    SLATE_ARGUMENT(TSharedPtr<MMDViewPanel>, ViewPanel)
    SLATE_END_ARGS()

    void Construct(const FArguments &InArgs);
    virtual ~MMDImportSetting() override;

    void ShowImportProgress(const FString &Message,EMMDMessageType Type = EMMDMessageType::Info);

    static void ShowGlobalImportProgress(const FString &Message, EMMDMessageType Type = EMMDMessageType::Info);

    // 注册当前实例，避免在 Construct 中调用 SharedThis
    static void RegisterInstance(const TSharedRef<MMDImportSetting>& InstanceRef);

private:
    friend class FMMDToolPanelPresetTest;
    friend class FMMDToolPanelMotionTest;
    friend class FMMDToolPanelExperienceTest;
    friend class FMMDDisplaySceneTest;
    friend class FMMDPresetClickAuditTest;
    friend class FMMDImportStateTest;
    friend class FMMDPreviewMovieTest;

    FReply SelectWorkflowPage(int32 Index);
    bool AreUIAnimationsEnabled() const { return bUIAnimationsEnabled; }
    void SetUIAnimationsEnabled(bool bEnabled, bool bSavePreference = true);
    float GetNavigationPosition() const;
    float GetPageTransitionAlpha() const;
    float GetStatusTransitionAlpha() const;

    /** 导入模型按钮点击事件 */
    FReply OnImportModelClicked();

    FReply OnImportVMDClicked();

    FReply OnAppendFacialVMDClicked();

    FReply OnImportVMDCameraClicked();

    FReply OnOpenSequenceComposerClicked();

    FReply OnOpenPhysicsBakeClicked();

    FReply OnApplyAuroraJewelToonClicked();

    FReply OnUndoShaderPresetClicked();

    TSharedRef<SWidget> BuildRenderPresetPanel();
    TSharedRef<SWidget> BuildScenePresetPanel();
    TSharedRef<SWidget> BuildMotionPresetPanel();
    TSharedRef<SWidget> BuildPreviewControls();
    TSharedRef<SWidget> BuildPreviewMovieControls();
    TSharedRef<SWidget> BuildPreviewMovieResultControls();
    bool BuildPreviewMovieRequest(FMMDPreviewMovieRequest& OutRequest, FString& OutError) const;
    FReply StartPreviewMovie();
    FReply CancelPreviewMovie();
    void ShowPreviewMovieResult(const FString& Output, int32 ExitCode);
    FString GetPreviewMovieResultPath(bool bOpenMovie) const;
    FReply OpenPreviewMovieResult(bool bOpenMovie);
    FReply ApplyRenderPreset(const FString& PresetPath, bool bForceRefresh = false);
    FReply RestoreRenderPreset();
    FReply PlaceStagePreset(const FString& PresetName);
    FReply OpenPresetMap(const FString& PackagePath);
    TArray<USkeletalMeshComponent*> GetRenderPresetTargets() const;
    FText GetRenderPresetTargetText() const;
    bool IsRenderPresetApplied(const FString& PresetPath) const;
    void SuspendLevelPresetPreviews(UWorld* World);
    void ResumeLevelPresetPreviews(UWorld* World);

    void LoadSelectedMMDActor();

    bool SetCurrentMMDActorClass(UClass* ActorClass, const FString& DisplayName);

    uint64 BeginModelImport(AActor* Actor);
    bool AdoptImportedActorClass(uint64 RequestRevision, UClass* ActorClass, const FString& DisplayName);
    bool IsAnimationCompatibleWithCurrentModel(const UAnimSequence* Animation) const;
    bool ShouldFilterAnimationForCurrentModel(const FAssetData& AssetData) const;
    bool SelectProductionAnimation(UAnimSequence* Animation, bool bForPhysicsBake);
    bool UseBakedAnimationResult(UAnimSequence* Animation, uint64 RequestRevision);

    UClass* PickMMDActorClassFromContentBrowser();

    UAnimSequence* PickAnimSequenceFromContentBrowser();

    bool EnsureCurrentMMDActorTarget();

    /** 导入MMD模型文件 */
    void ImportMMDModel();

    /** 导入静态网格模型 */
    void ImportStaticMesh();

    void ImportVMDAnimation();

    void AppendFacialVMDToAnimation();

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
    TSharedPtr<FMMDPreviewMovieJob> PreviewMovieJob;
    FString LastPreviewMovieDirectory;
    bool bLastPreviewMovieSucceeded = false;

    /** 状态文本显示 */
    TSharedPtr<STextBlock> StatusText;
    TSharedPtr<STextBlock> StatusTypeText;
    FString CurrentStatusMessage;
    EMMDMessageType CurrentStatusType = EMMDMessageType::Info;
    bool bUIAnimationsEnabled = true;
    float NavigationStart = 0.0f;
    float NavigationTarget = 0.0f;
    FCurveSequence PageTransition{0.0f, 0.18f, ECurveEaseFunction::CubicOut};
    FCurveSequence StatusTransition{0.0f, 0.2f, ECurveEaseFunction::CubicOut};
    TStrongObjectPtr<UMMDToolPanelWidget> ToolPanelWidget;

    /** 选中的模型/动画文本 */
    TSharedPtr<STextBlock> SelectedModelText;
    TSharedPtr<STextBlock> SelectedAnimText;
    TSharedPtr<SWidgetSwitcher> WorkflowSwitcher;
    bool bApplyPresetToLevelSelection = false;
    FString CustomRenderPresetPath;
    TMap<TWeakObjectPtr<USkeletalMeshComponent>, TStrongObjectPtr<UMMDStageRenderPreset>> LevelPresetPreviews;
    TSet<TWeakObjectPtr<USkeletalMeshComponent>> SuspendedLevelPresetPreviews;
    FDelegateHandle PreSaveWorldHandle;
    FDelegateHandle PostSaveWorldHandle;

    void SetSelectedModelTextUI(const FText& Text);
    void SetSelectedAnimTextUI(const FText& Text);
    bool SetPreviewActorClassUI(UClass* InClass);
    void SetPreviewSkeletalMeshUI(USkeletalMesh* InMesh);
    void SetPreviewAnimationUI(UAnimSequence* InAnim);

    TWeakObjectPtr<AActor> ComposerActor;
    TWeakObjectPtr<AActor> LastImportedMMDActor;
    TWeakObjectPtr<UClass> LastLoadedMMDActorClass;
    uint64 ModelSelectionRevision = 0;
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

    TArray<FMMDMaterialPresetUndoEntry> MaterialPresetUndoEntries;
        
    // 🔧 添加静态成员，保存当前实例的弱引用
    static TWeakPtr<MMDImportSetting> CurrentInstance;
};
