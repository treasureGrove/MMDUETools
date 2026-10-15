#include "Rendering/MMDEndfieldNativeBackendValidationActor.h"

#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/ToonProfile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProperties.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "RHI.h"
#include "RHIStrings.h"
#include "UnrealClient.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogMMDEndfieldBackendValidation, Log, All);

namespace
{
	FString MMDEndfieldRuntimeValidationQuote(const FString& Value)
	{
		FString Result(TEXT("\""));
		for (TCHAR Character : Value)
		{
			switch (Character)
			{
			case TEXT('"'): Result += TEXT("\\\""); break;
			case TEXT('\\'): Result += TEXT("\\\\"); break;
			case TEXT('\n'): Result += TEXT("\\n"); break;
			case TEXT('\r'): Result += TEXT("\\r"); break;
			case TEXT('\t'): Result += TEXT("\\t"); break;
			default:
				if (Character < 0x20) Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
				else Result.AppendChar(Character);
				break;
			}
		}
		return Result + TEXT("\"");
	}

	bool MMDEndfieldRuntimeValidationSafeOutput(const FString& Directory)
	{
		FString EngineRoot = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
		FPaths::NormalizeDirectoryName(EngineRoot);
		if (Directory.IsEmpty() || FPaths::IsSamePath(Directory, EngineRoot) || FPaths::IsUnderDirectory(Directory, EngineRoot)) return false;
		FString Current = Directory;
		while (!Current.IsEmpty())
		{
			if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

	bool MMDEndfieldRuntimeValidationProfileRole(const UToonProfile* Profile, EMMDMaterialRole& OutRole)
	{
		if (!IsValid(Profile)) return false;
		const float FirstAlpha = Profile->Settings.DiffuseRamp.GetLinearColorValue(0.0f).A;
		const float LastAlpha = Profile->Settings.DiffuseRamp.GetLinearColorValue(1.0f).A;
		if (FMath::IsFinite(FirstAlpha) && FMath::IsFinite(LastAlpha))
		{
			const float Decoded = -FirstAlpha - 32.0f;
			if (Decoded >= 0.0f && Decoded <= 5.0f)
			{
				const int32 Role = FMath::RoundToInt(Decoded);
				if (FMath::Abs(Decoded - Role) < 0.01f && FMath::Abs(LastAlpha + 64.0f + Role) < 0.01f)
				{
					OutRole = static_cast<EMMDMaterialRole>(Role);
					return true;
				}
			}
		}
		// 旧验证包可能没有双标记；只认可专用目录下六个精确 Profile 名称。
		if (!Profile->GetPathName().StartsWith(TEXT("/Game/MMDEndfieldNativeBackendValidation/Profiles/"))) return false;
		const TCHAR* RoleNames[] = { TEXT("Generic"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"), TEXT("Eye") };
		for (int32 Role = 0; Role < UE_ARRAY_COUNT(RoleNames); ++Role)
		{
			if (Profile->GetName() == FString(TEXT("TP_Validation_")) + RoleNames[Role])
			{
				OutRole = static_cast<EMMDMaterialRole>(Role);
				return true;
			}
		}
		return false;
	}
}

AMMDEndfieldNativeBackendValidationActor::AMMDEndfieldNativeBackendValidationActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	ValidationCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ValidationCamera"));
	SetRootComponent(ValidationCamera);
	ValidationCamera->SetFieldOfView(50.0f);
	ValidationCamera->bConstrainAspectRatio = false;
}

void AMMDEndfieldNativeBackendValidationActor::BeginPlay()
{
	Super::BeginPlay();
	if (!GetWorld() || !GetWorld()->IsGameWorld() || !FParse::Param(FCommandLine::Get(), TEXT("MMDEndfieldBackendValidation"))) return;
	bValidationEnabled = true;
	SetActorTickEnabled(true);
	FString PayloadOption;
	bPayloadOptionPresent = FParse::Value(FCommandLine::Get(), TEXT("MMDEndfieldValidationPayload="), PayloadOption);
	if (bPayloadOptionPresent)
	{
		if (PayloadOption == TEXT("0") || PayloadOption == TEXT("1")) PayloadRequested = PayloadOption == TEXT("1") ? 1 : 0;
		else FailureReason = TEXT("MMDEndfieldValidationPayload requires exactly 0 or 1.");
	}
	RebindValidationProfiles();
	const FString DefaultOutput = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MMDEndfieldNativeBackend/RuntimeValidation"));
	OutputDirectory = DefaultOutput;
	FParse::Value(FCommandLine::Get(), TEXT("MMDEndfieldValidationOut="), OutputDirectory);
	OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	FPaths::NormalizeDirectoryName(OutputDirectory);
	if (!FPaths::CollapseRelativeDirectories(OutputDirectory) || !MMDEndfieldRuntimeValidationSafeOutput(OutputDirectory))
	{
		FailureReason = TEXT("Unsafe validation output directory; engine paths and directory links are rejected.");
		OutputDirectory = FPaths::ConvertRelativePathToFull(DefaultOutput);
		FPaths::NormalizeDirectoryName(OutputDirectory);
	}
	ScreenshotPath = FPaths::Combine(OutputDirectory, TEXT("native_backend_runtime.png"));
	ReportPath = FPaths::Combine(OutputDirectory, TEXT("native_backend_runtime.json"));
	if (!MMDEndfieldRuntimeValidationSafeOutput(OutputDirectory) || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		FailureReason = TEXT("Cannot create a safe validation output directory.");
	}
	ValidationCamera->SetWorldLocation(CameraLocation);
	ValidationCamera->SetWorldRotation((CameraLookAt - CameraLocation).Rotation());
	ValidationCamera->SetFieldOfView(FMath::Clamp(CameraFieldOfView, 5.0f, 170.0f));
	bCameraApplied = ApplyValidationCamera();
	ScreenshotDelegateHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &AMMDEndfieldNativeBackendValidationActor::OnValidationScreenshot);
	if (IConsoleVariable* DelegateVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate")))
	{
		OriginalScreenshotDelegateValue = DelegateVariable->GetInt();
		if (OriginalScreenshotDelegateValue != 1)
		{
			DelegateVariable->Set(1, ECVF_SetByCode);
			bChangedScreenshotDelegate = true;
		}
	}
	UE_LOG(LogMMDEndfieldBackendValidation, Display, TEXT("Explicit game-world backend validation started; waiting for stable rendered frames."));
}

bool AMMDEndfieldNativeBackendValidationActor::ApplyValidationCamera()
{
	APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Player) return false;
	Player->SetViewTarget(CameraViewTarget ? CameraViewTarget.Get() : this);
	return true;
}

void AMMDEndfieldNativeBackendValidationActor::RebindValidationProfiles()
{
	const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
	const IConsoleVariable* GBufferFormat = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate.ProjectGBufferFormat"));
	const bool bPayloadDiagnostic = bPayloadOptionPresent && PayloadRequested >= 0 && FailureReason.IsEmpty()
		&& Backend.bRequested && Backend.bProfileMarkerAllowed && Backend.Mode == 2 && GBufferFormat && GBufferFormat->GetInt() == 1;
	if (bPayloadOptionPresent && !bPayloadDiagnostic)
	{
		PayloadValidationReason = TEXT("Payload diagnostics require explicit validation, backend Mode 2, native Profile routing and GBuffer format 1.");
		if (FailureReason.IsEmpty()) FailureReason = PayloadValidationReason;
	}
	if (!Backend.bRequested || !Backend.bProfileMarkerAllowed)
	{
		ProfileRebindReason = TEXT("Backend was not requested or Profile marker routing is unavailable; fixture materials retain their existing profiles.");
		return;
	}
	for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
	{
		TArray<UMeshComponent*> Components;
		Actor->GetComponents<UMeshComponent>(Components);
		for (UMeshComponent* Component : Components)
		{
			if (!IsValid(Component) || Component->GetWorld() != GetWorld()) continue;
			for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* SourceMaterial = Component->GetMaterial(Slot);
				if (!IsValid(SourceMaterial) || !SourceMaterial->GetPathName().StartsWith(TEXT("/Game/MMDEndfieldNativeBackendValidation/Materials/"))) continue;
				++EligibleProfileMaterialCount;
				UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(SourceMaterial);
				if (!SourceInstance) continue;
				UToonProfile* SourceProfile = SourceInstance->GetToonProfileOverride_Internal();
				if (!SourceProfile) SourceProfile = SourceInstance->ToonProfileOverride.Get();
				EMMDMaterialRole MaterialRole = EMMDMaterialRole::Generic;
				if (!MMDEndfieldRuntimeValidationProfileRole(SourceProfile, MaterialRole)) continue;
				const bool bSkinPayloadDiagnostic = bPayloadDiagnostic && MaterialRole == EMMDMaterialRole::Skin;
				if (bSkinPayloadDiagnostic) ++EligiblePayloadMaterialCount;
				UToonProfile* Profile = MMDEndfieldNativeProfile::GetOrCreate(SourceProfile, MaterialRole);
				if (!IsValid(Profile) || Profile == SourceProfile || !Profile->HasAnyFlags(RF_Transient)) continue;
				if (bSkinPayloadDiagnostic)
				{
					// 只在显式验证中改独立瞬态副本；不污染 GetOrCreate 缓存、Cook 资产或生产 Profile。
					if (Profile->Settings.DiffuseRamp.ExternalCurve) continue;
					Profile = DuplicateObject<UToonProfile>(Profile, GetTransientPackage());
					if (!Profile) continue;
					Profile->SetFlags(RF_Transient);
					Profile->ClearFlags(RF_Public | RF_Standalone);
					Profile->Guid = FGuid::NewGuid();
					FRichCurve& Alpha = Profile->Settings.DiffuseRamp.ColorCurves[3];
					for (float Position : { 0.71875f, 0.78125f })
						Alpha.SetKeyInterpMode(Alpha.UpdateOrAddKey(Position, 17.0f), RCIM_Linear);
				}
				// 新 MID 以原 MIC 为父，原参数保持继承；仅瞬态 Profile 覆盖送入渲染代理。
				UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(SourceMaterial, Component);
				if (!Material) continue;
				Material->ToonProfileOverride = Profile;
				Material->bOverrideToonProfile = true;
				Material->ClearParameterValues();
				if (bSkinPayloadDiagnostic)
				{
					Component->UpdateBounds();
					const FVector Center = Component->Bounds.Origin;
					const float Radius = FMath::Max(static_cast<float>(Component->Bounds.SphereRadius), 1.0f);
					const FLinearColor Head(Center.X, Center.Y, Center.Z, Radius);
					const FLinearColor Forward(0.0f, 1.0f, 0.0f, 0.0f);
					const FLinearColor Right(1.0f, 0.0f, 0.0f, 0.0f);
					Material->SetVectorParameterValue(UMMDHeadBoneSubsystem::ParamHeadPositionRadius, Head);
					Material->SetVectorParameterValue(UMMDHeadBoneSubsystem::ParamHeadForward, Forward);
					Material->SetVectorParameterValue(UMMDHeadBoneSubsystem::ParamHeadRight, Right);
					Material->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), static_cast<float>(PayloadRequested));
					Material->SetScalarParameterValue(TEXT("ToonFaceDirectionEnabled"), 0.0f);
					Material->SetScalarParameterValue(TEXT("ToonFaceSdfEnabled"), 0.0f);
					Material->SetScalarParameterValue(TEXT("ToonSphereBlend"), 0.75f);
					Material->SetScalarParameterValue(TEXT("ToonAnisotropy"), 0.0f);
					float Enabled = -1.0f;
					FLinearColor BoundHead = FLinearColor::Transparent;
					FLinearColor BoundForward = FLinearColor::Transparent, BoundRight = FLinearColor::Transparent;
					const bool bCpuBound = Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Enabled)
						&& FMath::IsNearlyEqual(Enabled, static_cast<float>(PayloadRequested))
						&& Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadPositionRadius), BoundHead)
						&& BoundHead.Equals(Head, 0.0001f)
						&& Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), BoundForward)
						&& BoundForward.Equals(Forward, 0.0001f)
						&& Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), BoundRight)
						&& BoundRight.Equals(Right, 0.0001f)
						&& FMath::IsNearlyEqual(Profile->Settings.DiffuseRamp.GetLinearColorValue(0.75f).A, 17.0f);
					if (bCpuBound) ++BoundPayloadMaterialCount;
					FString PayloadEntry = TEXT("{\"component\": ") + MMDEndfieldRuntimeValidationQuote(Component->GetPathName());
					PayloadEntry += FString::Printf(TEXT(", \"slot\": %d, \"cpu_parameters_bound\": %s, \"payload_enabled\": %d, \"head_position_radius\": [%.9f, %.9f, %.9f, %.9f], \"head_forward\": [0, 1, 0], \"head_right\": [1, 0, 0], \"sphere_blend\": 0.75, \"anisotropy\": 0, \"direction_texture_enabled\": false, \"diagnostic_alpha_at_075\": %.9f, \"gpu_payload_verified\": false}"),
						Slot, bCpuBound ? TEXT("true") : TEXT("false"), PayloadRequested, Head.R, Head.G, Head.B, Head.A,
						Profile->Settings.DiffuseRamp.GetLinearColorValue(0.75f).A);
					PayloadParameterReports.Add(MoveTemp(PayloadEntry));
				}
				Component->SetMaterial(Slot, Material);
				if (Component->GetMaterial(Slot) != Material || Material->GetToonProfileOverride_Internal() != Profile) continue;
				++ReboundProfileMaterialCount;
				const float Mode = Profile->Settings.DiffuseRamp.GetLinearColorValue(0.25f).A;
				const float DiffuseScale = Profile->Settings.DiffuseRamp.GetLinearColorValue(0.40625f).A;
				const float SpecularScale = Profile->Settings.DiffuseRamp.GetLinearColorValue(0.5625f).A;
				const bool bFinite = FMath::IsFinite(Mode) && FMath::IsFinite(DiffuseScale) && FMath::IsFinite(SpecularScale);
				const bool bMatches = bFinite && FMath::IsNearlyEqual(Mode, static_cast<float>(Backend.Mode), 0.0001f)
					&& FMath::IsNearlyEqual(DiffuseScale, Backend.IndirectDiffuseScale, 0.0001f)
					&& FMath::IsNearlyEqual(SpecularScale, Backend.IndirectSpecularScale, 0.0001f);
				if (bMatches) ++MatchedProfileMetadataCount;
				FString Entry = FString::Printf(TEXT("{\"role\": %d, \"slot\": %d, \"metadata_matches_requested\": %s, \"profile_transient\": true, \"profile_mode\": "), static_cast<int32>(MaterialRole), Slot, bMatches ? TEXT("true") : TEXT("false"));
				Entry += FMath::IsFinite(Mode) ? FString::Printf(TEXT("%.9f"), Mode) : TEXT("null");
				Entry += TEXT(", \"profile_indirect_diffuse_scale\": ");
				Entry += FMath::IsFinite(DiffuseScale) ? FString::Printf(TEXT("%.9f"), DiffuseScale) : TEXT("null");
				Entry += TEXT(", \"profile_indirect_specular_scale\": ");
				Entry += FMath::IsFinite(SpecularScale) ? FString::Printf(TEXT("%.9f"), SpecularScale) : TEXT("null");
				Entry += TEXT(", \"source_material\": ") + MMDEndfieldRuntimeValidationQuote(SourceMaterial->GetPathName());
				Entry += TEXT(", \"runtime_profile\": ") + MMDEndfieldRuntimeValidationQuote(Profile->GetPathName()) + TEXT("}");
				ReboundProfileReports.Add(MoveTemp(Entry));
			}
		}
	}
	ProfileRebindReason = EligibleProfileMaterialCount == 0 ? TEXT("No dedicated fixture material slots were found.")
		: ReboundProfileMaterialCount == EligibleProfileMaterialCount && MatchedProfileMetadataCount == ReboundProfileMaterialCount
		? TEXT("Dedicated fixture slots use transient Profiles whose CPU alpha metadata matches the requested controls.")
		: TEXT("Some dedicated fixture slots could not be rebound or their CPU metadata did not match the requested controls.");
	if (bPayloadDiagnostic)
	{
		const bool bComplete = EligiblePayloadMaterialCount > 0 && BoundPayloadMaterialCount == EligiblePayloadMaterialCount
			&& ReboundProfileMaterialCount == EligibleProfileMaterialCount && MatchedProfileMetadataCount == ReboundProfileMaterialCount;
		PayloadValidationReason = bComplete
			? TEXT("Dedicated Skin primitives have explicit CPU head frames and transient diagnostic Profiles; inspect paired green/red screenshots to verify the cooked packed write/read.")
			: TEXT("Payload diagnostic requested but no eligible Skin material was found or its CPU binding failed.");
		if (!bComplete && FailureReason.IsEmpty()) FailureReason = PayloadValidationReason;
	}
}

void AMMDEndfieldNativeBackendValidationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bValidationEnabled || bValidationFinished) return;
	if (bPayloadOptionPresent && !FailureReason.IsEmpty()) { FinishValidation(false); return; }
	++StableFrameCount;
	if (!bCameraApplied) bCameraApplied = ApplyValidationCamera();
	if (StableFrameCount >= 120 && !bScreenshotRequested && bCameraApplied && FailureReason.IsEmpty()
		&& GetWorld()->GetGameViewport() && !FScreenshotRequest::IsScreenshotRequested())
	{
		bScreenshotRequested = true;
		ScreenshotRequestFrame = StableFrameCount;
		// 只截游戏视口，不采集编辑器或其他窗口。
		FScreenshotRequest::RequestScreenshot(ScreenshotPath, false, false, false, FIntRect(), true);
	}
	if (StableFrameCount >= 160 && bScreenshotSaved && StableFrameCount >= ScreenshotCapturedFrame + 10)
	{
		FinishValidation(true);
	}
	else if (StableFrameCount >= 600)
	{
		if (FailureReason.IsEmpty()) FailureReason = bCameraApplied
			? TEXT("Screenshot pixel callback / PNG save did not complete within 600 frames. A rendered game viewport is required; use -RenderOffscreen for unattended validation.")
			: TEXT("No player controller became available to apply the validation camera within 600 frames.");
		FinishValidation(false);
	}
}

