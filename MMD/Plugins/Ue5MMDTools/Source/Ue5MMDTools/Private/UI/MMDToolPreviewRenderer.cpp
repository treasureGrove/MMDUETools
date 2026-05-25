#include "UI/MMDToolPreviewRenderer.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "PreviewScene.h"

void UMMDToolPreviewRenderer::Initialize(int32 InWidth, int32 InHeight)
{
	if (PreviewScene.IsValid() && RenderTarget)
	{
		return;
	}

	const int32 Width = FMath::Max(InWidth, 16);
	const int32 Height = FMath::Max(InHeight, 16);

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MMDToolPreviewRenderTarget"), RF_Transient);
	RenderTarget->RenderTargetFormat = RTF_RGBA8;
	RenderTarget->ClearColor = FLinearColor(0.86f, 0.94f, 0.98f, 1.0f);
	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);

	FPreviewScene::ConstructionValues PreviewSceneValues;
	PreviewSceneValues.SetCreateDefaultLighting(true)
		.SetLightBrightness(4.0f)
		.SetSkyBrightness(1.5f)
		.SetEditor(true);
	PreviewScene = MakeUnique<FPreviewScene>(PreviewSceneValues);

	SceneCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("MMDToolPreviewSceneCapture"), RF_Transient);
	SceneCapture->TextureTarget = RenderTarget;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->bCaptureEveryFrame = true;
	SceneCapture->bCaptureOnMovement = true;
	SceneCapture->FOVAngle = 35.0f;

	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		PreviewTestMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("MMDToolPreviewTestMesh"), RF_Transient);
		PreviewTestMeshComponent->SetStaticMesh(CubeMesh);
		PreviewTestMeshComponent->SetMobility(EComponentMobility::Movable);
		PreviewTestMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewScene->AddComponent(PreviewTestMeshComponent, FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 40.0f), FVector(0.8f)));
	}

	const FVector CameraLocation(-260.0f, -180.0f, 150.0f);
	const FRotator CameraRotation = (FVector(0.0f, 0.0f, 45.0f) - CameraLocation).Rotation();
	PreviewScene->AddComponent(SceneCapture, FTransform(CameraRotation, CameraLocation));

	Capture();
}

void UMMDToolPreviewRenderer::Shutdown()
{
	if (PreviewScene.IsValid() && SceneCapture)
	{
		PreviewScene->RemoveComponent(SceneCapture);
	}
	if (PreviewScene.IsValid() && PreviewTestMeshComponent)
	{
		PreviewScene->RemoveComponent(PreviewTestMeshComponent);
	}

	SceneCapture = nullptr;
	PreviewTestMeshComponent = nullptr;
	PreviewScene.Reset();
	RenderTarget = nullptr;
}

void UMMDToolPreviewRenderer::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

UWorld* UMMDToolPreviewRenderer::GetPreviewWorld() const
{
	return PreviewScene.IsValid() ? PreviewScene->GetWorld() : nullptr;
}

void UMMDToolPreviewRenderer::Capture()
{
	if (SceneCapture)
	{
		SceneCapture->CaptureScene();
	}
}
