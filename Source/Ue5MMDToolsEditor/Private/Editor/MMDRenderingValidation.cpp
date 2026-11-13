#include "MMDRenderingValidation.h"

#include "AssetCompilingManager.h"
#include "ContentStreaming.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderGraphBuilder.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDShadowMapSubsystem.h"
#include "RenderingThread.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCompiler.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/MMDToolPreviewRenderer.h"

namespace
{
	struct FTextureState
	{
		FTextureRHIRef Texture;
		FIntPoint Size = FIntPoint::ZeroValue;
		EPixelFormat Format = PF_Unknown;
		bool bUAV = false;
	};

	FTextureState ReadTextureState(UTextureRenderTarget2D* RenderTarget)
	{
		FTextureState State;
		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		ENQUEUE_RENDER_COMMAND(MMDValidateRenderTarget)(
			[Resource, &State](FRHICommandListImmediate& RHICmdList)
			{
				if (Resource)
				{
					State.Texture = Resource->GetRenderTargetTexture();
					if (State.Texture.IsValid())
					{
						const FRHITextureDesc& Desc = State.Texture->GetDesc();
						State.Size = Desc.Extent;
						State.Format = Desc.Format;
						State.bUAV = EnumHasAnyFlags(Desc.Flags, ETextureCreateFlags::UAV);
					}
				}
			});
		FlushRenderingCommands();
		return State;
	}

	bool WriteAndReadMetadata(UMMDAnimeLightDataSubsystem* Lights, UTextureRenderTarget2D* RenderTarget,
		const TArray<FVector4f>& PreviewLights, EMMDShadowFilterQuality Quality, FLinearColor& OutMetadata)
	{
		Lights->SetShadowFilterQuality(Quality);
		Lights->SetPreviewLightOverride(PreviewLights);
		Lights->CollectLightsForFrame(nullptr, nullptr);
		ENQUEUE_RENDER_COMMAND(MMDValidateLightMetadata)(
			[Lights](FRHICommandListImmediate& RHICmdList)
			{
				FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("MMDValidateLightMetadata"));
				Lights->WriteLightData_RenderThread(GraphBuilder);
				GraphBuilder.Execute();
			});
		FlushRenderingCommands();