void AMMDEndfieldNativeBackendValidationActor::OnValidationScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors)
{
	if (!bValidationEnabled || bValidationFinished || !bScreenshotRequested || bScreenshotSaved
		|| !FPaths::IsSamePath(FScreenshotRequest::GetFilename(), ScreenshotPath)) return;
	if (Width <= 0 || Height <= 0 || static_cast<int64>(Width) * Height != Colors.Num())
	{
		FailureReason = TEXT("Screenshot pixel callback returned invalid dimensions or pixel count.");
		return;
	}
	ScreenshotCapturedFrame = StableFrameCount;
	ScreenshotWidth = Width;
	ScreenshotHeight = Height;
	TArray64<uint8> PngBytes;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), PngBytes);
	if (PngBytes.IsEmpty() || !MMDEndfieldRuntimeValidationSafeOutput(ScreenshotPath)
		|| !FFileHelper::SaveArrayToFile(PngBytes, *ScreenshotPath) || IFileManager::Get().FileSize(*ScreenshotPath) != PngBytes.Num())
	{
		FailureReason = TEXT("Screenshot pixel callback ran, but the PNG could not be saved and verified.");
		return;
	}
	bScreenshotSaved = true;
	UE_LOG(LogMMDEndfieldBackendValidation, Display, TEXT("Game-viewport screenshot pixel callback completed: %d x %d."), Width, Height);
}

