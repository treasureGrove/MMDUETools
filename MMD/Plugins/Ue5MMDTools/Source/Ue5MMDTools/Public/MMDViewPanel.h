#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"

class UE5MMDTOOLS_API MMDViewPanel : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(MMDViewPanel) {}
	SLATE_END_ARGS()

	/** Constructs the viewport panel with the given arguments */
	void Construct(const FArguments &InArgs);

	/** 加载MMD模型文件到场景中 */
	void LoadMMDModel(const FString &FilePath);

protected:
	/** Creates the viewport client for this viewport */
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;

private:
	// 创建预览Actor的函数
	void CreatePreviewActor();

	// 导入模型的函数
	void ImportModelClicked();

	// 成员变量
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	AActor *SelectedActor = nullptr;
	TSharedPtr<FEditorViewportClient> CustomViewportClient;
	FVector WidgetLocation;
};