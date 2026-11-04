#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "PreviewScene.h"
#include "EditorViewportClient.h"
#include "AMMDActor.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/MMDStudioDisplayStyle.h"

class FEditorModeTools; // forward declare editor type
class UPoseableMeshComponent;
class UAnimSequence;
class UMaterialInterface;
class UPostProcessComponent;
class UStaticMeshComponent;
class AMMDStageActor;
class UMMDStageRenderPreset;
class FMMDStudioDisplayScene;
struct FReferenceSkeleton;
enum class EMMDLightingEnvironment : uint8;
enum class EMMDPreviewView : uint8 { FullBody, UpperBody, Face, Side, Back, Scene };

class UE5MMDTOOLSEDITOR_API MMDViewPanel : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(MMDViewPanel) {}
	SLATE_END_ARGS()

	/** Constructs the viewport panel with the given arguments */
	void Construct(const FArguments &InArgs);

	void LoadMMDModel(const FString &FilePath);

	void ShowImportedSkeletalMesh(class USkeletalMesh* SkeletalMesh);
	bool SetPreviewAnimation(class UAnimSequence* AnimSequence);
	bool PlayBuiltinMotion(FName Preset, FText& OutMessage);
	void ClearPreviewAnimation();
	FName GetBuiltinMotion() const { return CurrentBuiltinMotion; }
	bool HasPreviewAnimation() const;
	bool IsPreviewPlaying() const;
	bool HasPreviewModel() const;
	bool HasPreviewPhysics() const;
	bool IsPreviewPoseHeld() const { return bPreviewPoseHeld; }
	float GetPreviewPlaybackSpeed() const { return PreviewPlaybackSpeed; }
	void SetPreviewPoseHeld(bool bHeld);
	void SetPreviewPlaybackSpeed(float Speed);
	void RestartPreviewAnimation();
	void BeginPreviewScrub();
	void EndPreviewScrub();
	FText GetPreviewPlaybackStatus() const;
	TOptional<EMMDPreviewView> GetPreviewView() const { return CurrentPreviewView; }
	void ClearPreviewViewSelection() { CurrentPreviewView.Reset(); }
	bool HandlePreviewShortcut(FKey Key);
	bool IsPreviewPhysicsEnabled() const { return bPreviewPhysicsEnabled; }
	FText GetPreviewPhysicsStatus() const;
	FText GetPreviewAnimationLabel() const;
	float GetPreviewTime() const;
	float GetPreviewDuration() const;
	float GetPreviewFrameInterval() const;
	bool GetProductionCamera(FVector& OutLocation, FRotator& OutRotation, double& OutHorizontalFOV, FIntPoint& OutSize) const;
	void StepPreviewFrame(int32 Direction);
	void SeekPreviewAnimation(float Time);
	void SetPreviewView(EMMDPreviewView View, bool bInstant = false);
	void SetPreviewPlaying(bool bPlaying);
	void SetPreviewPhysicsEnabled(bool bEnabled);
	void ResetPreviewPhysics();
	int32 ApplyLightingEnvironment(EMMDLightingEnvironment Environment);
	bool PreviewStageEnvironment(UMMDStageRenderPreset* Preset);
	bool PreviewDisplayScene(EMMDStudioDisplayStyle Style);
	TOptional<EMMDStudioDisplayStyle> GetDisplaySceneStyle() const;
	FText GetLookDevDescription() const;
	EMMDLightingEnvironment GetLightingEnvironment() const;
	UMMDStageRenderPreset* GetStagePreviewPreset() const;
	void ClearLightingEnvironment();
	bool CreatePreviewActor(UClass* ActorClass);
	AActor* GetPreviewActor() const { return PreviewActor; }
	void BeginPhysicsBakePreview(class USkeletalMesh* SkeletalMesh);
	void PreviewPhysicsBakeFrame(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& ComponentTransforms);
	void EndPhysicsBakePreview();

	// destructor to clean up editor-only pointers
	virtual ~MMDViewPanel();

	/** 把编辑器主透视视口相机归位到看向舞台中央（模型原点）的标准机位。 */
	void ResetPreviewCamera();

	/** 在预览场景搭建 MMD LookDev 舞台、后处理与可见天空，与 Content 环境关卡一致。 */
	void BuildPreviewStage();

protected:
	/** Creates the viewport client for this viewport */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
#else
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
#endif

private:
	friend class FMMDToolPanelPresetTest;
	friend class FMMDToolPanelMotionTest;
	friend class FMMDDisplaySceneTest;
	USkeletalMeshComponent* GetPreviewMeshComponent() const;
	FFrameRate GetPreviewFrameRate() const;
	void ResetPreviewMotionState();
	void RefreshPreviewEnvironment();
	void DestroyStagePreview();
	void ImportModelClicked();

	TSharedPtr<FPreviewScene> PreviewScene;
	AActor *SelectedActor = nullptr;
	TSharedPtr<FEditorViewportClient> CustomViewportClient;
	FVector WidgetLocation;

	AActor* PreviewActor = nullptr;
	TWeakObjectPtr<AMMDStageActor> PreviewStageActor;
	UPoseableMeshComponent* PhysicsBakePreviewComponent = nullptr;
	EMMDLightingEnvironment CurrentLightingEnvironment;
	UStaticMeshComponent* PreviewFloorComponent = nullptr;
	UStaticMeshComponent* PreviewBackdropComponent = nullptr;
	AActor* PreviewHDRIBackdropActor = nullptr;
	UStaticMeshComponent* PreviewSkyboxComponent = nullptr;
	UPostProcessComponent* PreviewPostProcessComponent = nullptr;
	TSharedPtr<FMMDStudioDisplayScene> StudioDisplayScene;
	TMap<FName, TStrongObjectPtr<UAnimSequence>> BuiltinMotions;
	FName CurrentBuiltinMotion;
	bool bPreviewPhysicsEnabled = true;
	bool bPreviewPoseHeld = false;
	bool bPreviewScrubbing = false;
	bool bPreviewScrubWasPlaying = false;
	float PreviewPlaybackSpeed = 1.0f;
	TOptional<EMMDPreviewView> CurrentPreviewView;

	void SyncPreviewStageToEnvironment(EMMDLightingEnvironment Environment);

	TSharedPtr<FEditorModeTools> LocalModeTools;
};
