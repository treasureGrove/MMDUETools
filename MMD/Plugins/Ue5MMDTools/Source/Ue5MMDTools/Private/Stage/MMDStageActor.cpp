#include "Stage/MMDStageActor.h"

#include "Components/BoxComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Stage/MMDStageLibrary.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName GeneratedTag(TEXT("MMDGeneratedStage"));
	const TCHAR* FloorPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageFloor.M_MMD_StageFloor");
	const TCHAR* PanelPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StagePanel.M_MMD_StagePanel");
	const TCHAR* GlowPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageAccent.M_MMD_StageAccent");
	const TCHAR* BeamPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageBeam.M_MMD_StageBeam");
	const TCHAR* BackdropPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageBackdrop.M_MMD_StageBackdrop");
	const TCHAR* GardenSurfacePath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_GardenSurface.M_MMD_GardenSurface");
	const TCHAR* GardenWaterPath = TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_GardenWater.M_MMD_GardenWater");
	const TCHAR* GardenSkyPath = TEXT("/Ue5MMDTools/Stages/Textures/T_Garden_NightSky.T_Garden_NightSky");
	const FVector GardenPavilionLocation(145, 545, 0);
	const FRotator GardenPavilionRotation(0, -8, 0);
	const FVector GardenPavilionScale(0.74f, 0.74f, 0.62f);
	const FVector GardenLanternPositions[] = {
		FVector(-270, -25, -12), FVector(280, 70, -12), FVector(315, 335, -8),
		FVector(-330, 430, -12), FVector(340, 780, -10), FVector(-470, 725, -12),
		FVector(-245, 875, -12), FVector(-560, 1090, -12)
	};
	const float GardenLanternSizes[] = { 0.55f, 0.53f, 0.48f, 0.48f, 0.40f, 0.40f, 0.36f, 0.32f };
	FVector GardenPavilionPoint(const FVector& Local)
	{
		return GardenPavilionLocation + GardenPavilionRotation.RotateVector(Local * GardenPavilionScale);
	}
	constexpr double StageTimePeriod = 21600.0;
	float SafeScalar(float Value, float DefaultValue, float Minimum, float Maximum)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : DefaultValue;
	}
	FRotator SafeLightRotation(const FRotator& Rotation, const FRotator& DefaultRotation)
	{
		const double Pitch = FMath::IsFinite(Rotation.Pitch) ? Rotation.Pitch : DefaultRotation.Pitch;
		const double Yaw = FMath::IsFinite(Rotation.Yaw) ? Rotation.Yaw : DefaultRotation.Yaw;
		const double Roll = FMath::IsFinite(Rotation.Roll) ? Rotation.Roll : DefaultRotation.Roll;
		return FRotator(FMath::Clamp(FRotator::NormalizeAxis(Pitch), -89.0, 89.0),
			FRotator::NormalizeAxis(Yaw), FRotator::NormalizeAxis(Roll));
	}

	FLinearColor ThemeBase(EMMDStageTheme Theme)
	{
		return Theme == EMMDStageTheme::MoonMirror ? FLinearColor(0.050f, 0.078f, 0.145f)
			: Theme == EMMDStageTheme::SakuraTheater ? FLinearColor(0.135f, 0.049f, 0.068f)
			: FLinearColor(0.039f, 0.056f, 0.098f);
	}
	FLinearColor ThemeAccent(EMMDStageTheme Theme)
	{
		return Theme == EMMDStageTheme::MoonMirror ? FLinearColor(0.64f, 0.80f, 1.0f)
			: Theme == EMMDStageTheme::SakuraTheater ? FLinearColor(1.0f, 0.46f, 0.63f)
			: FLinearColor(0.045f, 0.82f, 1.0f);
	}
	FLinearColor ThemeSecondary(EMMDStageTheme Theme)
	{
		return Theme == EMMDStageTheme::MoonMirror ? FLinearColor(0.88f, 0.76f, 0.48f)
			: Theme == EMMDStageTheme::SakuraTheater ? FLinearColor(0.94f, 0.65f, 0.30f)
			: FLinearColor(0.70f, 0.10f, 0.46f);
	}

	void UpdateSurfaceBasis(UMaterialInstanceDynamic* Material, const FTransform& Transform)
	{
		if (!Material) return;
		const FVector Scale = Transform.GetScale3D();
		auto Axis = [&Transform](EAxis::Type Index, double AxisScale)
		{
			const double SafeScale = FMath::Abs(AxisScale) > 0.0001 ? AxisScale : 1.0;
			return Transform.GetUnitAxis(Index) / SafeScale;
		};
		Material->SetVectorParameterValue(TEXT("StageOrigin"), FLinearColor(Transform.GetLocation()));
		Material->SetVectorParameterValue(TEXT("StageRight"), FLinearColor(Axis(EAxis::X, Scale.X)));
		Material->SetVectorParameterValue(TEXT("StageForward"), FLinearColor(Axis(EAxis::Y, Scale.Y)));
		Material->SetVectorParameterValue(TEXT("StageUp"), FLinearColor(Axis(EAxis::Z, Scale.Z)));
	}
}

AMMDStageActor::AMMDStageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	StageRoot = CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
	SetRootComponent(StageRoot);
	StageRoot->SetMobility(EComponentMobility::Movable);
	KeyLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("KeyLight"));
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
	RimLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("RimLight"));
	for (UDirectionalLightComponent* Light : { KeyLight.Get(), FillLight.Get(), RimLight.Get() })
	{
		Light->SetupAttachment(StageRoot);
		Light->SetMobility(EComponentMobility::Movable);
	}
	KeyLight->SetRelativeRotation(FRotator(-38.0f, 40.0f, 0.0f));
	FillLight->SetRelativeRotation(FRotator(-20.0f, 140.0f, 0.0f));
	RimLight->SetRelativeRotation(FRotator(-12.0f, -90.0f, 0.0f));
	KeyLight->SetCastShadows(true);
	KeyLight->ContactShadowLength = 0.08f;
	KeyLight->ContactShadowLengthInWS = false;
	FillLight->SetCastShadows(false);
	RimLight->SetCastShadows(false);
	GardenSkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("GardenSkyLight"));
	GardenSkyLight->SetupAttachment(StageRoot);
	GardenSkyLight->SetMobility(EComponentMobility::Movable);
	GardenSkyLight->SourceType = SLS_SpecifiedCubemap;
	GardenSkyLight->SetRealTimeCaptureEnabled(false);
	GardenSkyLight->SetCastShadows(false);
	GardenSkyLight->bLowerHemisphereIsBlack = true;
	GardenSkyLight->SetLowerHemisphereColor(FLinearColor(0.016f, 0.023f, 0.033f));
	GardenSkyLight->SetIntensity(0.0f);
	GardenSkyLight->SetVisibility(false);
	PostProcessBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("PostProcessBounds"));
	PostProcessBounds->SetupAttachment(StageRoot);
	PostProcessBounds->SetMobility(EComponentMobility::Movable);
	PostProcessBounds->SetBoxExtent(FVector(1400.0f, 1600.0f, 1100.0f));
	PostProcessBounds->SetRelativeLocation(FVector(0.0f, 0.0f, 350.0f));
	PostProcessBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PostProcessBounds->SetHiddenInGame(true);
	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("StagePostProcess"));
	PostProcess->SetupAttachment(PostProcessBounds);
	PostProcess->SetMobility(EComponentMobility::Movable);
	PostProcess->bUnbound = false;
	PostProcess->BlendRadius = 180.0f;
	PostProcess->BlendWeight = 1.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UTextureCube> SkyFallback(TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StageFloor(FloorPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StagePanel(PanelPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StageGlow(GlowPath);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CharacterOpaque(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageToon_Opaque.M_MMD_StageToon_Opaque"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CharacterMasked(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageToon_Masked.M_MMD_StageToon_Masked"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CharacterTranslucent(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageToon_Translucent.M_MMD_StageToon_Translucent"));
	CubeMesh = Cube.Object;
	CylinderMesh = Cylinder.Object;
	SphereMesh = Sphere.Object;
	ConeMesh = Cone.Object;
	FallbackSkyCubemap = SkyFallback.Object;
	DefaultSurfaceMaterial = StageFloor.Succeeded() ? StageFloor.Object.Get() : DefaultMaterial.Object.Get();
	DefaultPanelMaterial = StagePanel.Succeeded() ? StagePanel.Object.Get() : DefaultMaterial.Object.Get();
	DefaultGlowMaterial = StageGlow.Succeeded() ? StageGlow.Object.Get() : DefaultMaterial.Object.Get();
	DefaultCharacterMaterialOpaque = CharacterOpaque.Object;
	DefaultCharacterMaterialMasked = CharacterMasked.Object;
	DefaultCharacterMaterialTranslucent = CharacterTranslucent.Object;
	LoadGardenMeshReferences();
	// 首次后台生成新材质前允许 Beam 缺席，分发资产存在时会成为 CDO 的硬引用。
	if (FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageBeam")))
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> StageBeam(BeamPath);
		DefaultBeamMaterial = StageBeam.Object;
	}
	if (FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageBackdrop")))
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> StageBackdrop(BackdropPath);
		DefaultBackdropMaterial = StageBackdrop.Object;
	}
	if (FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_GardenSurface")))
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> GardenSurface(GardenSurfacePath);
		DefaultGardenSurfaceMaterial = GardenSurface.Object;
	}
	if (FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_GardenWater")))
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> GardenWater(GardenWaterPath);
		DefaultGardenWaterMaterial = GardenWater.Object;
	}
	if (FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Textures/T_Garden_NightSky")))
	{
		ConstructorHelpers::FObjectFinder<UTextureCube> GardenSky(GardenSkyPath);
		GardenSkyCubemap = GardenSky.Object;
	}
	ConfigureLighting();
}

void AMMDStageActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildStage();
}

void AMMDStageActor::BeginPlay()
{
	Super::BeginPlay();
	if (StageMeshes.IsEmpty()) RebuildStage();
	ElapsedSeconds = 0.0f;
	EvaluateStageAtTime(bUseManualTime ? ManualTimeSeconds : 0.0f);
}

void AMMDStageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bUseManualTime)
	{
		EvaluateStageAtTime(ManualTimeSeconds);
		return;
	}
	if (!bAnimate || !FMath::IsFinite(DeltaSeconds))
	{
		for (UMaterialInstanceDynamic* Material : StageMaterials) UpdateSurfaceBasis(Material, GetActorTransform());
		return;
	}
	ElapsedSeconds = FMath::Fmod(ElapsedSeconds + static_cast<double>(FMath::Max(DeltaSeconds, 0.0f)), StageTimePeriod);
	EvaluateStageAtTime(static_cast<float>(ElapsedSeconds));
}

void AMMDStageActor::ClearGeneratedComponents()
{
	// 构造脚本可重入：只清理本系统创建的组件，保留用户添加的组件。
	TInlineComponentArray<UActorComponent*> Components(this);
	for (UActorComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(GeneratedTag))
		{
			RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
	StageMeshes.Reset();
	StageMaterials.Reset();
	MaterialEmissions.Reset();
	AccentLights.Reset();
	AccentLightScales.Reset();
	AccentLightOverrides.Reset();
	AnimatedInstances.Reset();
}

UInstancedStaticMeshComponent* AMMDStageActor::MakeGroup(const TCHAR* Name, UStaticMesh* Mesh,
	const FLinearColor& Color, bool bGlow, float Emission, bool bPanel)
{
	if (!Mesh) return nullptr;
	UInstancedStaticMeshComponent* Group = NewObject<UInstancedStaticMeshComponent>(this,
		MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), FName(Name)), RF_Transient);
	Group->ComponentTags.Add(GeneratedTag);
	Group->SetMobility(EComponentMobility::Movable);
	Group->SetupAttachment(StageRoot);
	Group->SetStaticMesh(Mesh);
	Group->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Group->SetCastShadow(!bGlow);
	Group->SetCastContactShadow(!bGlow);
	Group->bAffectDistanceFieldLighting = !bGlow;
	Group->SetCanEverAffectNavigation(false);
	UMaterialInterface* Parent = RenderPreset ? (bGlow ? RenderPreset->GlowMaterial.Get()
		: bPanel ? RenderPreset->PanelMaterial.Get() : RenderPreset->SurfaceMaterial.Get()) : nullptr;
	if (Theme == EMMDStageTheme::SakuraTheater && !bGlow)
	{
		const bool bWater = FCString::Strstr(Name, TEXT("GardenWater")) || FCString::Strstr(Name, TEXT("WaterRipples"));
		TObjectPtr<UMaterialInterface>& GardenParent = bWater ? DefaultGardenWaterMaterial : DefaultGardenSurfaceMaterial;
		if (!GardenParent)
			GardenParent = LoadObject<UMaterialInterface>(nullptr, bWater ? GardenWaterPath : GardenSurfacePath, nullptr, LOAD_NoWarn);
		if (GardenParent) Parent = GardenParent;
	}
	if (!Parent) Parent = LoadObject<UMaterialInterface>(nullptr, bGlow ? GlowPath : bPanel ? PanelPath : FloorPath);
	if (!Parent) Parent = bGlow ? DefaultGlowMaterial.Get() : bPanel ? DefaultPanelMaterial.Get() : DefaultSurfaceMaterial.Get();
	if (Parent)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
		FLinearColor SurfaceColor = Color;
		const bool bMoon = Theme == EMMDStageTheme::MoonMirror;
		const bool bSakura = Theme == EMMDStageTheme::SakuraTheater;
		if (!bGlow)
		{
			// 低照度舞台仍保留结构底色，按亮度缩放不改变预设的色相。
			const float MinimumLuminance = bMoon ? 0.075f : bSakura ? 0.070f : 0.055f;
			const float Luminance = SurfaceColor.R * 0.2126f + SurfaceColor.G * 0.7152f + SurfaceColor.B * 0.0722f;
			if (Luminance > 0.0001f && Luminance < MinimumLuminance)
			{
				const float Gain = MinimumLuminance / Luminance;
				SurfaceColor.R *= Gain;
				SurfaceColor.G *= Gain;
				SurfaceColor.B *= Gain;
			}
		}
		const float SurfaceEmission = bGlow ? Emission : bPanel ? 0.08f : 0.03f;
		Material->SetVectorParameterValue(TEXT("StageColor"), SurfaceColor);
		Material->SetVectorParameterValue(TEXT("BaseColor"), SurfaceColor);
		Material->SetScalarParameterValue(TEXT("EmissiveStrength"), SurfaceEmission);
		Material->SetScalarParameterValue(TEXT("Roughness"), bMoon ? 0.22f : bSakura ? 0.64f : 0.40f);
		Material->SetScalarParameterValue(TEXT("Metallic"), bSakura ? 0.04f : bMoon ? 0.30f : 0.20f);
		Material->SetScalarParameterValue(TEXT("PulseAmount"), 0.0f);
		if (bPanel && FCString::Strstr(Name, TEXT("LED")))
		{
			Material->SetScalarParameterValue(TEXT("PulseAmount"), 0.32f);
			Material->SetScalarParameterValue(TEXT("PatternScale"), 0.025f);
			Material->SetScalarParameterValue(TEXT("PulseSpeed"), 0.32f);
		}
		UpdateSurfaceBasis(Material, GetActorTransform());
		Group->SetMaterial(0, Material);
		StageMaterials.Add(Material);
		MaterialEmissions.Add(bGlow ? SurfaceEmission : 0.0f);
	}
	AddInstanceComponent(Group);
	Group->RegisterComponent();
	StageMeshes.Add(Group);
	return Group;
}