		TArray<FLinearColor> Pixels;
		FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
		const bool bRead = Resource && Resource->ReadLinearColorPixels(
			Pixels, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), FIntRect(18, 1, 19, 2));
		if (bRead && Pixels.Num() == 1)
		{
			OutMetadata = Pixels[0];
			return true;
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> ValidateLightMetadata(UMMDAnimeLightDataSubsystem* Lights)
	{
		TArray<TSharedPtr<FJsonValue>> Results;
		const bool bHadPreviewOverride = Lights->HasPreviewLightOverride();
		if (bHadPreviewOverride)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), TEXT("LightDataRT.MetadataPrecondition"));
			Result->SetBoolField(TEXT("passed"), false);
			Result->SetStringField(TEXT("error"), TEXT("Preview light override was already active"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return Results;
		}

		TStrongObjectPtr<UTextureRenderTarget2D> Original(Lights->GetLightDataRenderTarget());
		const EMMDShadowFilterQuality OriginalQuality = Lights->GetShadowFilterQuality();
		TStrongObjectPtr<UTextureRenderTarget2D> Source(LoadObject<UTextureRenderTarget2D>(
			nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT")));
		TStrongObjectPtr<UTextureRenderTarget2D> Clone(Source.IsValid()
			? DuplicateObject<UTextureRenderTarget2D>(Source.Get(), GetTransientPackage()) : nullptr);
		if (!Clone.IsValid())
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), TEXT("LightDataRT.MetadataSetup"));
			Result->SetBoolField(TEXT("passed"), false);
			Result->SetStringField(TEXT("error"), TEXT("Transient LightDataRT duplication failed"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return Results;
		}

		Clone->ClearFlags(RF_Public | RF_Standalone);
		Clone->SetFlags(RF_Transient);
		Lights->SetLightDataRenderTarget(Clone.Get());

		TArray<FVector4f> SparseLights;
		SparseLights.SetNumZeroed(UMMDAnimeLightDataSubsystem::MaxLights * 4);
		SparseLights[0].W = 3.0f;
		SparseLights[1].W = 1.0f;
		SparseLights[12].W = 1.0f;
		SparseLights[13].W = 2.0f;
		FLinearColor SparseMetadata;
		const bool bSparseRead = WriteAndReadMetadata(
			Lights, Clone.Get(), SparseLights, EMMDShadowFilterQuality::Low, SparseMetadata);

		TArray<FVector4f> EmptyLights;
		EmptyLights.SetNumZeroed(UMMDAnimeLightDataSubsystem::MaxLights * 4);
		FLinearColor EmptyMetadata;
		const bool bEmptyRead = WriteAndReadMetadata(
			Lights, Clone.Get(), EmptyLights, EMMDShadowFilterQuality::High, EmptyMetadata);

		Lights->SetShadowFilterQuality(OriginalQuality);
		Lights->ClearPreviewLightOverride();
		Lights->SetLightDataRenderTarget(Original.Get());
		FlushRenderingCommands();

		auto AddResult = [&Results](const TCHAR* Name, bool bRead, const FLinearColor& Actual,
			const FLinearColor& Expected)
		{
			const bool bPassed = bRead && Actual.Equals(Expected, KINDA_SMALL_NUMBER);
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Name);
			Result->SetBoolField(TEXT("readback_succeeded"), bRead);
			Result->SetStringField(TEXT("actual"), Actual.ToString());
			Result->SetStringField(TEXT("expected"), Expected.ToString());
			Result->SetBoolField(TEXT("passed"), bPassed);
			Results.Add(MakeShared<FJsonValueObject>(Result));
			UE_LOG(LogTemp, Display, TEXT("[MMDRenderingValidation] %s %s: actual=%s expected=%s"),
				bPassed ? TEXT("PASS") : TEXT("FAIL"), Name, *Actual.ToString(), *Expected.ToString());
		};
		AddResult(TEXT("LightDataRT.MetadataSparseLow"), bSparseRead, SparseMetadata, FLinearColor(4, 1, 0, 1));
		AddResult(TEXT("LightDataRT.MetadataEmptyHigh"), bEmptyRead, EmptyMetadata, FLinearColor(0, 0, 0, 1));
		return Results;
	}

	bool SavePreviewPng(UMMDToolPreviewRenderer* Renderer, const FString& OutputPath, FString& OutError)
	{
		Renderer->Capture();
		FlushRenderingCommands();
		UTextureRenderTarget2D* Target = Renderer->GetRenderTarget();
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FColor> Pixels;
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Target->SizeX * Target->SizeY)
		{
			OutError = TEXT("Render target pixel readback failed");
			return false;
		}
		uint8 MinLuma = 255;
		uint8 MaxLuma = 0;
		for (FColor& Pixel : Pixels)
		{
			const uint8 Luma = static_cast<uint8>((uint32(Pixel.R) + Pixel.G + Pixel.B) / 3);
			MinLuma = FMath::Min(MinLuma, Luma);
			MaxLuma = FMath::Max(MaxLuma, Luma);
			Pixel.A = 255;
		}
		if (MaxLuma - MinLuma < 8)
		{
			OutError = TEXT("Rendered image is effectively blank");
			return false;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY,
			TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *OutputPath))
		{
			OutError = TEXT("PNG encoding or file write failed");
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> ValidateTarget(const TCHAR* Name, const TCHAR* AssetPath,
		const FIntPoint& ExpectedSize, EPixelFormat ExpectedFormat, UTextureRenderTarget2D* Original,
		TFunctionRef<void(UTextureRenderTarget2D*)> SetTarget,
		TFunction<bool(UTextureRenderTarget2D*)> CheckFirstFrame = nullptr)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Name);
		Result->SetStringField(TEXT("asset"), AssetPath);
		Result->SetBoolField(TEXT("passed"), false);
		TStrongObjectPtr<UTextureRenderTarget2D> Source(LoadObject<UTextureRenderTarget2D>(nullptr, AssetPath));
		if (!Source.IsValid())
		{
			Result->SetStringField(TEXT("error"), TEXT("Source render target could not be loaded"));
			return Result;
		}

		const FIntPoint StoredSize(Source->SizeX, Source->SizeY);
		const EPixelFormat StoredFormat = Source->GetFormat();
		const bool bStoredUAV = Source->bCanCreateUAV;
		const bool bStoredMips = Source->bAutoGenerateMips;
		const bool bPackageWasDirty = Source->GetOutermost()->IsDirty();
		Result->SetNumberField(TEXT("stored_width"), StoredSize.X);
		Result->SetNumberField(TEXT("stored_height"), StoredSize.Y);
		Result->SetStringField(TEXT("stored_format"), GetPixelFormatString(StoredFormat));

		TStrongObjectPtr<UTextureRenderTarget2D> Clone(DuplicateObject<UTextureRenderTarget2D>(Source.Get(), GetTransientPackage()));
		if (!Clone.IsValid())
		{
			Result->SetStringField(TEXT("error"), TEXT("Transient render target duplication failed"));
			return Result;
		}
		Clone->ClearFlags(RF_Public | RF_Standalone);
		Clone->SetFlags(RF_Transient);
		TStrongObjectPtr<UTextureRenderTarget2D> OriginalGuard(Original);
		SetTarget(Clone.Get());
		const bool bFirstFrameKeepsTarget = !CheckFirstFrame || CheckFirstFrame(Clone.Get());
		if (CheckFirstFrame)
		{
			Result->SetBoolField(TEXT("first_frame_keeps_target"), bFirstFrameKeepsTarget);
		}
		FTextureRenderTargetResource* FirstResource = Clone->GameThread_GetRenderTargetResource();
		const FTextureState FirstState = ReadTextureState(Clone.Get());
		const bool bRuntimeLayout = Clone->SizeX == ExpectedSize.X && Clone->SizeY == ExpectedSize.Y &&
			Clone->GetFormat() == ExpectedFormat && Clone->bCanCreateUAV && !Clone->bAutoGenerateMips;
		const bool bGpuLayout = FirstState.Texture.IsValid() && FirstState.Size == ExpectedSize &&
			FirstState.Format == ExpectedFormat && FirstState.bUAV;

		// 保留首次 RHI 引用，可确定检测重复注册是否重建纹理，避免资源地址被分配器复用。
		SetTarget(Clone.Get());
		const FTextureState RepeatedState = ReadTextureState(Clone.Get());
		const bool bReusedResource = FirstResource && FirstResource == Clone->GameThread_GetRenderTargetResource() &&
			FirstState.Texture.IsValid() && FirstState.Texture == RepeatedState.Texture;
		SetTarget(OriginalGuard.Get());
		FlushRenderingCommands();

		const bool bSourceUnchanged = Source->SizeX == StoredSize.X && Source->SizeY == StoredSize.Y &&
			Source->GetFormat() == StoredFormat && Source->bCanCreateUAV == bStoredUAV &&
			Source->bAutoGenerateMips == bStoredMips && Source->GetOutermost()->IsDirty() == bPackageWasDirty;
		const bool bPassed = bRuntimeLayout && bGpuLayout && bReusedResource && bSourceUnchanged && bFirstFrameKeepsTarget;
		Result->SetBoolField(TEXT("runtime_layout_valid"), bRuntimeLayout);
		Result->SetBoolField(TEXT("gpu_layout_valid"), bGpuLayout);
		Result->SetBoolField(TEXT("repeat_reuses_resource"), bReusedResource);
		Result->SetBoolField(TEXT("source_unchanged"), bSourceUnchanged);
		Result->SetNumberField(TEXT("runtime_width"), FirstState.Size.X);
		Result->SetNumberField(TEXT("runtime_height"), FirstState.Size.Y);
		Result->SetStringField(TEXT("runtime_format"), GetPixelFormatString(FirstState.Format));
		Result->SetBoolField(TEXT("gpu_uav"), FirstState.bUAV);
		Result->SetBoolField(TEXT("passed"), bPassed);
		UE_LOG(LogTemp, Display, TEXT("[MMDRenderingValidation] %s %s: stored=%dx%d runtime=%dx%d layout=%d GPU=%d reuse=%d source=%d first=%d"),
			bPassed ? TEXT("PASS") : TEXT("FAIL"), Name, StoredSize.X, StoredSize.Y,
			FirstState.Size.X, FirstState.Size.Y, bRuntimeLayout, bGpuLayout, bReusedResource, bSourceUnchanged, bFirstFrameKeepsTarget);
		return Result;
	}
}

