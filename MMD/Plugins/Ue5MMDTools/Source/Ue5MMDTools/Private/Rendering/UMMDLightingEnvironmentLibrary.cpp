#include "Rendering/UMMDLightingEnvironmentLibrary.h"

#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/Light.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "PreviewScene.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Camera/CameraActor.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "Factories/WorldFactory.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
	enum class EMMDLightKind : uint8
	{
		Directional,
		Point,
		Spot,
		Rect,
		Sky,
	};

	// 单盏灯的规格。坐标：UE 世界坐标（X=右 Y=前 Z=上），模型默认在原点。
	// 平行光 Rotation 的 Forward = 光传播方向（从光源指向场景，同引擎约定）。
	// 点/聚/矩形光 Location 为灯位置；聚光/矩形光的 Forward 指向要照亮的目标。
	struct FMMDLightSpec
	{
		EMMDLightKind Kind = EMMDLightKind::Directional;
		FVector Location = FVector::ZeroVector;
		FVector Aim = FVector(0.0f, 0.0f, 80.0f); // 聚光/矩形光朝向的目标点（默认模型躯干）
		FRotator Rotation = FRotator::ZeroRotator;
		FLinearColor Color = FLinearColor::White;
		float Intensity = 1.0f;
		float Radius = 600.0f;       // 点/聚/矩形光衰减半径（cm）
		float InnerCone = 20.0f;     // 聚光内锥半角（度）
		float OuterCone = 35.0f;     // 聚光外锥半角（度）
		float SourceWidth = 64.0f;   // 矩形光宽度（cm）
		float SourceHeight = 64.0f;  // 矩形光高度（cm）
		float SourceAngle = 0.5357f; // 平行光角直径（度）；阴天使用更大角度获得软阴影
		const TCHAR* CubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap");
		float CubemapAngle = 0.0f;
	};

	// 固定 EV100=10 下的统一强度标尺：平行光用 lux，本地点/聚/面光用 lumen。
	// 37440 经 18% 灰球校准：Studio 的 3.2/1.15 档对应约 120k/43k lm。
	float GetPhysicalIntensity(const FMMDLightSpec& Spec)
	{
		return Spec.Intensity * (Spec.Kind == EMMDLightKind::Directional ? 2000.0f : 37440.0f);
	}

	FMMDLightSpec MakeDir(FRotator Rot, const FLinearColor& Color, float Intensity, float SourceAngle = 0.5357f)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Directional;
		S.Rotation = Rot;
		S.Color = Color;
		S.Intensity = Intensity;
		S.SourceAngle = SourceAngle;
		return S;
	}

	FMMDLightSpec MakePoint(const FVector& Loc, const FLinearColor& Color, float Intensity, float Radius)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Point;
		S.Location = Loc;
		S.Color = Color;
		S.Intensity = Intensity;
		S.Radius = Radius;
		return S;
	}

	FMMDLightSpec MakeSpot(const FVector& Loc, const FVector& Aim, const FLinearColor& Color, float Intensity, float Radius, float InnerCone, float OuterCone)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Spot;
		S.Location = Loc;
		S.Aim = Aim;
		S.Rotation = (Aim - Loc).Rotation();
		S.Color = Color;
		S.Intensity = Intensity;
		S.Radius = Radius;
		S.InnerCone = InnerCone;
		S.OuterCone = OuterCone;
		return S;
	}

	FMMDLightSpec MakeRect(const FVector& Loc, const FVector& Aim, const FLinearColor& Color, float Intensity, float Radius, float W, float H)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Rect;
		S.Location = Loc;
		S.Aim = Aim;
		// UE5.8 RectLight 的组件 Forward 指向受光目标；采集时再取 -Forward 作为点到灯方向。
		S.Rotation = (Aim - Loc).Rotation();
		S.Color = Color;
		S.Intensity = Intensity;
		S.Radius = Radius;
		S.SourceWidth = W;
		S.SourceHeight = H;
		return S;
	}

	FMMDLightSpec MakeSky(const FLinearColor& Color, float Intensity,
		const TCHAR* CubemapPath = TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"),
		float CubemapAngle = 0.0f)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Sky;
		S.Color = Color;
		S.Intensity = Intensity;
		S.CubemapPath = CubemapPath;
		S.CubemapAngle = CubemapAngle;
		return S;
	}

	TArray<FMMDLightSpec> BuildEnvironment(EMMDLightingEnvironment Environment)
	{
		TArray<FMMDLightSpec> Specs;

		switch (Environment)
		{
		case EMMDLightingEnvironment::Studio3Point:
			// 中性影棚：大面积主光、低一档冷辅光、窄轮廓光。D65 天空只负责抬黑位。
			Specs.Add(MakeRect(FVector(220.0f, 260.0f, 260.0f), FVector(0.0f, 0.0f, 90.0f), FLinearColor(1.0f, 0.98f, 0.95f), 3.2f, 1000.0f, 180.0f, 240.0f));
			Specs.Add(MakeRect(FVector(-260.0f, 180.0f, 170.0f), FVector(0.0f, 0.0f, 90.0f), FLinearColor(0.78f, 0.86f, 1.0f), 1.15f, 1000.0f, 220.0f, 260.0f));
			Specs.Add(MakeSpot(FVector(120.0f, -260.0f, 260.0f), FVector(0.0f, 0.0f, 110.0f), FLinearColor(0.82f, 0.90f, 1.0f), 2.1f, 900.0f, 18.0f, 32.0f));
			Specs.Add(MakeSky(FLinearColor(0.72f, 0.76f, 0.82f), 0.55f));
			break;

		case EMMDLightingEnvironment::Daylight:
			// 户外日光：强太阳(唯一直射光) + 反向天空补光(点光) + 天光。
			Specs.Add(MakeDir(FRotator(-48.0f, -32.0f, 0.0f), FLinearColor(1.0f, 0.96f, 0.90f), 3.8f));
			Specs.Add(MakeSky(FLinearColor(0.62f, 0.76f, 1.0f), 1.15f));
			break;

		case EMMDLightingEnvironment::OvercastSoft:
			// 阴天柔光：顶部柔光 + 高天光，几乎无硬阴影。
			Specs.Add(MakeDir(FRotator(-78.0f, -10.0f, 0.0f), FLinearColor(0.86f, 0.90f, 0.98f), 2.4f, 5.0f));
			Specs.Add(MakeSky(FLinearColor(0.72f, 0.78f, 0.88f), 2.2f));
			break;

		case EMMDLightingEnvironment::IndoorWarm:
			// 室内暖光：暖色主点光 + 面光补光 + 暖背光。
			Specs.Add(MakeRect(FVector(180.0f, 170.0f, 220.0f), FVector(0.0f, 0.0f, 85.0f), FLinearColor(1.0f, 0.72f, 0.48f), 3.0f, 850.0f, 150.0f, 190.0f));
			Specs.Add(MakeRect(FVector(-220.0f, 120.0f, 170.0f), FVector(0.0f, 0.0f, 85.0f), FLinearColor(0.58f, 0.70f, 1.0f), 0.85f, 850.0f, 180.0f, 220.0f));
			Specs.Add(MakePoint(FVector(0.0f, -220.0f, 190.0f), FLinearColor(1.0f, 0.55f, 0.32f), 1.35f, 650.0f));
			Specs.Add(MakeSky(FLinearColor(0.45f, 0.48f, 0.56f), 0.3f));
			break;

		case EMMDLightingEnvironment::NeonNight:
			// 赛博霓虹：品红 + 青两侧点光 + 顶部紫色聚光，无主光、无天光（暗基底）。
			Specs.Add(MakePoint(FVector(-160.0f, -40.0f, 170.0f), FLinearColor(1.0f, 0.15f, 0.65f), 4.0f, 450.0f));
			Specs.Add(MakePoint(FVector(160.0f, -40.0f, 170.0f), FLinearColor(0.15f, 0.90f, 1.0f), 4.0f, 450.0f));
			Specs.Add(MakeSpot(FVector(0.0f, 200.0f, 280.0f), FVector(0.0f, 0.0f, 40.0f), FLinearColor(0.65f, 0.25f, 1.0f), 4.5f, 500.0f, 25.0f, 40.0f));
			break;

		case EMMDLightingEnvironment::RimSilhouette:
			// 轮廓剪影：强背光 + 极弱正面补光，突出轮廓。
			Specs.Add(MakeDir(FRotator(-25.0f, 160.0f, 0.0f), FLinearColor(0.70f, 0.85f, 1.0f), 6.0f));
			Specs.Add(MakePoint(FVector(60.0f, -40.0f, 140.0f), FLinearColor(0.25f, 0.30f, 0.40f), 0.4f, 300.0f));
			break;

		case EMMDLightingEnvironment::GoldenHour:
			// 黄金时刻：低角度暖橙太阳(唯一直射光) + 暖色点光补光 + 暖色天光。
			Specs.Add(MakeDir(FRotator(-14.0f, -30.0f, 0.0f), FLinearColor(1.0f, 0.50f, 0.20f), 3.6f));
			Specs.Add(MakePoint(FVector(300.0f, -200.0f, 120.0f), FLinearColor(1.0f, 0.72f, 0.50f), 0.8f, 1200.0f));
			Specs.Add(MakeSky(FLinearColor(1.0f, 0.68f, 0.46f), 0.8f,
				TEXT("/Engine/MapTemplates/Sky/SunsetAmbientCubemap.SunsetAmbientCubemap"), 25.0f));
			break;

		case EMMDLightingEnvironment::HorrorGreen:
			// 恐怖绿光：底部绿色上打光 + 冷色顶光(唯一直射光) + 绿色聚光背光。
			Specs.Add(MakePoint(FVector(0.0f, 0.0f, -80.0f), FLinearColor(0.10f, 0.90f, 0.35f), 4.0f, 450.0f));
			Specs.Add(MakeDir(FRotator(-60.0f, 0.0f, 0.0f), FLinearColor(0.55f, 0.72f, 0.88f), 1.2f));
			Specs.Add(MakeSpot(FVector(0.0f, -350.0f, 200.0f), FVector(0.0f, 0.0f, 80.0f), FLinearColor(0.20f, 0.80f, 0.40f), 2.0f, 900.0f, 20.0f, 45.0f));
			break;

		case EMMDLightingEnvironment::None:
		default:
			break;
		}

		return Specs;
	}

	AActor* SpawnLight(UWorld* World, const FMMDLightSpec& Spec, bool bPersistent = false)
	{
		if (!World)
		{
			return nullptr;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (bPersistent)
		{
			Params.ObjectFlags |= RF_Transactional; // 持久：保存进关卡，可在编辑器里编辑
		}
		else
		{
			Params.ObjectFlags |= RF_Transient; // 临时：不污染关卡，不参与保存
		}

		AActor* Actor = nullptr;
		switch (Spec.Kind)
		{
		case EMMDLightKind::Directional: Actor = World->SpawnActor<ADirectionalLight>(Spec.Location, Spec.Rotation, Params); break;
		case EMMDLightKind::Point:        Actor = World->SpawnActor<APointLight>(Spec.Location, Spec.Rotation, Params); break;
		case EMMDLightKind::Spot:         Actor = World->SpawnActor<ASpotLight>(Spec.Location, Spec.Rotation, Params); break;
		case EMMDLightKind::Rect:         Actor = World->SpawnActor<ARectLight>(Spec.Location, Spec.Rotation, Params); break;
		case EMMDLightKind::Sky:          Actor = World->SpawnActor<ASkyLight>(Spec.Location, Spec.Rotation, Params); break;
		}

		if (!Actor)
		{
			return nullptr;
		}

		if (Spec.Kind == EMMDLightKind::Sky)
		{
			if (USkyLightComponent* Sky = Actor->FindComponentByClass<USkyLightComponent>())
			{
				Sky->SetMobility(EComponentMobility::Movable);
				Sky->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
				Sky->bLowerHemisphereIsBlack = false;
				if (UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, Spec.CubemapPath))
				{
					Sky->SetCubemap(Cubemap);
					Sky->SetSourceCubemapAngle(Spec.CubemapAngle);
				}
				// SkyLight Intensity 是无量纲倍率（引擎默认 1），不能按 lux/lumen 再乘 1000。
				// 可见 HDRI 的亮度由 Backdrop 单独控制，IBL 只使用这里的场景规格。
				Sky->SetIntensity(Spec.Intensity);
				Sky->SetLightColor(Spec.Color);
			}
		}
		else if (ULightComponent* Light = Actor->FindComponentByClass<ULightComponent>())
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetLightColor(Spec.Color);
			const float UeIntensity = GetPhysicalIntensity(Spec);
			Light->SetIntensity(UeIntensity);
			if (ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(Light))
			{
				LocalLight->SetIntensityUnits(ELightUnits::Lumens);
			}
			if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light))
			{
				PointLight->AttenuationRadius = Spec.Radius;
			}
			Light->SetCastShadows(true);
		}
		if (UDirectionalLightComponent* Directional = Actor->FindComponentByClass<UDirectionalLightComponent>())
		{
			Directional->SetLightSourceAngle(Spec.SourceAngle);
		}

		if (USpotLightComponent* Spot = Actor->FindComponentByClass<USpotLightComponent>())
		{
			Spot->SetInnerConeAngle(Spec.InnerCone);
			Spot->SetOuterConeAngle(Spec.OuterCone);
		}

		if (URectLightComponent* Rect = Actor->FindComponentByClass<URectLightComponent>())
		{
			Rect->SetSourceWidth(Spec.SourceWidth);
			Rect->SetSourceHeight(Spec.SourceHeight);
		}