UInstancedStaticMeshComponent* AMMDStageActor::MakeBeamGroup(const TCHAR* Name, const FLinearColor& Color)
{
	UMaterialInterface* Parent = RenderPreset ? RenderPreset->BeamMaterial.Get() : nullptr;
	if (!Parent) Parent = DefaultBeamMaterial;
	if (!Parent) Parent = LoadObject<UMaterialInterface>(nullptr, BeamPath, nullptr, LOAD_NoWarn);
	if (!Parent) return nullptr;
	UInstancedStaticMeshComponent* Group = MakeGroup(Name, ConeMesh, Color, true, 3.0f);
	if (!Group) return nullptr;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetVectorParameterValue(TEXT("StageColor"), Color);
	Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 3.0f);
	Material->SetScalarParameterValue(TEXT("Opacity"), 0.055f);
	Material->SetScalarParameterValue(TEXT("DepthFadeDistance"), 100.0f);
	Group->SetMaterial(0, Material);
	if (!StageMaterials.IsEmpty()) StageMaterials.Last() = Material;
	return Group;
}

void AMMDStageActor::AddInstance(UInstancedStaticMeshComponent* Group, const FVector& Position,
	const FVector& Scale, const FRotator& Rotation)
{
	if (Group) Group->AddInstance(FTransform(Rotation, Position, Scale));
}

void AMMDStageActor::AddAnimated(UInstancedStaticMeshComponent* Group, const FVector& Position,
	const FVector& Scale, EMotion Motion, float Phase, const FRotator& Rotation)
{
	if (!Group) return;
	FAnimatedInstance Item;
	Item.Component = Group;
	Item.BaseTransform = FTransform(Rotation, Position, Scale);
	Item.Index = Group->AddInstance(Item.BaseTransform);
	Item.Motion = Motion;
	Item.Phase = Phase;
	AnimatedInstances.Add(Item);
}

void AMMDStageActor::AddRing(UInstancedStaticMeshComponent* Group, const FVector& Center, float Radius,
	float Width, bool bVertical, int32 Segments, EMotion Motion, bool bAnimated)
{
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float Angle = TWO_PI * Index / Segments;
		const FVector Position = Center + (bVertical ? FVector(FMath::Cos(Angle), 0.0f, FMath::Sin(Angle))
			: FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f)) * Radius;
		const float Length = TWO_PI * Radius / Segments * 0.91f;
		const FRotator Rotation = bVertical ? FRotator(FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f, 0.0f)
			: FRotator(0.0f, FMath::RadiansToDegrees(Angle) + 90.0f, 0.0f);
		const FVector Scale(Length / 100.0f, Width / 100.0f, Width / 100.0f);
		if (bAnimated) AddAnimated(Group, Position, Scale, Motion, Angle, Rotation);
		else AddInstance(Group, Position, Scale, Rotation);
	}
}

void AMMDStageActor::AddArc(UInstancedStaticMeshComponent* Group, const FVector& Center, float Radius,
	float Width, float StartAngleDegrees, float EndAngleDegrees, int32 Segments, float Depth)
{
	if (!Group || Segments <= 0) return;
	const float Start = FMath::DegreesToRadians(StartAngleDegrees);
	const float Range = FMath::DegreesToRadians(EndAngleDegrees - StartAngleDegrees);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float Angle = Start + Range * (Index + 0.5f) / Segments;
		const FVector Position = Center + FVector(FMath::Cos(Angle), 0.0f, FMath::Sin(Angle)) * Radius;
		AddInstance(Group, Position, FVector(FMath::Abs(Range) * Radius / Segments * 1.03f / 100.0f,
			Depth / 100.0f, Width / 100.0f), FRotator(FMath::RadiansToDegrees(Angle) + 90.0f, 0, 0));
	}
}

void AMMDStageActor::AddCylinderSegment(UInstancedStaticMeshComponent* Group,
	const FVector& Start, const FVector& End, float Radius)
{
	const FVector Direction = End - Start;
	const double Length = Direction.Length();
	if (Length < 0.001) return;
	AddInstance(Group, (Start + End) * 0.5, FVector(Radius / 50.0f, Radius / 50.0f, Length / 100.0),
		FQuat::FindBetweenNormals(FVector::UpVector, Direction / Length).Rotator());
}

void AMMDStageActor::BuildNeonConcert(const FLinearColor& Base, const FLinearColor& Accent,
	const FLinearColor& Secondary)
{
	const FLinearColor Steel = Base * 1.6f + Accent * 0.035f;
	UInstancedStaticMeshComponent* Floor = MakeGroup(TEXT("ConcertFloor"), CylinderMesh, Base, false);
	AddInstance(Floor, FVector(0, 55, -17), FVector(12.4f, 9.7f, 0.34f));
	AddInstance(Floor, FVector(0, 55, -36), FVector(12.8f, 10.1f, 0.10f));
	UInstancedStaticMeshComponent* Deck = MakeGroup(TEXT("ConcertDeckAndSteps"), CubeMesh, Steel, false);
	UInstancedStaticMeshComponent* Structure = MakeGroup(TEXT("ConcertArchitecturalRibs"), CubeMesh, Steel * 1.5f, false);
	UInstancedStaticMeshComponent* Panels = MakeGroup(TEXT("ConcertRecessedWallBays"), CubeMesh, Base * 1.9f, false, 1, true);
	UInstancedStaticMeshComponent* Truss = MakeGroup(TEXT("ConcertTubularTruss"), CylinderMesh, Steel * 1.5f, false);
	UInstancedStaticMeshComponent* Cyan = MakeGroup(TEXT("ConcertCyanLight"), CubeMesh, Accent, true, 1.5f);
	UInstancedStaticMeshComponent* Pink = MakeGroup(TEXT("ConcertPinkLight"), CubeMesh, Secondary, true, 1.25f);

	// 后墙、门架和吊装桁架各占一层，中央表演区保持开敞。
	for (int32 Index = 0; Index < 4; ++Index)
	{
		AddInstance(Deck, FVector(0, -445 - Index * 27, -8 - Index * 8), FVector(7.6f + Index * 0.6f, 0.40f, 0.16f));
		AddInstance(Deck, FVector(0, 322 + Index * 28, -5 + Index * 5), FVector(7.6f - Index * 0.25f, 0.45f, 0.30f + Index * 0.1f));
		AddInstance(Index % 2 ? Cyan : Pink, FVector(0, -445 - Index * 27, 0.7f - Index * 8),
			FVector(7.4f + Index * 0.6f, 0.023f, 0.02f));
	}
	AddInstance(Deck, FVector(0, 458, 20), FVector(7.0f, 1.40f, 0.60f));
	AddInstance(Deck, FVector(0, 586, 0), FVector(12.9f, 1.30f, 0.30f));
	AddInstance(Structure, FVector(0, 646, 216), FVector(12.7f, 0.18f, 4.02f));
	for (int32 Bay = -4; Bay <= 4; ++Bay)
	{
		const float X = Bay * 136.0f;
		const float Depth = Bay % 2 ? 614.0f : 628.0f;
		AddInstance(Panels, FVector(X, Depth, 210), FVector(1.19f, 0.18f, 3.42f));
		for (float Edge : { -1.0f, 1.0f })
		{
			AddInstance(Structure, FVector(X + Edge * 65, 601, 210), FVector(0.07f, 0.31f, 3.58f));
			AddInstance(Structure, FVector(X + Edge * 58, 587, 210), FVector(0.10f, 0.13f, 3.50f), FRotator(0, Edge * 35, 0));
		}
		for (float Z : { 34.0f, 386.0f })
			AddInstance(Structure, FVector(X, 597, Z), FVector(1.31f, 0.38f, 0.09f));
		if (FMath::Abs(Bay) >= 2)
			AddInstance(Bay < 0 ? Cyan : Pink, FVector(X + (Bay < 0 ? -49 : 49), 580, 224), FVector(0.018f, 0.022f, 2.83f));
	}
	AddInstance(Structure, FVector(0, 608, 407), FVector(12.90f, 0.78f, 0.20f));
	AddInstance(Structure, FVector(0, 572, 399), FVector(12.75f, 0.12f, 0.085f));
	AddInstance(Cyan, FVector(0, 565, 396), FVector(12.48f, 0.025f, 0.022f));
	AddInstance(Structure, FVector(0, 599, 19), FVector(12.82f, 0.37f, 0.10f));

	for (float Side : { -1.0f, 1.0f })
	{
		AddInstance(Deck, FVector(Side * 535, 380, -9), FVector(2.12f, 5.60f, 0.18f));
		for (int32 Layer = 0; Layer < 3; ++Layer)
		{
			const float Y = 185 + Layer * 190;
			const float Height = 308 + Layer * 30;
			const float CenterX = Side * 535;
			AddInstance(Deck, FVector(CenterX, Y + 17, 12), FVector(1.92f, 1.24f, 0.24f));
			for (float Edge : { -1.0f, 1.0f })
			{
				const float X = CenterX + Edge * 83;
				AddInstance(Structure, FVector(X, Y, 24 + Height * 0.5f), FVector(0.13f, 0.25f, Height / 100.0f));
				AddInstance(Structure, FVector(X + Edge * 7, Y - 16, 24 + Height * 0.5f),
					FVector(0.07f, 0.13f, Height / 100.0f), FRotator(0, Edge * 35, 0));
			}
			AddInstance(Structure, FVector(CenterX, Y, Height + 26), FVector(1.87f, 0.32f, 0.15f));
			AddInstance(Structure, FVector(CenterX, Y + 12, Height + 37), FVector(2.06f, 0.45f, 0.08f));
			AddInstance(Layer % 2 ? Pink : Cyan, FVector(CenterX, Y - 18, Height + 20), FVector(1.60f, 0.025f, 0.025f));
			if (Layer < 2)
			{
				AddCylinderSegment(Truss, FVector(Side * 618, Y + 15, 65), FVector(Side * 618, Y + 178, Height - 5), 3.2f);
				AddCylinderSegment(Truss, FVector(Side * 618, Y + 15, Height - 5), FVector(Side * 618, Y + 178, 65), 3.2f);
				AddInstance(Structure, FVector(CenterX, Y + 95, Height + 18), FVector(0.15f, 1.80f, 0.13f));
			}
		}
		AddInstance(Panels, FVector(Side * 408, 594, 104), FVector(1.10f, 0.12f, 1.79f));
		for (float Z : { 16.0f, 194.0f })
			AddInstance(Structure, FVector(Side * 408, 581, Z), FVector(1.14f, 0.22f, 0.06f));
		for (int32 Bar = 0; Bar < 8; ++Bar)
			AddAnimated(Side < 0 ? Cyan : Pink, FVector(Side * 408 + (Bar - 3.5f) * 10, 579, 15),
				FVector(0.050f, 0.025f, 0.65f), EMotion::Equalizer, Bar * 0.63f + Side * 0.4f);
	}

	// 四根弦杆和交替斜撑组成实物桁架，灯具通过吊架连接。
	for (float Side : { -1.0f, 1.0f })
	{
		for (float Y : { 325.0f, 391.0f })
			AddCylinderSegment(Truss, FVector(Side * 600, Y, 0), FVector(Side * 600, Y, 458), 3.8f);
		for (int32 Section = 0; Section < 4; ++Section)
		{
			const float Z = 24 + Section * 108.0f;
			AddCylinderSegment(Truss, FVector(Side * 600, 325, Z), FVector(Side * 600, 391, Z + 108), 2.2f);
			AddCylinderSegment(Truss, FVector(Side * 600, 391, Z), FVector(Side * 600, 325, Z + 108), 2.2f);
		}
	}
	for (float Y : { 325.0f, 391.0f })
		for (float Z : { 424.0f, 458.0f })
			AddCylinderSegment(Truss, FVector(-600, Y, Z), FVector(600, Y, Z), 3.2f);
	for (int32 Segment = 0; Segment < 12; ++Segment)
	{
		const float X = -600 + Segment * 100.0f;
		for (float Y : { 325.0f, 391.0f })
		{
			AddCylinderSegment(Truss, FVector(X, Y, 424), FVector(X, Y, 458), 2.0f);
			AddCylinderSegment(Truss, FVector(X, Y, Segment % 2 ? 458 : 424),
				FVector(X + 100, Y, Segment % 2 ? 424 : 458), 2.0f);
		}
		AddCylinderSegment(Truss, FVector(X, 325, 458), FVector(X, 391, 458), 2.0f);
	}
	AddInstance(Cyan, FVector(0, 321, 420), FVector(11.68f, 0.025f, 0.020f));
	UInstancedStaticMeshComponent* Lamps = MakeGroup(TEXT("ConcertMovingHeadBodies"), CylinderMesh, Base * 1.9f, false);
	UInstancedStaticMeshComponent* Lenses = MakeGroup(TEXT("ConcertLampLenses"), CylinderMesh, Accent, true, 1.5f);
	for (int32 Index = 0; Index < 7; ++Index)
	{
		const float X = (Index - 3) * 156.0f;
		for (float Edge : { -1.0f, 1.0f })
			AddInstance(Structure, FVector(X + Edge * 18, 350, 410), FVector(0.04f, 0.20f, 0.30f));
		AddInstance(Structure, FVector(X, 358, 425), FVector(0.48f, 0.76f, 0.04f));
		AddInstance(Lamps, FVector(X, 350, 396), FVector(0.34f, 0.34f, 0.28f));
		AddInstance(Lenses, FVector(X, 350, 380), FVector(0.27f, 0.27f, 0.03f));
	}
	UInstancedStaticMeshComponent* BeamCyan = MakeBeamGroup(TEXT("ConcertCyanBeams"), Accent);
	UInstancedStaticMeshComponent* BeamPink = MakeBeamGroup(TEXT("ConcertPinkBeams"), Secondary);
	for (float Side : { -1.0f, 1.0f })
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Lamp(Side * (455 + Index * 94), 355 + Index * 112, 40);
			AddInstance(Deck, Lamp - FVector(0, 0, 23.5f), FVector(0.59f, 0.59f, 0.33f));
			AddInstance(Lamps, Lamp, FVector(0.36f, 0.36f, 0.22f));
			AddInstance(Lenses, Lamp + FVector(0, 0, 13), FVector(0.25f, 0.25f, 0.03f));
			AddAnimated(Side < 0 ? BeamCyan : BeamPink, Lamp + FVector(0, 0, 174),
				FVector(1.10f, 1.10f, 3.22f), EMotion::Beam, Index * 1.1f + Side, FRotator(180, 0, 0));
		}
	AddRing(Cyan, FVector(0, 55, 0.7f), 336, 2.4f, false, 96);
}

