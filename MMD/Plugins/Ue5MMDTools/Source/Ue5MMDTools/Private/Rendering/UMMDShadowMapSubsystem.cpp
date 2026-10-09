#include "Rendering/UMMDShadowMapSubsystem.h"

#include "EngineModule.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/FMMDShadowCustomRenderPass.h"
#include "Actors/AMMDActor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "Math/RotationMatrix.h"
#include "RenderingThread.h"

#if WITH_EDITOR
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

namespace
{
	const TCHAR* GMMDShadowMapRT_PackagePath = TEXT("/Ue5MMDTools/Rendering");
	const TCHAR* GMMDShadowMapRT_AssetName   = TEXT("MMDShadowMapRT");
	const TCHAR* GMMDShadowMapRT_AssetPath   = TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT");

	// 把这个 Tag 挂到 actor（或组件）上，即可让该物体不参与阴影投射（不自投影、不投给别处）。
	// 相当于给任意 mesh 一个"类似 AMMDActor"的属性，见 CollectHiddenMMDPrimitives。
	const FName GMMDShadowExcludeTag(TEXT("MMDShadowExclude"));
	const FName GMMDShadowIncludeTag(TEXT("MMDShadowInclude"));
}

UMMDShadowMapSubsystem* UMMDShadowMapSubsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UMMDShadowMapSubsystem>() : nullptr;
}

void UMMDShadowMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 实际清理放到第一次 UpdateShadowForFrame 时做（节流版），这里不做。
	// 因为 EngineSubsystem 初始化时 GWorld 可能还没加载到目标关卡，TActorIterator 找不到。
}

void UMMDShadowMapSubsystem::Deinitialize()
{
	// 深度 pass 持有 RT 资源指针，释放子系统持有的纹理前等待渲染线程消费完。
	FlushRenderingCommands();
	Super::Deinitialize();
}

// 兼容旧版捕获器：仅停用仍指向插件阴影 RT 的对象，避免自动删除用户关卡 actor。
void UMMDShadowMapSubsystem::CleanupStaleCaptureActors(UWorld* World)
{
	const double Now = FPlatformTime::Seconds();
	if (LastCleanupWorld.Get() == World && Now - LastCleanupTime < 2.0)
	{
		return;
	}
	LastCleanupWorld = World;
	LastCleanupTime = Now;

	for (TActorIterator<ASceneCapture2D> It(World); It; ++It)
	{
		ASceneCapture2D* SC = *It;
		if (!SC)
		{
			continue;
		}
		const FString Name = SC->GetName();
		if (Name.StartsWith(TEXT("MMDShadowMapCapture")) || Name.StartsWith(TEXT("MMDShadowCapture")))
		{
			if (USceneCaptureComponent2D* Cap = SC->GetCaptureComponent2D())
			{
				UTextureRenderTarget2D* Target = Cap->TextureTarget;
				if (Target && (Target == ShadowMapRT || Target->GetPathName() == GMMDShadowMapRT_AssetPath))
				{
					Cap->bCaptureEveryFrame = false;
					Cap->bCaptureOnMovement = false;
					Cap->TextureTarget = nullptr;
					UE_LOG(LogTemp, Warning, TEXT("[MMDShadowMap] 已停用旧版阴影捕获器：%s"), *SC->GetName());
				}
			}
		}
	}
}