TSharedRef<FJsonObject> ValidateMMDRenderTargets()
{
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), false);
	Report->SetNumberField(TEXT("test_count"), 0);
	Report->SetNumberField(TEXT("passed"), 0);
	Report->SetNumberField(TEXT("failed"), 1);
	Report->SetArrayField(TEXT("tests"), TArray<TSharedPtr<FJsonValue>>());
	if (!IsInGameThread() || GUsingNullRHI)
	{
		Report->SetStringField(TEXT("error"), TEXT("Render target validation requires the game thread and a real RHI"));
		return Report;
	}
	UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
	UMMDShadowMapSubsystem* Shadows = UMMDShadowMapSubsystem::Get();
	if (!Lights || !Shadows)
	{
		Report->SetStringField(TEXT("error"), TEXT("Rendering subsystems are unavailable"));
		return Report;
	}
	TArray<TSharedPtr<FJsonValue>> Results;
	const TSharedRef<FJsonObject> LightResult = ValidateTarget(TEXT("LightDataRT.RuntimeSetup"),
		TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT"),
		FIntPoint(UMMDAnimeLightDataSubsystem::MaxLights * 4, 2), PF_A32B32G32R32F,
		Lights->GetLightDataRenderTarget(), [Lights](UTextureRenderTarget2D* RT) { Lights->SetLightDataRenderTarget(RT); },
		[Lights](UTextureRenderTarget2D* RT)
		{
			Lights->CollectLightsForFrame(nullptr, nullptr);
			FlushRenderingCommands();
			return Lights->GetLightDataRenderTarget() == RT;
		});
	const int32 AtlasSize = Shadows->GetShadowMapResolution() * 2;
	const TSharedRef<FJsonObject> ShadowResult = ValidateTarget(TEXT("MMDShadowMapRT.RuntimeSetup"),
		TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT"),
		FIntPoint(AtlasSize, AtlasSize), PF_R32_FLOAT,
		Shadows->GetShadowMapRenderTarget(), [Shadows](UTextureRenderTarget2D* RT) { Shadows->SetShadowMapRenderTarget(RT); });
	Results.Add(MakeShared<FJsonValueObject>(LightResult));
	Results.Add(MakeShared<FJsonValueObject>(ShadowResult));
	Results.Append(ValidateLightMetadata(Lights));
	int32 PassedCount = 0;
	for (const TSharedPtr<FJsonValue>& Value : Results)
	{
		const TSharedPtr<FJsonObject> Result = Value->AsObject();
		PassedCount += Result.IsValid() && Result->GetBoolField(TEXT("passed")) ? 1 : 0;
	}
	Report->SetNumberField(TEXT("test_count"), Results.Num());
	Report->SetNumberField(TEXT("passed"), PassedCount);
	Report->SetNumberField(TEXT("failed"), Results.Num() - PassedCount);
	Report->SetBoolField(TEXT("success"), PassedCount == Results.Num());
	Report->SetArrayField(TEXT("tests"), Results);
	return Report;
}