void AMMDStageActor::BuildMoonMirror(const FLinearColor& Base, const FLinearColor& Accent,
	const FLinearColor& Secondary)
{
	const FLinearColor StoneColor = Base * 0.65f + Accent * 0.20f;
	UInstancedStaticMeshComponent* Stone = MakeGroup(TEXT("MoonStoneTerraces"), CubeMesh, StoneColor, false, 1, true);
	UInstancedStaticMeshComponent* Pool = MakeGroup(TEXT("MoonMirrorPool"), CylinderMesh, Base * 1.2f + Accent * 0.045f, false);
	AddInstance(Pool, FVector(0, 55, -8), FVector(10.1f, 9.5f, 0.12f));
	if (Pool && Pool->GetMaterial(0))
	{
		UMaterialInstanceDynamic* Water = Cast<UMaterialInstanceDynamic>(Pool->GetMaterial(0));
		Water->SetScalarParameterValue(TEXT("Roughness"), 0.095f);
		Water->SetScalarParameterValue(TEXT("Metallic"), 0.66f);
		Water->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.03f);
		Water->SetScalarParameterValue(TEXT("PulseAmount"), 0.24f);
		Water->SetScalarParameterValue(TEXT("PatternScale"), 0.022f);
		Water->SetScalarParameterValue(TEXT("PulseSpeed"), 0.16f);
	}
	UInstancedStaticMeshComponent* Island = MakeGroup(TEXT("MoonDancerIsland"), CylinderMesh, StoneColor, false);
	AddInstance(Island, FVector(0, -20, -10), FVector(3.75f, 3.75f, 0.20f));
	AddRing(Stone, FVector(0, 55, -33.5f), 523, 67, false, 112);
	AddInstance(Stone, FVector(0, -315, -10), FVector(1.85f, 2.40f, 0.20f));
	for (float Side : { -1.0f, 1.0f })
		AddInstance(Stone, FVector(Side * 505, 195, -12), FVector(1.40f, 7.3f, 0.24f));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		AddInstance(Stone, FVector(0, -520 - Index * 28, -9 - Index * 9), FVector(2.45f + Index * 0.5f, 0.38f, 0.18f));
		AddInstance(Stone, FVector(0, 324 + Index * 29, -5 + Index * 5), FVector(4.05f + Index * 0.3f, 0.46f, 0.30f + Index * 0.1f));
	}
	AddInstance(Stone, FVector(0, 479, 19), FVector(11.8f, 2.10f, 0.62f));
	UInstancedStaticMeshComponent* Pillars = MakeGroup(TEXT("MoonSilverColumns"), CylinderMesh, StoneColor * 1.18f, false);
	UInstancedStaticMeshComponent* Carving = MakeGroup(TEXT("MoonColumnBases"), CubeMesh, StoneColor * 0.72f, false, 1, true);
	UInstancedStaticMeshComponent* Metal = MakeGroup(TEXT("MoonSilverBalustrades"), CubeMesh, Accent * 0.22f + Base * 0.6f, false, 1, true);
	UInstancedStaticMeshComponent* BlueLines = MakeGroup(TEXT("MoonBlueLightInlay"), CubeMesh, Accent, true, 0.75f);
	UInstancedStaticMeshComponent* GoldLines = MakeGroup(TEXT("MoonSilverLightInlay"), CubeMesh, Secondary, true, 0.70f);
	for (float Side : { -1.0f, 1.0f })
	{
		const float X = Side * 215;
		AddInstance(Pillars, FVector(X, 449, 180), FVector(0.34f, 0.34f, 2.60f));
		AddInstance(Carving, FVector(X, 449, 56), FVector(0.78f, 0.78f, 0.18f));
		AddInstance(Carving, FVector(X, 449, 305), FVector(0.64f, 0.64f, 0.18f));
		AddInstance(Pillars, FVector(X, 449, 71), FVector(0.48f, 0.48f, 0.09f));
		AddInstance(Pillars, FVector(X, 449, 290), FVector(0.49f, 0.49f, 0.10f));
		AddInstance(BlueLines, FVector(X - Side * 19, 432, 180), FVector(0.020f, 0.020f, 2.33f));
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FVector BasePosition(Side * (484 - Index * 7), 55 + Index * 127, 0);
			AddInstance(Pillars, BasePosition + FVector(0, 0, 140), FVector(0.28f, 0.28f, 2.80f));
			AddInstance(Carving, BasePosition + FVector(0, 0, 9), FVector(0.60f, 0.60f, 0.18f));
			AddInstance(Carving, BasePosition + FVector(0, 0, 278), FVector(0.52f, 0.52f, 0.14f));
			AddInstance(Pillars, BasePosition + FVector(0, 0, 24), FVector(0.42f, 0.42f, 0.08f));
			AddInstance(Pillars, BasePosition + FVector(0, 0, 264), FVector(0.42f, 0.42f, 0.08f));
			if (Index < 3)
				AddInstance(Metal, BasePosition + FVector(0, 63, 290), FVector(0.26f, 1.45f, 0.18f));
		}
		for (int32 Index = 0; Index < 9; ++Index)
		{
			const float Y = -220 + Index * 72;
			AddInstance(Metal, FVector(Side * 535, Y, 46), FVector(0.06f, 0.06f, 0.88f));
			AddInstance(Metal, FVector(Side * 535, Y + 35, 87), FVector(0.095f, 0.76f, 0.055f));
			AddInstance(GoldLines, FVector(Side * 535, Y + 35, 93), FVector(0.018f, 0.74f, 0.02f));
		}
		AddArc(Metal, FVector(Side * 472, 434, 290), 73, 12, 0, 180, 24, 19);
		for (float Offset : { -73.0f, 73.0f })
		{
			AddInstance(Pillars, FVector(Side * 472 + Offset, 434, 171), FVector(0.22f, 0.22f, 2.38f));
			AddInstance(Carving, FVector(Side * 472 + Offset, 434, 55), FVector(0.45f, 0.45f, 0.13f));
		}
	}
	AddArc(Metal, FVector(0, 449, 306), 215, 18, 0, 180, 64, 28);
	AddArc(BlueLines, FVector(0, 430, 306), 197, 2.6f, 0, 180, 64, 2.5f);
	UInstancedStaticMeshComponent* StarVault = MakeGroup(TEXT("MoonStarVaultStructure"), CubeMesh, Base * 1.1f, false, 1, true);
	AddArc(StarVault, FVector(0, 570, 125), 382, 18, 15, 165, 72, 22);
	for (float Side : { -1.0f, 1.0f })
		AddInstance(StarVault, FVector(Side * 369, 570, 139), FVector(0.18f, 0.22f, 1.78f));
	AddArc(GoldLines, FVector(0, 556, 125), 368, 1.8f, 15, 165, 72, 1.8f);
	UInstancedStaticMeshComponent* Moon = MakeGroup(TEXT("MoonSculptedCrescent"), CubeMesh,
		FLinearColor(0.66f, 0.76f, 0.92f), true, 0.72f);
	for (int32 Index = 0; Index < 60; ++Index)
	{
		const float Fraction = (Index + 0.5f) / 60;
		const float Angle = FMath::DegreesToRadians(50.0f + Fraction * 260.0f);
		const float Thickness = 1.0f + 22.0f * FMath::Pow(FMath::Sin(Fraction * PI), 0.85f);
		const float Radius = 91.0f - Thickness * 0.5f;
		AddInstance(Moon, FVector(FMath::Cos(Angle) * Radius, 560, 349 + FMath::Sin(Angle) * Radius),
			FVector(0.080f, 0.035f, Thickness / 100.0f), FRotator(FMath::RadiansToDegrees(Angle) + 90, 0, 0));
	}
	AddRing(BlueLines, FVector(0, -20, 0.6f), 177, 1.8f, false, 88);
	for (int32 Index = 0; Index < 3; ++Index)
		AddRing(Index % 2 ? GoldLines : BlueLines, FVector(0, 55, -1.2f), 255 + Index * 80,
			1.05f, false, 112, EMotion::Ripple, true);
	UInstancedStaticMeshComponent* Stars = MakeGroup(TEXT("MoonConstellationStars"), SphereMesh, Accent, true, 1.0f);
	for (int32 Index = 0; Index < 68; ++Index)
	{
		const float Phase = Index * 2.39996f;
		AddAnimated(Stars, FVector(FMath::Cos(Phase) * (220 + Index % 7 * 45), 520 + Index % 4 * 27,
			147 + Index % 9 * 37), FVector(Index % 7 == 0 ? 0.033f : 0.014f), EMotion::Orbit, Phase);
	}
}