bool AMMDEndfieldNativeBackendValidationActor::WriteValidationReport(bool bSuccess)
{
	const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
	const auto Boolean = [](bool Value) { return Value ? TEXT("true") : TEXT("false"); };
	FString Json = FString::Printf(TEXT("{\n  \"success\": %s,\n  \"CookedData\": %s,\n  \"IsEngineBackendRequested\": %s,\n  \"profile_marker_allowed\": %s,\n  \"source_mapping_active\": %s,\n  \"mode\": %d,\n  \"indirect_diffuse_scale\": %.9f,\n  \"indirect_specular_scale\": %.9f,\n  \"stable_frames\": %d,\n  \"screenshot_requested\": %s,\n  \"screenshot_request_frame\": %d,\n  \"screenshot_pixel_callback_received\": %s,\n  \"screenshot_saved\": %s,\n  \"screenshot_captured_frame\": %d,\n  \"screenshot_width\": %d,\n  \"screenshot_height\": %d,\n  \"camera_applied\": %s,\n  \"shader_platform\": %d"),
		Boolean(bSuccess), Boolean(FPlatformProperties::RequiresCookedData()), Boolean(Backend.bRequested), Boolean(Backend.bProfileMarkerAllowed), Boolean(Backend.bActive),
		Backend.Mode, Backend.IndirectDiffuseScale, Backend.IndirectSpecularScale, StableFrameCount,
		Boolean(bScreenshotRequested), ScreenshotRequestFrame, Boolean(ScreenshotCapturedFrame > 0), Boolean(bScreenshotSaved), ScreenshotCapturedFrame, ScreenshotWidth, ScreenshotHeight,
		Boolean(bCameraApplied), static_cast<int32>(GMaxRHIShaderPlatform));
	Json += TEXT(",\n  \"shader_format\": ") + MMDEndfieldRuntimeValidationQuote(LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
	Json += TEXT(",\n  \"backend_reason\": ") + MMDEndfieldRuntimeValidationQuote(Backend.Reason);
	Json += FString::Printf(TEXT(",\n  \"requested_mode\": %d,\n  \"requested_indirect_diffuse_scale\": %.9f,\n  \"requested_indirect_specular_scale\": %.9f"), Backend.Mode, Backend.IndirectDiffuseScale, Backend.IndirectSpecularScale);
	Json += FString::Printf(TEXT(",\n  \"profile_rebind_eligible_materials\": %d,\n  \"profile_rebound_materials\": %d,\n  \"profile_rebind_skipped_materials\": %d,\n  \"profile_metadata_matching_materials\": %d,\n  \"profile_metadata_controls_verified\": %s,\n  \"compiled_kernel_verified_by_this_report\": false"),
		EligibleProfileMaterialCount, ReboundProfileMaterialCount, EligibleProfileMaterialCount - ReboundProfileMaterialCount, MatchedProfileMetadataCount,
		Boolean(ReboundProfileMaterialCount > 0 && ReboundProfileMaterialCount == EligibleProfileMaterialCount && MatchedProfileMetadataCount == ReboundProfileMaterialCount));
	Json += TEXT(",\n  \"profile_rebind_reason\": ") + MMDEndfieldRuntimeValidationQuote(ProfileRebindReason);
	Json += TEXT(",\n  \"payload_requested\": ") + (bPayloadOptionPresent ? FString::FromInt(PayloadRequested) : FString(TEXT("null")));
	Json += FString::Printf(TEXT(",\n  \"payload_diagnostic_eligible_skin_materials\": %d,\n  \"payload_cpu_bound_materials\": %d,\n  \"payload_gpu_verified_by_this_report\": false"), EligiblePayloadMaterialCount, BoundPayloadMaterialCount);
	Json += TEXT(",\n  \"payload_validation_reason\": ") + MMDEndfieldRuntimeValidationQuote(PayloadValidationReason);
	Json += TEXT(",\n  \"payload_expected_screenshot_color\": ") + MMDEndfieldRuntimeValidationQuote(
		bPayloadOptionPresent && Backend.Mode == 2 && BoundPayloadMaterialCount > 0 ? PayloadRequested == 1 ? TEXT("green") : TEXT("red") : TEXT("not_active"));
	Json += TEXT(",\n  \"payload_cpu_parameters\": [");
	for (int32 Index = 0; Index < PayloadParameterReports.Num(); ++Index)
	{
		Json += Index == 0 ? TEXT("\n    ") : TEXT(",\n    ");
		Json += PayloadParameterReports[Index];
	}
	Json += TEXT("\n  ]");
	Json += TEXT(",\n  \"runtime_profile_metadata\": [");
	for (int32 Index = 0; Index < ReboundProfileReports.Num(); ++Index)
	{
		Json += Index == 0 ? TEXT("\n    ") : TEXT(",\n    ");
		Json += ReboundProfileReports[Index];
	}
	Json += TEXT("\n  ]");
	Json += TEXT(",\n  \"failure_reason\": ") + MMDEndfieldRuntimeValidationQuote(FailureReason);
	Json += TEXT(",\n  \"screenshot_file\": ") + MMDEndfieldRuntimeValidationQuote(ScreenshotPath);
	Json += TEXT(",\n  \"camera_location\": ") + MMDEndfieldRuntimeValidationQuote(ValidationCamera->GetComponentLocation().ToString());
	Json += TEXT(",\n  \"camera_look_at\": ") + MMDEndfieldRuntimeValidationQuote(CameraLookAt.ToString());
	Json += FString::Printf(TEXT(",\n  \"external_camera_view_target\": %s"), Boolean(CameraViewTarget != nullptr));
	Json += TEXT(",\n  \"cook_validation_note\": \"Mode and GI scales are controls encoded in transient runtime Profile alpha metadata, not per-mode cooked kernel constants. The runtime_profile_metadata records actual CPU curve reads at t=.25/.40625/.5625; legacy mode/indirect_*_scale fields remain requested options. Cooked source mapping is inactive by design. One Cook can use different Profile controls at runtime; whether its compiled kernel consumes them correctly still requires baseline/variant image comparison and Cook provenance.\"");
	Json += TEXT(",\n  \"console_variables\": {");
	const TCHAR* Variables[] =
	{
		TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Lumen.DiffuseIndirect.Allow"),
		TEXT("r.Lumen.Reflections.Allow"), TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.GenerateMeshDistanceFields"),
		TEXT("r.ShadowQuality"), TEXT("r.Shadow.Virtual.Enable"), TEXT("r.Shadow.Virtual.SMRT.RayCountDirectional"),
		TEXT("r.Shadow.Virtual.SMRT.RayCountLocal"), TEXT("r.RayTracing"), TEXT("r.ScreenshotDelegate"),
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Variables); ++Index)
	{
		Json += Index == 0 ? TEXT("\n    ") : TEXT(",\n    ");
		Json += MMDEndfieldRuntimeValidationQuote(Variables[Index]) + TEXT(": ");
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Variables[Index]);
		Json += Variable ? MMDEndfieldRuntimeValidationQuote(Variable->GetString()) : TEXT("null");
	}
	Json += TEXT("\n  }\n}\n");
	return MMDEndfieldRuntimeValidationSafeOutput(ReportPath)
		&& FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