TSharedRef<FJsonObject> CaptureMMDPreviewScreenshots()
{
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Results;
	Report->SetBoolField(TEXT("success"), false);
	Report->SetNumberField(TEXT("passed"), 0);
	Report->SetNumberField(TEXT("failed"), 1);
	if (!IsInGameThread() || GUsingNullRHI)
	{
		Report->SetStringField(TEXT("error"), TEXT("Preview screenshots require the game thread and a real RHI"));
		return Report;
	}
	UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
	if (!Lights || Lights->HasPreviewLightOverride())
	{
		Report->SetStringField(TEXT("error"), TEXT("LightData subsystem is unavailable or already has a preview override"));
		return Report;
	}
	TStrongObjectPtr<UTextureRenderTarget2D> OriginalTarget(Lights->GetLightDataRenderTarget());
	TStrongObjectPtr<UTextureRenderTarget2D> LightDataTarget(LoadObject<UTextureRenderTarget2D>(
		nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT")));
	if (!LightDataTarget.IsValid())
	{
		Report->SetStringField(TEXT("error"), TEXT("LightDataRT asset could not be loaded"));
		return Report;
	}
	const int32 OriginalSizeX = LightDataTarget->SizeX;
	const int32 OriginalSizeY = LightDataTarget->SizeY;
	const ETextureRenderTargetFormat OriginalRenderTargetFormat = LightDataTarget->RenderTargetFormat;
	const bool bOriginalUAV = LightDataTarget->bCanCreateUAV;
	const bool bOriginalMips = LightDataTarget->bAutoGenerateMips;
	const bool bOriginalPackageDirty = LightDataTarget->GetOutermost()->IsDirty();
	ON_SCOPE_EXIT
	{
		Lights->ClearPreviewLightOverride();
		Lights->SetLightDataRenderTarget(OriginalTarget.Get());
		LightDataTarget->RenderTargetFormat = OriginalRenderTargetFormat;
		LightDataTarget->bCanCreateUAV = bOriginalUAV;
		LightDataTarget->bAutoGenerateMips = bOriginalMips;
		LightDataTarget->InitAutoFormat(OriginalSizeX, OriginalSizeY);
		LightDataTarget->UpdateResourceImmediate(true);
		if (!bOriginalPackageDirty)
		{
			LightDataTarget->GetOutermost()->SetDirtyFlag(false);
		}
		FlushRenderingCommands();
	};
	// 材质图直接引用该资产，因此临时配置原 RT，确保截图 shader 读取到本次写入的数据。
	Lights->SetLightDataRenderTarget(LightDataTarget.Get());
	TArray<FVector4f> PreviewLights;
	PreviewLights.SetNumZeroed(UMMDAnimeLightDataSubsystem::MaxLights * 4);
	PreviewLights[0] = FVector4f(0, 0, 0, 3);
	PreviewLights[1] = FVector4f(1.0f, 0.94f, 0.86f, 4.0f);
	PreviewLights[2] = FVector4f(-0.55f, -0.45f, -0.70f, 0);
	Lights->SetPreviewLightOverride(PreviewLights);
	Lights->CollectLightsForFrame(nullptr, nullptr);
	ENQUEUE_RENDER_COMMAND(MMDCapturePreviewLighting)(
		[Lights](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("MMDCapturePreviewLighting"));
			Lights->WriteLightData_RenderThread(GraphBuilder);
			GraphBuilder.Execute();
		});
	FlushRenderingCommands();

	const FString ImportRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/ImportedAssets"));
	TArray<FString> Manifests;
	IFileManager::Get().FindFilesRecursive(Manifests, *ImportRoot, TEXT("manifest.json"), true, false);
	struct FPreviewCandidate
	{
		FString Directory;
		FString Name;
		FDateTime Timestamp;
		bool bCharacter = false;
		bool bHasOutline = false;
	};
	FPreviewCandidate LatestCharacter;
	FPreviewCandidate LatestSword;
	for (const FString& ManifestPath : Manifests)
	{
		FString Json;
		TSharedPtr<FJsonObject> Manifest;
		if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
		{
			continue;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
		{
			continue;
		}
		double Vertices = 0.0;
		if (!Manifest->TryGetNumberField(TEXT("vertices"), Vertices))
		{
			continue;
		}
		FPreviewCandidate Candidate;
		Candidate.Directory = FPaths::GetPath(ManifestPath) + TEXT("/");
		Candidate.Name = FPaths::GetCleanFilename(FPaths::GetPath(ManifestPath));
		Candidate.Timestamp = IFileManager::Get().GetTimeStamp(*ManifestPath);
		Candidate.bCharacter = Vertices > 5000.0;
		double OutlineMaterials = 0.0;
		Candidate.bHasOutline = Manifest->TryGetNumberField(TEXT("outline_materials"), OutlineMaterials)
			&& OutlineMaterials > 0.0;
		FPreviewCandidate& Current = Candidate.bCharacter ? LatestCharacter : LatestSword;
		if (Current.Directory.IsEmpty() || Candidate.Timestamp > Current.Timestamp)
		{
			Current = MoveTemp(Candidate);
		}
	}

	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/screenshots"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	auto CaptureCandidate = [&Results, &OutputDirectory](const FPreviewCandidate& Candidate, const TCHAR* Label)
	{
		if (Candidate.Directory.IsEmpty())
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Label);
			Result->SetBoolField(TEXT("passed"), false);
			Result->SetStringField(TEXT("error"), TEXT("No matching import manifest was found"));
			Results.Add(MakeShared<FJsonValueObject>(Result));
			return;
		}

		const FString VirtualRoot = FString::Printf(TEXT("/Game/MMDModels/%s/"), *Candidate.Name);
		FPackageName::RegisterMountPoint(VirtualRoot, Candidate.Directory);
		const FString ObjectPath = FString::Printf(TEXT("%sModel/%s.%s"), *VirtualRoot, *Candidate.Name, *Candidate.Name);
		TStrongObjectPtr<USkeletalMesh> Mesh(LoadObject<USkeletalMesh>(nullptr, *ObjectPath));
		TStrongObjectPtr<UMMDToolPreviewRenderer> Renderer(NewObject<UMMDToolPreviewRenderer>(GetTransientPackage()));
		bool bSetup = Mesh.IsValid() && Renderer.IsValid();
		if (bSetup)
		{
			Renderer->Initialize(1280, 720);
			bSetup = Renderer->SetPreviewSkeletalMesh(Mesh.Get());
			// 首次提交触发沙箱 MI 的 shader permutation 与骨骼 VF 编译。
			Renderer->Capture();
			FlushRenderingCommands();
			FAssetCompilingManager::Get().FinishAllCompilation();
			if (GShaderCompilingManager)
			{
				GShaderCompilingManager->FinishAllCompilation();
			}
			for (const FSkeletalMaterial& Slot : Mesh->GetMaterials())
			{
				if (UMaterialInterface* Material = Slot.MaterialInterface)
				{
					Material->SetForceMipLevelsToBeResident(false, true, 30.0f, 0, true);
				}
			}
			IStreamingManager::Get().StreamAllResources(30.0f);
			FlushRenderingCommands();
			// 重建 primitive，避免继续使用编译期间的默认材质代理。
			bSetup = Renderer->SetPreviewSkeletalMesh(Mesh.Get());
		}

		auto CaptureVariant = [&Results, &OutputDirectory, &ObjectPath, &Renderer, bSetup](
			const FString& Variant, bool bApplyOutline, bool bOutline)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			const FString OutputPath = FPaths::Combine(OutputDirectory, Variant + TEXT(".png"));
			FString Error;
			const bool bOutlineApplied = bSetup && (!bApplyOutline || Renderer->SetOutlineEnabled(bOutline));
			const bool bSaved = bOutlineApplied && SavePreviewPng(Renderer.Get(), OutputPath, Error);
			Result->SetStringField(TEXT("name"), Variant);
			Result->SetStringField(TEXT("mesh"), ObjectPath);
			Result->SetStringField(TEXT("output"), OutputPath);
			Result->SetBoolField(TEXT("passed"), bSaved);
			if (!bSaved)
			{
				Result->SetStringField(TEXT("error"), bSetup
					? (bOutlineApplied ? Error : TEXT("Outline state could not be applied"))
					: TEXT("Preview mesh or renderer setup failed"));
			}
			Results.Add(MakeShared<FJsonValueObject>(Result));
		};
		if (!Candidate.bCharacter && !Candidate.bHasOutline)
		{
			// Sword 样本的 PMX 合法关闭了边线，只输出基础画面，不伪称描边对比。
			CaptureVariant(FString(Label), false, false);
		}
		else
		{
			CaptureVariant(FString::Printf(TEXT("%s_outline_off"), Label), true, false);
			CaptureVariant(FString::Printf(TEXT("%s_outline_on"), Label), true, true);
			if (Candidate.bCharacter && Renderer->FocusUpperBody())
			{
				CaptureVariant(FString::Printf(TEXT("%s_detail_outline_on"), Label), true, true);
			}
		}
		if (Renderer.IsValid())
		{
			Renderer->Shutdown();
		}
		FPackageName::UnRegisterMountPoint(VirtualRoot, Candidate.Directory);
	};
	CaptureCandidate(LatestCharacter, TEXT("character"));
	CaptureCandidate(LatestSword, TEXT("sword"));

	int32 Passed = 0;
	for (const TSharedPtr<FJsonValue>& Value : Results)
	{
		const TSharedPtr<FJsonObject> Result = Value->AsObject();
		Passed += Result.IsValid() && Result->GetBoolField(TEXT("passed")) ? 1 : 0;
	}
	Report->SetArrayField(TEXT("tests"), Results);
	Report->SetNumberField(TEXT("passed"), Passed);
	Report->SetNumberField(TEXT("failed"), Results.Num() - Passed);
	Report->SetBoolField(TEXT("success"), !Results.IsEmpty() && Passed == Results.Num());
	return Report;
}
