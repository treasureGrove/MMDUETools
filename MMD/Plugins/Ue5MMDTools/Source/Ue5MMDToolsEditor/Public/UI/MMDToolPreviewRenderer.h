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
class UStaticMeshComponent;
class UTextureRenderTarget2D;

UCLASS()
class UE5MMDTOOLSEDITOR_API UMMDToolPreviewRenderer : public UObject
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
	void TickPreview(float DeltaSeconds);
	void SetPreviewPlaying(bool bPlaying);
	bool IsPreviewPlaying() const;
	void SetPreviewPhysicsEnabled(bool bEnabled);
	void ResetPreviewPhysics();
	bool SetOutlineEnabled(bool bEnabled);
	bool FocusUpperBody();
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
	TObjectPtr<UStaticMeshComponent> BackdropComponent;

	UPROPERTY(Transient)
	TObjectPtr<UDirectionalLightComponent> KeyLightComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> PreviewSkeletalMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviewSkeletalMeshActor;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviewActor;

	TUniquePtr<FPreviewScene> PreviewScene;

	FVector CameraTarget = FVector(0.0f, 0.0f, 80.0f);
	float CameraDistance = 360.0f;
	float CameraYaw = -145.0f;
	float CameraPitch = -12.0f;
	float PreviewAspectRatio = 16.0f / 9.0f;
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
