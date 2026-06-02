#include "UI/MMDToolPreviewRenderer.h"

#include "Animation/AnimSequence.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
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
	ClearPreviewActor();
	ClearPreviewSkeletalMeshComponent();

	SceneCapture = nullptr;
	PreviewTestMeshComponent = nullptr;
	PreviewSkeletalMeshComponent = nullptr;
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

bool UMMDToolPreviewRenderer::SetPreviewActorClass(UClass* ActorClass)
{
	if (!PreviewScene.IsValid() || !ActorClass || !ActorClass->IsChildOf<AActor>())
	{
		return false;
	}

	UWorld* PreviewWorld = PreviewScene->GetWorld();
	if (!PreviewWorld)
	{
		return false;
	}

	if (PreviewTestMeshComponent)
	{
		PreviewScene->RemoveComponent(PreviewTestMeshComponent);
		PreviewTestMeshComponent = nullptr;
	}
	ClearPreviewActor();
	ClearPreviewSkeletalMeshComponent();

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;

	PreviewActor = PreviewWorld->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
	if (!PreviewActor)
	{
		return false;
	}

	TArray<UPrimitiveComponent*> PrimitiveComponents;
	PreviewActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (Component)
		{
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->MarkRenderStateDirty();
		}
	}

	FocusCaptureOnActor(PreviewActor);
	Capture();
	return true;
}

bool UMMDToolPreviewRenderer::SetPreviewSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	if (!PreviewScene.IsValid() || !SkeletalMesh)
	{
		return false;
	}

	if (PreviewTestMeshComponent)
	{
		PreviewScene->RemoveComponent(PreviewTestMeshComponent);
		PreviewTestMeshComponent = nullptr;
	}
	ClearPreviewActor();
	ClearPreviewSkeletalMeshComponent();

	PreviewSkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, TEXT("MMDToolPreviewSkeletalMesh"), RF_Transient);
	if (!PreviewSkeletalMeshComponent)
	{
		return false;
	}

	PreviewSkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
	PreviewSkeletalMeshComponent->SetMobility(EComponentMobility::Movable);
	PreviewSkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewSkeletalMeshComponent->SetVisibility(true);
	PreviewScene->AddComponent(PreviewSkeletalMeshComponent, FTransform::Identity);

	const FBoxSphereBounds MeshBounds = SkeletalMesh->GetBounds();
	FocusCaptureOnBox(FBox::BuildAABB(MeshBounds.Origin, MeshBounds.BoxExtent));
	Capture();
	return true;
}

bool UMMDToolPreviewRenderer::SetPreviewAnimation(UAnimSequence* AnimSequence)
{
	USkeletalMeshComponent* SkelComp = GetPreviewSkeletalMeshComponent();
	if (!SkelComp || !AnimSequence)
	{
		return false;
	}

	SkelComp->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SkelComp->SetAnimation(AnimSequence);
	SkelComp->Play(true);
	Capture();
	return true;
}

void UMMDToolPreviewRenderer::Capture()
{
	if (SceneCapture)
	{
		SceneCapture->CaptureScene();
	}
}

void UMMDToolPreviewRenderer::ClearPreviewActor()
{
	if (PreviewActor)
	{
		if (UWorld* PreviewWorld = PreviewActor->GetWorld())
		{
			PreviewWorld->DestroyActor(PreviewActor);
		}
		PreviewActor = nullptr;
	}
}

void UMMDToolPreviewRenderer::ClearPreviewSkeletalMeshComponent()
{
	if (PreviewSkeletalMeshComponent)
	{
		if (PreviewScene.IsValid())
		{
			PreviewScene->RemoveComponent(PreviewSkeletalMeshComponent);
		}
		PreviewSkeletalMeshComponent->DestroyComponent();
		PreviewSkeletalMeshComponent = nullptr;
	}
}

void UMMDToolPreviewRenderer::FocusCaptureOnActor(AActor* Actor)
{
	if (!SceneCapture || !Actor)
	{
		return;
	}

	const FBox Bounds = Actor->GetComponentsBoundingBox(true);
	FocusCaptureOnBox(Bounds);
}

void UMMDToolPreviewRenderer::FocusCaptureOnBox(const FBox& Bounds)
{
	if (!SceneCapture)
	{
		return;
	}

	const FVector Center = Bounds.IsValid ? Bounds.GetCenter() : FVector(0.0f, 0.0f, 80.0f);
	const float Radius = Bounds.IsValid ? Bounds.GetExtent().Size() : 120.0f;
	const float Distance = FMath::Max(Radius * 2.4f, 220.0f);
	const FVector CameraLocation = Center + FVector(-Distance, -Distance * 0.65f, Distance * 0.55f);
	const FRotator CameraRotation = (Center - CameraLocation).Rotation();

	SceneCapture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
}

USkeletalMeshComponent* UMMDToolPreviewRenderer::GetPreviewSkeletalMeshComponent() const
{
	if (PreviewActor)
	{
		return PreviewActor->FindComponentByClass<USkeletalMeshComponent>();
	}
	return PreviewSkeletalMeshComponent;
}