#if WITH_EDITOR
		Actor->SetActorLabel(TEXT("MMDEnv_Light"));
		Actor->SetFolderPath(TEXT("MMD Lighting Environment"));
		if (bPersistent)
		{
			Actor->MarkPackageDirty();
		}
#endif

		return Actor;
	}

	// 每个世界各自跟踪已生成的环境光，避免跨世界误删。
	TMap<TWeakObjectPtr<UWorld>, TArray<TWeakObjectPtr<AActor>>> GSpawnedEnvironmentLights;

	// 创建灯光组件并加到预览场景（不 spawn actor，避免污染关卡）。
	USceneComponent* CreatePreviewLightComponent(FPreviewScene* PreviewScene, const FMMDLightSpec& Spec)
	{
		if (!PreviewScene)
		{
			return nullptr;
		}

		USceneComponent* SceneComp = nullptr;
		switch (Spec.Kind)
		{
		case EMMDLightKind::Directional: SceneComp = NewObject<UDirectionalLightComponent>(GetTransientPackage(), NAME_None, RF_Transient); break;
		case EMMDLightKind::Point:        SceneComp = NewObject<UPointLightComponent>(GetTransientPackage(), NAME_None, RF_Transient); break;
		case EMMDLightKind::Spot:         SceneComp = NewObject<USpotLightComponent>(GetTransientPackage(), NAME_None, RF_Transient); break;
		case EMMDLightKind::Rect:         SceneComp = NewObject<URectLightComponent>(GetTransientPackage(), NAME_None, RF_Transient); break;
		case EMMDLightKind::Sky:          SceneComp = NewObject<USkyLightComponent>(GetTransientPackage(), NAME_None, RF_Transient); break;
		}
		if (!SceneComp)
		{
			return nullptr;
		}

		SceneComp->SetMobility(EComponentMobility::Movable);

		if (Spec.Kind == EMMDLightKind::Sky)
		{
			if (USkyLightComponent* Sky = Cast<USkyLightComponent>(SceneComp))
			{
				Sky->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
				Sky->bLowerHemisphereIsBlack = false;
				if (UTextureCube* Cubemap = LoadObject<UTextureCube>(nullptr, Spec.CubemapPath))
				{
					Sky->SetCubemap(Cubemap);
					Sky->SetSourceCubemapAngle(Spec.CubemapAngle);
				}
				Sky->SetIntensity(Spec.Intensity);
				Sky->SetLightColor(Spec.Color);
			}
		}
		else if (ULightComponent* Light = Cast<ULightComponent>(SceneComp))
		{
			const float UeIntensity = GetPhysicalIntensity(Spec);
			Light->SetIntensity(UeIntensity);
			Light->SetLightColor(Spec.Color);
			Light->SetCastShadows(true);
			if (ULocalLightComponent* LocalLight = Cast<ULocalLightComponent>(Light))
			{
				LocalLight->SetIntensityUnits(ELightUnits::Lumens);
			}

			if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
			{
				Point->AttenuationRadius = Spec.Radius;
			}
			if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
			{
				Spot->SetInnerConeAngle(Spec.InnerCone);
				Spot->SetOuterConeAngle(Spec.OuterCone);
			}
			if (URectLightComponent* Rect = Cast<URectLightComponent>(Light))
			{
				Rect->SetSourceWidth(Spec.SourceWidth);
				Rect->SetSourceHeight(Spec.SourceHeight);
			}
		}

		if (UDirectionalLightComponent* Directional = Cast<UDirectionalLightComponent>(SceneComp))
		{
			Directional->SetLightSourceAngle(Spec.SourceAngle);
		}

		PreviewScene->AddComponent(SceneComp, FTransform(Spec.Rotation, Spec.Location));
		return SceneComp;
	}

	// 每个预览场景各自跟踪已加的环境光组件，避免跨场景误删。
	TMap<FPreviewScene*, TArray<TWeakObjectPtr<USceneComponent>>> GPreviewEnvironmentLights;

	// 按模型实际大小变换灯光规格：位置平移到模型中心并缩放，半径/面光尺寸同步缩放。
	// Scale = 模型半径 / 标准半径(100cm)，灯位 = Center + Spec.Location*Scale。
	FMMDLightSpec TransformSpec(const FMMDLightSpec& Spec, const FVector& Center, float Scale)
	{
		FMMDLightSpec Out = Spec;
		Out.Location = Center + Spec.Location * Scale;
		Out.Radius = Spec.Radius * Scale;
		Out.SourceWidth = Spec.SourceWidth * Scale;
		Out.SourceHeight = Spec.SourceHeight * Scale;
		// 聚光/矩形光的朝向目标点也要跟着模型中心走，重算朝向。
		if (Spec.Kind == EMMDLightKind::Spot || Spec.Kind == EMMDLightKind::Rect)
		{
			const FVector NewAim = Center + Spec.Aim * Scale;
			Out.Aim = NewAim;
			Out.Rotation = (NewAim - Out.Location).Rotation();
		}
		return Out;
	}

