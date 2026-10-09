#include "UI/MMDToolPreviewRenderer.h"

#include "Animation/AnimSequence.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Components/PrimitiveComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "PreviewScene.h"
#include "Rendering/MMDOutlineLibrary.h"

namespace
{
	constexpr float MMDPreviewLookDegreesPerPixel = 0.12f;
	constexpr float MMDPreviewOrbitDegreesPerPixel = 0.12f;
	constexpr float MMDPreviewPanDistanceFactor = 0.0015f;
	constexpr float MMDPreviewBaseFlySpeed = 480.0f;
	constexpr float MMDPreviewWheelDollyUnits = 48.0f;
}

void UMMDToolPreviewRenderer::Initialize(int32 InWidth, int32 InHeight)
{
	if (PreviewScene.IsValid() && RenderTarget)
	{
		return;
	}

	const int32 Width = FMath::Max(InWidth, 16);
	const int32 Height = FMath::Max(InHeight, 16);
	PreviewAspectRatio = static_cast<float>(Width) / static_cast<float>(Height);

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
	SetupPreviewLighting();

	SceneCapture = NewObject<USceneCaptureComponent2D>(this, TEXT("MMDToolPreviewSceneCapture"), RF_Transient);
	SceneCapture->TextureTarget = RenderTarget;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->ProjectionType = ECameraProjectionMode::Perspective;
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->bAlwaysPersistRenderingState = true;
	SceneCapture->FOVAngle = 35.0f;

	if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		PreviewTestMeshComponent = NewObject<UStaticMeshComponent>(this, TEXT("MMDToolPreviewTestMesh"), RF_Transient);
		PreviewTestMeshComponent->SetStaticMesh(CubeMesh);
		PreviewTestMeshComponent->SetMobility(EComponentMobility::Movable);
		PreviewTestMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewScene->AddComponent(PreviewTestMeshComponent, FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 40.0f), FVector(0.8f)));
	}

	UpdateCaptureTransform();
	PreviewScene->AddComponent(SceneCapture, SceneCapture->GetComponentTransform());

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
	if (PreviewScene.IsValid() && BackdropComponent)
	{
		PreviewScene->RemoveComponent(BackdropComponent);
	}
	if (PreviewScene.IsValid() && KeyLightComponent)
	{
		PreviewScene->RemoveComponent(KeyLightComponent);
	}
	ClearPreviewActor();
	ClearPreviewSkeletalMeshComponent();

	SceneCapture = nullptr;
	PreviewTestMeshComponent = nullptr;
	BackdropComponent = nullptr;
	KeyLightComponent = nullptr;
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

	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.ObjectFlags = RF_Transient;
	PreviewSkeletalMeshActor = PreviewScene->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
	PreviewSkeletalMeshComponent = PreviewSkeletalMeshActor
		? NewObject<USkeletalMeshComponent>(PreviewSkeletalMeshActor, TEXT("MMDToolPreviewSkeletalMesh"), RF_Transient) : nullptr;
	if (!PreviewSkeletalMeshComponent)
	{
		return false;
	}
	PreviewSkeletalMeshActor->SetRootComponent(PreviewSkeletalMeshComponent);
	PreviewSkeletalMeshActor->AddInstanceComponent(PreviewSkeletalMeshComponent);

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

	if (!UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(SkelComp, AnimSequence))
	{
		return false;
	}
	Capture();
	return true;
}

void UMMDToolPreviewRenderer::TickPreview(float DeltaSeconds)
{
	if (UWorld* World = GetPreviewWorld())
	{
		if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.0f)
		{
			World->Tick(LEVELTICK_All, FMath::Min(DeltaSeconds, 1.0f / 15.0f));
			World->SendAllEndOfFrameUpdates();
			Capture();
		}
	}
}

void UMMDToolPreviewRenderer::SetPreviewPlaying(bool bPlaying)
{
	if (USkeletalMeshComponent* Component = GetPreviewSkeletalMeshComponent())
	{
		if (UAnimSingleNodeInstance* Instance = Component->GetSingleNodeInstance()) Instance->SetPlaying(bPlaying);
	}
}

bool UMMDToolPreviewRenderer::IsPreviewPlaying() const
{
	const USkeletalMeshComponent* Component = GetPreviewSkeletalMeshComponent();
	const UAnimSingleNodeInstance* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	return Instance && Instance->IsPlaying();
}

void UMMDToolPreviewRenderer::SetPreviewPhysicsEnabled(bool bEnabled)
{
	if (USkeletalMeshComponent* Component = GetPreviewSkeletalMeshComponent())
	{
		if (UMMDPhysicsPlaybackAnimInstance* Instance = Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()))
			Instance->SetPhysicsEnabled(bEnabled);
	}
}