void AMMDStageActor::LoadGardenMeshReferences()
{
	auto LoadMesh = [](TObjectPtr<UStaticMesh>& Mesh, const TCHAR* Name)
	{
		if (Mesh) return;
		const FString PackagePath = FString::Printf(TEXT("/Ue5MMDTools/Stages/Meshes/%s"), Name);
		if (FPackageName::DoesPackageExist(PackagePath))
			Mesh = LoadObject<UStaticMesh>(nullptr, *(PackagePath + TEXT(".") + Name), nullptr, LOAD_NoWarn);
	};
	// 保留硬引用，使已生成的原创网格随舞台地图进入 cook。
	LoadMesh(GardenRoofHipMesh, TEXT("SM_GardenRoofHip"));
	LoadMesh(GardenRoofTileMesh, TEXT("SM_GardenRoofTile"));
	LoadMesh(GardenCurvedBeamMesh, TEXT("SM_GardenCurvedBeam"));
	LoadMesh(GardenBranchMesh, TEXT("SM_GardenBranch"));
	LoadMesh(GardenTreeTrunkV2Mesh, TEXT("SM_GardenTreeTrunkV2"));
	LoadMesh(GardenBranchV2Mesh, TEXT("SM_GardenBranchV2"));
	LoadMesh(GardenBlossomClusterV2Mesh, TEXT("SM_GardenBlossomClusterV2"));
	LoadMesh(GardenReedClumpV2Mesh, TEXT("SM_GardenReedClumpV2"));
	LoadMesh(GardenBlossomClusterMesh, TEXT("SM_GardenBlossomCluster"));
	LoadMesh(GardenPetalMesh, TEXT("SM_GardenPetal"));
	LoadMesh(GardenRockMesh, TEXT("SM_GardenRock"));
	LoadMesh(GardenLanternCapMesh, TEXT("SM_GardenLanternCap"));
	LoadMesh(GardenLanternPedestalMesh, TEXT("SM_GardenLanternPedestal"));
	LoadMesh(GardenWaterRippleMesh, TEXT("SM_GardenWaterRipple"));
}