void AMMDEndfieldNativeBackendValidationActor::FinishValidation(bool bSuccess)
{
	if (bValidationFinished) return;
	bValidationFinished = true;
	const bool bReportSaved = WriteValidationReport(bSuccess);
	ReleaseScreenshotDelegate();
	SetActorTickEnabled(false);
	UE_LOG(LogMMDEndfieldBackendValidation, Display, TEXT("Backend runtime validation finished (capture=%d report=%d frames=%d)."), bSuccess, bReportSaved, StableFrameCount);
	FPlatformMisc::RequestExitWithStatus(false, bSuccess && bReportSaved ? 0 : 1, TEXT("MMDEndfieldNativeBackendValidationActor"));
}

void AMMDEndfieldNativeBackendValidationActor::ReleaseScreenshotDelegate()
{
	if (ScreenshotDelegateHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		ScreenshotDelegateHandle.Reset();
	}
	if (bChangedScreenshotDelegate)
	{
		if (IConsoleVariable* DelegateVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"))) DelegateVariable->Set(OriginalScreenshotDelegateValue, ECVF_SetByCode);
		bChangedScreenshotDelegate = false;
	}
}

void AMMDEndfieldNativeBackendValidationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseScreenshotDelegate();
	Super::EndPlay(EndPlayReason);
}