#if WITH_EDITOR
	void ConfigureLookDevPostProcessSettings(FPostProcessSettings& S)
	{
		S.bOverride_AutoExposureMethod = true;
		S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		S.bOverride_AutoExposureBias = true;
		S.AutoExposureBias = 0.0f;
		S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		S.AutoExposureApplyPhysicalCameraExposure = true;
		S.bOverride_CameraISO = true;
		S.CameraISO = 100.0f;
		S.bOverride_CameraShutterSpeed = true;
		S.CameraShutterSpeed = 60.0f;
		S.bOverride_DepthOfFieldFstop = true;
		S.DepthOfFieldFstop = 4.0f;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 0.0f;
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = 0.0f;
		S.bOverride_MotionBlurAmount = true;
		S.MotionBlurAmount = 0.0f;
		S.bOverride_DepthOfFieldScale = true;
		S.DepthOfFieldScale = 0.0f;
		S.bOverride_FilmGrainIntensity = true;
		S.FilmGrainIntensity = 0.0f;
		S.bOverride_LensFlareIntensity = true;
		S.LensFlareIntensity = 0.0f;
		S.bOverride_WhiteTemp = true;
		S.WhiteTemp = 6500.0f;
		S.bOverride_WhiteTint = true;
		S.WhiteTint = 0.0f;
	}

	void ClearGeneratedLookDevActors(UWorld* World)
	{
		if (!World)
		{
			return;
		}

		TArray<AActor*> ToRemove;
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (Actor && (Actor->IsA<ALight>() || Actor->IsA<AStaticMeshActor>() ||
				Actor->IsA<APostProcessVolume>() || Actor->IsA<ACameraActor>()))
			{
				ToRemove.Add(Actor);
			}
		}
		for (AActor* Actor : ToRemove)
		{
			World->DestroyActor(Actor, true);
		}
	}

	void BuildLookDevStage(UWorld* World, EMMDLightingEnvironment Environment)
	{
		if (!World)
		{
			return;
		}

		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UMaterialInterface* NeutralMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Ue5MMDTools/LookDev/Materials/MI_LookDevGray18.MI_LookDevGray18"));
		if (!NeutralMaterial)
		{
			NeutralMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Engine/BasicShapes/BasicShapeMaterial_Inst.BasicShapeMaterial_Inst"));
		}

		auto SpawnStageMesh = [World, Cube, NeutralMaterial](const TCHAR* Label, const FVector& Location, const FVector& Scale)
		{
			if (!Cube)
			{
				return;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transactional;
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator, Params);
			if (!Actor)
			{
				return;
			}
			Actor->SetActorScale3D(Scale);
			Actor->SetActorLabel(Label);
			Actor->SetFolderPath(TEXT("MMD LookDev/Stage"));
			UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
			Comp->SetStaticMesh(Cube);
			Comp->SetMobility(EComponentMobility::Static);
			Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Comp->SetCastShadow(true);
			if (NeutralMaterial)
			{
				Comp->SetMaterial(0, NeutralMaterial);
			}
			Actor->MarkPackageDirty();
		};

		// 12m 中性灰舞台：地面与背景墙足够大，但不加侧墙，避免遮光和错误反射。
		SpawnStageMesh(TEXT("MMDStage_Floor"), FVector(0.0f, 0.0f, -5.0f), FVector(12.0f, 12.0f, 0.1f));
		SpawnStageMesh(TEXT("MMDStage_Backdrop"), FVector(0.0f, -420.0f, 220.0f), FVector(12.0f, 0.1f, 4.5f));

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transactional;

		APostProcessVolume* PP = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (PP)
		{
			PP->SetActorLabel(TEXT("MMDLookDev_PostProcess"));
			PP->SetFolderPath(TEXT("MMD LookDev"));
			PP->bUnbound = true;
			PP->Priority = 100.0f;
			ConfigureLookDevPostProcessSettings(PP->Settings);
		}

		ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(0.0f, 450.0f, 105.0f), FRotator(0.0f, -90.0f, 0.0f), Params);
		if (Camera)
		{
			Camera->SetActorLabel(TEXT("MMDStageCamera"));
			Camera->SetFolderPath(TEXT("MMD LookDev"));
			Camera->GetCameraComponent()->SetFieldOfView(35.0f);
		}

		for (const FMMDLightSpec& Spec : BuildEnvironment(Environment))
		{
			if (AActor* Light = SpawnLight(World, Spec, true))
			{
				Light->SetActorLabel(FString::Printf(TEXT("MMDEnv_%s"), *Light->GetClass()->GetName()));
				Light->SetFolderPath(TEXT("MMD LookDev/Lights"));
			}
		}
	}