void UMMDShadowMapSubsystem::UpdateShadowForFrame(FSceneViewFamily* InViewFamily, const FSceneView* InView)
{
	UMMDAnimeLightDataSubsystem* LightSubsystem = UMMDAnimeLightDataSubsystem::Get();
	if (!LightSubsystem)
	{
		return;
	}

	FVector4f Invalid[18];
	FMemory::Memzero(Invalid, sizeof(Invalid));
	FSceneInterface* Scene = InViewFamily ? InViewFamily->Scene : nullptr;
	UWorld* World = Scene ? Scene->GetWorld() : nullptr;
	if (!World || !InView)
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	if (!bAutoSetupDone)
	{
		bAutoSetupDone = true;
		AutoSetupShadowMapRT();
	}

	// 关卡可能晚于子系统加载，保留节流后的旧版捕获器检查。
	CleanupStaleCaptureActors(World);

	if (!bEnabled || !ShadowMapRT)
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	FVector LightDir;
	if (!GetMainLightDirection(World, LightDir))
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	// 四级联共用一个 scratch，结果逐级复制到现有 MMDShadowMapRT 的 2x2 atlas。
	if (!ShadowMapScratchRT)
	{
		ShadowMapScratchRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		if (ShadowMapScratchRT)
		{
			ShadowMapScratchRT->RenderTargetFormat = RTF_R32f;
			ShadowMapScratchRT->bAutoGenerateMips = false;
			ShadowMapScratchRT->InitAutoFormat(ShadowMapResolution, ShadowMapResolution);
			ShadowMapScratchRT->UpdateResourceImmediate(true);
		}
	}
	if (!ShadowMapScratchRT)
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	const FVector D = LightDir.GetSafeNormal();
	const FVector WorldUp = FMath::Abs(FVector::DotProduct(D, FVector::UpVector)) > 0.99f
		? FVector::RightVector : FVector::UpVector;
	const FVector R = (WorldUp ^ D).GetSafeNormal();
	const FVector U = (D ^ R).GetSafeNormal();
	const FVector F = D;

	const FVector CamPos = InView->ViewLocation;
	const FRotationMatrix CamRotation(InView->ViewRotation);
	const FVector CamForward = CamRotation.GetUnitAxis(EAxis::X);
	const FVector CamRight = CamRotation.GetUnitAxis(EAxis::Y);
	const FVector CamUp = CamRotation.GetUnitAxis(EAxis::Z);
	const FMatrix& CameraProjection = InView->ViewMatrices.GetViewToClip();
	const bool bPerspective = CameraProjection.M[3][3] < 1.0f;
	const float TanHalfHorizontal = 1.0f / FMath::Max(FMath::Abs(CameraProjection.M[0][0]), 0.0001f);
	const float TanHalfVertical = 1.0f / FMath::Max(FMath::Abs(CameraProjection.M[1][1]), 0.0001f);
	const float NearClip = bPerspective
		? FMath::Max(static_cast<float>(CameraProjection.M[3][2]), 5.0f)
		: 0.0f;
	const float MaxShadowDistance = FMath::Max(ShadowDistance, NearClip + 100.0f);

	CollectHiddenMMDPrimitives(World, CachedHiddenPrimitives);

	static constexpr int32 CascadeCount = 4;
	struct FCascadeData
	{
		FVector Origin = FVector::ZeroVector;
		FMatrix ViewRotation = FMatrix::Identity;
		FMatrix Projection = FMatrix::Identity;
		float Width = 0.0f;
		float DepthRange = 0.0f;
		float SplitFar = 0.0f;
		float WorldUnitsPerTexel = 0.0f;
	};
	FCascadeData Cascades[CascadeCount];

	// Unity/UE 常用 practical split：线性与对数分割混合。每级用该段视锥的
	// 包围球生成稳定方形投影，再把光空间中心吸附到阴影 texel，避免相机移动时游泳。
	float SplitNear = NearClip;
	for (int32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
	{
		const float Ratio = static_cast<float>(CascadeIndex + 1) / static_cast<float>(CascadeCount);
		const float LinearSplit = FMath::Lerp(NearClip, MaxShadowDistance, Ratio);
		const float LogSplit = NearClip > 0.0f
			? NearClip * FMath::Pow(MaxShadowDistance / NearClip, Ratio)
			: LinearSplit;
		const float SplitFar = FMath::Lerp(LinearSplit, LogSplit, CascadeSplitLambda);

		FVector Corners[8];
		int32 CornerIndex = 0;
		for (int32 PlaneIndex = 0; PlaneIndex < 2; ++PlaneIndex)
		{
			const float PlaneDistance = PlaneIndex == 0 ? SplitNear : SplitFar;
			const float HalfWidth = bPerspective ? PlaneDistance * TanHalfHorizontal : TanHalfHorizontal;
			const float HalfHeight = bPerspective ? PlaneDistance * TanHalfVertical : TanHalfVertical;
			const FVector PlaneCenter = CamPos + CamForward * PlaneDistance;
			for (int32 Y = -1; Y <= 1; Y += 2)
			{
				for (int32 X = -1; X <= 1; X += 2)
				{
					Corners[CornerIndex++] = PlaneCenter
						+ CamRight * (HalfWidth * static_cast<float>(X))
						+ CamUp * (HalfHeight * static_cast<float>(Y));
				}
			}
		}

		FVector Center = FVector::ZeroVector;
		for (const FVector& Corner : Corners)
		{
			Center += Corner;
		}
		Center /= UE_ARRAY_COUNT(Corners);

		float Radius = 0.0f;
		for (const FVector& Corner : Corners)
		{
			Radius = FMath::Max(Radius, static_cast<float>(FVector::Distance(Center, Corner)));
		}
		Radius = FMath::CeilToFloat(Radius * 16.0f) / 16.0f;

		FCascadeData& Out = Cascades[CascadeIndex];
		Out.Width = FMath::Max(Radius * 2.0f, 100.0f);
		Out.WorldUnitsPerTexel = Out.Width / static_cast<float>(ShadowMapResolution);
		Out.SplitFar = SplitFar;
		Out.DepthRange = FMath::Max(OrthoWidth, Out.Width + 1000.0f);

		const double CenterR = FVector::DotProduct(Center, R);
		const double CenterU = FVector::DotProduct(Center, U);
		const double SnappedR = FMath::RoundToDouble(CenterR / Out.WorldUnitsPerTexel) * Out.WorldUnitsPerTexel;
		const double SnappedU = FMath::RoundToDouble(CenterU / Out.WorldUnitsPerTexel) * Out.WorldUnitsPerTexel;
		Center += R * (SnappedR - CenterR) + U * (SnappedU - CenterU);
		Out.Origin = Center - D * (Out.DepthRange * 0.5f);

		FMatrix ViewMatrix = FMatrix::Identity;
		ViewMatrix.SetColumn(0, FVector4(R, 0.f));
		ViewMatrix.SetColumn(1, FVector4(U, 0.f));
		ViewMatrix.SetColumn(2, FVector4(F, 0.f));
		ViewMatrix.M[3][0] = -FVector::DotProduct(Out.Origin, R);
		ViewMatrix.M[3][1] = -FVector::DotProduct(Out.Origin, U);
		ViewMatrix.M[3][2] = -FVector::DotProduct(Out.Origin, F);
		Out.ViewRotation = ViewMatrix;
		Out.ViewRotation.M[3][0] = 0.0;
		Out.ViewRotation.M[3][1] = 0.0;
		Out.ViewRotation.M[3][2] = 0.0;
		Out.Projection = FReversedZOrthoMatrix(
			Out.Width * 0.5f,
			Out.Width * 0.5f,
			1.0 / Out.DepthRange,
			0.0);
		SplitNear = SplitFar;
	}

	bool bCascadeValid[CascadeCount] = { false, false, false, false };
	int32 AddedPassCount = 0;
	for (int32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
	{
		const FCascadeData& Cascade = Cascades[CascadeIndex];
		FSceneInterface::FCustomRenderPassRendererInput PassInput;
		PassInput.ViewLocation = Cascade.Origin;
		PassInput.ViewRotationMatrix = Cascade.ViewRotation;
		PassInput.ProjectionMatrix = Cascade.Projection;
		PassInput.bIsSceneCapture = true;
		const FIntPoint AtlasOffset(
			(CascadeIndex & 1) * ShadowMapResolution,
			(CascadeIndex >> 1) * ShadowMapResolution);
		PassInput.CustomRenderPass = new FMMDShadowCustomRenderPass(
			ShadowMapScratchRT,
			FIntPoint(ShadowMapResolution, ShadowMapResolution),
			ShadowMapRT,
			AtlasOffset,
			CascadeIndex == CascadeCount - 1);
		if (CachedHiddenPrimitives.Num() > 0)
		{
			PassInput.HiddenPrimitives = CachedHiddenPrimitives;
		}
		if (Scene->AddCustomRenderPass(InViewFamily, PassInput))
		{
			bCascadeValid[CascadeIndex] = true;
			++AddedPassCount;
		}
	}

	// 周期性诊断输出（每 ~5 秒一次，避免刷屏）。
	static double LastDiagTime = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastDiagTime > 5.0)
	{
		LastDiagTime = Now;
		UE_LOG(LogTemp, Log,
			TEXT("[MMDShadowMap] CSM: Passes=%d/4 LightDir=(%.2f,%.2f,%.2f) "
				 "Splits=(%.0f,%.0f,%.0f,%.0f) CascadeRes=%d Atlas=%d Hidden=%d"),
			AddedPassCount,
			D.X, D.Y, D.Z,
			Cascades[0].SplitFar, Cascades[1].SplitFar,
			Cascades[2].SplitFar, Cascades[3].SplitFar,
			ShadowMapResolution,
			ShadowMapResolution * 2,
			CachedHiddenPrimitives.Num());
	}

	// 每级 4 texel，最后 2 texel 保存主相机位置和前向。Data0.w 是深度范围，0 表示无效。
	FVector4f Data[18];
	for (int32 CascadeIndex = 0; CascadeIndex < CascadeCount; ++CascadeIndex)
	{
		const FCascadeData& Cascade = Cascades[CascadeIndex];
		const float DepthRange = bCascadeValid[CascadeIndex] ? Cascade.DepthRange : 0.0f;
		Data[CascadeIndex * 4 + 0] = FVector4f(Cascade.Origin.X, Cascade.Origin.Y, Cascade.Origin.Z, DepthRange);
		Data[CascadeIndex * 4 + 1] = FVector4f(R.X / Cascade.Width, R.Y / Cascade.Width, R.Z / Cascade.Width, GlobalBias);
		Data[CascadeIndex * 4 + 2] = FVector4f(U.X / Cascade.Width, U.Y / Cascade.Width, U.Z / Cascade.Width,
			Cascade.WorldUnitsPerTexel * NormalBiasInTexels);
		Data[CascadeIndex * 4 + 3] = FVector4f(F.X, F.Y, F.Z, Cascade.SplitFar);
	}
	Data[16] = FVector4f(CamPos.X, CamPos.Y, CamPos.Z, 0.0f);
	Data[17] = FVector4f(CamForward.X, CamForward.Y, CamForward.Z,
		AddedPassCount == CascadeCount ? 1.0f : 0.0f);

	LightSubsystem->SetShadowCameraData(Data);
}

void UMMDShadowMapSubsystem::SetShadowEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
}

