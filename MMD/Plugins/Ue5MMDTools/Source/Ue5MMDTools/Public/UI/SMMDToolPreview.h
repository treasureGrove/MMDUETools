#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/MMDToolPreviewRenderer.h"

class UAnimSequence;
class USkeletalMesh;
class UTextureRenderTarget2D;
class SImage;

/**
 * MMD 工具面板里的模型预览视口（Slate 版）。
 * 包装 UMMDToolPreviewRenderer（SceneCapture2D 渲染到 RT，每帧更新），
 * 提供鼠标左键旋转、右键环绕、中键/左右键平移、滚轮缩放。
 */
class UE5MMDTOOLS_API SMMDToolPreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMMDToolPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMMDToolPreview();

	bool SetPreviewActorClass(UClass* ActorClass);
	bool SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh);
	bool SetPreviewAnimation(UAnimSequence* AnimSequence);
	void Capture();
	UTextureRenderTarget2D* GetRenderTarget() const;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	enum class EDragMode : uint8
	{
		None,
		MoveRotate,
		Orbit,
		Pan,
	};

	void EnsureRenderer();

	TStrongObjectPtr<UMMDToolPreviewRenderer> Renderer;
	TSharedPtr<SImage> PreviewImage;
	FSlateBrush PreviewBrush;

	bool bDragging = false;
	EDragMode DragMode = EDragMode::None;
	FVector2D LastMousePos = FVector2D::ZeroVector;
};
