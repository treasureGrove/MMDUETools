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
	Super::Deinitialize();
}

// 清理关卡里残留的旧版本 SceneCapture2D actor（之前用 SpawnActor 创建 + RF_Transactional 保存进关卡的）。
// 这些 actor 每帧也会跑独立 SceneCapture 渲染，写入同名 RT 会覆盖我们 CustomRenderPass 的输出，
// 还可能触发 RDG 校验崩溃。命名兼容历史版本：MMDShadowMapCapture / MMDShadowCapture。
// 节流：每 2 秒扫一次，避免每帧遍历 actor 的开销。
void UMMDShadowMapSubsystem::CleanupStaleCaptureActors(UWorld* World)
{
	static double LastCleanupTime = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCleanupTime < 2.0)
	{
		return;
	}
	LastCleanupTime = Now;

	TArray<AActor*> Stale;
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
			Stale.Add(SC);
		}
	}

	for (AActor* A : Stale)
	{
		// 先断开 TextureTarget 指向 MMDShadowMapRT，
		// 否则下一帧 SceneCapture 还会用同一张 RHI 纹理再注册一次（状态污染会导致 RDG 校验崩溃）。
		if (ASceneCapture2D* SC = Cast<ASceneCapture2D>(A))
		{
			if (USceneCaptureComponent2D* Cap = SC->GetCaptureComponent2D())
			{
				Cap->TextureTarget = nullptr;
			}
		}
		UE_LOG(LogTemp, Warning, TEXT("[MMDShadowMap] Cleaning up stale capture actor: %s (this is a leftover from old versions)"), *A->GetName());
		A->Destroy();
	}
}

void UMMDShadowMapSubsystem::UpdateShadowForFrame(FSceneViewFamily* InViewFamily, const FSceneView* InView)
{
	UWorld* World = GWorld;
	if (!World || !InViewFamily || !InView)
	{
		return;
	}

	// 清理关卡里残留的旧版 SceneCapture2D actor（带节流）。
	// 必须每帧检查机会：关卡可能比 EngineSubsystem 后加载，Initialize 那一次扫不到。
	CleanupStaleCaptureActors(World);

	if (!bAutoSetupDone)
	{
		bAutoSetupDone = true;
		AutoSetupShadowMapRT();
	}

	UMMDAnimeLightDataSubsystem* LightSubsystem = UMMDAnimeLightDataSubsystem::Get();
	if (!LightSubsystem)
	{
		return;
	}

	FVector4f Invalid[18];
	FMemory::Memzero(Invalid, sizeof(Invalid));

	if (!bEnabled || !ShadowMapRT)
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	FSceneInterface* Scene = InViewFamily->Scene;
	if (!Scene)
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
	ShadowMapResolution = FMath::Clamp(InResolution, 256, 4096);
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
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}
		if (UDirectionalLightComponent* DirComp = Actor->FindComponentByClass<UDirectionalLightComponent>())
		{
			if (DirComp->IsVisible())
			{
				OutDir = Actor->GetActorForwardVector();
				return true;
			}
		}
	}
	return false;
}

void UMMDShadowMapSubsystem::SetShadowMapExcludedActors(const TArray<AActor*>& Actors)
{
	ExcludedActors.Reset();
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
	//   1) MMD 模型自身（bHideMMDActors，默认 true）
	//   2) 显式排除的 actor（SetShadowMapExcludedActors）
	//   3) 挂上 GMMDShadowExcludeTag 的 actor 或组件（给任意 mesh 一个"类似 AMMDActor"的属性）
	// 避免自阴影与 toon ramp 叠加双重变暗。
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsHidden())
		{
			continue;
		}

		bool bExcludeActor = Actor->Tags.Contains(GMMDShadowExcludeTag)
			|| (bHideMMDActors && Actor->IsA<AMMDActor>());

		if (!bExcludeActor)
		{
			for (TWeakObjectPtr<AActor> Weak : ExcludedActors)
			{
				if (Weak.Get() == Actor)
				{
					bExcludeActor = true;
					break;
				}
			}
		}

		TArray<UPrimitiveComponent*> Comps;
		Actor->GetComponents<UPrimitiveComponent>(Comps);
		for (UPrimitiveComponent* Comp : Comps)
		{
			const bool bExcludeComp = bExcludeActor || Comp->ComponentTags.Contains(GMMDShadowExcludeTag);
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