void UMMDShadowMapSubsystem::SetShadowMapRenderTarget(UTextureRenderTarget2D* InRenderTarget)
{
	ShadowMapRT = InRenderTarget;
	if (ShadowMapRT)
	{
		if (ShadowMapRT->SizeX == ShadowMapResolution * 2 && ShadowMapRT->SizeY == ShadowMapResolution * 2 &&
			ShadowMapRT->GetFormat() == PF_R32_FLOAT && ShadowMapRT->bCanCreateUAV &&
			!ShadowMapRT->bAutoGenerateMips && ShadowMapRT->GetResource())
		{
			return;
		}
		ShadowMapRT->bCanCreateUAV = true;
		ShadowMapRT->RenderTargetFormat = RTF_R32f;
		ShadowMapRT->bAutoGenerateMips = false;
		ShadowMapRT->InitAutoFormat(ShadowMapResolution * 2, ShadowMapResolution * 2);
		ShadowMapRT->UpdateResourceImmediate(true);

		UE_LOG(LogTemp, Log, TEXT("[MMDShadowMap] Shadow depth RT set: %s (%dx%d R32F)"),
			*ShadowMapRT->GetName(), ShadowMapRT->SizeX, ShadowMapRT->SizeY);
	}
}

void UMMDShadowMapSubsystem::SetShadowDistance(float InDistance)
{
	ShadowDistance = FMath::Max(InDistance, 100.0f);
}