#endif
}

void UMMDLightingEnvironmentLibrary::ConfigureLookDevPostProcess(FPostProcessSettings& Settings)
{
	#if WITH_EDITOR
	ConfigureLookDevPostProcessSettings(Settings);
	Settings.bOverride_AutoExposureMethod = false;
	Settings.bOverride_AutoExposureBias = false;
	Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = false;
	Settings.bOverride_CameraISO = false;
	Settings.bOverride_CameraShutterSpeed = false;
	Settings.bOverride_DepthOfFieldFstop = false;
	#else
	Settings = FPostProcessSettings();
	#endif
}

int32 UMMDLightingEnvironmentLibrary::ApplyLightingEnvironment(UWorld* World, EMMDLightingEnvironment Environment)
{
	if (!World)
	{
#if WITH_EDITOR
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
#else
		World = GWorld;
#endif
	}

	ClearLightingEnvironment(World);

	if (!World || Environment == EMMDLightingEnvironment::None)
	{
		return 0;
	}

	const TArray<FMMDLightSpec> Specs = BuildEnvironment(Environment);
	TArray<TWeakObjectPtr<AActor>>& Tracked = GSpawnedEnvironmentLights.FindOrAdd(World);

	int32 Count = 0;
	for (const FMMDLightSpec& Spec : Specs)
	{
		if (AActor* Actor = SpawnLight(World, Spec))
		{
			Tracked.Add(Actor);
			++Count;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 应用光照环境 %s：生成 %d 盏灯"),
		*GetEnvironmentDisplayName(Environment), Count);
	return Count;
}

void UMMDLightingEnvironmentLibrary::ClearLightingEnvironment(UWorld* World)
{
	if (!World)
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>>* Tracked = GSpawnedEnvironmentLights.Find(World);
	if (!Tracked)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& WeakActor : *Tracked)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			Actor->Destroy();
		}
	}

	GSpawnedEnvironmentLights.Remove(World);
}

int32 UMMDLightingEnvironmentLibrary::ApplyLightingEnvironmentToPreview(FPreviewScene* PreviewScene, EMMDLightingEnvironment Environment)
{
	return ApplyLightingEnvironmentToPreviewScaled(PreviewScene, Environment, FVector::ZeroVector, 1.0f);
}

int32 UMMDLightingEnvironmentLibrary::ApplyLightingEnvironmentToPreviewScaled(FPreviewScene* PreviewScene, EMMDLightingEnvironment Environment,
	const FVector& Center, float Scale)
{
	if (!PreviewScene || Environment == EMMDLightingEnvironment::None)
	{
		return 0;
	}

	ClearLightingEnvironmentFromPreview(PreviewScene);

	const TArray<FMMDLightSpec> Specs = BuildEnvironment(Environment);
	TArray<TWeakObjectPtr<USceneComponent>>& Tracked = GPreviewEnvironmentLights.FindOrAdd(PreviewScene);

	int32 Count = 0;
	for (const FMMDLightSpec& RawSpec : Specs)
	{
		const FMMDLightSpec Spec = (FMath::IsNearlyEqual(Scale, 1.0f) && Center.IsNearlyZero())
			? RawSpec : TransformSpec(RawSpec, Center, Scale);
		if (USceneComponent* Comp = CreatePreviewLightComponent(PreviewScene, Spec))
		{
			Tracked.Add(Comp);
			++Count;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已应用光照环境到预览场景 %s：%d 盏灯（Center=%s Scale=%.2f）"),
		*GetEnvironmentDisplayName(Environment), Count, *Center.ToString(), Scale);
	return Count;
}

void UMMDLightingEnvironmentLibrary::ClearLightingEnvironmentFromPreview(FPreviewScene* PreviewScene)
{
	if (!PreviewScene)
	{
		return;
	}

	TArray<TWeakObjectPtr<USceneComponent>>* Tracked = GPreviewEnvironmentLights.Find(PreviewScene);
	if (!Tracked)
	{
		return;
	}

	for (const TWeakObjectPtr<USceneComponent>& WeakComp : *Tracked)
	{
		if (USceneComponent* Comp = WeakComp.Get())
		{
			PreviewScene->RemoveComponent(Comp);
		}
	}

	GPreviewEnvironmentLights.Remove(PreviewScene);
}

TArray<FVector4f> UMMDLightingEnvironmentLibrary::PackEnvironmentLightData(EMMDLightingEnvironment Environment)
{
	return PackEnvironmentLightDataScaled(Environment, FVector::ZeroVector, 1.0f);
}

TArray<FVector4f> UMMDLightingEnvironmentLibrary::PackEnvironmentLightDataScaled(EMMDLightingEnvironment Environment,
	const FVector& Center, float Scale)
{
	// LightDataRT 第 0 行布局（与 UMMDAnimeLightDataSubsystem::CollectLights 打包格式一致）：
	//   每盏灯 4 个 texel（4 个 float4）：
	//     +0: (位置.xyz, 类型)   类型: 1=点光 2=聚光 3=平行光 4=面光
	//     +1: (颜色.rgb, 强度)
	//     +2: (方向.xyz, 半径)   平行光用方向；点/聚/面光用半径
	//     +3: (内锥cos, 外锥cos, falloff, 0)   面光: (宽, 高, 0, 0)
	// 天空光不进 LightDataRT（环境光由引擎 SH/天光处理），与 CollectLights 一致。
	TArray<FVector4f> Data;
	Data.SetNumZeroed(16 * 4); // MaxLights = 16

	if (Environment == EMMDLightingEnvironment::None)
	{
		return Data;
	}

	const TArray<FMMDLightSpec> RawSpecs = BuildEnvironment(Environment);
	int32 Slot = 0;
	for (const FMMDLightSpec& RawSpec : RawSpecs)
	{
		if (Slot >= 16)
		{
			break;
		}

		const FMMDLightSpec Spec = (FMath::IsNearlyEqual(Scale, 1.0f) && Center.IsNearlyZero())
			? RawSpec : TransformSpec(RawSpec, Center, Scale);

		// FMMDLightSpec::Rotation 的 Forward = 光传播方向（从光源指向场景，同引擎约定），
		// 与 CollectLights 里 GetActorForwardVector()/GetForwardVector() 一致。
		const FVector Fwd = Spec.Rotation.Vector();
		const int32 B = Slot * 4;

		switch (Spec.Kind)
		{
		case EMMDLightKind::Directional:
			Data[B + 0] = FVector4f(0.0f, 0.0f, 0.0f, 3.0f); // Directional
			Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
			Data[B + 2] = FVector4f(Fwd.X, Fwd.Y, Fwd.Z, 0.0f);
			break;
		case EMMDLightKind::Point:
			Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 1.0f); // Point
			Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
			Data[B + 2] = FVector4f(0.0f, 0.0f, 0.0f, Spec.Radius);
			break;
		case EMMDLightKind::Spot:
			Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 2.0f); // Spot
			Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
			Data[B + 2] = FVector4f(Fwd.X, Fwd.Y, Fwd.Z, Spec.Radius);
			{
				const float HalfInner = FMath::DegreesToRadians(Spec.InnerCone);
				const float HalfOuter = FMath::DegreesToRadians(Spec.OuterCone);
				Data[B + 3] = FVector4f(FMath::Cos(HalfInner), FMath::Cos(HalfOuter), 2.0f, 0.0f);
			}
			break;
		case EMMDLightKind::Rect:
			Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 4.0f); // Rect
			Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
			Data[B + 2] = FVector4f(-Fwd.X, -Fwd.Y, -Fwd.Z, Spec.Radius); // rect 发射方向沿 -forward（同 CollectLights）
			Data[B + 3] = FVector4f(Spec.SourceWidth, Spec.SourceHeight, 0.0f, 0.0f);
			break;
		case EMMDLightKind::Sky:
		default:
			continue; // 天空光不占灯位
		}
		Slot++;
	}
	return Data;
}

