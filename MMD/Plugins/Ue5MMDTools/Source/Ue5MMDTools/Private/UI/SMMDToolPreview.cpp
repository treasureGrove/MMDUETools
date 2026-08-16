#include "UI/SMMDToolPreview.h"

#include "UI/MMDToolPreviewRenderer.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/AppStyle.h"

void SMMDToolPreview::Construct(const FArguments& InArgs)
{
	SetCanTick(false);

	EnsureRenderer();

	if (UTextureRenderTarget2D* RT = Renderer ? Renderer->GetRenderTarget() : nullptr)
	{
		PreviewBrush.SetResourceObject(RT);
		PreviewBrush.ImageSize = FVector2D(RT->SizeX, RT->SizeY);
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(4.0f)
		[
			SAssignNew(PreviewImage, SImage)
			.Image(&PreviewBrush)
		]
	];
}

SMMDToolPreview::~SMMDToolPreview()
{
	if (Renderer)
	{
		Renderer->Shutdown();
	}
	Renderer.Reset();
}

void SMMDToolPreview::EnsureRenderer()
{
	if (!Renderer)
	{
		Renderer = TStrongObjectPtr<UMMDToolPreviewRenderer>(
			NewObject<UMMDToolPreviewRenderer>(GetTransientPackage(), NAME_None, RF_Transient));
		Renderer->Initialize(1920, 1080);
	}
}

bool SMMDToolPreview::SetPreviewActorClass(UClass* ActorClass)
{
	EnsureRenderer();
	return Renderer ? Renderer->SetPreviewActorClass(ActorClass) : false;
}

bool SMMDToolPreview::SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	EnsureRenderer();
	return Renderer ? Renderer->SetPreviewSkeletalMesh(SkeletalMesh) : false;
}

bool SMMDToolPreview::SetPreviewAnimation(UAnimSequence* AnimSequence)
{
	EnsureRenderer();
	return Renderer ? Renderer->SetPreviewAnimation(AnimSequence) : false;
}

void SMMDToolPreview::Capture()
{
	EnsureRenderer();
	if (Renderer)
	{
		Renderer->Capture();
	}
}

UTextureRenderTarget2D* SMMDToolPreview::GetRenderTarget() const
{
	return Renderer ? Renderer->GetRenderTarget() : nullptr;
}

FReply SMMDToolPreview::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Renderer)
	{
		return FReply::Unhandled();
	}

	const FKey EffectingButton = MouseEvent.GetEffectingButton();
	if (EffectingButton == EKeys::LeftMouseButton || EffectingButton == EKeys::RightMouseButton || EffectingButton == EKeys::MiddleMouseButton)
	{
		bDragging = true;
		if (EffectingButton == EKeys::MiddleMouseButton
			|| (EffectingButton == EKeys::LeftMouseButton && MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
			|| (EffectingButton == EKeys::RightMouseButton && MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)))
		{
			DragMode = EDragMode::Pan;
		}
		else if (EffectingButton == EKeys::RightMouseButton)
		{
			DragMode = EDragMode::Orbit;
		}
		else
		{
			DragMode = EDragMode::MoveRotate;
		}
		LastMousePos = MouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply SMMDToolPreview::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging)
	{
		bDragging = false;
		DragMode = EDragMode::None;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SMMDToolPreview::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bDragging && Renderer)
	{
		const FVector2D Current = MouseEvent.GetScreenSpacePosition();
		const FVector2D Delta = Current - LastMousePos;
		LastMousePos = Current;

		if (!Delta.IsNearlyZero())
		{
			switch (DragMode)
			{
			case EDragMode::Pan:
				Renderer->PanCamera(Delta);
				break;
			case EDragMode::Orbit:
				Renderer->LookCamera(Delta);
				break;
			case EDragMode::MoveRotate:
				Renderer->LookCamera(FVector2D(Delta.X, 0.0f));
				Renderer->DollyCamera(-Delta.Y);
				break;
			default:
				break;
			}
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SMMDToolPreview::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Renderer)
	{
		if (bDragging && DragMode == EDragMode::Orbit)
		{
			Renderer->AdjustCameraMoveSpeed(MouseEvent.GetWheelDelta());
		}
		else
		{
			Renderer->DollyCamera(MouseEvent.GetWheelDelta());
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
