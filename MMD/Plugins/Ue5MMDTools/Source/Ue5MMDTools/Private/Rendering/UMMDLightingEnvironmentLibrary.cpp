#include "Rendering/UMMDLightingEnvironmentLibrary.h"

#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"
#include "Engine/SkyLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Factories/WorldFactory.h"
#include "FileHelpers.h"
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
		FRotator Rotation = FRotator::ZeroRotator;
		FLinearColor Color = FLinearColor::White;
		float Intensity = 1.0f;
		float Radius = 600.0f;       // 点/聚/矩形光衰减半径（cm）
		float InnerCone = 20.0f;     // 聚光内锥半角（度）
		float OuterCone = 35.0f;     // 聚光外锥半角（度）
		float SourceWidth = 64.0f;   // 矩形光宽度（cm）
		float SourceHeight = 64.0f;  // 矩形光高度（cm）
	};

	FMMDLightSpec MakeDir(FRotator Rot, const FLinearColor& Color, float Intensity)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Directional;
		S.Rotation = Rot;
		S.Color = Color;
		S.Intensity = Intensity;
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
		S.Rotation = (Aim - Loc).Rotation();
		S.Color = Color;
		S.Intensity = Intensity;
		S.Radius = Radius;
		S.SourceWidth = W;
		S.SourceHeight = H;
		return S;
	}

	FMMDLightSpec MakeSky(const FLinearColor& Color, float Intensity)
	{
		FMMDLightSpec S;
		S.Kind = EMMDLightKind::Sky;
		S.Color = Color;
		S.Intensity = Intensity;
		return S;
	}

	TArray<FMMDLightSpec> BuildEnvironment(EMMDLightingEnvironment Environment)
	{
		TArray<FMMDLightSpec> Specs;

		switch (Environment)
		{
		case EMMDLightingEnvironment::Studio3Point:
			// 标准三点布光：主光（暖）、辅光（冷）、轮廓光（冷）+ 天光。
			Specs.Add(MakeDir(FRotator(-38.0f, -35.0f, 0.0f), FLinearColor(1.0f, 0.95f, 0.88f), 4.0f));
			Specs.Add(MakeDir(FRotator(-18.0f, 145.0f, 0.0f), FLinearColor(0.68f, 0.82f, 1.0f), 0.75f));
			Specs.Add(MakeDir(FRotator(-10.0f, 35.0f, 0.0f), FLinearColor(0.75f, 0.92f, 1.0f), 1.35f));
			Specs.Add(MakeSky(FLinearColor(0.82f, 0.88f, 1.0f), 1.25f));
			break;

		case EMMDLightingEnvironment::Daylight:
			// 户外日光：强太阳 + 天光 + 反向天空补光。
			Specs.Add(MakeDir(FRotator(-55.0f, -25.0f, 0.0f), FLinearColor(1.0f, 0.97f, 0.92f), 5.5f));
			Specs.Add(MakeDir(FRotator(-10.0f, 180.0f, 0.0f), FLinearColor(0.55f, 0.75f, 1.0f), 1.2f));
			Specs.Add(MakeSky(FLinearColor(0.72f, 0.84f, 1.0f), 2.0f));
			break;

		case EMMDLightingEnvironment::OvercastSoft:
			// 阴天柔光：顶部柔光 + 高天光，几乎无硬阴影。
			Specs.Add(MakeDir(FRotator(-80.0f, 0.0f, 0.0f), FLinearColor(0.82f, 0.86f, 0.92f), 1.6f));
			Specs.Add(MakeSky(FLinearColor(0.75f, 0.80f, 0.88f), 3.0f));
			break;

		case EMMDLightingEnvironment::IndoorWarm:
			// 室内暖光：暖色主点光 + 面光补光 + 暖背光。
			Specs.Add(MakePoint(FVector(140.0f, -60.0f, 210.0f), FLinearColor(1.0f, 0.80f, 0.60f), 4.0f, 500.0f));
			Specs.Add(MakeRect(FVector(-160.0f, -80.0f, 150.0f), FVector::ZeroVector, FLinearColor(0.95f, 0.82f, 0.70f), 2.5f, 450.0f, 120.0f, 160.0f));
			Specs.Add(MakePoint(FVector(0.0f, 240.0f, 190.0f), FLinearColor(1.0f, 0.65f, 0.45f), 2.0f, 400.0f));
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
			// 黄金时刻：低角度暖橙太阳 + 暖色补光 + 暖色天光。
			Specs.Add(MakeDir(FRotator(-18.0f, -30.0f, 0.0f), FLinearColor(1.0f, 0.55f, 0.25f), 5.0f));
			Specs.Add(MakeDir(FRotator(-6.0f, 150.0f, 0.0f), FLinearColor(1.0f, 0.72f, 0.50f), 0.8f));
			Specs.Add(MakeSky(FLinearColor(1.0f, 0.70f, 0.50f), 1.0f));
			break;

		case EMMDLightingEnvironment::HorrorGreen:
			// 恐怖绿光：底部绿色上打光 + 冷色顶光 + 绿色背光。
			Specs.Add(MakePoint(FVector(0.0f, 0.0f, -80.0f), FLinearColor(0.10f, 0.90f, 0.35f), 4.0f, 450.0f));
			Specs.Add(MakeDir(FRotator(-60.0f, 0.0f, 0.0f), FLinearColor(0.55f, 0.72f, 0.88f), 1.2f));
			Specs.Add(MakeDir(FRotator(-15.0f, 180.0f, 0.0f), FLinearColor(0.20f, 0.80f, 0.40f), 2.0f));
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
				Sky->SetIntensity(Spec.Intensity);
				Sky->SetLightColor(Spec.Color);
			}
		}
		else if (ULightComponent* Light = Actor->FindComponentByClass<ULightComponent>())
		{
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetLightColor(Spec.Color);
			Light->SetIntensity(Spec.Intensity);
			if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light))
			{
				PointLight->AttenuationRadius = Spec.Radius;
			}
			Light->SetCastShadows(true);
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