FString UMMDLightingEnvironmentLibrary::GetEnvironmentDisplayName(EMMDLightingEnvironment Environment)
{
	switch (Environment)
	{
	case EMMDLightingEnvironment::Studio3Point:  return TEXT("三点布光");
	case EMMDLightingEnvironment::Daylight:      return TEXT("户外日光");
	case EMMDLightingEnvironment::OvercastSoft:  return TEXT("阴天柔光");
	case EMMDLightingEnvironment::IndoorWarm:    return TEXT("室内暖光");
	case EMMDLightingEnvironment::NeonNight:     return TEXT("赛博霓虹");
	case EMMDLightingEnvironment::RimSilhouette: return TEXT("轮廓剪影");
	case EMMDLightingEnvironment::GoldenHour:    return TEXT("黄金时刻");
	case EMMDLightingEnvironment::HorrorGreen:   return TEXT("恐怖绿光");
	case EMMDLightingEnvironment::None:
	default:                                      return TEXT("无");
	}
}

bool UMMDLightingEnvironmentLibrary::IsSpecialEnvironment(EMMDLightingEnvironment Environment)
{
	switch (Environment)
	{
	case EMMDLightingEnvironment::NeonNight:
	case EMMDLightingEnvironment::RimSilhouette:
	case EMMDLightingEnvironment::GoldenHour:
	case EMMDLightingEnvironment::HorrorGreen:
		return true;
	default:
		return false;
	}
}

FString UMMDLightingEnvironmentLibrary::GetEnvironmentAssetName(EMMDLightingEnvironment Environment)
{
	switch (Environment)
	{
	case EMMDLightingEnvironment::Studio3Point:  return TEXT("LE_Studio3Point");
	case EMMDLightingEnvironment::Daylight:      return TEXT("LE_Daylight");
	case EMMDLightingEnvironment::OvercastSoft:  return TEXT("LE_OvercastSoft");
	case EMMDLightingEnvironment::IndoorWarm:    return TEXT("LE_IndoorWarm");
	case EMMDLightingEnvironment::NeonNight:     return TEXT("LE_NeonNight");
	case EMMDLightingEnvironment::RimSilhouette: return TEXT("LE_RimSilhouette");
	case EMMDLightingEnvironment::GoldenHour:    return TEXT("LE_GoldenHour");
	case EMMDLightingEnvironment::HorrorGreen:   return TEXT("LE_HorrorGreen");
	case EMMDLightingEnvironment::None:
	default:                                      return FString();
	}
}

namespace
{
FString BuildEnvironmentLevelAsset(EMMDLightingEnvironment Environment, const FString& FolderPath, bool bRebuild)
{
#if WITH_EDITOR
	if (Environment == EMMDLightingEnvironment::None)
	{
		return FString();
	}

	const FString AssetName = UMMDLightingEnvironmentLibrary::GetEnvironmentAssetName(Environment);
	if (AssetName.IsEmpty())
	{
		return FString();
	}

	const FString PackagePath = FolderPath / AssetName;

	// 已存在则直接返回（不覆盖用户编辑过的场景）。
	const bool bExists = FPackageName::DoesPackageExist(PackagePath);
	if (bExists && !bRebuild)
	{
		UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 关卡资产已存在，跳过生成：%s"), *PackagePath);
		return PackagePath;
	}

	// 1) 创建包 + 用 UWorldFactory 生成一个空的 Inactive 世界（带正确的 PersistentLevel）。
	UPackage* Package = nullptr;
	UWorld* NewWorld = nullptr;
	if (bExists)
	{
		Package = LoadPackage(nullptr, *PackagePath, LOAD_None);
		NewWorld = Package ? FindObject<UWorld>(Package, *AssetName) : nullptr;
	}
	else
	{
		Package = CreatePackage(*PackagePath);
		if (Package)
		{
			UWorldFactory* Factory = NewObject<UWorldFactory>();
			NewWorld = Cast<UWorld>(Factory->FactoryCreateNew(
				UWorld::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));
		}
	}
	if (!NewWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDLighting] 无法创建或加载 LookDev 关卡：%s"), *PackagePath);
		return FString();
	}

	// 2) 往关卡里 spawn 灯光（持久 actor，保存进关卡，可在编辑器编辑）。
	if (bRebuild)
	{
		ClearGeneratedLookDevActors(NewWorld);
	}
	BuildLookDevStage(NewWorld, Environment);

	NewWorld->MarkPackageDirty();

	// 3) 保存为 .umap。
	FString FileName;
	if (!FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FileName, FPackageName::GetMapPackageExtension()))
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDLighting] 包名转文件路径失败：%s"), *PackagePath);
		return FString();
	}

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GWarn;
	const bool bSaved = UPackage::SavePackage(Package, NewWorld, *FileName, SaveArgs);
	if (!bSaved)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDLighting] 保存关卡失败：%s"), *FileName);
		return FString();
	}

	// World Partition 会把舞台、灯光等 Actor 存到独立 External Actor 包。
	// 只保存主 .umap 会留下旧 Actor，下一次加载时就会把刚重建的内容覆盖掉。
	TArray<UPackage*> DirtyWorldPackages;
	FEditorFileUtils::GetDirtyWorldPackages(DirtyWorldPackages);
	FString PluginRoot = FolderPath;
	int32 LastSlash = INDEX_NONE;
	if (PluginRoot.FindLastChar(TEXT('/'), LastSlash))
	{
		PluginRoot.LeftInline(LastSlash, EAllowShrinking::No);
	}
	const FString ExternalActorPrefix = PluginRoot / TEXT("__ExternalActors__") / TEXT("Maps") / AssetName;
	const FString ExternalObjectPrefix = PluginRoot / TEXT("__ExternalObjects__") / TEXT("Maps") / AssetName;
	TArray<UPackage*> ExternalPackages;
	for (UPackage* DirtyPackage : DirtyWorldPackages)
	{
		if (!DirtyPackage)
		{
			continue;
		}
		const FString DirtyName = DirtyPackage->GetName();
		if (DirtyName.StartsWith(ExternalActorPrefix) || DirtyName.StartsWith(ExternalObjectPrefix))
		{
			ExternalPackages.AddUnique(DirtyPackage);
		}
	}
	if (ExternalPackages.Num() > 0)
	{
		TArray<UPackage*> FailedPackages;
		const FEditorFileUtils::EPromptReturnCode SaveResult = FEditorFileUtils::PromptForCheckoutAndSave(
			ExternalPackages, false, false, &FailedPackages, false, false);
		if (SaveResult == FEditorFileUtils::PR_Failure || FailedPackages.Num() > 0)
		{
			UE_LOG(LogTemp, Error, TEXT("[MMDLighting] External Actor 保存失败：%s (%d 个包)"),
				*PackagePath, FailedPackages.Num());
			return FString();
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已%s LookDev 关卡：%s"), bRebuild ? TEXT("重建") : TEXT("生成"), *PackagePath);
	return PackagePath;
#else
	UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 生成关卡资产仅编辑器可用。"));
	return FString();
#endif
}
}

