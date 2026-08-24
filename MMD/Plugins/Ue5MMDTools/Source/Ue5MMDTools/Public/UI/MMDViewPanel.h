#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "PreviewScene.h"
#include "EditorViewportClient.h"
#include "AMMDActor.h"

class FEditorModeTools; // forward declare editor type
class UPoseableMeshComponent;
class UAnimSequence;
class UMaterialInterface;
class UPostProcessComponent;
class UStaticMeshComponent;
struct FReferenceSkeleton;
enum class EMMDLightingEnvironment : uint8;

class UE5MMDTOOLS_API MMDViewPanel : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(MMDViewPanel) {}
	SLATE_END_ARGS()

	/** Constructs the viewport panel with the given arguments */
	void Construct(const FArguments &InArgs);

	void LoadMMDModel(const FString &FilePath);

	void ShowImportedSkeletalMesh(class USkeletalMesh* SkeletalMesh);
	void SetPreviewAnimation(class UAnimSequence* AnimSequence);
	int32 ApplyLightingEnvironment(EMMDLightingEnvironment Environment);
	void ClearLightingEnvironment();
	bool CreatePreviewActor(UClass* ActorClass);
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
	void ImportModelClicked();

	TSharedPtr<FPreviewScene> PreviewScene;
	AActor *SelectedActor = nullptr;
	TSharedPtr<FEditorViewportClient> CustomViewportClient;
	FVector WidgetLocation;

	AActor* PreviewActor = nullptr;
	UPoseableMeshComponent* PhysicsBakePreviewComponent = nullptr;
	EMMDLightingEnvironment CurrentLightingEnvironment;
	UStaticMeshComponent* PreviewFloorComponent = nullptr;
	UStaticMeshComponent* PreviewBackdropComponent = nullptr;
	AActor* PreviewHDRIBackdropActor = nullptr;
	UStaticMeshComponent* PreviewSkyboxComponent = nullptr;
	UPostProcessComponent* PreviewPostProcessComponent = nullptr;

	void SyncPreviewStageToEnvironment(EMMDLightingEnvironment Environment);

	TSharedPtr<FEditorModeTools> LocalModeTools;
};
