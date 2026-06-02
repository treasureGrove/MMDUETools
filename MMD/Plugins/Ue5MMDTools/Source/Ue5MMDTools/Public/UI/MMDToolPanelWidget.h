#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MMDToolPanelWidget.generated.h"

class UButton;
class UImage;
class UAnimSequence;
class UMMDToolPreviewRenderer;
class UProgressBar;
class USkeletalMesh;
class USpinBox;
class UTextBlock;
class UTextureRenderTarget2D;

UCLASS(Abstract, Blueprintable)
class UE5MMDTOOLS_API UMMDToolPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> OnImportModelRequested;
	TFunction<void()> OnImportVMDRequested;
	TFunction<void()> OnImportCameraRequested;
	TFunction<void()> OnLoadMMDActorRequested;
	TFunction<void()> OnComposeSequenceRequested;
	TFunction<void()> OnBakePhysicsRequested;
	TFunction<void()> OnSelectModeRequested;
	TFunction<void()> OnMoveModeRequested;
	TFunction<void()> OnRotateModeRequested;
	TFunction<void()> OnScaleModeRequested;
	TFunction<void()> OnPreviewPlayRequested;

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION(BlueprintCallable, Category = "MMD Tools")
	void SetStatusText(const FText& InText, const FLinearColor& InColor);

	UFUNCTION(BlueprintCallable, Category = "MMD Tools")
	void SetSelectedModelText(const FText& InText);

	UFUNCTION(BlueprintCallable, Category = "MMD Tools")
	void SetSelectedAnimText(const FText& InText);

	UFUNCTION(BlueprintCallable, Category = "MMD Tools")
	void SetBakeProgress(float InProgress);

	UFUNCTION(BlueprintPure, Category = "MMD Tools")
	float GetBakeFrameRate() const;

	UFUNCTION(BlueprintPure, Category = "MMD Tools")
	int32 GetWarmupFrames() const;

	UFUNCTION(BlueprintPure, Category = "MMD Tools|Preview")
	UTextureRenderTarget2D* GetPreviewRenderTarget() const;

	bool SetPreviewActorClass(UClass* ActorClass);
	bool SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh);
	bool SetPreviewAnimation(UAnimSequence* AnimSequence);
	void CapturePreview();

protected:
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Header")
	TObjectPtr<UTextBlock> TxtStatus;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Selection")
	TObjectPtr<UTextBlock> TxtSelectedModel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Selection")
	TObjectPtr<UTextBlock> TxtSelectedAnim;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Bake")
	TObjectPtr<UProgressBar> ProgressBake;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Bake")
	TObjectPtr<USpinBox> SpinBakeFrameRate;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Bake")
	TObjectPtr<USpinBox> SpinWarmupFrames;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnImportModel;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnImportVMD;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnImportCamera;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnLoadMMDActor;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnComposeSequence;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Actions")
	TObjectPtr<UButton> BtnBakePhysics;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Viewport")
	TObjectPtr<UButton> BtnSelectMode;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Viewport")
	TObjectPtr<UButton> BtnMoveMode;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Viewport")
	TObjectPtr<UButton> BtnRotateMode;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Viewport")
	TObjectPtr<UButton> BtnScaleMode;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Preview")
	TObjectPtr<UButton> BtnPreviewPlay;

	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "MMD Tools|Preview")
	TObjectPtr<UImage> ImgPreviewRenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Tools|Preview", meta = (ClampMin = "16"))
	int32 PreviewRenderTargetWidth = 1920;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Tools|Preview", meta = (ClampMin = "16"))
	int32 PreviewRenderTargetHeight = 1080;

	UFUNCTION(BlueprintImplementableEvent, Category = "MMD Tools")
	void OnStatusChanged(const FText& InText, const FLinearColor& InColor);

	UFUNCTION(BlueprintImplementableEvent, Category = "MMD Tools")
	void OnBakeProgressChanged(float InProgress);

private:
	UPROPERTY(Transient)
	TObjectPtr<UMMDToolPreviewRenderer> PreviewRenderer;

	void InitializePreviewRenderTarget();

	UFUNCTION()
	void HandleImportModelClicked();

	UFUNCTION()
	void HandleImportVMDClicked();

	UFUNCTION()
	void HandleImportCameraClicked();

	UFUNCTION()
	void HandleLoadMMDActorClicked();

	UFUNCTION()
	void HandleComposeSequenceClicked();

	UFUNCTION()
	void HandleBakePhysicsClicked();

	UFUNCTION()
	void HandleSelectModeClicked();

	UFUNCTION()
	void HandleMoveModeClicked();

	UFUNCTION()
	void HandleRotateModeClicked();

	UFUNCTION()
	void HandleScaleModeClicked();

	UFUNCTION()
	void HandlePreviewPlayClicked();
};