void AMMDStageActor::BuildSakuraTheater(const FLinearColor& Base, const FLinearColor& Accent,
	const FLinearColor& Secondary)
{
	LoadGardenMeshReferences();
	if (!GardenTreeTrunkV2Mesh || !GardenBranchV2Mesh || !GardenBlossomClusterV2Mesh || !GardenReedClumpV2Mesh)
		UE_LOG(LogTemp, Warning, TEXT("MMD 樱庭 V2 原创网格未完整生成，当前只使用可用网格兜底；请运行舞台资产生成验证。"));
	const FLinearColor Wood = FLinearColor(0.115f, 0.063f, 0.037f) + Base * 0.045f;
	const FLinearColor DarkWood = FLinearColor(0.064f, 0.035f, 0.022f);
	const FLinearColor Stone = FLinearColor(0.105f, 0.125f, 0.132f);
	const FLinearColor RoofColor = FLinearColor(0.050f, 0.075f, 0.095f);
	const FLinearColor WarmLight = FLinearColor(1.0f, 0.58f, 0.22f) + Secondary * 0.04f;
	auto Surface = [](UInstancedStaticMeshComponent* Group, float Roughness, float Metallic, float SurfaceRole)
	{
		if (!Group) return;
		if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Group->GetMaterial(0)))
		{
			Material->SetScalarParameterValue(TEXT("Roughness"), Roughness);
			Material->SetScalarParameterValue(TEXT("Metallic"), Metallic);
			Material->SetScalarParameterValue(TEXT("SurfaceRole"), SurfaceRole);
			Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.015f);
		}
	};

	UInstancedStaticMeshComponent* Water = MakeGroup(TEXT("SakuraGardenWater"), CylinderMesh,
		FLinearColor(0.030f, 0.070f, 0.095f), false);
	Surface(Water, 0.11f, 0.28f, 4.0f);
	AddInstance(Water, FVector(0, 245, -30), FVector(17.4f, 17.8f, 0.04f));
	UInstancedStaticMeshComponent* Shore = MakeGroup(TEXT("SakuraNaturalShoreStones"), GardenRockMesh, Stone, false, 1, true);
	Surface(Shore, 0.82f, 0.0f, 3.0f);
	FRandomStream Random(164793);
	// 不规则岸线与成组草叶形成近、中景，水域中央保持开放。
	for (int32 Index = 0; Index < 74; ++Index)
	{
		const float Angle = TWO_PI * (Index + Random.FRandRange(-0.23f, 0.23f)) / 74;
		const float Edge = 1.0f + 0.018f * FMath::Sin(Angle * 5) + 0.012f * FMath::Cos(Angle * 9);
		const float Size = Random.FRandRange(0.54f, 1.06f);
		AddInstance(Shore, FVector(FMath::Cos(Angle) * 859 * Edge, 245 + FMath::Sin(Angle) * 878 * Edge, -48),
			FVector(Size, Size * Random.FRandRange(0.67f, 1.05f), Size * Random.FRandRange(0.42f, 0.65f)),
			FRotator(Random.FRandRange(-10, 10), Random.FRandRange(0, 360), Random.FRandRange(-8, 8)));
	}
	const FVector BankCenters[] = {
		FVector(465, -290, -33), FVector(-480, -310, -33), FVector(690, 280, -35),
		FVector(-690, 535, -35), FVector(570, 850, -36), FVector(-485, 1080, -35),
		FVector(280, 345, -36), FVector(-325, 455, -36), FVector(300, 745, -36)
	};
	UInstancedStaticMeshComponent* Reeds = MakeGroup(TEXT("SakuraPondBankReeds"), GardenReedClumpV2Mesh,
		FLinearColor(0.070f, 0.102f, 0.064f), false);
	Surface(Reeds, 0.91f, 0.0f, 0.0f);
	for (int32 Bank = 0; Bank < UE_ARRAY_COUNT(BankCenters); ++Bank)
	{
		for (int32 Rock = 0; Rock < 8; ++Rock)
		{
			const FVector Offset(Random.FRandRange(-78, 78), Random.FRandRange(-52, 52), Random.FRandRange(-8, 4));
			const float Size = Random.FRandRange(0.40f, 0.78f);
			AddInstance(Shore, BankCenters[Bank] + Offset, FVector(Size, Size * 0.77f, Size * 0.41f),
				FRotator(Random.FRandRange(-9, 9), Random.FRandRange(0, 360), 0));
		}
		for (int32 Grass = 0; Grass < 7; ++Grass)
		{
			const float Size = Random.FRandRange(0.55f, 1.03f);
			AddInstance(Reeds, BankCenters[Bank] + FVector(Random.FRandRange(-71, 71), Random.FRandRange(-46, 46), 9),
				FVector(Size, Size * 0.84f, Size), FRotator(0, Random.FRandRange(0, 360), 0));
		}
	}

	// 木平台顶面保持 z=0；细分真实板缝，水面低于台面，不遮住角色脚。
	UInstancedStaticMeshComponent* Deck = MakeGroup(TEXT("SakuraDeckPlanks"), CubeMesh, Wood, false);
	UInstancedStaticMeshComponent* DeckAlternate = MakeGroup(TEXT("SakuraDeckPlanksAlternate"), CubeMesh, Wood * 0.83f, false);
	UInstancedStaticMeshComponent* Timber = MakeGroup(TEXT("SakuraPavilionTimber"), CubeMesh, DarkWood, false, 1, true);
	UInstancedStaticMeshComponent* RoundWood = MakeGroup(TEXT("SakuraTurnedWoodPosts"), CylinderMesh, Wood * 0.90f, false);
	UInstancedStaticMeshComponent* Foundation = MakeGroup(TEXT("SakuraCutStoneFoundations"), CubeMesh, Stone * 0.82f, false);
	Surface(Deck, 0.24f, 0.02f, 1.0f);
	Surface(DeckAlternate, 0.28f, 0.02f, 1.0f);
	Surface(Timber, 0.69f, 0.0f, 1.0f);
	Surface(RoundWood, 0.66f, 0.0f, 1.0f);
	Surface(Foundation, 0.82f, 0.0f, 3.0f);
	UInstancedStaticMeshComponent* WetStone = MakeGroup(TEXT("SakuraWetStoneSteps"), GardenRockMesh,
		FLinearColor(0.13f, 0.16f, 0.17f), false);
	Surface(WetStone, 0.28f, 0.01f, 3.0f);
	for (int32 Step = 0; Step < 12; ++Step)
	{
		const float T = Step / 11.0f;
		const FVector Position(FMath::Lerp(237.0f, 195.0f, T), FMath::Lerp(208.0f, 398.0f, T), -12);
		AddInstance(WetStone, Position, FVector(0.78f, 0.55f, 0.17f), FRotator(0, 22 + Step * 8, 0));
	}
	for (float Side : { -1.0f, 1.0f })
		for (int32 Step = 0; Step < 4; ++Step)
			AddInstance(WetStone, FVector(Side * (237 + Step * 26), -248 + Step * 25, -17),
				FVector(0.62f, 0.44f, 0.20f), FRotator(0, Side * (20 + Step * 11), 0));
	for (int32 Board = 0; Board < 23; ++Board)
		AddInstance(Board % 4 ? Deck : DeckAlternate, FVector((Board - 11) * 18.4f, -40, -6),
			FVector(0.178f, 4.72f, 0.12f));
	for (float X : { -198.0f, 0.0f, 198.0f })
		AddInstance(Timber, FVector(X, -40, -22), FVector(0.16f, 4.90f, 0.32f));
	for (float X : { -193.0f, 193.0f })
		for (float Y : { -228.0f, 146.0f })
			AddInstance(RoundWood, FVector(X, Y, -32), FVector(0.16f, 0.16f, 0.72f));
	for (float Y : { -278.0f, 198.0f })
		AddInstance(Timber, FVector(0, Y, -11), FVector(4.32f, 0.16f, 0.22f));
	for (int32 Board = 0; Board < 16; ++Board)
		AddInstance(Board % 3 ? Deck : DeckAlternate, FVector(0, -292 - Board * 23, -7),
			FVector(2.24f, 0.221f, 0.10f));
	for (float Side : { -1.0f, 1.0f })
	{
		for (int32 Post = 0; Post < 5; ++Post)
		{
			const float Y = -205 + Post * 90;
			AddInstance(Timber, FVector(Side * 225, Y, 42), FVector(0.115f, 0.115f, 0.90f));
			if (Post < 4)
			{
				AddInstance(Timber, FVector(Side * 225, Y + 45, 77), FVector(0.105f, 0.92f, 0.095f));
				AddInstance(Timber, FVector(Side * 225, Y + 45, 35), FVector(0.065f, 0.92f, 0.065f));
				for (int32 Spindle = 0; Spindle < 3; ++Spindle)
					AddInstance(Timber, FVector(Side * 225, Y + 18 + Spindle * 22, 54), FVector(0.034f, 0.034f, 0.42f));
			}
		}
	}

	const FRotator PavilionRotation = GardenPavilionRotation;
	auto PavilionPoint = [](const FVector& Local)
	{
		return GardenPavilionPoint(Local);
	};
	auto PavilionPart = [this, &PavilionPoint, &PavilionRotation](UInstancedStaticMeshComponent* Group,
		const FVector& Local, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		AddInstance(Group, PavilionPoint(Local), Scale * GardenPavilionScale,
			(PavilionRotation.Quaternion() * Rotation.Quaternion()).Rotator());
	};
	for (int32 Block = 0; Block < 6; ++Block)
		PavilionPart(Foundation, FVector((Block - 2.5f) * 65, 0, 3), FVector(0.63f, 2.97f, 0.34f));
	for (int32 Board = 0; Board < 20; ++Board)
		PavilionPart(Board % 3 ? Deck : DeckAlternate, FVector((Board - 9.5f) * 19.5f, 0, 26),
			FVector(0.187f, 2.98f, 0.12f));
	for (int32 Step = 0; Step < 3; ++Step)
		PavilionPart(Foundation, FVector(0, -165 - Step * 27, 12 - Step * 10), FVector(1.54f, 0.29f, 0.20f));
	for (float X : { -167.0f, 0.0f, 167.0f })
	{
		for (float Y : { -112.0f, 112.0f })
		{
			PavilionPart(RoundWood, FVector(X, Y, 181), FVector(0.20f, 0.20f, 2.98f));
			PavilionPart(Foundation, FVector(X, Y, 37), FVector(0.42f, 0.42f, 0.18f));
			for (float Z : { 51.0f, 302.0f, 322.0f })
				PavilionPart(RoundWood, FVector(X, Y, Z), FVector(0.29f, 0.29f, 0.055f));
			PavilionPart(Timber, FVector(X, Y, 314), FVector(0.64f, 0.36f, 0.11f));
			PavilionPart(Timber, FVector(X, Y, 328), FVector(0.82f, 0.43f, 0.095f));
			for (float Side : { -1.0f, 1.0f })
				PavilionPart(Timber, FVector(X + Side * 20, Y, 308), FVector(0.35f, 0.15f, 0.06f), FRotator(Side * 24, 0, 0));
		}
	}
	for (float Y : { -112.0f, 112.0f })
	{
		PavilionPart(Timber, FVector(0, Y, 295), FVector(3.76f, 0.20f, 0.20f));
		PavilionPart(Timber, FVector(0, Y, 332), FVector(4.30f, 0.28f, 0.17f));
	}
	for (int32 Rafter = 0; Rafter < 15; ++Rafter)
		PavilionPart(Timber, FVector((Rafter - 7) * 28, 0, 329), FVector(0.085f, 3.48f, 0.085f));
	// 前面保持通透，后侧仅留细木格，避免整面暗纸墙占据主构图。
	for (int32 Panel = 0; Panel < 4; ++Panel)
	{
		const float X = -125 + Panel * 83;
		for (int32 Grid = 0; Grid < 4; ++Grid)
			PavilionPart(Timber, FVector(X - 39 + Grid * 26, 122, 160), FVector(0.022f, 0.040f, 2.30f));
		for (int32 Grid = 0; Grid < 8; ++Grid)
			PavilionPart(Timber, FVector(X, 122, 48 + Grid * 32), FVector(0.81f, 0.040f, 0.026f));
	}
	for (float Side : { -1.0f, 1.0f })
	{
		for (int32 Rail = 0; Rail < 8; ++Rail)
		{
			PavilionPart(Timber, FVector(Side * 181, -105 + Rail * 30, 62), FVector(0.052f, 0.052f, 0.60f));
			if (Rail < 7)
				PavilionPart(Timber, FVector(Side * 181, -90 + Rail * 30, 91), FVector(0.09f, 0.31f, 0.085f));
		}
	}
	UInstancedStaticMeshComponent* Roof = MakeGroup(TEXT("SakuraPavilionCurvedRoof"), GardenRoofHipMesh, RoofColor, false, 1, true);
	UInstancedStaticMeshComponent* Tiles = MakeGroup(TEXT("SakuraRoofTiles"), GardenRoofTileMesh, RoofColor * 1.18f, false, 1, true);
	UInstancedStaticMeshComponent* Fascia = MakeGroup(TEXT("SakuraCurvedEaveBeams"), GardenCurvedBeamMesh, DarkWood, false, 1, true);
	Surface(Roof, 0.50f, 0.02f, 2.0f);
	Surface(Tiles, 0.43f, 0.03f, 2.0f);
	Surface(Fascia, 0.65f, 0.0f, 1.0f);
	PavilionPart(Roof, FVector(0, 0, 340), FVector(1.20f, 1.30f, 0.60f));
	for (float Side : { -1.0f, 1.0f })
	{
		PavilionPart(Fascia, FVector(0, Side * 187, 338), FVector(1.99f, 0.82f, 0.48f));
		PavilionPart(Fascia, FVector(Side * 231, 0, 338), FVector(1.56f, 0.82f, 0.48f), FRotator(0, 90, 0));
	}
	auto RoofHeight = [](float X, float Y)
	{
		const float T = FMath::Clamp(FMath::Min(1.0f - FMath::Abs(Y) / 150.0f, (200.0f - FMath::Abs(X)) / 120.0f), 0.0f, 1.0f);
		const float TileDetail = FMath::Square(FMath::Sin(PI * T)) * (0.60f * FMath::Square(FMath::Sin(PI * X / 12.0f))
			+ 0.20f * FMath::Square(FMath::Sin(PI * Y / 42.0f)));
		return 0.60f * (85.0f * (0.70f * T + 0.30f * T * T) + TileDetail
			+ 18.0f * FMath::Pow(FMath::Abs(X) / 200.0f, 4.0f) * FMath::Pow(FMath::Abs(Y) / 150.0f, 4.0f));
	};
	for (int32 Column = 0; Column < 31; ++Column)
	{
		const float X = (Column - 15) * 12.5f;
		for (int32 Row = 0; Row < 8; ++Row)
		{
			const float Y = (Row - 3.5f) * 37.0f;
			const float DZDX = (RoofHeight(X + 1, Y) - RoofHeight(X - 1, Y)) * GardenPavilionScale.Z / (2.40f * GardenPavilionScale.X);
			const float DZDY = (RoofHeight(X, Y + 1) - RoofHeight(X, Y - 1)) * GardenPavilionScale.Z / (2.60f * GardenPavilionScale.Y);
			const FVector Normal(-DZDX, -DZDY, 1.0f);
			const FVector Tangent = FVector(1, 0, DZDX).GetSafeNormal();
			const FRotator Rotation = FRotationMatrix::MakeFromXZ(Tangent, Normal.GetSafeNormal()).Rotator();
			PavilionPart(Tiles, FVector(X * 1.20f, Y * 1.30f, 341.7f + RoofHeight(X, Y)),
				FVector(1.18f, 1.19f, 0.60f), Rotation);
		}
	}
	// 小上檐与低外檐形成两层完整轮廓，瓦脊仍控制在约2.6m。
	PavilionPart(Roof, FVector(0, 0, 378), FVector(0.68f, 0.73f, 0.52f));
	for (float Side : { -1.0f, 1.0f })
	{
		PavilionPart(Fascia, FVector(0, Side * 107, 377), FVector(1.12f, 0.60f, 0.24f));
		PavilionPart(Fascia, FVector(Side * 130, 0, 377), FVector(0.88f, 0.60f, 0.24f), FRotator(0, 90, 0));
		for (int32 Tile = 0; Tile < 17; ++Tile)
		{
			const float X = (Tile - 8) * 15.4f;
			PavilionPart(Tiles, FVector(X, Side * 107, 379), FVector(0.72f, 0.74f, 0.52f), FRotator(0, 0, Side * 5));
		}
	}
	PavilionPart(RoundWood, FVector(0, 0, 424), FVector(0.13f, 0.13f, 1.34f), FRotator(90, 0, 0));
	for (int32 Ridge = 0; Ridge < 15; ++Ridge)
		PavilionPart(Tiles, FVector((Ridge - 7) * 12.2f, 0, 424.5f), FVector(0.79f, 0.34f, 0.50f));
	// 侧廊由同一组屋顶、柱梁、木板组成，前侧通透，避免只剩一片栏杆。
	PavilionPart(Roof, FVector(278, 34, 298), FVector(0.78f, 0.69f, 0.53f));
	for (int32 Board = 0; Board < 16; ++Board)
		PavilionPart(Board % 3 ? Deck : DeckAlternate, FVector(170 + Board * 14.5f, 34, 26), FVector(0.14f, 1.94f, 0.12f));
	for (float X : { 174.0f, 292.0f, 410.0f })
	{
		for (float Y : { -44.0f, 112.0f })
		{
			PavilionPart(RoundWood, FVector(X, Y, 155), FVector(0.18f, 0.18f, 2.44f));
			PavilionPart(Foundation, FVector(X, Y, 33), FVector(0.38f, 0.38f, 0.15f));
			PavilionPart(Timber, FVector(X, Y, 287), FVector(0.63f, 0.26f, 0.09f));
		}
	}
	for (float Y : { -44.0f, 112.0f })
	{
		PavilionPart(Timber, FVector(292, Y, 285), FVector(2.62f, 0.15f, 0.18f));
		PavilionPart(Fascia, FVector(278, Y == -44.0f ? -99.0f : 165.0f, 297), FVector(1.29f, 0.62f, 0.24f));
	}
	for (int32 Rafter = 0; Rafter < 11; ++Rafter)
		PavilionPart(Timber, FVector(180 + Rafter * 22, 34, 289), FVector(0.065f, 2.11f, 0.075f));
	UInstancedStaticMeshComponent* Shoji = MakeGroup(TEXT("SakuraTeaHouseWarmShoji"), CubeMesh,
		FLinearColor(1.0f, 0.67f, 0.34f), true, 0.24f);
	for (int32 Panel = 0; Panel < 4; ++Panel)
		PavilionPart(Shoji, FVector(-125 + Panel * 83, 125, 149), FVector(0.79f, 0.025f, 2.07f));
	for (int32 Panel = 0; Panel < 3; ++Panel)
	{
		const float X = 196 + Panel * 71;
		PavilionPart(Shoji, FVector(X, 117, 137), FVector(0.65f, 0.025f, 1.96f));
		for (int32 Grid = 0; Grid < 4; ++Grid)
			PavilionPart(Timber, FVector(X - 32 + Grid * 21, 114, 140), FVector(0.026f, 0.04f, 2.03f));
		for (int32 Grid = 0; Grid < 7; ++Grid)
			PavilionPart(Timber, FVector(X, 114, 47 + Grid * 30), FVector(0.67f, 0.04f, 0.03f));
	}
	for (int32 Step = 0; Step < 7; ++Step)
	{
		const float T = Step / 6.0f;
		const FVector Position(FMath::Lerp(264.0f, 393.0f, T), FMath::Lerp(202.0f, 335.0f, T), -19);
		AddInstance(Shore, Position, FVector(0.45f, 0.35f, 0.12f), FRotator(0, 23 + Step * 11, 0));
	}

	UInstancedStaticMeshComponent* Torii = MakeGroup(TEXT("SakuraVermilionTorii"), CylinderMesh,
		FLinearColor(0.31f, 0.095f, 0.045f), false, 1, true);
	UInstancedStaticMeshComponent* ToriiBeam = MakeGroup(TEXT("SakuraToriiCurvedTop"), GardenCurvedBeamMesh,
		FLinearColor(0.26f, 0.068f, 0.030f), false, 1, true);
	UInstancedStaticMeshComponent* ToriiLintel = MakeGroup(TEXT("SakuraToriiWoodCrossbars"), CubeMesh,
		FLinearColor(0.29f, 0.088f, 0.040f), false, 1, true);
	Surface(Torii, 0.58f, 0.0f, 1.0f);
	Surface(ToriiBeam, 0.55f, 0.0f, 1.0f);
	Surface(ToriiLintel, 0.63f, 0.0f, 1.0f);
	for (int32 Gate = 0; Gate < 2; ++Gate)
	{
		const float Scale = Gate == 0 ? 0.54f : 0.36f;
		const FVector Origin(Gate == 0 ? -540 : -710, Gate == 0 ? 1045 : 1115, -12);
		for (float Side : { -1.0f, 1.0f })
		{
			AddInstance(Torii, Origin + FVector(Side * 109, 0, 154) * Scale, FVector(0.24f, 0.24f, 3.08f) * Scale,
				FRotator(0, 0, Side * 3));
			AddInstance(Foundation, Origin + FVector(Side * 116, 0, 9) * Scale, FVector(0.50f, 0.50f, 0.20f) * Scale);
			AddInstance(Torii, Origin + FVector(Side * 113, 0, 44) * Scale, FVector(0.29f, 0.29f, 0.14f) * Scale);
		}
		AddInstance(ToriiBeam, Origin + FVector(0, 0, 318) * Scale, FVector(1.47f, 1.08f, 1.0f) * Scale);
		AddInstance(ToriiLintel, Origin + FVector(0, 0, 272) * Scale, FVector(3.08f, 0.19f, 0.15f) * Scale);
		AddInstance(ToriiLintel, Origin + FVector(0, 0, 294) * Scale, FVector(0.15f, 0.18f, 0.42f) * Scale);
		AddInstance(Timber, Origin + FVector(0, -12, 296) * Scale, FVector(0.27f, 0.05f, 0.37f) * Scale);
	}
	for (int32 StoneIndex = 0; StoneIndex < 13; ++StoneIndex)
	{
		const float Blend = StoneIndex / 12.0f;
		AddInstance(Shore, FVector(FMath::Lerp(-258.0f, -625.0f, Blend), FMath::Lerp(365.0f, 1080.0f, Blend), -22),
			FVector(0.51f, 0.39f, 0.11f), FRotator(0, -16 + StoneIndex * 5, 0));
	}

	UStaticMesh* BlossomMesh = GardenBlossomClusterV2Mesh ? GardenBlossomClusterV2Mesh.Get() : GardenBlossomClusterMesh.Get();
	UStaticMesh* TwigMesh = GardenBranchV2Mesh ? GardenBranchV2Mesh.Get() : GardenBranchMesh.Get();
	UInstancedStaticMeshComponent* Trunks = MakeGroup(TEXT("SakuraNaturalTreeTrunks"), GardenTreeTrunkV2Mesh,
		FLinearColor(0.071f, 0.045f, 0.035f), false, 1, true);
	UInstancedStaticMeshComponent* Branches = MakeGroup(TEXT("SakuraNaturalTreeTwigs"), TwigMesh,
		FLinearColor(0.074f, 0.047f, 0.035f), false, 1, true);
	UInstancedStaticMeshComponent* FlowersIvory = MakeGroup(TEXT("SakuraFlowerIvory"), BlossomMesh,
		FLinearColor(0.70f, 0.49f, 0.53f) + Accent * 0.012f, false, 1, true);
	UInstancedStaticMeshComponent* FlowersPink = MakeGroup(TEXT("SakuraFlowerPink"), BlossomMesh,
		FLinearColor(0.58f, 0.285f, 0.35f), false, 1, true);
	UInstancedStaticMeshComponent* FlowersDeep = MakeGroup(TEXT("SakuraFlowerDeepPink"), BlossomMesh,
		FLinearColor(0.38f, 0.13f, 0.21f), false, 1, true);
	Surface(Trunks, 0.88f, 0.0f, 1.0f);
	Surface(Branches, 0.88f, 0.0f, 1.0f);
	Surface(FlowersIvory, 0.73f, 0.0f, 5.0f);
	Surface(FlowersPink, 0.76f, 0.0f, 5.0f);
	Surface(FlowersDeep, 0.79f, 0.0f, 5.0f);
	auto Branch = [this, Branches](const FVector& From, const FVector& To, float Radius)
	{
		const FVector Direction = To - From;
		const float Length = Direction.Size();
		if (Length < 0.01f) return;
		AddInstance(Branches, From, FVector(Radius / 10.0f, Radius / 10.0f, Length / 100.0f),
			FQuat::FindBetweenNormals(FVector::UpVector, Direction / Length).Rotator());
	};
	struct FTreeLayout
	{
		FVector Root;
		FVector Scale;
		float Yaw;
		int32 Clusters;
	};
	const FTreeLayout Trees[] = {
		{ FVector(420, 310, -34), FVector(1.26f, 1.08f, 0.85f), -32, 460 },
		{ FVector(-485, 615, -34), FVector(1.35f, 1.15f, 0.92f), 72, 420 },
		{ FVector(10, 860, -34), FVector(0.78f, 0.85f, 0.76f), 157, 210 },
		{ FVector(-300, 1070, -34), FVector(0.72f, 0.78f, 0.75f), -108, 170 }
	};
	const FVector BoughCenters[] = {
		FVector(-142, -30, 338), FVector(154, 33, 363),
		FVector(30, 123, 390), FVector(12, -82, 327)
	};
	const FVector BoughStarts[] = {
		FVector(6, 4, 158), FVector(17, 8, 192),
		FVector(20, 12, 235), FVector(16, 9, 215)
	};
	for (int32 Tree = 0; Tree < UE_ARRAY_COUNT(Trees); ++Tree)
	{
		const FTreeLayout& Layout = Trees[Tree];
		const FTransform TreeTransform(FRotator(0, Layout.Yaw, 0), Layout.Root, Layout.Scale);
		AddInstance(Trunks, Layout.Root, Layout.Scale, FRotator(0, Layout.Yaw, 0));
		if (!GardenTreeTrunkV2Mesh)
			Branch(Layout.Root, TreeTransform.TransformPosition(FVector(18, 10, 325)), 18 * Layout.Scale.Z);
		Branch(TreeTransform.TransformPosition(BoughStarts[3]), TreeTransform.TransformPosition(BoughCenters[3]),
			5.2f * Layout.Scale.Z);
		// 大枝来自整体网格，细枝按枝尖延伸，避免周期扭曲的树干拼接。
		for (int32 Bough = 0; Bough < UE_ARRAY_COUNT(BoughCenters); ++Bough)
		{
			for (int32 Twig = 0; Twig < 3; ++Twig)
			{
				const float Angle = 0.63f * Bough + Twig * 1.21f + Tree * 0.37f;
				const FVector LocalStart = FMath::Lerp(BoughStarts[Bough], BoughCenters[Bough], 0.73f);
				const FVector LocalTip = BoughCenters[Bough] + FVector(FMath::Cos(Angle) * (57 + Twig * 15),
					FMath::Sin(Angle) * 49, 23 + Twig * 9);
				Branch(TreeTransform.TransformPosition(LocalStart), TreeTransform.TransformPosition(LocalTip), 2.1f * Layout.Scale.Z);
			}
		}
		// 四个不对称花棚沿主枝展开；外沿下垂，内部透出枝干，不做球形花团。
		for (int32 Cluster = 0; Cluster < Layout.Clusters; ++Cluster)
		{
			const int32 Lobe = Cluster % UE_ARRAY_COUNT(BoughCenters);
			const int32 Samples = (Layout.Clusters + 3) / 4;
			const float Fraction = (Cluster / 4 + 0.5f) / Samples;
			const float Angle = Cluster * 2.3999632f + Tree * 0.77f;
			const float Radius = FMath::Sqrt(Fraction);
			const float Outline = 1.0f + 0.16f * FMath::Sin(Angle * 3 + Lobe) + 0.10f * FMath::Cos(Angle * 5 + Tree);
			const float Width = Lobe == 2 ? 131.0f : 164.0f;
			const FVector Offset(FMath::Cos(Angle) * Radius * Width * Outline,
				FMath::Sin(Angle) * Radius * 105 * Outline,
				18 - 30 * Radius * Radius + Random.FRandRange(-18, 21) + 9 * FMath::Sin(Angle * 2));
			const FVector LocalPosition = BoughCenters[Lobe] + Offset;
			const float FlowerScale = Random.FRandRange(1.32f, 1.83f) * Layout.Scale.Z;
			UInstancedStaticMeshComponent* FlowerGroup = Cluster % 13 == 0 ? FlowersDeep
				: Cluster % 5 < 2 ? FlowersIvory : FlowersPink;
			const FRotator FlowerRotation(Random.FRandRange(-42, 42), Random.FRandRange(0, 360),
				Random.FRandRange(-36, 36));
			AddInstance(FlowerGroup, TreeTransform.TransformPosition(LocalPosition),
				FVector(FlowerScale, FlowerScale * Random.FRandRange(0.88f, 1.07f), FlowerScale),
				(FRotator(0, Layout.Yaw, 0).Quaternion() * FlowerRotation.Quaternion()).Rotator());
		}
		for (int32 StoneIndex = 0; StoneIndex < 7; ++StoneIndex)
		{
			const float Angle = StoneIndex * TWO_PI / 7 + Tree * 0.83f;
			AddInstance(Shore, Layout.Root + FVector(FMath::Cos(Angle) * 55, FMath::Sin(Angle) * 49, -8),
				FVector(0.54f, 0.43f, 0.29f) * Layout.Scale, FRotator(0, StoneIndex * 47, 0));
		}
	}

	// 中景矮花丛只贴水岸，近景枝花位于镜头两侧，中央表演区保持无遮挡。
	const FVector ShrubCenters[] = {
		FVector(282, 355, -6), FVector(-319, 466, -5), FVector(292, 740, -4), FVector(-385, 850, -7)
	};
	for (int32 Bush = 0; Bush < UE_ARRAY_COUNT(ShrubCenters); ++Bush)
	{
		for (int32 Cluster = 0; Cluster < 34; ++Cluster)
		{
			const float Angle = Cluster * 2.3999632f + Bush;
			const float Radius = FMath::Sqrt((Cluster + 0.5f) / 34.0f);
			const FVector Offset(FMath::Cos(Angle) * Radius * 60, FMath::Sin(Angle) * Radius * 42,
				38 - 19 * Radius * Radius + Random.FRandRange(-8, 9));
			AddInstance(Cluster % 4 ? FlowersPink : FlowersIvory, ShrubCenters[Bush] + Offset,
				FVector(Random.FRandRange(0.61f, 0.87f)), FRotator(Random.FRandRange(-30, 30), Cluster * 23, 0));
		}
		for (int32 Stem = 0; Stem < 5; ++Stem)
			Branch(ShrubCenters[Bush] + FVector(0, 0, -15),
				ShrubCenters[Bush] + FVector((Stem - 2) * 15, Stem % 2 ? 12 : -12, 32), 0.72f);
	}
	const FVector ForegroundStarts[] = { FVector(110, -299, 142), FVector(-20, -290, 110) };
	const FVector ForegroundEnds[] = { FVector(91, -283, 150), FVector(-2, -303, 145) };
	for (int32 Side = 0; Side < UE_ARRAY_COUNT(ForegroundStarts); ++Side)
	{
		Branch(ForegroundStarts[Side], ForegroundEnds[Side], 1.15f);
		for (int32 Twig = 0; Twig < 3; ++Twig)
		{
			const FVector Joint = FMath::Lerp(ForegroundStarts[Side], ForegroundEnds[Side], 0.40f + Twig * 0.17f);
			Branch(Joint, Joint + FVector(Side ? -8 : 6, 7 + Twig * 3, 9), 0.48f);
		}
		for (int32 Cluster = 0; Cluster < 14; ++Cluster)
		{
			const float T = 0.25f + Cluster / 13.0f * 0.73f;
			const FVector Position = FMath::Lerp(ForegroundStarts[Side], ForegroundEnds[Side], T)
				+ FVector(Random.FRandRange(-5, 5), Random.FRandRange(-4, 4), Random.FRandRange(-4, 7));
			AddInstance(Cluster % 3 ? FlowersIvory : FlowersPink, Position,
				FVector(Random.FRandRange(0.34f, 0.46f)), FRotator(Random.FRandRange(-20, 20), Cluster * 37, -25));
		}
	}
	UInstancedStaticMeshComponent* LanternPedestals = MakeGroup(TEXT("SakuraCarvedStoneLanterns"), GardenLanternPedestalMesh, Stone, false, 1, true);
	UInstancedStaticMeshComponent* LanternCaps = MakeGroup(TEXT("SakuraStoneLanternRoofs"), GardenLanternCapMesh, Stone * 0.89f, false, 1, true);
	UInstancedStaticMeshComponent* LanternFrames = MakeGroup(TEXT("SakuraStoneLanternWindowFrames"), CubeMesh, Stone * 0.90f, false);
	UInstancedStaticMeshComponent* LanternGlow = MakeGroup(TEXT("SakuraLanternWarmWindowGlass"), CubeMesh, WarmLight, true, 2.15f);
	Surface(LanternPedestals, 0.79f, 0.0f, 3.0f);
	Surface(LanternCaps, 0.76f, 0.0f, 3.0f);
	Surface(LanternFrames, 0.81f, 0.0f, 3.0f);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GardenLanternPositions); ++Index)
	{
		const FVector Origin = GardenLanternPositions[Index];
		const float Size = GardenLanternSizes[Index];
		AddInstance(LanternPedestals, Origin, FVector(Size));
		AddInstance(LanternCaps, Origin + FVector(0, 0, 133) * Size, FVector(Size));
		AddInstance(LanternGlow, Origin + FVector(0, 0, 106) * Size, FVector(0.33f, 0.33f, 0.33f) * Size);
		for (float X : { -24.0f, 24.0f })
			for (float Y : { -24.0f, 24.0f })
				AddInstance(LanternFrames, Origin + FVector(X, Y, 105) * Size, FVector(0.073f, 0.073f, 0.47f) * Size);
		for (float Z : { 83.0f, 129.0f })
			AddInstance(LanternFrames, Origin + FVector(0, 0, Z) * Size, FVector(0.58f, 0.58f, 0.09f) * Size);
		AddInstance(Shore, Origin + FVector(0, 0, 159) * Size, FVector(0.16f, 0.16f, 0.20f) * Size, FRotator(0, Index * 21, 0));
	}
	UInstancedStaticMeshComponent* PendantGlow = MakeGroup(TEXT("SakuraPavilionPendantPaper"), CubeMesh, WarmLight, true, 1.48f);
	UInstancedStaticMeshComponent* PendantFrames = MakeGroup(TEXT("SakuraPavilionPendantWood"), CubeMesh, DarkWood, false, 1, true);
	Surface(PendantFrames, 0.64f, 0.0f, 1.0f);
	for (float X : { -162.0f, -108.0f, -54.0f, 0.0f, 54.0f, 108.0f, 162.0f })
	{
		PavilionPart(PendantGlow, FVector(X, -108, 248), FVector(0.25f, 0.25f, 0.42f));
		for (float DX : { -15.0f, 15.0f })
			for (float DY : { -15.0f, 15.0f })
				PavilionPart(PendantFrames, FVector(X + DX, -108 + DY, 248), FVector(0.034f, 0.034f, 0.49f));
		for (float Z : { 223.0f, 273.0f })
			PavilionPart(PendantFrames, FVector(X, -108, Z), FVector(0.36f, 0.36f, 0.055f));
		for (int32 Slat = 0; Slat < 3; ++Slat)
			PavilionPart(PendantFrames, FVector(X, -124, 232 + Slat * 16), FVector(0.31f, 0.030f, 0.026f));
		PavilionPart(PendantFrames, FVector(X, -108, 299), FVector(0.022f, 0.022f, 0.52f));
	}
	for (float X : { 235.0f, 300.0f, 365.0f })
	{
		PavilionPart(PendantGlow, FVector(X, 20, 225), FVector(0.23f, 0.23f, 0.39f));
		for (float DX : { -14.0f, 14.0f })
			for (float DY : { -14.0f, 14.0f })
				PavilionPart(PendantFrames, FVector(X + DX, 20 + DY, 225), FVector(0.03f, 0.03f, 0.43f));
		for (float Z : { 203.0f, 247.0f })
			PavilionPart(PendantFrames, FVector(X, 20, Z), FVector(0.31f, 0.31f, 0.045f));
		PavilionPart(PendantFrames, FVector(X, 20, 270), FVector(0.022f, 0.022f, 0.43f));
	}

	UInstancedStaticMeshComponent* Ripples = MakeGroup(TEXT("SakuraQuietWaterRipples"), GardenWaterRippleMesh,
		FLinearColor(0.040f, 0.078f, 0.105f), false);
	Surface(Ripples, 0.13f, 0.28f, 4.0f);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		const float Side = Index % 2 ? 1.0f : -1.0f;
		const float Size = 0.44f + Index % 3 * 0.14f;
		AddAnimated(Ripples, FVector(Side * (310 + Index % 3 * 104), -95 + Index * 85, -27.7f),
			FVector(Size, Size * 0.73f, 1.0f), EMotion::Ripple, Index * 0.72f);
	}
	UInstancedStaticMeshComponent* Petals = MakeGroup(TEXT("SakuraNaturalFallingPetals"), GardenPetalMesh,
		FLinearColor(0.69f, 0.40f, 0.44f), false, 1, true);
	Surface(Petals, 0.71f, 0.0f, 5.0f);
	for (int32 Index = 0; Index < 28; ++Index)
	{
		const float Phase = Index * 2.39996f;
		AddAnimated(Petals, FVector(FMath::Cos(Phase) * (245 + Index % 5 * 82), -135 + Index % 7 * 104,
			55 + Index % 13 * 37), FVector(0.26f + Index % 3 * 0.055f), EMotion::Petal, Phase,
			FRotator(Index * 47, Index * 61, Index * 17));
	}
	for (int32 Index = 0; Index < 72; ++Index)
	{
		const float X = Random.FRandRange(-194, 194);
		const float Y = Random.FRandRange(-260, 180);
		AddInstance(Petals, FVector(X, Y, 0.9f), FVector(Random.FRandRange(0.15f, 0.27f)),
			FRotator(Random.FRandRange(-4, 4), Random.FRandRange(0, 360), Random.FRandRange(-5, 5)));
	}
}