FString UMMDLightingEnvironmentLibrary::CreateEnvironmentLevelAsset(EMMDLightingEnvironment Environment, const FString& FolderPath)
{
	return BuildEnvironmentLevelAsset(Environment, FolderPath, false);
}

void UMMDLightingEnvironmentLibrary::ResetLevelViewportCamera()
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return;
	}

	// 标准机位（与场景里 MMDStageCamera 的配套设计一致）：
	//   相机在模型正前方（Y+ 方向）2.4m、躯干高度 1.1m，forward 朝 -Y 平视模型。
	//   模型在原点，标准 MMD 身高约 160cm（中心 Z≈80cm）。
	const FVector CamLoc(0.0f, 240.0f, 110.0f);
	const FRotator CamRot(0.0f, -90.0f, 0.0f); // yaw=-90: forward 指向 -Y（面向模型）

	for (FLevelEditorViewportClient* EVC : GEditor->GetLevelViewportClients())
	{
		if (!EVC || !EVC->IsPerspective())
		{
			continue;
		}
		EVC->ExposureSettings.bFixed = true;
		EVC->ExposureSettings.FixedEV100 = 10.0f;
		EVC->SetGameView(true);
		EVC->SetViewLocation(CamLoc);
		EVC->SetViewRotation(CamRot);
		EVC->Invalidate();
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已归位摄像机到舞台标准机位"));
#endif
}

bool UMMDLightingEnvironmentLibrary::SyncToStageCamera()
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return false;
	}

	// 场景配套机位：找关卡里的 MMDStageCamera（设计师在关卡里放置/调整的机位）。
	// 兼容两种命名：add 的 name 参数可能不生效（对象名是 CameraActor_UAID_xxx），
	// 所以优先按名字找，找不到就取关卡里第一个 CameraActor。
	ACameraActor* StageCam = nullptr;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == TEXT("MMDStageCamera") || It->GetName() == TEXT("MMDStageCamera"))
		{
			StageCam = *It;
			break;
		}
	}
	if (!StageCam)
	{
		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			if (It->GetClass() == ACameraActor::StaticClass()) // 跳过 CameraRig 等子类
			{
				StageCam = *It;
				break;
			}
		}
	}

	if (!StageCam)
	{
		UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 关卡里没有 CameraActor，退回标准机位"));
		return false;
	}

	const FVector CamLoc = StageCam->GetActorLocation();
	const FRotator CamRot = StageCam->GetActorRotation();

	for (FLevelEditorViewportClient* EVC : GEditor->GetLevelViewportClients())
	{
		if (!EVC || !EVC->IsPerspective())
		{
			continue;
		}
		EVC->ExposureSettings.bFixed = true;
		EVC->ExposureSettings.FixedEV100 = 10.0f;
		EVC->SetGameView(true);
		EVC->SetViewLocation(CamLoc);
		EVC->SetViewRotation(CamRot);
		EVC->Invalidate();
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已同步视口相机到 MMDStageCamera（%s）"), *CamLoc.ToString());
	return true;
#else
	return false;
#endif
}

bool UMMDLightingEnvironmentLibrary::OpenEnvironmentLevel(EMMDLightingEnvironment Environment, const FString& FolderPath)
{
#if WITH_EDITOR
	if (Environment == EMMDLightingEnvironment::None)
	{
		return false;
	}

	FString PackagePath = FolderPath / GetEnvironmentAssetName(Environment);

	// 不存在则先自动生成。
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		PackagePath = CreateEnvironmentLevelAsset(Environment, FolderPath);
		if (PackagePath.IsEmpty())
		{
			return false;
		}
	}

	// 用编辑器打开该关卡（FEditorFileUtils::LoadMap 接受包路径，内部转成 .umap 文件）。
	const bool bLoaded = FEditorFileUtils::LoadMap(PackagePath, /*bLoadAsTemplate*/false, /*bShowProgress*/true);
	if (bLoaded)
	{
		// 归位摄像机：优先同步到场景里配套放置的 MMDStageCamera（机位是场景内容，设计师可在关卡里调整）；
		// 若关卡里没有该相机，退回标准机位（看向舞台中央）。
		if (!SyncToStageCamera())
		{
			ResetLevelViewportCamera();
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 打开关卡失败：%s"), *PackagePath);
	}
	return bLoaded;
#else
	return false;
#endif
}