void UMMDToolPreviewRenderer::ResetPreviewPhysics()
{
	if (USkeletalMeshComponent* Component = GetPreviewSkeletalMeshComponent())
	{
		if (UMMDPhysicsPlaybackAnimInstance* Instance = Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()))
			Instance->ResetPhysics();
	}
}

bool UMMDToolPreviewRenderer::SetOutlineEnabled(bool bEnabled)
{
	USkeletalMeshComponent* SkelComp = GetPreviewSkeletalMeshComponent();
	return SkelComp && UMMDOutlineLibrary::ApplyMMDOutline(SkelComp, bEnabled);
}

bool UMMDToolPreviewRenderer::FocusUpperBody()
{
	USkeletalMeshComponent* SkelComp = GetPreviewSkeletalMeshComponent();
	if (!SkelComp)
	{
		return false;
	}
	FBox Bounds = SkelComp->Bounds.GetBox();
	if (!Bounds.IsValid)
	{
		return false;
	}
	Bounds.Min.Z = FMath::Lerp(Bounds.Min.Z, Bounds.Max.Z, 0.42f);
	FocusCaptureOnBox(Bounds);
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

void UMMDToolPreviewRenderer::OrbitCamera(const FVector2D& DragDelta)
{
	CameraYaw += DragDelta.X * MMDPreviewOrbitDegreesPerPixel;
	CameraPitch = FMath::Clamp(CameraPitch + DragDelta.Y * MMDPreviewOrbitDegreesPerPixel, -80.0f, 80.0f);
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::LookCamera(const FVector2D& DragDelta)
{
	if (!SceneCapture)
	{
		return;
	}

	const FVector CameraLocation = SceneCapture->GetComponentLocation();
	CameraYaw += DragDelta.X * MMDPreviewLookDegreesPerPixel;
	CameraPitch = FMath::Clamp(CameraPitch - DragDelta.Y * MMDPreviewLookDegreesPerPixel, -80.0f, 80.0f);

	const FRotator LookRotation(CameraPitch, CameraYaw, 0.0f);
	const FVector Forward = FRotationMatrix(LookRotation).GetUnitAxis(EAxis::X);
	CameraTarget = CameraLocation + Forward * CameraDistance;
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::PanCamera(const FVector2D& DragDelta)
{
	if (!SceneCapture)
	{
		return;
	}

	const FRotationMatrix ViewRotation(SceneCapture->GetComponentRotation());
	const FVector Right = ViewRotation.GetUnitAxis(EAxis::Y);
	const FVector Up = ViewRotation.GetUnitAxis(EAxis::Z);
	const float PanScale = FMath::Max(CameraDistance, 1.0f) * MMDPreviewPanDistanceFactor;
	CameraTarget += (-Right * DragDelta.X + Up * DragDelta.Y) * PanScale;
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::ZoomCamera(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta))
	{
		return;
	}

	CameraDistance = FMath::Clamp(CameraDistance * FMath::Pow(0.85f, WheelDelta), 30.0f, 100000.0f);
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::MoveCameraLocal(const FVector& LocalDirection, float DeltaSeconds)
{
	if (!SceneCapture || LocalDirection.IsNearlyZero() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const FVector ClampedDirection = LocalDirection.GetClampedToMaxSize(1.0f);
	const FRotationMatrix ViewRotation(SceneCapture->GetComponentRotation());
	const FVector Forward = ViewRotation.GetUnitAxis(EAxis::X);
	const FVector Right = ViewRotation.GetUnitAxis(EAxis::Y);
	const FVector Up = FVector::UpVector;
	const float Speed = MMDPreviewBaseFlySpeed * GetCameraSpeedScale();
	const FVector WorldDelta = (Forward * ClampedDirection.X + Right * ClampedDirection.Y + Up * ClampedDirection.Z) * Speed * DeltaSeconds;

	CameraTarget += WorldDelta;
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::DollyCamera(float InputAmount)
{
	if (!SceneCapture || FMath::IsNearlyZero(InputAmount))
	{
		return;
	}

	const FVector Forward = SceneCapture->GetComponentRotation().Vector();
	CameraTarget += Forward * InputAmount * MMDPreviewWheelDollyUnits * GetCameraSpeedScale();
	UpdateCaptureTransform();
	Capture();
}

void UMMDToolPreviewRenderer::AdjustCameraMoveSpeed(float WheelDelta)
{
	if (FMath::IsNearlyZero(WheelDelta))
	{
		return;
	}

	const int32 SpeedStep = WheelDelta > 0.0f ? 1 : -1;
	CameraSpeedSetting = FMath::Clamp(CameraSpeedSetting + SpeedStep, 1, 8);
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
	if (IsValid(PreviewSkeletalMeshActor))
	{
		if (UWorld* World = PreviewSkeletalMeshActor->GetWorld()) World->DestroyActor(PreviewSkeletalMeshActor);
		PreviewSkeletalMeshActor = nullptr;
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

	CameraTarget = Bounds.IsValid ? Bounds.GetCenter() : FVector(0.0f, 0.0f, 80.0f);
	CameraYaw = -145.0f;
	CameraPitch = -12.0f;
	const FVector BoxExtent = Bounds.IsValid ? Bounds.GetExtent() : FVector(120.0f);
	const FRotationMatrix ViewRotation(FRotator(CameraPitch, CameraYaw, 0.0f));
	const FVector Forward = ViewRotation.GetUnitAxis(EAxis::X);
	const FVector Right = ViewRotation.GetUnitAxis(EAxis::Y);
	const FVector Up = ViewRotation.GetUnitAxis(EAxis::Z);
	auto ProjectExtent = [&BoxExtent](const FVector& Axis)
	{
		return FMath::Abs(Axis.X) * BoxExtent.X + FMath::Abs(Axis.Y) * BoxExtent.Y + FMath::Abs(Axis.Z) * BoxExtent.Z;
	};
	const float HalfHorizontalFov = FMath::DegreesToRadians(SceneCapture->FOVAngle * 0.5f);
	const float HorizontalTan = FMath::Max(FMath::Tan(HalfHorizontalFov), 0.01f);
	const float VerticalTan = HorizontalTan / FMath::Max(PreviewAspectRatio, 0.01f);
	const float FramingDistance = FMath::Max(ProjectExtent(Right) / HorizontalTan,
		ProjectExtent(Up) / VerticalTan) + ProjectExtent(Forward);
	CameraDistance = FMath::Max(FramingDistance * 1.12f, 220.0f);

	if (!BackdropComponent)
	{
		if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			BackdropComponent = NewObject<UStaticMeshComponent>(this, TEXT("MMDToolPreviewBackdrop"), RF_Transient);
			BackdropComponent->SetStaticMesh(CubeMesh);
			BackdropComponent->SetMobility(EComponentMobility::Movable);
			BackdropComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			BackdropComponent->SetCastShadow(false);
			PreviewScene->AddComponent(BackdropComponent, FTransform::Identity);
		}
	}
	if (BackdropComponent)
	{
		const float BackdropSize = FMath::Max(BoxExtent.Size() * 8.0f, 1000.0f);
		BackdropComponent->SetWorldTransform(FTransform(
			Forward.Rotation(), CameraTarget + Forward * (ProjectExtent(Forward) + 100.0f),
			FVector(0.01f, BackdropSize / 100.0f, BackdropSize / 100.0f)));
	}
	UpdateCaptureTransform();
}

void UMMDToolPreviewRenderer::SetupPreviewLighting()
{
	if (!PreviewScene.IsValid())
	{
		return;
	}

	// 预览基础照明：只有一盏直射光（无天光/无环境光）。
	// 光照环境的验证走场景切换（UMMDLightingEnvironmentLibrary 的 .umap），
	// 预览视口保持干净，避免自带灯光干扰对目标光照环境观感的判断。
	KeyLightComponent = NewObject<UDirectionalLightComponent>(this, TEXT("MMDToolPreviewKeyLight"), RF_Transient);
	if (KeyLightComponent)
	{
		KeyLightComponent->SetMobility(EComponentMobility::Movable);
		KeyLightComponent->SetIntensity(4.0f);
		KeyLightComponent->SetLightColor(FLinearColor(1.0f, 0.94f, 0.86f));
		PreviewScene->AddComponent(KeyLightComponent, FTransform(FRotator(-38.0f, -35.0f, 0.0f)));
	}
}

void UMMDToolPreviewRenderer::UpdateCaptureTransform()
{
	if (!SceneCapture)
	{
		return;
	}

	const FRotator OrbitRotation(CameraPitch, CameraYaw, 0.0f);
	const FVector Forward = FRotationMatrix(OrbitRotation).GetUnitAxis(EAxis::X);
	const FVector CameraLocation = CameraTarget - Forward * CameraDistance;
	const FRotator CameraRotation = (CameraTarget - CameraLocation).Rotation();
	SceneCapture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);
}

float UMMDToolPreviewRenderer::GetCameraSpeedScale() const
{
	static constexpr float SpeedScales[] = {
		0.125f,
		0.25f,
		0.5f,
		1.0f,
		2.0f,
		4.0f,
		8.0f,
		16.0f
	};

	const int32 Index = FMath::Clamp(CameraSpeedSetting, 1, 8) - 1;
	return SpeedScales[Index];
}

USkeletalMeshComponent* UMMDToolPreviewRenderer::GetPreviewSkeletalMeshComponent() const
{
	if (PreviewActor)
	{
		return PreviewActor->FindComponentByClass<USkeletalMeshComponent>();
	}
	return PreviewSkeletalMeshComponent;
}