void AMMDStageActor::BuildStageBackdrop()
{
	UMaterialInterface* Parent = RenderPreset ? RenderPreset->BackdropMaterial.Get() : nullptr;
	if (!Parent) Parent = DefaultBackdropMaterial;
	if (!Parent) Parent = LoadObject<UMaterialInterface>(nullptr, BackdropPath, nullptr, LOAD_NoWarn);
	if (!Parent) return;
	FLinearColor SkyColor(0.022f, 0.040f, 0.11f);
	FLinearColor HorizonColor(0.10f, 0.075f, 0.16f);
	if (Theme == EMMDStageTheme::MoonMirror)
	{
		SkyColor = FLinearColor(0.040f, 0.090f, 0.20f);
		HorizonColor = FLinearColor(0.16f, 0.28f, 0.38f);
	}
	else if (Theme == EMMDStageTheme::SakuraTheater)
	{
		SkyColor = FLinearColor(0.006f, 0.012f, 0.035f);
		HorizonColor = FLinearColor(0.025f, 0.035f, 0.07f);
	}
	UInstancedStaticMeshComponent* Group = MakeGroup(TEXT("StageAtmosphereBackdrop"), CubeMesh, SkyColor, true);
	if (!Group) return;
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, this);
	Material->SetVectorParameterValue(TEXT("StageColor"), SkyColor);
	Material->SetVectorParameterValue(TEXT("HorizonColor"), HorizonColor);
	Material->SetScalarParameterValue(TEXT("BackdropTheme"), static_cast<float>(Theme));
	Material->SetScalarParameterValue(TEXT("EmissiveStrength"), 1.0f);
	UpdateSurfaceBasis(Material, GetActorTransform());
	Group->SetMaterial(0, Material);
	if (!StageMaterials.IsEmpty())
	{
		StageMaterials.Last() = Material;
		MaterialEmissions.Last() = 0.0f;
	}
	// 远景载体在全部建筑后方，不投影、不参与碰撞；宽镜头也不会露出黑边。
	if (Theme == EMMDStageTheme::SakuraTheater)
		AddInstance(Group, FVector(0, 1800, 570), FVector(42.0f, 0.40f, 14.0f));
	else
		AddInstance(Group, FVector(0, 1200, 450), FVector(44.0f, 0.40f, 24.0f));
	for (float Side : { -1.0f, 1.0f })
		AddInstance(Group, FVector(Side * 2500, 880, 450), FVector(12.8f, 0.40f, 24.0f), FRotator(0, -Side * 45, 0));
}