int32 UMMDLightingEnvironmentLibrary::CreateAllEnvironmentLevelAssets(const FString& FolderPath)
{
	int32 Count = 0;
	const TArray<EMMDLightingEnvironment> AllEnvs = {
		EMMDLightingEnvironment::Studio3Point,
		EMMDLightingEnvironment::Daylight,
		EMMDLightingEnvironment::OvercastSoft,
		EMMDLightingEnvironment::IndoorWarm,
		EMMDLightingEnvironment::NeonNight,
		EMMDLightingEnvironment::RimSilhouette,
		EMMDLightingEnvironment::GoldenHour,
		EMMDLightingEnvironment::HorrorGreen,
	};

	for (EMMDLightingEnvironment Env : AllEnvs)
	{
		if (!CreateEnvironmentLevelAsset(Env, FolderPath).IsEmpty())
		{
			++Count;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 批量生成光照环境关卡完成：%d/%d"), Count, AllEnvs.Num());
	return Count;
}

int32 UMMDLightingEnvironmentLibrary::RebuildAllEnvironmentLevelAssets(const FString& FolderPath)
{
	const TArray<EMMDLightingEnvironment> AllEnvs = {
		EMMDLightingEnvironment::Studio3Point,
		EMMDLightingEnvironment::Daylight,
		EMMDLightingEnvironment::OvercastSoft,
		EMMDLightingEnvironment::IndoorWarm,
		EMMDLightingEnvironment::NeonNight,
		EMMDLightingEnvironment::RimSilhouette,
		EMMDLightingEnvironment::GoldenHour,
		EMMDLightingEnvironment::HorrorGreen,
	};

	int32 Count = 0;
	for (EMMDLightingEnvironment Env : AllEnvs)
	{
		if (!BuildEnvironmentLevelAsset(Env, FolderPath, true).IsEmpty())
		{
			++Count;
		}
	}

#if WITH_EDITOR
	if (GEditor)
	{
		for (FLevelEditorViewportClient* EVC : GEditor->GetLevelViewportClients())
		{
			if (EVC && EVC->IsPerspective())
			{
				EVC->ExposureSettings.bFixed = true;
				EVC->ExposureSettings.FixedEV100 = 10.0f;
				EVC->SetGameView(true);
				EVC->Invalidate();
			}
		}
	}
#endif

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已重建 LookDev 关卡：%d/%d"), Count, AllEnvs.Num());
	return Count;
}

// ---- Technical Validation Environments (T00-T12) ----
// These are deterministic test environments for shader validation.
// They do NOT use the EMMDLightingEnvironment enum — accessed by integer ID.

TArray<FMMDLightSpec> BuildTechnicalEnvSpecs(int32 TechEnvId)
{
	TArray<FMMDLightSpec> Specs;

	switch (TechEnvId)
	{
	case 0: // T00_NeutralStudio: Basic shader calibration, 18% gray card reference
		Specs.Add(MakeDir(FRotator(-45.0f, 0.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 3.14f));
		Specs.Add(MakeSky(FLinearColor(0.5f, 0.5f, 0.5f), 1.0f));
		break;

	case 1: // T01_DirectionalOnly: Validate Toon Ramp / NdotL / Face SDF
		Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 5.0f));
		break;

	case 2: // T02_DirectionalSoft: Test soft light layering
		Specs.Add(MakeDir(FRotator(-80.0f, 0.0f, 0.0f), FLinearColor(0.9f, 0.92f, 0.95f), 2.0f));
		Specs.Add(MakeSky(FLinearColor(0.6f, 0.65f, 0.7f), 2.0f));
		break;

	case 3: // T03_SkyOnly: Validate SkyAmbient
		Specs.Add(MakeSky(FLinearColor(0.7f, 0.8f, 1.0f), 3.0f));
		break;

	case 4: // T04_PointOnly: Test distance attenuation
		Specs.Add(MakePoint(FVector(200.0f, -150.0f, 200.0f), FLinearColor(1.0f, 1.0f, 1.0f), 5.0f, 600.0f));
		break;

	case 5: // T05_SpotOnly: Test spot cone / attenuation
		Specs.Add(MakeSpot(FVector(200.0f, -200.0f, 300.0f), FVector(0.0f, 0.0f, 80.0f),
			FLinearColor(1.0f, 1.0f, 1.0f), 5.0f, 800.0f, 20.0f, 35.0f));
		break;

	case 6: // T06_RectOnly: Test Rect Light
		Specs.Add(MakeRect(FVector(-200.0f, -200.0f, 200.0f), FVector::ZeroVector,
			FLinearColor(1.0f, 1.0f, 1.0f), 5.0f, 600.0f, 128.0f, 128.0f));
		break;

	case 7: // T07_WarmCoolMixed: Multi-light + colored light stability
		Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(1.0f, 0.92f, 0.82f), 5.0f)); // Warm key
		Specs.Add(MakePoint(FVector(-200.0f, 100.0f, 180.0f), FLinearColor(0.7f, 0.82f, 1.0f), 1.5f, 700.0f)); // Cool fill
		Specs.Add(MakeSky(FLinearColor(0.65f, 0.72f, 0.85f), 1.5f));
		break;

	case 8: // T08_BackLight: Validate Hair / Rim / Silhouette
		Specs.Add(MakeDir(FRotator(-25.0f, 160.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 6.0f));
		Specs.Add(MakePoint(FVector(60.0f, -60.0f, 140.0f), FLinearColor(0.3f, 0.3f, 0.3f), 0.5f, 400.0f));
		break;

	case 9: // T09_HighDynamicRange: Strong key + dark env, test highlight clipping
		Specs.Add(MakeDir(FRotator(-45.0f, -20.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 10.0f));
		Specs.Add(MakeSky(FLinearColor(0.1f, 0.12f, 0.15f), 0.3f));
		break;

	case 10: // T10_NoSky: Close Skylight, verify shader works without SkyIrradiance
		Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 5.0f));
		// No SkyLight — intentionally omitted
		break;

	case 11: // T11_ShadowOccluder: Directional + standard occluder, test MMD ShadowMap
		Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(1.0f, 1.0f, 1.0f), 5.0f));
		Specs.Add(MakeSky(FLinearColor(0.5f, 0.5f, 0.5f), 1.0f));
		// Occluder geometry should be added separately to the level
		break;

	case 12: // T12_IBLRotation: Same env at 0/90/180/270°, test material IBL response
		Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(0.95f, 0.95f, 1.0f), 4.0f));
		Specs.Add(MakeSky(FLinearColor(0.7f, 0.75f, 0.85f), 2.5f));
		// Sky rotation should be varied between captures (0°/90°/180°/270°)
		break;

	default:
		break;
	}

	return Specs;
}

FString UMMDLightingEnvironmentLibrary::GetTechEnvName(int32 TechEnvId)
{
	static const TCHAR* Names[] = {
		TEXT("T00_NeutralStudio"),
		TEXT("T01_DirectionalOnly"),
		TEXT("T02_DirectionalSoft"),
		TEXT("T03_SkyOnly"),
		TEXT("T04_PointOnly"),
		TEXT("T05_SpotOnly"),
		TEXT("T06_RectOnly"),
		TEXT("T07_WarmCoolMixed"),
		TEXT("T08_BackLight"),
		TEXT("T09_HighDynamicRange"),
		TEXT("T10_NoSky"),
		TEXT("T11_ShadowOccluder"),
		TEXT("T12_IBLRotation"),
	};
	if (TechEnvId >= 0 && TechEnvId < UE_ARRAY_COUNT(Names))
	{
		return FString(Names[TechEnvId]);
	}
	return FString();
}

void UMMDLightingEnvironmentLibrary::CreateTechnicalPostProcessVolume(UWorld* World)
{
	if (!World) return;

	// Remove existing technical PP volume
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->GetName().StartsWith(TEXT("MMDTechPP")))
		{
			It->Destroy();
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Name = FName(TEXT("MMDTechPPVolume"));

	APostProcessVolume* PP = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!PP) return;

	PP->bUnbound = true; // Apply to entire scene
	PP->Priority = 100.0f; // High priority to override other PP

	FPostProcessSettings& S = PP->Settings;

	// Manual Exposure (EV100 = 0)
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = 0.0f;

	// Disable Bloom
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.0f;

	// Disable DOF
	S.bOverride_DepthOfFieldEnabled = true;
	S.DepthOfFieldFocalDistance = 0.0f;

	// Disable Vignette
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.0f;

	// Disable Chromatic Aberration
	S.bOverride_ChromaticAberrationStartOffset = true;
	S.ChromaticAberrationStartOffset = 0.0f;

	// Disable Lens Flares
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.0f;

	// Disable Motion Blur
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = 0.0f;

	// Disable Film Grain
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = 0.0f;

	// Disable Ambient Occlusion
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = 0.0f;

	// Disable Screen Space Reflections
	S.bOverride_ScreenSpaceReflectionIntensity = true;
	S.ScreenSpaceReflectionIntensity = 0.0f;

	// Neutral color grading (D65 white balance)
	S.bOverride_WhiteTemp = true;
	S.WhiteTemp = 6500.0f;
	S.bOverride_WhiteTint = true;
	S.WhiteTint = 0.0f;

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 技术验证后处理体积已创建（手动曝光 EV100=0，后处理效果已关闭）"));
}

