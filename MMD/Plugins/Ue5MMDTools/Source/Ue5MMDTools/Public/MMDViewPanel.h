#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "AMMDActor.h"

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
protected:
	/** Creates the viewport client for this viewport */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;

private:


	void ImportModelClicked();

	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	AActor *SelectedActor = nullptr;
	TSharedPtr<FEditorViewportClient> CustomViewportClient;
	FVector WidgetLocation;

	AActor* PreviewActor = nullptr;
	

};