void AMMDStageActor::ConfigureLighting()
{
	if (!KeyLight || !FillLight || !RimLight || !PostProcess) return;
	if (GardenSkyLight)
	{
		const bool bGarden = Theme == EMMDStageTheme::SakuraTheater;
		if (bGarden)
		{
			if (!GardenSkyCubemap && FPackageName::DoesPackageExist(TEXT("/Ue5MMDTools/Stages/Textures/T_Garden_NightSky")))
				GardenSkyCubemap = LoadObject<UTextureCube>(nullptr, GardenSkyPath, nullptr, LOAD_NoWarn);
			UTextureCube* Sky = GardenSkyCubemap ? GardenSkyCubemap.Get() : FallbackSkyCubemap.Get();
			if (GardenSkyLight->Cubemap != Sky) GardenSkyLight->SetCubemap(Sky);
			GardenSkyLight->SetLightColor(FLinearColor(0.48f, 0.62f, 0.9f));
			if (!GardenSkyLight->bLowerHemisphereIsBlack)
			{
				// UE 此开关实际启用下半球纯色；关闭时颜色参数不生效，会引入 HDR 下半球补光。
				GardenSkyLight->bLowerHemisphereIsBlack = true;
				GardenSkyLight->RecaptureSky();
			}
			GardenSkyLight->SetLowerHemisphereColor(FLinearColor(0.06f, 0.035f, 0.025f));
		}
		GardenSkyLight->SetIntensity(bGarden ? 0.28f : 0.0f);
		GardenSkyLight->SetVisibility(bGarden);
	}
	const FRotator DefaultKey(-38.0f, 40.0f, 0.0f);
	const FRotator DefaultFill(-20.0f, 140.0f, 0.0f);
	const FRotator DefaultRim(-12.0f, -90.0f, 0.0f);
	KeyLight->SetRelativeRotation(SafeLightRotation(RenderPreset ? RenderPreset->KeyLightRotation : DefaultKey, DefaultKey));
	FillLight->SetRelativeRotation(SafeLightRotation(RenderPreset ? RenderPreset->FillLightRotation : DefaultFill, DefaultFill));
	RimLight->SetRelativeRotation(SafeLightRotation(RenderPreset ? RenderPreset->RimLightRotation : DefaultRim, DefaultRim));
	KeyLight->SetCastShadows(true);
	FillLight->SetCastShadows(false);
	RimLight->SetCastShadows(false);
	KeyLight->SetSpecularScale(Theme == EMMDStageTheme::SakuraTheater ? 0.28f : 1.0f);
	FillLight->SetSpecularScale(Theme == EMMDStageTheme::SakuraTheater ? 0.12f : 1.0f);
	RimLight->SetSpecularScale(Theme == EMMDStageTheme::SakuraTheater ? 0.16f : 1.0f);
	if (KeyLight->ContactShadowLength != 0.08f || KeyLight->ContactShadowLengthInWS)
	{
		KeyLight->ContactShadowLength = 0.08f;
		KeyLight->ContactShadowLengthInWS = false;
		KeyLight->MarkRenderStateDirty();
	}
	KeyLight->SetIntensity(RenderPreset ? SafeScalar(RenderPreset->KeyLightIntensity, 3.4f, 0.0f, 10.0f) : 3.4f);
	FillLight->SetIntensity(RenderPreset ? SafeScalar(RenderPreset->FillLightIntensity, 0.8f, 0.0f, 10.0f) : 0.8f);
	RimLight->SetIntensity(RenderPreset ? SafeScalar(RenderPreset->RimLightIntensity, 1.8f, 0.0f, 10.0f) : 1.8f);
	KeyLight->SetLightColor(RenderPreset ? RenderPreset->KeyLightColor : FLinearColor(1.0f, 0.92f, 0.87f));
	FillLight->SetLightColor(RenderPreset ? RenderPreset->FillLightColor : FLinearColor(0.69f, 0.84f, 1.0f));
	RimLight->SetLightColor(RenderPreset ? RenderPreset->RimLightColor : ThemeAccent(Theme));
	FPostProcessSettings& Settings = PostProcess->Settings;
	Settings.bOverride_AutoExposureMethod = true;
	Settings.AutoExposureMethod = AEM_Manual;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Settings.AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = RenderPreset ? SafeScalar(RenderPreset->ExposureBias, 0.25f, -4.0f, 4.0f) : 0.25f;
	Settings.bOverride_BloomIntensity = true;
	Settings.BloomIntensity = RenderPreset ? FMath::Clamp(RenderPreset->BloomIntensity, 0.0f, 2.0f) : 0.16f;
	Settings.bOverride_BloomThreshold = true;
	Settings.BloomThreshold = Theme == EMMDStageTheme::SakuraTheater ? 0.60f : 0.70f;
	Settings.bOverride_VignetteIntensity = true;
	Settings.VignetteIntensity = Theme == EMMDStageTheme::SakuraTheater ? 0.20f : 0.12f;
	Settings.bOverride_FilmSlope = true;
	Settings.FilmSlope = 0.86f;
	Settings.bOverride_FilmToe = true;
	Settings.FilmToe = RenderPreset ? SafeScalar(RenderPreset->FilmToe, 0.45f, 0.0f, 1.0f) : 0.45f;
	Settings.bOverride_ColorSaturation = true;
	Settings.bOverride_ColorContrast = true;
	Settings.bOverride_ColorGamma = true;
	Settings.bOverride_ColorGain = true;
	Settings.bOverride_WhiteTemp = true;
	const float Saturation = RenderPreset ? SafeScalar(RenderPreset->ColorGradingSaturation, 1.0f, 0.0f, 2.0f) : 1.0f;
	const float Contrast = RenderPreset ? SafeScalar(RenderPreset->ColorGradingContrast, 1.03f, 0.5f, 1.5f) : 1.03f;
	const float Gamma = RenderPreset ? SafeScalar(RenderPreset->ColorGradingGamma, 1.0f, 0.5f, 1.5f) : 1.0f;
	const FLinearColor Gain = RenderPreset ? RenderPreset->ColorGradingGain : FLinearColor::White;
	Settings.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0f);
	Settings.ColorContrast = FVector4(Contrast, Contrast, Contrast, 1.0f);
	Settings.ColorGamma = FVector4(Gamma, Gamma, Gamma, 1.0f);
	Settings.ColorGain = FVector4(SafeScalar(Gain.R, 1.0f, 0.0f, 4.0f), SafeScalar(Gain.G, 1.0f, 0.0f, 4.0f),
		SafeScalar(Gain.B, 1.0f, 0.0f, 4.0f), 1.0f);
	Settings.WhiteTemp = RenderPreset ? SafeScalar(RenderPreset->WhiteTemperature, 6500.0f, 1500.0f, 15000.0f) : 6500.0f;
	Settings.bOverride_ScreenSpaceReflectionIntensity = true;
	Settings.ScreenSpaceReflectionIntensity = 85.0f;
	Settings.bOverride_ScreenSpaceReflectionQuality = true;
	Settings.ScreenSpaceReflectionQuality = 75.0f;
	Settings.bOverride_ScreenSpaceReflectionMaxRoughness = true;
	Settings.ScreenSpaceReflectionMaxRoughness = 0.65f;
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;
}

