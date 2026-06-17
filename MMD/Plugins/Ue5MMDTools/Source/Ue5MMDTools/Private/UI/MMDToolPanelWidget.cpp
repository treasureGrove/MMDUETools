#include "UI/MMDToolPanelWidget.h"

#include "UI/MMDToolPreviewRenderer.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "InputCoreTypes.h"

void UMMDToolPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);

	if (BtnImportModel)
	{
		BtnImportModel->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleImportModelClicked);
	}
	if (BtnImportVMD)
	{
		BtnImportVMD->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleImportVMDClicked);
	}
	if (BtnAppendFacialVMD)
	{
		BtnAppendFacialVMD->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleAppendFacialVMDClicked);
	}
	if (BtnImportCamera)
	{
		BtnImportCamera->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleImportCameraClicked);
	}
	if (BtnLoadMMDActor)
	{
		BtnLoadMMDActor->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleLoadMMDActorClicked);
	}
	if (BtnComposeSequence)
	{
		BtnComposeSequence->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleComposeSequenceClicked);
	}
	if (BtnBakePhysics)
	{
		BtnBakePhysics->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleBakePhysicsClicked);
	}
	if (BtnSelectMode)
	{
		BtnSelectMode->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleSelectModeClicked);
	}
	if (BtnMoveMode)
	{
		BtnMoveMode->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleMoveModeClicked);
	}
	if (BtnRotateMode)
	{
		BtnRotateMode->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleRotateModeClicked);
	}
	if (BtnScaleMode)
	{
		BtnScaleMode->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleScaleModeClicked);
	}
	if (BtnPreviewPlay)
	{
		BtnPreviewPlay->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandlePreviewPlayClicked);
	}

	SetBakeProgress(0.0f);
}

void UMMDToolPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	InitializePreviewRenderTarget();
}

void UMMDToolPanelWidget::NativeDestruct()
{
	bPreviewMouseDragging = false;
	PreviewMouseDragMode = EPreviewMouseDragMode::None;
	ResetPreviewNavigationState();

	if (PreviewRenderer)
	{
		PreviewRenderer->Shutdown();
		PreviewRenderer = nullptr;
	}

	Super::NativeDestruct();
}

FReply UMMDToolPanelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PreviewRenderer && IsMouseOverPreviewImage(InMouseEvent) && !IsMouseOverPreviewControl(InMouseEvent))
	{
		const FKey EffectingButton = InMouseEvent.GetEffectingButton();
		if (EffectingButton == EKeys::LeftMouseButton || EffectingButton == EKeys::RightMouseButton || EffectingButton == EKeys::MiddleMouseButton)
		{
			bPreviewMouseDragging = true;
			if (EffectingButton == EKeys::MiddleMouseButton
				|| (EffectingButton == EKeys::LeftMouseButton && InMouseEvent.IsMouseButtonDown(EKeys::RightMouseButton))
				|| (EffectingButton == EKeys::RightMouseButton && InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton)))
			{
				PreviewMouseDragMode = EPreviewMouseDragMode::Pan;
			}
			else if (EffectingButton == EKeys::RightMouseButton)
			{
				PreviewMouseDragMode = EPreviewMouseDragMode::Orbit;
			}
			else
			{
				PreviewMouseDragMode = EPreviewMouseDragMode::MoveRotate;
			}
			LastPreviewMouseScreenPosition = InMouseEvent.GetScreenSpacePosition();
			SetKeyboardFocus();
			return FReply::Handled()
				.CaptureMouse(TakeWidget())
				.SetUserFocus(TakeWidget())
				.UseHighPrecisionMouseMovement(TakeWidget());
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMMDToolPanelWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bPreviewMouseDragging)
	{
		bPreviewMouseDragging = false;
		PreviewMouseDragMode = EPreviewMouseDragMode::None;
		ResetPreviewNavigationState();
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UMMDToolPanelWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bPreviewMouseDragging && PreviewRenderer)
	{
		const FVector2D CurrentMousePosition = InMouseEvent.GetScreenSpacePosition();
		const FVector2D DragDelta = CurrentMousePosition - LastPreviewMouseScreenPosition;
		LastPreviewMouseScreenPosition = CurrentMousePosition;

		if (!DragDelta.IsNearlyZero())
		{
			if (PreviewMouseDragMode == EPreviewMouseDragMode::Pan)
			{
				PreviewRenderer->PanCamera(DragDelta);
			}
			else if (PreviewMouseDragMode == EPreviewMouseDragMode::Zoom)
			{
				PreviewRenderer->ZoomCamera(-DragDelta.Y * 0.03f);
			}
			else if (PreviewMouseDragMode == EPreviewMouseDragMode::Orbit)
			{
				PreviewRenderer->LookCamera(DragDelta);
			}
			else if (PreviewMouseDragMode == EPreviewMouseDragMode::MoveRotate)
			{
				PreviewRenderer->LookCamera(FVector2D(DragDelta.X, 0.0f));
				PreviewRenderer->DollyCamera(-DragDelta.Y);
			}
		}

		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMMDToolPanelWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PreviewRenderer && IsMouseOverPreviewImage(InMouseEvent))
	{
		if (bPreviewMouseDragging && PreviewMouseDragMode == EPreviewMouseDragMode::Orbit)
		{
			PreviewRenderer->AdjustCameraMoveSpeed(InMouseEvent.GetWheelDelta());
		}
		else
		{
			PreviewRenderer->DollyCamera(InMouseEvent.GetWheelDelta());
		}
		return FReply::Handled();
	}

	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UMMDToolPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bPreviewMouseDragging && PreviewMouseDragMode == EPreviewMouseDragMode::Orbit && SetPreviewNavigationKeyState(InKeyEvent.GetKey(), true))
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UMMDToolPanelWidget::NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (SetPreviewNavigationKeyState(InKeyEvent.GetKey(), false))
	{
		return FReply::Handled();
	}

	return Super::NativeOnKeyUp(InGeometry, InKeyEvent);
}

void UMMDToolPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!PreviewRenderer || !bPreviewMouseDragging || PreviewMouseDragMode != EPreviewMouseDragMode::Orbit)
	{
		return;
	}

	FVector LocalDirection = FVector::ZeroVector;
	LocalDirection.X += bPreviewMoveForward ? 1.0f : 0.0f;
	LocalDirection.X -= bPreviewMoveBackward ? 1.0f : 0.0f;
	LocalDirection.Y += bPreviewMoveRight ? 1.0f : 0.0f;
	LocalDirection.Y -= bPreviewMoveLeft ? 1.0f : 0.0f;
	LocalDirection.Z += bPreviewMoveUp ? 1.0f : 0.0f;
	LocalDirection.Z -= bPreviewMoveDown ? 1.0f : 0.0f;

	if (!LocalDirection.IsNearlyZero())
	{
		PreviewRenderer->MoveCameraLocal(LocalDirection, InDeltaTime);
	}
}

void UMMDToolPanelWidget::SetStatusText(const FText& InText, const FLinearColor& InColor)
{
	if (TxtStatus)
	{
		TxtStatus->SetText(InText);
		TxtStatus->SetColorAndOpacity(FSlateColor(InColor));
	}

	OnStatusChanged(InText, InColor);
}

void UMMDToolPanelWidget::SetSelectedModelText(const FText& InText)
{
	if (TxtSelectedModel)
	{
		TxtSelectedModel->SetText(InText);
	}
}

void UMMDToolPanelWidget::SetSelectedAnimText(const FText& InText)
{
	if (TxtSelectedAnim)
	{
		TxtSelectedAnim->SetText(InText);
	}
}

void UMMDToolPanelWidget::SetBakeProgress(float InProgress)
{
	const float ClampedProgress = FMath::Clamp(InProgress, 0.0f, 1.0f);
	if (ProgressBake)
	{
		ProgressBake->SetPercent(ClampedProgress);
	}

	OnBakeProgressChanged(ClampedProgress);
}

void UMMDToolPanelWidget::RequestAppendFacialVMD()
{
	if (OnAppendFacialVMDRequested)
	{
		OnAppendFacialVMDRequested();
	}
}

float UMMDToolPanelWidget::GetBakeFrameRate() const
{
	return SpinBakeFrameRate ? SpinBakeFrameRate->GetValue() : 30.0f;
}

int32 UMMDToolPanelWidget::GetWarmupFrames() const
{
	return SpinWarmupFrames ? FMath::RoundToInt32(SpinWarmupFrames->GetValue()) : 0;
}

UTextureRenderTarget2D* UMMDToolPanelWidget::GetPreviewRenderTarget() const
{
	return PreviewRenderer ? PreviewRenderer->GetRenderTarget() : nullptr;
}

bool UMMDToolPanelWidget::SetPreviewActorClass(UClass* ActorClass)
{
	InitializePreviewRenderTarget();
	return PreviewRenderer ? PreviewRenderer->SetPreviewActorClass(ActorClass) : false;
}

bool UMMDToolPanelWidget::SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	InitializePreviewRenderTarget();
	return PreviewRenderer ? PreviewRenderer->SetPreviewSkeletalMesh(SkeletalMesh) : false;
}

bool UMMDToolPanelWidget::SetPreviewAnimation(UAnimSequence* AnimSequence)
{
	InitializePreviewRenderTarget();
	return PreviewRenderer ? PreviewRenderer->SetPreviewAnimation(AnimSequence) : false;
}

void UMMDToolPanelWidget::CapturePreview()
{
	if (PreviewRenderer)
	{
		PreviewRenderer->Capture();
	}
}

