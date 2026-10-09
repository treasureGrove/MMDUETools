#include "Rendering/UMMDAnimeLightDataSubsystem.h"

#include "RenderGraphUtils.h"
#include "EngineUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "MMDAnimeWriteLightsCS.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

// Light type codes written into the light data texture.
namespace MMDAnimeLightType
{
	constexpr float Empty = 0.0f;
	constexpr float Point = 1.0f;
	constexpr float Spot = 2.0f;
	constexpr float Directional = 3.0f;
	constexpr float Rect = 4.0f;
}

// LightDataRT 直接传递灯光组件的艺术强度，不换算 lux/lumen/candela。
// 曝光由 UE 默认流程管理，材质侧自行把强度映射到稳定的 toon 范围。

namespace
{
	const TCHAR* GMMDAnimeLightDataRT_PackagePath = TEXT("/Ue5MMDTools/Rendering");
	const TCHAR* GMMDAnimeLightDataRT_AssetName   = TEXT("LightDataRT");
	const TCHAR* GMMDAnimeLightDataRT_AssetPath   = TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT");
}

void UMMDAnimeLightDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ShadowCameraData.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), 18);
	UE_LOG(LogTemp, Log, TEXT("[MMDAnimeLight] Subsystem initialized."));
}

UMMDAnimeLightDataSubsystem* UMMDAnimeLightDataSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UMMDAnimeLightDataSubsystem>() : nullptr;
}

void UMMDAnimeLightDataSubsystem::Deinitialize()
{
	FlushRenderingCommands();
	Super::Deinitialize();
}

void UMMDAnimeLightDataSubsystem::SetLightDataRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	LightDataRT = InRenderTarget;
	if (LightDataRT)
	{
		// 显式配置优先于首帧自动加载，避免自定义 RT 被默认资产覆盖。
		bAutoSetupDone = true;
		// 重复注册已配置的 RT 时保留现有资源，避免清空灯光数据并阻塞渲染线程。
		if (LightDataRT->SizeX == MaxLights * 4 && LightDataRT->SizeY == 2 &&
			LightDataRT->GetFormat() == PF_A32B32G32R32F && LightDataRT->bCanCreateUAV &&
			!LightDataRT->bAutoGenerateMips && LightDataRT->GetResource())
		{
			return;
		}
		LightDataRT->bCanCreateUAV = true;
		LightDataRT->RenderTargetFormat = RTF_RGBA32f;
		LightDataRT->bAutoGenerateMips = false;
		LightDataRT->InitAutoFormat(MaxLights * 4, 2);   // 第 2 行 = 阴影相机基
		LightDataRT->UpdateResourceImmediate(true);
		UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] Light data RT set: %s (%dx%d RGBA32f UAV)"),
			*LightDataRT->GetName(), LightDataRT->SizeX, LightDataRT->SizeY);
	}
}

void UMMDAnimeLightDataSubsystem::SetShadowCameraData(const FVector4f InData[18])
{
	if (ShadowCameraData.Num() != 18)
	{
		ShadowCameraData.SetNumUninitialized(18);
	}
	for (int32 i = 0; i < 18; i++)
	{
		ShadowCameraData[i] = InData[i];
	}
}

void UMMDAnimeLightDataSubsystem::SetPreviewLightOverride(const TArray<FVector4f>& InData)
{
	PreviewLightOverrideData = InData;
	UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] 预览灯光覆盖已设置：%d 个 float4（%d 盏灯）"),
		InData.Num(), InData.Num() / 4);
}

void UMMDAnimeLightDataSubsystem::ClearPreviewLightOverride()
{
	if (PreviewLightOverrideData.Num() > 0)
	{
		PreviewLightOverrideData.Reset();
		UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] 预览灯光覆盖已清除，恢复主世界灯光收集"));
	}
}