void UMMDShadowMapSubsystem::SetOrthoWidth(float InWidth)
{
	OrthoWidth = FMath::Max(InWidth, 100.0f);
}

void UMMDShadowMapSubsystem::SetGlobalBias(float InBias)
{
	GlobalBias = InBias;
}

void UMMDShadowMapSubsystem::SetShadowMapResolution(int32 InResolution)
{
	// 这是单级分辨率；atlas 为 2x2，因此总边长不超过 8192。
	const int32 NewResolution = FMath::Clamp(InResolution, 256, 4096);
	if (ShadowMapResolution == NewResolution)
	{
		return;
	}
	ShadowMapResolution = NewResolution;
	if (ShadowMapRT)
	{
		ShadowMapRT->InitAutoFormat(ShadowMapResolution * 2, ShadowMapResolution * 2);
		ShadowMapRT->UpdateResourceImmediate(true);
	}
	if (ShadowMapScratchRT)
	{
		ShadowMapScratchRT->InitAutoFormat(ShadowMapResolution, ShadowMapResolution);
		ShadowMapScratchRT->UpdateResourceImmediate(true);
	}
}

void UMMDShadowMapSubsystem::SetCascadeSplitLambda(float InLambda)
{
	CascadeSplitLambda = FMath::Clamp(InLambda, 0.0f, 1.0f);
}