void UMMDToolPanelWidget::InitializePreviewRenderTarget()
{
	if (!ImgPreviewRenderTarget)
	{
		return;
	}

	if (!PreviewRenderer)
	{
		PreviewRenderer = NewObject<UMMDToolPreviewRenderer>(this, TEXT("MMDToolPreviewRenderer"), RF_Transient);
		PreviewRenderer->Initialize(PreviewRenderTargetWidth, PreviewRenderTargetHeight);
	}

	if (UTextureRenderTarget2D* RenderTarget = PreviewRenderer->GetRenderTarget())
	{
		FSlateBrush PreviewBrush = ImgPreviewRenderTarget->GetBrush();
		PreviewBrush.SetResourceObject(RenderTarget);
		PreviewBrush.ImageSize = FVector2D(RenderTarget->SizeX, RenderTarget->SizeY);
		ImgPreviewRenderTarget->SetBrush(PreviewBrush);
	}
}

bool UMMDToolPanelWidget::IsMouseOverPreviewImage(const FPointerEvent& InMouseEvent) const
{
	return ImgPreviewRenderTarget && ImgPreviewRenderTarget->GetCachedGeometry().IsUnderLocation(InMouseEvent.GetScreenSpacePosition());
}

bool UMMDToolPanelWidget::IsMouseOverPreviewControl(const FPointerEvent& InMouseEvent) const
{
	const FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();
	const TArray<UWidget*> PreviewControls = {
		BtnPreviewPlay.Get(),
		BtnSelectMode.Get(),
		BtnMoveMode.Get(),
		BtnRotateMode.Get(),
		BtnScaleMode.Get()
	};

	for (UWidget* Control : PreviewControls)
	{
		if (Control && Control->GetCachedGeometry().IsUnderLocation(ScreenPosition))
		{
			return true;
		}
	}

	return false;
}

bool UMMDToolPanelWidget::SetPreviewNavigationKeyState(const FKey& Key, bool bIsDown)
{
	if (Key == EKeys::W)
	{
		bPreviewMoveForward = bIsDown;
		return true;
	}
	if (Key == EKeys::S)
	{
		bPreviewMoveBackward = bIsDown;
		return true;
	}
	if (Key == EKeys::A)
	{
		bPreviewMoveLeft = bIsDown;
		return true;
	}
	if (Key == EKeys::D)
	{
		bPreviewMoveRight = bIsDown;
		return true;
	}
	if (Key == EKeys::E)
	{
		bPreviewMoveUp = bIsDown;
		return true;
	}
	if (Key == EKeys::Q)
	{
		bPreviewMoveDown = bIsDown;
		return true;
	}

	return false;
}

void UMMDToolPanelWidget::ResetPreviewNavigationState()
{
	bPreviewMoveForward = false;
	bPreviewMoveBackward = false;
	bPreviewMoveLeft = false;
	bPreviewMoveRight = false;
	bPreviewMoveUp = false;
	bPreviewMoveDown = false;
}

void UMMDToolPanelWidget::HandleImportModelClicked()
{
	if (OnImportModelRequested)
	{
		OnImportModelRequested();
	}
}

void UMMDToolPanelWidget::HandleImportVMDClicked()
{
	if (OnImportVMDRequested)
	{
		OnImportVMDRequested();
	}
}

void UMMDToolPanelWidget::HandleAppendFacialVMDClicked()
{
	RequestAppendFacialVMD();
}

void UMMDToolPanelWidget::HandleImportCameraClicked()
{
	if (OnImportCameraRequested)
	{
		OnImportCameraRequested();
	}
}

void UMMDToolPanelWidget::HandleLoadMMDActorClicked()
{
	if (OnLoadMMDActorRequested)
	{
		OnLoadMMDActorRequested();
	}
}

void UMMDToolPanelWidget::HandleComposeSequenceClicked()
{
	if (OnComposeSequenceRequested)
	{
		OnComposeSequenceRequested();
	}
}

void UMMDToolPanelWidget::HandleBakePhysicsClicked()
{
	if (OnBakePhysicsRequested)
	{
		OnBakePhysicsRequested();
	}
}

void UMMDToolPanelWidget::HandleSelectModeClicked()
{
	if (OnSelectModeRequested)
	{
		OnSelectModeRequested();
	}
}

void UMMDToolPanelWidget::HandleMoveModeClicked()
{
	if (OnMoveModeRequested)
	{
		OnMoveModeRequested();
	}
}

void UMMDToolPanelWidget::HandleRotateModeClicked()
{
	if (OnRotateModeRequested)
	{
		OnRotateModeRequested();
	}
}

void UMMDToolPanelWidget::HandleScaleModeClicked()
{
	if (OnScaleModeRequested)
	{
		OnScaleModeRequested();
	}
}

void UMMDToolPanelWidget::HandlePreviewPlayClicked()
{
	if (OnPreviewPlayRequested)
	{
		OnPreviewPlayRequested();
	}
}