int32 UMMDLightingEnvironmentLibrary::ApplyTechnicalEnvironmentToWorld(UWorld* World, int32 TechEnvId)
{
	if (!World || TechEnvId < 0 || TechEnvId >= NumTechnicalEnvironments) return 0;

	// Clear existing environment lights
	ClearLightingEnvironment(World);

	// Build and spawn technical lights
	const TArray<FMMDLightSpec> Specs = BuildTechnicalEnvSpecs(TechEnvId);
	int32 Count = 0;
	for (const FMMDLightSpec& Spec : Specs)
	{
		if (SpawnLight(World, Spec))
		{
			++Count;
		}
	}

	// Apply post-process settings
	CreateTechnicalPostProcessVolume(World);

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已应用技术验证环境 %s：%d 盏灯"),
		*GetTechEnvName(TechEnvId), Count);
	return Count;
}

int32 UMMDLightingEnvironmentLibrary::ApplyTechnicalEnvironmentToPreview(FPreviewScene* PreviewScene, int32 TechEnvId)
{
	if (!PreviewScene || TechEnvId < 0 || TechEnvId >= NumTechnicalEnvironments) return 0;

	ClearLightingEnvironmentFromPreview(PreviewScene);

	const TArray<FMMDLightSpec> Specs = BuildTechnicalEnvSpecs(TechEnvId);
	TArray<TWeakObjectPtr<USceneComponent>>& Tracked = GPreviewEnvironmentLights.FindOrAdd(PreviewScene);

	int32 Count = 0;
	for (const FMMDLightSpec& Spec : Specs)
	{
		if (USceneComponent* Comp = CreatePreviewLightComponent(PreviewScene, Spec))
		{
			Tracked.Add(Comp);
			++Count;
		}
	}

	// Pack and push to LightDataRT override
	if (UMMDAnimeLightDataSubsystem* Subsystem = UMMDAnimeLightDataSubsystem::Get())
	{
		// Pack technical env data (same format as PackEnvironmentLightData)
		TArray<FVector4f> Data;
		Data.SetNumZeroed(16 * 4);
		int32 Slot = 0;
		for (const FMMDLightSpec& Spec : Specs)
		{
			if (Slot >= 16) break;
			const FVector Fwd = Spec.Rotation.Vector();
			const int32 B = Slot * 4;

			switch (Spec.Kind)
			{
			case EMMDLightKind::Directional:
				Data[B + 0] = FVector4f(0.0f, 0.0f, 0.0f, 3.0f);
				Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
				Data[B + 2] = FVector4f(Fwd.X, Fwd.Y, Fwd.Z, 0.0f);
				break;
			case EMMDLightKind::Point:
				Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 1.0f);
				Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
				Data[B + 2] = FVector4f(0.0f, 0.0f, 0.0f, Spec.Radius);
				break;
			case EMMDLightKind::Spot:
				Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 2.0f);
				Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
				Data[B + 2] = FVector4f(Fwd.X, Fwd.Y, Fwd.Z, Spec.Radius);
				{
					const float HalfInner = FMath::DegreesToRadians(Spec.InnerCone);
					const float HalfOuter = FMath::DegreesToRadians(Spec.OuterCone);
					Data[B + 3] = FVector4f(FMath::Cos(HalfInner), FMath::Cos(HalfOuter), 2.0f, 0.0f);
				}
				break;
			case EMMDLightKind::Rect:
				Data[B + 0] = FVector4f(Spec.Location.X, Spec.Location.Y, Spec.Location.Z, 4.0f);
				Data[B + 1] = FVector4f(Spec.Color.R, Spec.Color.G, Spec.Color.B, GetPhysicalIntensity(Spec));
				Data[B + 2] = FVector4f(-Fwd.X, -Fwd.Y, -Fwd.Z, Spec.Radius);
				Data[B + 3] = FVector4f(Spec.SourceWidth, Spec.SourceHeight, 0.0f, 0.0f);
				break;
			case EMMDLightKind::Sky:
			default:
				continue; // Sky doesn't occupy light slots
			}
			Slot++;
		}
		Subsystem->SetPreviewLightOverride(Data);
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已应用技术验证环境 %s 到预览场景：%d 盏灯"),
		*GetTechEnvName(TechEnvId), Count);
	return Count;
}

#if WITH_EDITOR
// 控制台命令：MMD.Lighting <3point|daylight|overcast|indoor|neon|rim|golden|horror|clear>
static void ExecMMDLighting(const TArray<FString>& Args)
{
	UWorld* World = (GEditor && GEditor->GetEditorWorldContext().World())
		? GEditor->GetEditorWorldContext().World()
		: GWorld;

	if (Args.Num() < 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 用法: MMD.Lighting <3point|daylight|overcast|indoor|neon|rim|golden|horror|clear>"));
		return;
	}

	const FString Name = Args[0].ToLower();
	EMMDLightingEnvironment Env = EMMDLightingEnvironment::None;

	if (Name == TEXT("3point") || Name == TEXT("studio")) Env = EMMDLightingEnvironment::Studio3Point;
	else if (Name == TEXT("daylight"))                   Env = EMMDLightingEnvironment::Daylight;
	else if (Name == TEXT("overcast"))                   Env = EMMDLightingEnvironment::OvercastSoft;
	else if (Name == TEXT("indoor"))                     Env = EMMDLightingEnvironment::IndoorWarm;
	else if (Name == TEXT("neon"))                       Env = EMMDLightingEnvironment::NeonNight;
	else if (Name == TEXT("rim"))                        Env = EMMDLightingEnvironment::RimSilhouette;
	else if (Name == TEXT("golden"))                     Env = EMMDLightingEnvironment::GoldenHour;
	else if (Name == TEXT("horror"))                     Env = EMMDLightingEnvironment::HorrorGreen;
	else if (Name == TEXT("clear") || Name == TEXT("none"))
	{
		UMMDLightingEnvironmentLibrary::ClearLightingEnvironment(World);
		UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已清除环境光"));
		return;
	}
	else
	{
		// 尝试解析 Technical Validation Environment: T00-T12
		if (Name.StartsWith(TEXT("t")) && Name.Len() >= 2)
		{
			const FString NumStr = Name.Mid(1);
			int32 TechId = -1;
			if (NumStr.IsNumeric())
			{
				TechId = FCString::Atoi(*NumStr);
			}
			if (TechId >= 0 && TechId < UMMDLightingEnvironmentLibrary::NumTechnicalEnvironments)
			{
				int32 Count = UMMDLightingEnvironmentLibrary::ApplyTechnicalEnvironmentToWorld(World, TechId);
				UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已应用技术验证环境 %s (%d 灯)"),
					*UMMDLightingEnvironmentLibrary::GetTechEnvName(TechId), Count);
				return;
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 未知环境名: %s"), *Args[0]);
		return;
	}

	UMMDLightingEnvironmentLibrary::ApplyLightingEnvironment(World, Env);
}

static FAutoConsoleCommand CmdMMDLighting(
	TEXT("MMD.Lighting"),
	TEXT("MMD: 应用/清除光照环境到当前关卡. 用法: MMD.Lighting <3point|daylight|overcast|indoor|neon|rim|golden|horror|clear>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecMMDLighting));

static FAutoConsoleCommand CmdMMDRebuildLookDev(
	TEXT("MMD.RebuildLookDev"),
	TEXT("MMD: 重建并保存插件内全部 LookDev 关卡。"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		const int32 Count = UMMDLightingEnvironmentLibrary::RebuildAllEnvironmentLevelAssets();
		UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 控制台重建完成：%d/8"), Count);
	}));

#endif // WITH_EDITOR