void AMMDStageActor::RebuildStage()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !StageRoot) return;
	if (RenderPreset && RenderPreset->bOverrideStageTheme) Theme = RenderPreset->Theme;
	ClearGeneratedComponents();
	const FLinearColor Base = RenderPreset ? RenderPreset->BaseTint : ThemeBase(Theme);
	const FLinearColor Accent = RenderPreset ? RenderPreset->AccentTint : ThemeAccent(Theme);
	const FLinearColor Secondary = RenderPreset ? RenderPreset->SecondaryTint : ThemeSecondary(Theme);
	if (Theme == EMMDStageTheme::MoonMirror) BuildMoonMirror(Base, Accent, Secondary);
	else if (Theme == EMMDStageTheme::SakuraTheater) BuildSakuraTheater(Base, Accent, Secondary);
	else BuildNeonConcert(Base, Accent, Secondary);
	BuildStageBackdrop();
	const bool bGarden = Theme == EMMDStageTheme::SakuraTheater;
	const FVector GardenLightPositions[] = {
		FVector(-170, -150, 145), FVector(230, 40, 105),
		GardenLanternPositions[2] + FVector(0, 0, 106 * GardenLanternSizes[2]),
		GardenLanternPositions[3] + FVector(0, 0, 106 * GardenLanternSizes[3]),
		GardenLanternPositions[6] + FVector(0, 0, 106 * GardenLanternSizes[6]),
		GardenLanternPositions[7] + FVector(0, 0, 106 * GardenLanternSizes[7]),
		GardenPavilionPoint(FVector(-108, -108, 248)), GardenPavilionPoint(FVector(108, -108, 248)),
		GardenPavilionPoint(FVector(235, 20, 225)), GardenPavilionPoint(FVector(365, 20, 225))
	};
	const float GardenLightRadii[] = { 560, 460, 205, 205, 160, 150, 220, 220, 180, 180 };
	const float GardenLightScales[] = { 1, 1, 0.90f, 0.90f, 0.68f, 0.58f, 1.15f, 1.10f, 0.88f, 0.80f };
	const int32 AccentLightCount = bGarden ? UE_ARRAY_COUNT(GardenLightPositions) : 4;
	for (int32 Index = 0; Index < AccentLightCount; ++Index)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, NAME_None, RF_Transient);
		Light->ComponentTags.Add(GeneratedTag);
		Light->SetupAttachment(StageRoot);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetRelativeLocation(FVector(Index % 2 ? 425 : -425, Index < 2 ? -80 : 350, Index < 2 ? 190 : 275));
		Light->SetAttenuationRadius(850.0f);
		Light->SetLightColor(Index % 2 ? Secondary : Accent);
		float IntensityScale = 1.0f;
		float FixedIntensity = -1.0f;
		if (bGarden)
		{
			Light->SetRelativeLocation(GardenLightPositions[Index]);
			Light->SetAttenuationRadius(GardenLightRadii[Index]);
			Light->SetLightColor(Index == 0 ? FLinearColor(1.0f, 0.48f, 0.18f)
				: Index == 1 ? FLinearColor(1.0f, 0.58f, 0.28f) : FLinearColor(1.0f, 0.51f, 0.17f));
			Light->SetSpecularScale(Index < 2 ? 0.22f : 0.16f);
			// Unitless低强度灯使用平缓局部衰减，让木梁与石阶也接收到实际引擎照明。
			Light->SetUseInverseSquaredFalloff(false);
			Light->SetLightFalloffExponent(2.0f);
			IntensityScale = GardenLightScales[Index];
			if (Index < 2) FixedIntensity = Index == 0 ? 0.90f : 0.35f;
		}
		Light->SetIntensityUnits(ELightUnits::Unitless);
		Light->SetIntensity(FixedIntensity >= 0 ? FixedIntensity
			: (RenderPreset ? RenderPreset->AccentLightIntensity : 0.70f)
				* (bGarden ? 1.85f : 1.0f) * IntensityScale);
		Light->SetCastShadows(false);
		Light->SetVisibility(true);
		AddInstanceComponent(Light);
		Light->RegisterComponent();
		AccentLights.Add(Light);
		AccentLightScales.Add(IntensityScale);
		AccentLightOverrides.Add(FixedIntensity);
	}
	ConfigureLighting();
	EvaluateStageAtTime(bUseManualTime ? ManualTimeSeconds : static_cast<float>(ElapsedSeconds));
}

void AMMDStageActor::EvaluateStageAtTime(float TimeSeconds)
{
	const double SafeTime = FMath::IsFinite(TimeSeconds) ? FMath::Max(static_cast<double>(TimeSeconds), 0.0) : 0.0;
	const float Speed = FMath::IsFinite(AnimationSpeed) ? FMath::Clamp(AnimationSpeed, 0.0f, 4.0f) : 1.0f;
	const float BeatRate = FMath::IsFinite(BPM) ? FMath::Clamp(BPM, 1.0f, 300.0f) / 60.0f : 118.0f / 60.0f;
	const float StageTime = static_cast<float>(FMath::Fmod(SafeTime * static_cast<double>(Speed), StageTimePeriod));
	const float Beat = StageTime * BeatRate;
	TSet<UInstancedStaticMeshComponent*> UpdatedGroups;
	for (const FAnimatedInstance& Item : AnimatedInstances)
	{
		UInstancedStaticMeshComponent* Group = Item.Component.Get();
		if (!Group || Item.Index < 0 || Item.Index >= Group->GetInstanceCount()) continue;
		FTransform Transform = Item.BaseTransform;
		FVector Position = Transform.GetLocation();
		FVector Scale = Transform.GetScale3D();
		if (Item.Motion == EMotion::Equalizer)
		{
			const float Height = 40.0f + 100.0f * FMath::Square(0.5f + 0.5f * FMath::Sin(Beat * 1.6f + Item.Phase));
			Scale.Z = Height / 100.0f;
			Position.Z = 15.0f + Height * 0.5f;
		}
		else if (Item.Motion == EMotion::Petal)
		{
			if (Theme == EMMDStageTheme::SakuraTheater)
			{
				Position.Z = -19.0f + FMath::Fmod(Item.BaseTransform.GetLocation().Z + 19.0f + 540.0f
					- FMath::Fmod(StageTime * 18.0f, 540.0f), 540.0f);
				Position.X += FMath::Sin(StageTime * 0.37f + Item.Phase) * 14.0f;
				Position.Y += FMath::Cos(StageTime * 0.29f + Item.Phase) * 9.0f;
				Transform.SetRotation(FQuat(FRotator(StageTime * 19.0f + Item.Phase * 20,
					StageTime * 11.0f + Item.Phase * 25, StageTime * 26.0f + Item.Phase * 30)));
			}
			else
			{
				Position.Z = 30.0f + FMath::Fmod(Item.BaseTransform.GetLocation().Z - 30.0f + 420.0f
					- FMath::Fmod(Beat * 15.0f, 420.0f), 420.0f);
				Position.X += FMath::Sin(Beat * 0.42f + Item.Phase) * 32.0f;
				Position.Y += FMath::Cos(Beat * 0.37f + Item.Phase) * 16.0f;
				Transform.SetRotation(FQuat(FRotator(Beat * 14.0f + Item.Phase * 20,
					Beat * 8.0f + Item.Phase * 25, Beat * 22.0f + Item.Phase * 30)));
			}
		}
		else if (Item.Motion == EMotion::Ripple)
		{
			if (Theme == EMMDStageTheme::SakuraTheater)
			{
				const float Expansion = 1.0f + 0.055f * FMath::Sin(StageTime * 0.70f + Item.Phase);
				Scale.X *= Expansion;
				Scale.Y *= Expansion;
				Position.Z += FMath::Sin(StageTime * 0.47f + Item.Phase) * 0.06f;
			}
			else
			{
				const float Expansion = 1.0f + 0.032f * FMath::Sin(Beat * 0.7f + Item.Phase);
				Position.X *= Expansion;
				Position.Y *= Expansion;
				Scale.X *= Expansion;
			}
		}
		else if (Item.Motion == EMotion::Ring)
		{
			Position.X += FMath::Cos(Item.Phase) * FMath::Sin(Beat * 0.65f) * 4.5f;
			Position.Z += FMath::Sin(Item.Phase) * FMath::Sin(Beat * 0.65f) * 4.5f;
		}
		else if (Item.Motion == EMotion::Beam)
		{
			const FQuat Swing = FQuat(FRotator(FMath::Sin(Beat * 0.23f + Item.Phase) * 9.0f,
				0, FMath::Cos(Beat * 0.19f + Item.Phase) * 11.0f));
			const FVector Apex = Position + Item.BaseTransform.GetRotation().RotateVector(FVector(0, 0, Scale.Z * 50));
			Position = Apex + Swing.RotateVector(Position - Apex);
			Transform.SetRotation(Swing * Item.BaseTransform.GetRotation());
		}
		else
		{
			const bool bLantern = Theme == EMMDStageTheme::SakuraTheater;
			Position.X += FMath::Sin(Beat * 0.37f + Item.Phase) * (bLantern ? 3.0f : 16.0f);
			Position.Z += FMath::Sin(Beat * 0.55f + Item.Phase) * (bLantern ? 3.5f : 12.0f);
		}
		Transform.SetLocation(Position);
		Transform.SetScale3D(Scale);
		Group->UpdateInstanceTransform(Item.Index, Transform, false, false, true);
		UpdatedGroups.Add(Group);
	}
	for (UInstancedStaticMeshComponent* Group : UpdatedGroups) Group->MarkRenderStateDirty();
	const float Pulse = Theme == EMMDStageTheme::SakuraTheater
		? 0.985f + 0.015f * FMath::Sin(StageTime * 0.75f)
		: 0.90f + 0.10f * FMath::Sin(Beat * PI);
	for (int32 Index = 0; Index < StageMaterials.Num(); ++Index)
	{
		UMaterialInstanceDynamic* Material = StageMaterials[Index];
		if (!Material) continue;
		UpdateSurfaceBasis(Material, GetActorTransform());
		Material->SetScalarParameterValue(TEXT("StageTime"), StageTime);
		Material->SetScalarParameterValue(TEXT("UseStageTime"), 1.0f);
		if (MaterialEmissions.IsValidIndex(Index) && MaterialEmissions[Index] > 0.0f)
			Material->SetScalarParameterValue(TEXT("EmissiveStrength"), MaterialEmissions[Index] * Pulse);
	}
	for (int32 Index = 0; Index < AccentLights.Num(); ++Index)
	{
		UPointLightComponent* Light = AccentLights[Index];
		if (!Light) continue;
		const float FixedIntensity = AccentLightOverrides.IsValidIndex(Index) ? AccentLightOverrides[Index] : -1.0f;
		const float IntensityScale = AccentLightScales.IsValidIndex(Index) ? AccentLightScales[Index] : 1.0f;
		Light->SetIntensity(FixedIntensity >= 0 ? FixedIntensity
			: (RenderPreset ? FMath::Clamp(RenderPreset->AccentLightIntensity, 0.0f, 10.0f) : 0.70f)
				* (Theme == EMMDStageTheme::SakuraTheater ? 1.85f : 1.0f) * IntensityScale * Pulse);
	}
}

void AMMDStageActor::ApplyStageRenderPreset(UMMDStageRenderPreset* Preset)
{
	if (!IsValid(Preset)) return;
	RenderPreset = Preset;
	if (Preset->bOverrideStageTheme) Theme = Preset->Theme;
	RebuildStage();
	for (int32 Index = StyledCharacters.Num() - 1; Index >= 0; --Index)
	{
		USkeletalMeshComponent* Component = StyledCharacters[Index].Component.Get();
		if (!Component) StyledCharacters.RemoveAtSwap(Index);
		else UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Preset, StyledCharacters[Index].bReplaceMaterials);
	}
}

bool AMMDStageActor::ApplyCharacterRenderPreset(USkeletalMeshComponent* Component, bool bReplaceMaterials)
{
	if (!UMMDStageLibrary::ApplyCharacterRenderPreset(Component, RenderPreset, bReplaceMaterials)) return false;
	FStyledCharacter* Existing = StyledCharacters.FindByPredicate([Component](const FStyledCharacter& Item)
		{ return Item.Component.Get() == Component; });
	if (!Existing)
	{
		FStyledCharacter Item;
		Item.Component = Component;
		Item.bReplaceMaterials = bReplaceMaterials;
		StyledCharacters.Add(Item);
	}
	else Existing->bReplaceMaterials = bReplaceMaterials;
	return true;
}

bool AMMDStageActor::RestoreCharacterMaterials(USkeletalMeshComponent* Component)
{
	StyledCharacters.RemoveAll([Component](const FStyledCharacter& Item)
		{ return !Item.Component.IsValid() || Item.Component.Get() == Component; });
	return UMMDStageLibrary::RestoreCharacterMaterials(Component);
}