void UMMDShadowMapSubsystem::SetNormalBias(float InBiasInTexels)
{
	NormalBiasInTexels = FMath::Clamp(InBiasInTexels, 0.0f, 8.0f);
}

void UMMDShadowMapSubsystem::SetHideMMDActors(bool bInHide)
{
	bHideMMDActors = bInHide;
}

bool UMMDShadowMapSubsystem::GetMainLightDirection(UWorld* World, FVector& OutDir)
{
	float StrongestIntensity = 0.0f;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}
		TInlineComponentArray<UDirectionalLightComponent*> Comps(Actor);
		for (UDirectionalLightComponent* DirComp : Comps)
		{
			if (DirComp && DirComp->IsRegistered() && DirComp->IsVisible() && DirComp->Intensity > StrongestIntensity)
			{
				OutDir = DirComp->GetForwardVector();
				StrongestIntensity = DirComp->Intensity;
			}
		}
	}
	return StrongestIntensity > 0.0f;
}

void UMMDShadowMapSubsystem::SetShadowMapExcludedActors(const TArray<AActor*>& Actors)
{
	ExcludedActors.Empty(Actors.Num());
	for (AActor* Actor : Actors)
	{
		if (Actor)
		{
			ExcludedActors.Add(Actor);
		}
	}
}

void UMMDShadowMapSubsystem::CollectHiddenMMDPrimitives(UWorld* World, TSet<FPrimitiveComponentId>& OutHidden)
{
	OutHidden.Reset();

	// 单趟遍历所有 actor，命中任一条即从阴影 capture 剔除：
	//   1) 未启用 MMDShadowInclude 的 MMD 组件（bHideMMDActors）
	//   2) 显式排除的 actor（SetShadowMapExcludedActors）
	//   3) 挂上 GMMDShadowExcludeTag 的 actor 或组件（给任意 mesh 一个"类似 AMMDActor"的属性）
	// 显式排除始终优先于预设的组件自投影选择。
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}

		const bool bExcludeActor = Actor->Tags.Contains(GMMDShadowExcludeTag)
			|| ExcludedActors.Contains(TWeakObjectPtr<AActor>(Actor));
		const bool bHideActorByDefault = bHideMMDActors && Actor->IsA<AMMDActor>();

		TInlineComponentArray<UPrimitiveComponent*> Comps(Actor);
		for (UPrimitiveComponent* Comp : Comps)
		{
			// 预设只为自己的组件启用自投影；显式排除仍覆盖此选择。
			const bool bExcludeComp = bExcludeActor || Comp->ComponentTags.Contains(GMMDShadowExcludeTag)
				|| (bHideActorByDefault && !Comp->ComponentTags.Contains(GMMDShadowIncludeTag));
			if (bExcludeComp && Comp->IsVisible() && Comp->SceneProxy)
			{
				OutHidden.Add(Comp->GetPrimitiveSceneId());
			}
		}
	}
}

void UMMDShadowMapSubsystem::AutoSetupShadowMapRT()
{
	UTextureRenderTarget2D* ExistingRT = LoadObject<UTextureRenderTarget2D>(nullptr, GMMDShadowMapRT_AssetPath);
	if (ExistingRT)
	{
		SetShadowMapRenderTarget(ExistingRT);
		return;
	}

#if WITH_EDITOR
	const FString PackagePath = FString(GMMDShadowMapRT_PackagePath) / GMMDShadowMapRT_AssetName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (Package)
	{
		UTextureRenderTarget2D* NewRT = NewObject<UTextureRenderTarget2D>(
			Package, GMMDShadowMapRT_AssetName, RF_Public | RF_Standalone);

		if (NewRT)
		{
			NewRT->RenderTargetFormat = RTF_R32f;
			NewRT->bCanCreateUAV = true;
			NewRT->bAutoGenerateMips = false;
			NewRT->InitAutoFormat(ShadowMapResolution * 2, ShadowMapResolution * 2);
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

			UE_LOG(LogTemp, Log, TEXT("[MMDShadowMap] Auto-created shadow depth RT asset at %s.%s"),
				*PackagePath, GMMDShadowMapRT_AssetName);
			SetShadowMapRenderTarget(NewRT);
			return;
		}
	}
#endif

	UE_LOG(LogTemp, Warning, TEXT("[MMDShadowMap] Shadow depth RT not found at %s and could not be created."),
		GMMDShadowMapRT_AssetPath);
}
