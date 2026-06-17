#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "UObject/Object.h"
#include "MMDToolPreviewRenderer.generated.h"

class USceneCaptureComponent2D;
class AActor;
class UAnimSequence;
class UDirectionalLightComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkyLightComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

UCLASS()
class UE5MMDTOOLS_API UMMDToolPreviewRenderer : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(int32 InWidth = 1920, int32 InHeight = 1080);
	void Shutdown();

	virtual void BeginDestroy() override;

	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }
	UWorld* GetPreviewWorld() const;

	bool SetPreviewActorClass(UClass* ActorClass);
	bool SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh);
	bool SetPreviewAnimation(UAnimSequence* AnimSequence);
	void Capture();
	void OrbitCamera(const FVector2D& DragDelta);
	void LookCamera(const FVector2D& DragDelta);
	void PanCamera(const FVector2D& DragDelta);
	void ZoomCamera(float WheelDelta);
	void MoveCameraLocal(const FVector& LocalDirection, float DeltaSeconds);
	void DollyCamera(float InputAmount);
	void AdjustCameraMoveSpeed(float WheelDelta);

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PreviewTestMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> KeyLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> FillLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> RimLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkyLightComponent> SkyLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> PreviewSkeletalMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviewActor;

	TUniquePtr<FPreviewScene> PreviewScene;

	FVector CameraTarget = FVector(0.0f, 0.0f, 80.0f);
	float CameraDistance = 360.0f;
	float CameraYaw = -145.0f;
	float CameraPitch = -12.0f;
	int32 CameraSpeedSetting = 4;

	void ClearPreviewActor();
	void ClearPreviewSkeletalMeshComponent();
	void FocusCaptureOnActor(AActor* Actor);
	void FocusCaptureOnBox(const FBox& Bounds);
	void SetupPreviewLighting();
	void UpdateCaptureTransform();
	float GetCameraSpeedScale() const;
	USkeletalMeshComponent* GetPreviewSkeletalMeshComponent() const;
};
