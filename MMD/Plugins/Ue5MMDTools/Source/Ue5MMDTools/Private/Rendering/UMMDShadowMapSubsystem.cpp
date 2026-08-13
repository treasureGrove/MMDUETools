#include "Rendering/UMMDShadowMapSubsystem.h"

#include "EngineModule.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/SceneCapture2D.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
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
	const int32  GShadowMapResolution = 2048;

	// 与引擎 SceneCaptureRendering.cpp 内部一致的轴交换矩阵（向量 (x,y,z) -> (y,z,x)）。
	const FMatrix GMMDShadowAxisSwap(
		FPlane(0.f, 0.f, 1.f, 0.f),
		FPlane(1.f, 0.f, 0.f, 0.f),
		FPlane(0.f, 1.f, 0.f, 0.f),
		FPlane(0.f, 0.f, 0.f, 1.f));
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

void UMMDShadowMapSubsystem::UpdateShadowForFrame(FSceneViewFamily* InViewFamily)
{
	UWorld* World = GWorld;
	if (!World || !InViewFamily)
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

	// 未启用 / 无 RT / 无平行光 / 无 scene -> 写无效标记（Valid=0），材质侧自动退回不受影。
	FVector4f Invalid[4] = {
		FVector4f(0.f, 0.f, 0.f, 0.f),
		FVector4f(0.f, 0.f, 0.f, 0.f),
		FVector4f(0.f, 0.f, 0.f, 0.f),
		FVector4f(0.f, 0.f, 0.f, 0.f) };

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

	FVector CamPos;
	if (!GetCameraPosition(World, CamPos))
	{
		LightSubsystem->SetShadowCameraData(Invalid);
		return;
	}

	const FVector D = LightDir.GetSafeNormal();

	// ---- 阴影相机：放在主相机沿光来源方向后退 ShadowDistance 处，看向 +D ----
	// Origin 是阴影相机的世界位置；从此点看向 +D（光前进方向）即为阴影相机视角。
	const FVector Origin = CamPos - D * ShadowDistance;

	// ---- 选择世界向上方向（避免与光方向平行时退化）----
	const FVector WorldUp = FMath::Abs(FVector::DotProduct(D, FVector::UpVector)) > 0.99f
		? FVector::RightVector : FVector::UpVector;

	// ---- 构造 ViewMatrix（与 FLookFromMatrix 同一约定：视空间 +Z = LookDirection = D）----
	//
	// 这是引擎内部球形阴影、cloud shadow 等通用做法（HairStrandsUtils.cpp:208 用 FLookAtMatrix）。
	// 列向量 = 视空间 +X/+Y/+Z 在世界中的方向：
	//   R = Up ^ D           (世界中的"右"方向)
	//   U = D ^ R            (世界中的"上"方向)
	//   F = D                (世界中的"前"方向 = 光前进方向)
	//
	// ViewMatrix.TransformVector((1,0,0)) = 列 0 = R，正好对应 shader 端要用的轴。
	const FVector R = (WorldUp ^ D).GetSafeNormal();
	const FVector U = (D ^ R).GetSafeNormal();
	const FVector F = D;

	FMatrix ViewMatrix = FMatrix::Identity;
	ViewMatrix.SetColumn(0, FVector4(R, 0.f));
	ViewMatrix.SetColumn(1, FVector4(U, 0.f));
	ViewMatrix.SetColumn(2, FVector4(F, 0.f));
	ViewMatrix.SetColumn(3, FVector4(FVector::ZeroVector, 1.f));

	// 加 translation（把世界原点搬到 Origin）：
	//   ViewMatrix * (p - Origin) = (R|U|F|0) * (p-Origin) 视空间向量
	//   translation = -dot(Origin, R/U/F)，对应 FLookFromMatrix line 970-972
	ViewMatrix.M[3][0] = -FVector::DotProduct(Origin, R);
	ViewMatrix.M[3][1] = -FVector::DotProduct(Origin, U);
	ViewMatrix.M[3][2] = -FVector::DotProduct(Origin, F);

	// 引擎 CustomRenderPass 需要的 ViewRotationMatrix（不含 translation）+ ProjectionMatrix 分别传：
	//   ViewRotationMatrix 用于 ViewInitOptions；最终引擎自己构造完整 ViewMatrix
	//   所以这里要把 translation 拆出去：
	FMatrix ViewRotationMatrix = ViewMatrix;
	ViewRotationMatrix.M[3][0] = 0.0;
	ViewRotationMatrix.M[3][1] = 0.0;
	ViewRotationMatrix.M[3][2] = 0.0;

	// ---- 正交投影（覆盖范围 = OrthoWidth × OrthoWidth，深度 = ShadowDistance × 2 + OrthoWidth）----
	// 引擎方向光投影用的是 FShadowProjectionMatrix（MinProjected/MaxProjected 决定 off-center 范围），
	// 这里简化为对称正交，视空间 XY 范围 [-OrthoWidth/2, +OrthoWidth/2]。
	// ⚠️ FReversedZOrthoMatrix 的 Width/Height 是【半宽】（引擎 SceneCapture/VSM/相机均传半宽，
	//    见 SceneCaptureRendering.cpp BuildOrthoMatrix: OrthoWidth = InOrthoWidth/2）。
	//    要覆盖 OrthoWidth 全宽，必须传 OrthoWidth/2，与 shader 端 UV = dot(ToPix,R)/OrthoWidth + 0.5 对齐。
	const float Far = FMath::Max(ShadowDistance * 2.0f + OrthoWidth, 1000.0f);
	const FMatrix ProjectionMatrix =
		FReversedZOrthoMatrix(OrthoWidth * 0.5f, OrthoWidth * 0.5f, 1.0 / Far, 0.0);

	// ---- 提交 CustomRenderPass（无 actor / 无 component，纯引擎 API）----
	FSceneInterface::FCustomRenderPassRendererInput PassInput;
	PassInput.ViewLocation = Origin;
	PassInput.ViewRotationMatrix = ViewRotationMatrix;
	PassInput.ProjectionMatrix = ProjectionMatrix;
	PassInput.bIsSceneCapture = true;
	PassInput.CustomRenderPass =
		new FMMDShadowCustomRenderPass(ShadowMapRT, FIntPoint(GShadowMapResolution, GShadowMapResolution));

	// 排除 MMD 模型自身（避免自阴影与 toon ramp 叠加双重变暗）。
	if (bHideMMDActors)
	{
		int32 CachedCount = CachedHiddenPrimitives.Num();
		CollectHiddenMMDPrimitives(World, CachedHiddenPrimitives);
		if (CachedHiddenPrimitives.Num() != CachedCount)
		{
			LastHiddenPrimitiveCount = CachedHiddenPrimitives.Num();
		}
		PassInput.HiddenPrimitives = CachedHiddenPrimitives;
	}

	const bool bPassAdded = Scene->AddCustomRenderPass(InViewFamily, PassInput);

	// 周期性诊断输出（每 ~5 秒一次，避免刷屏）。
	static double LastDiagTime = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastDiagTime > 5.0)
	{
		LastDiagTime = Now;
		UE_LOG(LogTemp, Log,
			TEXT("[MMDShadowMap] Diag: PassAdded=%d Origin=(%.0f,%.0f,%.0f) LightDir=(%.2f,%.2f,%.2f) "
				 "OrthoWidth=%.0f ShadowDist=%.0f HiddenPrims=%d RT=%s(%dx%d)"),
			bPassAdded ? 1 : 0,
			Origin.X, Origin.Y, Origin.Z,
			D.X, D.Y, D.Z,
			OrthoWidth, ShadowDistance,
			bHideMMDActors ? CachedHiddenPrimitives.Num() : 0,
			ShadowMapRT ? *ShadowMapRT->GetName() : TEXT("null"),
			ShadowMapRT ? ShadowMapRT->SizeX : 0,
			ShadowMapRT ? ShadowMapRT->SizeY : 0);
	}

	// ---- 打包阴影相机基（写入 LightDataRT 第 2 行）----
	// 直接用上面构造的 ViewMatrix 列向量（与渲染严格一致）。
	//   texel0 = (Origin.xyz, Valid)
	//   texel1 = (R/OrthoWidth, GlobalBias)        R = view+X 在世界中的方向
	//   texel2 = (U/OrthoWidth, 0)                 U = view+Y 在世界中的方向
	//   texel3 = (F.xyz, TexelSize)                F = view+Z 在世界中的方向 = 光方向
	const float ShadowTexel = 1.0f / (float)GShadowMapResolution;
	FVector4f Data[4];
	Data[0] = FVector4f(Origin.X, Origin.Y, Origin.Z, 1.f);
	Data[1] = FVector4f(R.X / OrthoWidth, R.Y / OrthoWidth, R.Z / OrthoWidth, GlobalBias);
	Data[2] = FVector4f(U.X / OrthoWidth, U.Y / OrthoWidth, U.Z / OrthoWidth, 0.f);
	Data[3] = FVector4f(F.X, F.Y, F.Z, ShadowTexel);

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
		ShadowMapRT->RenderTargetFormat = RTF_RGBA16f;
		ShadowMapRT->bAutoGenerateMips = false;
		ShadowMapRT->InitAutoFormat(GShadowMapResolution, GShadowMapResolution);
		ShadowMapRT->UpdateResourceImmediate(true);

		UE_LOG(LogTemp, Log, TEXT("[MMDShadowMap] Shadow depth RT set: %s (%dx%d RGBA16F)"),
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

bool UMMDShadowMapSubsystem::GetCameraPosition(UWorld* World, FVector& OutPos)
{
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			OutPos = PC->PlayerCameraManager->GetCameraLocation();
			return true;
		}
	}