FString UMMDLightingEnvironmentLibrary::CreateEnvironmentLevelAsset(EMMDLightingEnvironment Environment, const FString& FolderPath)
{
#if WITH_EDITOR
	if (Environment == EMMDLightingEnvironment::None)
	{
		return FString();
	}

	const FString AssetName = GetEnvironmentAssetName(Environment);
	if (AssetName.IsEmpty())
	{
		return FString();
	}

	const FString PackagePath = FolderPath / AssetName;

	// 已存在则直接返回（不覆盖用户编辑过的场景）。
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 关卡资产已存在，跳过生成：%s"), *PackagePath);
		return PackagePath;
	}

	// 1) 创建包 + 用 UWorldFactory 生成一个空的 Inactive 世界（带正确的 PersistentLevel）。
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDLighting] CreatePackage failed: %s"), *PackagePath);
		return FString();
	}

	UWorldFactory* Factory = NewObject<UWorldFactory>();
	UWorld* NewWorld = Cast<UWorld>(Factory->FactoryCreateNew(
		UWorld::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, GWarn));
	if (!NewWorld)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDLighting] UWorldFactory create failed: %s"), *AssetName);
		return FString();
	}

	// 2) 往关卡里 spawn 灯光（持久 actor，保存进关卡，可在编辑器编辑）。
	const TArray<FMMDLightSpec> Specs = BuildEnvironment(Environment);
	for (const FMMDLightSpec& Spec : Specs)
	{
		SpawnLight(NewWorld, Spec, /*bPersistent*/true);
	}

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

	UE_LOG(LogTemp, Log, TEXT("[MMDLighting] 已生成光照环境关卡资产：%s（%d 盏灯）"), *PackagePath, Specs.Num());
	return PackagePath;
#else
	UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 生成关卡资产仅编辑器可用。"));
	return FString();
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
	if (!bLoaded)
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
		UE_LOG(LogTemp, Warning, TEXT("[MMDLighting] 未知环境名: %s"), *Args[0]);
		return;
	}

	UMMDLightingEnvironmentLibrary::ApplyLightingEnvironment(World, Env);
}

static FAutoConsoleCommand CmdMMDLighting(
	TEXT("MMD.Lighting"),
	TEXT("应用/清除 MMD 光照环境。用法: MMD.Lighting <3point|daylight|overcast|indoor|neon|rim|golden|horror|clear>"),
	FConsoleCommandWithArgsDelegate::CreateStatic(ExecMMDLighting));
#endif // WITH_EDITOR