void UMMDAnimeLightDataSubsystem::AutoSetupLightDataRT()
{
	// 1. Try to load an existing asset.
	UTextureRenderTarget2D* ExistingRT = LoadObject<UTextureRenderTarget2D>(nullptr, GMMDAnimeLightDataRT_AssetPath);
	if (ExistingRT)
	{
		SetLightDataRenderTarget(ExistingRT);
		return;
	}

#if WITH_EDITOR
	// 2. Auto-create the asset in the editor so the user only has to drag it into a material.
	const FString PackagePath = FString(GMMDAnimeLightDataRT_PackagePath) / GMMDAnimeLightDataRT_AssetName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (Package)
	{
		UTextureRenderTarget2D* NewRT = NewObject<UTextureRenderTarget2D>(
			Package,
			GMMDAnimeLightDataRT_AssetName,
			RF_Public | RF_Standalone);

		if (NewRT)
		{
			NewRT->RenderTargetFormat = RTF_RGBA32f;
			NewRT->bCanCreateUAV = true;
			NewRT->bAutoGenerateMips = false;
			NewRT->InitAutoFormat(MaxLights * 4, 2);   // 第 2 行 = 阴影相机基
			NewRT->UpdateResourceImmediate(true);
			NewRT->MarkPackageDirty();

			FString FileName;
			if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, FileName, FPackageName::GetAssetPackageExtension()))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				UPackage::SavePackage(Package, NewRT, *FileName, SaveArgs);
			}

			if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
			{
				AssetRegistry->AssetCreated(NewRT);
			}

			UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] Auto-created light data RT asset at %s.%s"),
				*PackagePath, GMMDAnimeLightDataRT_AssetName);
			SetLightDataRenderTarget(NewRT);
			return;
		}
	}
#endif

	UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] Light data RT not found at %s and could not be created."),
		GMMDAnimeLightDataRT_AssetPath);
}

void UMMDAnimeLightDataSubsystem::CollectLightsForFrame(UWorld* InWorld, const FVector* InCameraPosition)
{
	if (!bAutoSetupDone)
	{
		bAutoSetupDone = true;
		AutoSetupLightDataRT();
	}

	if (!bCollectionEnabled)
	{
		return;
	}

	TArray<FVector4f> Data;
	if (HasPreviewLightOverride())
	{
		// 预览灯光覆盖：直接使用预览场景打包好的灯光数据（预览场景的灯是孤儿组件，
		// 无法从 GWorld 扫描到，所以由 MMDViewPanel 显式推入）。
		Data = PreviewLightOverrideData;
	}
	else
	{
		// 多世界编辑器/PIE 以当前视图的场景为准；无视图的旧调用仍兼容 GWorld。
		UWorld* World = InWorld ? InWorld : (InCameraPosition ? nullptr : GWorld);
		if (World)
		{
			CollectLights(World, Data, InCameraPosition);
		}
	}

	// 第 2 行：texel 0..15 为四级联基，texel 16/17 为主相机位置/前向。
	Data.SetNumZeroed(MaxLights * 4 + 19);
	for (int32 i = 0; i < 18; i++)
	{
		Data[MaxLights * 4 + i] = ShadowCameraData[i];
	}

	// 使用最后一个有效槽的末端，兼容预览数据中间存在空槽。
	int32 LightSpan = 0;
	for (int32 i = 0; i < MaxLights; ++i)
	{
		if (Data[i * 4].W >= 0.5f && Data[i * 4 + 1].W > 0.0f) { LightSpan = i + 1; }
	}
	Data[MaxLights * 4 + 18] = FVector4f(static_cast<float>(LightSpan), static_cast<float>(ShadowFilterQuality), 0.0f, 1.0f);

	// SetupView 在场景渲染提交前推到渲染线程；PreRenderViewFamily 随后在同帧写 RT。
	TArray<FVector4f> DataCopy = MoveTemp(Data);
	ENQUEUE_RENDER_COMMAND(MMDAnimeUpdateLightData)(
		[this, DataCopy = MoveTemp(DataCopy)](FRHICommandListImmediate& RHICmdList) mutable
		{
			RenderThreadLightData = MoveTemp(DataCopy);
		});
}

void UMMDAnimeLightDataSubsystem::CollectLights(UWorld* World, TArray<FVector4f>& OutData, const FVector* InCameraPosition)
{
	OutData.SetNumZeroed(MaxLights * 4);

	// ---- camera for distance culling ----
	FVector CameraPos = InCameraPosition ? *InCameraPosition : FVector::ZeroVector;
	bool bHasCamera = InCameraPosition != nullptr;

	if (!bHasCamera)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				CameraPos = PC->PlayerCameraManager->GetCameraLocation();
				bHasCamera = true;
			}
		}
	}
#if WITH_EDITOR
	if (!bHasCamera && GEditor)
	{
		// 用 GetLevelViewportClients()（类型安全，返回 FLevelEditorViewportClient*），
		// 不要对 GetClient() 强转 —— 那可能是非 editor 视口（缩略图/后台），会崩。
		for (FLevelEditorViewportClient* EVC : GEditor->GetLevelViewportClients())
		{
			if (EVC && EVC->GetWorld() == World)
			{
				CameraPos = EVC->GetViewLocation();
				bHasCamera = true;
				break;
			}
		}
	}
