#include "UI/MMDToolPanelWidget.h"

#include "UI/MMDToolPreviewRenderer.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Engine/TextureRenderTarget2D.h"

void UMMDToolPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (BtnImportModel)
	{
		BtnImportModel->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleImportModelClicked);
	}
	if (BtnImportVMD)
	{
		BtnImportVMD->OnClicked.AddDynamic(this, &UMMDToolPanelWidget::HandleImportVMDClicked);
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
	InitializePreviewRenderTarget();
}

void UMMDToolPanelWidget::NativeDestruct()
{
	if (PreviewRenderer)
	{
		PreviewRenderer->Shutdown();
		PreviewRenderer = nullptr;
	}

	Super::NativeDestruct();
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
