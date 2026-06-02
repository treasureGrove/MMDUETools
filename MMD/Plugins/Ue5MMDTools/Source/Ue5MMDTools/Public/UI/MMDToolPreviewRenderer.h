#pragma once

#include "CoreMinimal.h"
#include "PreviewScene.h"
#include "UObject/Object.h"
#include "MMDToolPreviewRenderer.generated.h"

class USceneCaptureComponent2D;
class AActor;
class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
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

private:
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> PreviewTestMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> PreviewSkeletalMeshComponent;

	UPROPERTY(Transient)
	TObjectPtr<AActor> PreviewActor;

	TUniquePtr<FPreviewScene> PreviewScene;

	void ClearPreviewActor();
	void ClearPreviewSkeletalMeshComponent();
	void FocusCaptureOnActor(AActor* Actor);
	void FocusCaptureOnBox(const FBox& Bounds);
	USkeletalMeshComponent* GetPreviewSkeletalMeshComponent() const;
};