#endif

	const float MaxLightDistance = bHasCamera ? 5000.0f : 1e9f;
	const float MaxDistSq = MaxLightDistance * MaxLightDistance;

	struct FPointCand { FVector Pos; FLinearColor Color; float Intensity; float Radius; float DistSq; };
	struct FSpotCand  { FVector Pos; FVector Dir; FLinearColor Color; float Intensity; float Radius; float InnerCos; float OuterCos; float DistSq; };
	struct FRectCand  { FVector Pos; FVector Dir; FLinearColor Color; float Intensity; float Radius; float SizeX; float SizeY; float DistSq; };
	struct FDirectionalCand { FVector Dir; FLinearColor Color; float Intensity; };

	TArray<FPointCand> Points;
	TArray<FSpotCand> Spots;
	TArray<FRectCand> Rects;
	TArray<FDirectionalCand, TInlineAllocator<4>> Directionals;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}

		// 单次扫描灯光组件，常见 actor 的组件列表直接使用栈内存。
		TInlineComponentArray<ULightComponent*> Comps(Actor);
		for (ULightComponent* LightComp : Comps)
		{
			if (!LightComp || !LightComp->IsRegistered() || !LightComp->IsVisible() || LightComp->Intensity <= 0.0f)
			{
				continue;
			}

			if (UDirectionalLightComponent* DirComp = Cast<UDirectionalLightComponent>(LightComp))
			{
				Directionals.Add({DirComp->GetForwardVector(), DirComp->GetLightColor(), DirComp->Intensity});
				continue;
			}

			const FVector Pos = LightComp->GetComponentLocation();
			const float DistSq = FVector::DistSquared(Pos, CameraPos);
			if (DistSq > MaxDistSq)
			{
				continue;
			}

			// 聚光继承点光，必须先分类，避免同一盏灯打包两次。
			if (USpotLightComponent* spot_light = Cast<USpotLightComponent>(LightComp))
			{
				FSpotCand& C = Spots.AddDefaulted_GetRef();
				C.Pos = Pos;
				C.Dir = spot_light->GetForwardVector();
				C.Color = spot_light->GetLightColor();
				C.Intensity = spot_light->Intensity;
				C.Radius = spot_light->AttenuationRadius;
				// UE 的锥角属性已经是半角。
				C.InnerCos = FMath::Cos(FMath::DegreesToRadians(spot_light->InnerConeAngle));
				C.OuterCos = FMath::Cos(FMath::DegreesToRadians(spot_light->OuterConeAngle));
				C.DistSq = DistSq;
			}
			else if (UPointLightComponent* point_light = Cast<UPointLightComponent>(LightComp))
			{
				FPointCand& C = Points.AddDefaulted_GetRef();
				C.Pos = Pos;
				C.Color = point_light->GetLightColor();
				C.Intensity = point_light->Intensity;
				C.Radius = point_light->AttenuationRadius;
				C.DistSq = DistSq;
			}
			else if (URectLightComponent* rect_light = Cast<URectLightComponent>(LightComp))
			{
				FRectCand& C = Rects.AddDefaulted_GetRef();
				C.Pos = Pos;
				C.Dir = -rect_light->GetForwardVector();
				C.Color = rect_light->GetLightColor();
				C.Intensity = rect_light->Intensity;
				C.Radius = rect_light->AttenuationRadius;
				C.SizeX = rect_light->SourceWidth;
				C.SizeY = rect_light->SourceHeight;
				C.DistSq = DistSq;
			}
		}
	}

	// ---- sort by distance (closest first) ----
	Points.Sort([](const FPointCand& A, const FPointCand& B) { return A.DistSq < B.DistSq; });
	Spots.Sort([](const FSpotCand& A, const FSpotCand& B) { return A.DistSq < B.DistSq; });
	Rects.Sort([](const FRectCand& A, const FRectCand& B) { return A.DistSq < B.DistSq; });
	// 主光优先，舞台的补光与轮廓光也进入同一套材质光照。
	Directionals.StableSort([](const FDirectionalCand& A, const FDirectionalCand& B) { return A.Intensity > B.Intensity; });

	// ---- pack into unified light slots ----
	int32 Slot = 0;

	for (const FDirectionalCand& C : Directionals)
	{
		if (Slot >= MaxLights) { break; }
		const int32 B = Slot * 4;
		OutData[B + 0] = FVector4f(0.0f, 0.0f, 0.0f, MMDAnimeLightType::Directional);
		OutData[B + 1] = FVector4f(C.Color.R, C.Color.G, C.Color.B, C.Intensity);
		OutData[B + 2] = FVector4f(C.Dir.X, C.Dir.Y, C.Dir.Z, 0.0f);
		OutData[B + 3] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		Slot++;
	}

	for (const FPointCand& C : Points)
	{
		if (Slot >= MaxLights)
		{
			break;
		}
		const int32 B = Slot * 4;
		OutData[B + 0] = FVector4f(C.Pos.X, C.Pos.Y, C.Pos.Z, MMDAnimeLightType::Point);
		OutData[B + 1] = FVector4f(C.Color.R, C.Color.G, C.Color.B, C.Intensity);
		OutData[B + 2] = FVector4f(0.0f, 0.0f, 0.0f, C.Radius);
		OutData[B + 3] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
		Slot++;
	}

	for (const FSpotCand& C : Spots)
	{
		if (Slot >= MaxLights)
		{
			break;
		}
		const int32 B = Slot * 4;
		OutData[B + 0] = FVector4f(C.Pos.X, C.Pos.Y, C.Pos.Z, MMDAnimeLightType::Spot);
		OutData[B + 1] = FVector4f(C.Color.R, C.Color.G, C.Color.B, C.Intensity);
		OutData[B + 2] = FVector4f(C.Dir.X, C.Dir.Y, C.Dir.Z, C.Radius);
		OutData[B + 3] = FVector4f(C.InnerCos, C.OuterCos, 2.0f, 0.0f);
		Slot++;
	}

	for (const FRectCand& C : Rects)
	{
		if (Slot >= MaxLights)
		{
			break;
		}
		const int32 B = Slot * 4;
		OutData[B + 0] = FVector4f(C.Pos.X, C.Pos.Y, C.Pos.Z, MMDAnimeLightType::Rect);
		OutData[B + 1] = FVector4f(C.Color.R, C.Color.G, C.Color.B, C.Intensity);
		OutData[B + 2] = FVector4f(C.Dir.X, C.Dir.Y, C.Dir.Z, C.Radius);
		OutData[B + 3] = FVector4f(C.SizeX, C.SizeY, 0.0f, 0.0f);
		Slot++;
	}

	// ---- debug log when the light configuration changes ----
	static int32 LastLoggedKey = -1;
	const uint32 KeyBits = static_cast<uint32>(Directionals.Num())
		| (static_cast<uint32>(Points.Num()) << 8) | (static_cast<uint32>(Spots.Num()) << 16)
		| (static_cast<uint32>(Rects.Num()) << 24);
	const int32 Key = static_cast<int32>(KeyBits);
	if (Key != LastLoggedKey)
	{
		LastLoggedKey = Key;
		UE_LOG(LogTemp, Warning, TEXT("[MMDAnimeLight] Collected -> Directional=%d Point=%d Spot=%d Rect=%d"),
			FMath::Min(Directionals.Num(), MaxLights),
			FMath::Min(Points.Num(), MaxLights),
			FMath::Min(Spots.Num(), MaxLights),
			FMath::Min(Rects.Num(), MaxLights));
	}
}