#if WITH_EDITOR
	if (GEditor)
	{
		for (FLevelEditorViewportClient* EVC : GEditor->GetLevelViewportClients())
		{
			if (EVC)
			{
				OutPos = EVC->GetViewLocation();
				return true;
			}
		}
	}
#endif
	return false;
}

void UMMDShadowMapSubsystem::CollectHiddenMMDPrimitives(UWorld* World, TSet<FPrimitiveComponentId>& OutHidden)
{
	OutHidden.Reset();
	for (TActorIterator<AMMDActor> It(World); It; ++It)
	{
		AMMDActor* MMDActor = *It;
		if (!MMDActor || MMDActor->IsHidden())
		{
			continue;
		}
		// 把骨骼网格的 PrimitiveComponentId 加入隐藏集合，
		// CustomRenderPass 渲染时引擎会跳过这些 primitive。
		TArray<USkeletalMeshComponent*> Comps;
		MMDActor->GetComponents<USkeletalMeshComponent>(Comps);
		for (USkeletalMeshComponent* Skc : Comps)
		{
			if (Skc && Skc->IsVisible() && Skc->SceneProxy)
			{
				OutHidden.Add(Skc->GetPrimitiveSceneId());
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
			NewRT->RenderTargetFormat = RTF_RGBA16f;
			NewRT->bCanCreateUAV = true;
			NewRT->bAutoGenerateMips = false;
			NewRT->InitAutoFormat(GShadowMapResolution, GShadowMapResolution);
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
