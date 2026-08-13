#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "AMMDActor.h"

class FEditorModeTools; // forward declare editor type
class UPoseableMeshComponent;
struct FReferenceSkeleton;

class UE5MMDTOOLS_API MMDViewPanel : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(MMDViewPanel) {}
	SLATE_END_ARGS()

	/** Constructs the viewport panel with the given arguments */
	void Construct(const FArguments &InArgs);

	void LoadMMDModel(const FString &FilePath);

	void ShowImportedSkeletalMesh(class USkeletalMesh* SkeletalMesh);
	bool CreatePreviewActor(UClass* ActorClass);
	void BeginPhysicsBakePreview(class USkeletalMesh* SkeletalMesh);
	void PreviewPhysicsBakeFrame(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& ComponentTransforms);
	void EndPhysicsBakePreview();

	// destructor to clean up editor-only pointers
	virtual ~MMDViewPanel();

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

	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	AActor *SelectedActor = nullptr;
	TSharedPtr<FEditorViewportClient> CustomViewportClient;
	FVector WidgetLocation;

	AActor* PreviewActor = nullptr;
	UPoseableMeshComponent* PhysicsBakePreviewComponent = nullptr;

	TSharedPtr<FEditorModeTools> LocalModeTools;
};