void UMMDAnimeLightDataSubsystem::WriteLightData_RenderThread(FRDGBuilder& GraphBuilder)
{
	check(IsInRenderingThread());
	if (!bCollectionEnabled || !LightDataRT || !LightDataRT->GetRenderTargetResource())
	{
		return;
	}

	if (RenderThreadLightData.Num() == 0)
	{
		return;
	}

	FRHITexture* RTTex = LightDataRT->GetRenderTargetResource()->GetRenderTargetTexture().GetReference();
	if (!RTTex)
	{
		return;
	}

	FRDGTextureRef RT = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(RTTex, TEXT("MMDAnimeLightDataRT")));
	GraphBuilder.UseInternalAccessMode(RT);

	FRDGBufferRef LightBuffer = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("MMDAnimeLightData"),
		sizeof(FVector4f),
		RenderThreadLightData.Num(),
		RenderThreadLightData.GetData(),
		RenderThreadLightData.Num() * sizeof(FVector4f));

	FRDGBufferSRVRef LightSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LightBuffer));
	FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RT));

	FMMDAnimeWriteLightsCS::FParameters* Params =
		GraphBuilder.AllocParameters<FMMDAnimeWriteLightsCS::FParameters>();
	Params->LightData = LightSRV;
	Params->OutputTexture = OutputUAV;

	TShaderMapRef<FMMDAnimeWriteLightsCS> CS(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	if (!CS.GetShader())
	{
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogTemp, Error, TEXT("[MMDAnimeLight] FMMDAnimeWriteLightsCS shader is NULL (feature level %d). Check shader compile errors in Output Log."),
				(int32)GMaxRHIFeatureLevel);
		}
		return;
	}

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MMDAnimeWriteLights"),
		CS,
		Params,
		FIntVector(FMath::DivideAndRoundUp(MaxLights * 4 + 19, 64), 1, 1));
	GraphBuilder.UseExternalAccessMode(RT, ERHIAccess::SRVMask);
}
