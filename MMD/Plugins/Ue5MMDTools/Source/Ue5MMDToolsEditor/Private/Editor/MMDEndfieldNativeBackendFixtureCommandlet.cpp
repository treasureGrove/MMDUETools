// 用法：-run=MMDEndfieldNativeBackendFixture -AllowCommandletRendering -d3d12 [-Report=<Saved JSON>]
// 仅创建 13 个专用验证包；生成成功不代表 Cook 字节码或最终画面已经通过验证。
#include "MMDEndfieldNativeBackendFixtureCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/ToonProfile.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialShared.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDEndfieldNativeBackendValidationActor.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageRenderPreset.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr const TCHAR* FixtureTool = TEXT("MMDEndfieldNativeBackendFixture");
	constexpr int32 FixtureVersion = 1;
	constexpr const TCHAR* FixtureRoot = TEXT("/Game/MMDEndfieldNativeBackendValidation");
	constexpr const TCHAR* FixtureMap = TEXT("/Game/MMDEndfieldNativeBackendValidation/Maps/M_EndfieldNativeBackendValidation");
	constexpr const TCHAR* FixtureParent = TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Opaque");
	constexpr const TCHAR* FixturePreset = TEXT("/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2");
	const TCHAR* FixtureRoleNames[] = { TEXT("Generic"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"), TEXT("Eye") };
	const FLinearColor FixtureColors[] = {
		FLinearColor(0.08f, 0.65f, 0.72f), FLinearColor(0.90f, 0.38f, 0.18f),
		FLinearColor(0.32f, 0.07f, 0.55f), FLinearColor(0.035f, 0.12f, 0.72f),
		FLinearColor(0.78f, 0.56f, 0.045f), FLinearColor(0.045f, 0.78f, 0.20f) };

	FString FixtureObjectPath(const FString& PackageName)
	{
		return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
	}

	FString FixtureProfilePackage(int32 Role)
	{
		return FString::Printf(TEXT("%s/Profiles/TP_Validation_%s"), FixtureRoot, FixtureRoleNames[Role]);
	}

	FString FixtureMaterialPackage(int32 Role)
	{
		return FString::Printf(TEXT("%s/Materials/MI_Validation_%s"), FixtureRoot, FixtureRoleNames[Role]);
	}

	TArray<FString> FixturePackages()
	{
		TArray<FString> Packages;
		for (int32 Role = 0; Role < UE_ARRAY_COUNT(FixtureRoleNames); ++Role) Packages.Add(FixtureProfilePackage(Role));
		for (int32 Role = 0; Role < UE_ARRAY_COUNT(FixtureRoleNames); ++Role) Packages.Add(FixtureMaterialPackage(Role));
		Packages.Add(FixtureMap);
		return Packages;
	}

	FString FixturePackageFilename(const FString& PackageName)
	{
		return FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(PackageName,
			PackageName == FixtureMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()));
	}

	bool FixtureWriteJson(const FString& Filename, const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text))) return false;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		return FFileHelper::SaveStringToFile(Text, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	TArray<TSharedPtr<FJsonValue>> FixtureJsonStrings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Json;
		for (const FString& Value : Values) Json.Add(MakeShared<FJsonValueString>(Value));
		return Json;
	}

	bool FixtureCheckOwnership(const FString& Directory, const FString& ManifestPath,
		const TArray<FString>& Packages, FString& Error)
	{
		const FString ExpectedDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MMDEndfieldNativeBackendValidation")));
		if (!FPaths::IsSamePath(Directory, ExpectedDirectory))
		{
			Error = TEXT("验证目录解析超出固定 Content 目录");
			return false;
		}
		if (!IFileManager::Get().DirectoryExists(*Directory)) return true;
		FString ExistingText;
		TSharedPtr<FJsonObject> Existing;
		FString Tool, Root;
		double Version = 0.0;
		const TArray<TSharedPtr<FJsonValue>>* ExistingPackages = nullptr;
		if (!FFileHelper::LoadFileToString(ExistingText, *ManifestPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ExistingText), Existing) || !Existing.IsValid()
			|| !Existing->TryGetStringField(TEXT("tool"), Tool) || Tool != FixtureTool
			|| !Existing->TryGetNumberField(TEXT("fixture_version"), Version) || Version != FixtureVersion
			|| !Existing->TryGetStringField(TEXT("package_root"), Root) || Root != FixtureRoot
			|| !Existing->TryGetArrayField(TEXT("owned_packages"), ExistingPackages) || ExistingPackages->Num() != Packages.Num())
		{
			Error = TEXT("专用目录已存在但没有匹配的工具归属 manifest，拒绝覆盖：") + Directory;
			return false;
		}
		for (int32 Index = 0; Index < Packages.Num(); ++Index)
		{
			FString Package;
			if (!(*ExistingPackages)[Index]->TryGetString(Package) || Package != Packages[Index])
			{
				Error = TEXT("manifest 的包白名单不匹配，拒绝覆盖");
				return false;
			}
		}
		TSet<FString> AllowedFiles;
		FString NormalizedManifest = ManifestPath;
		FPaths::NormalizeFilename(NormalizedManifest);
		AllowedFiles.Add(NormalizedManifest.ToLower());
		for (const FString& Package : Packages)
		{
			FString Filename = FixturePackageFilename(Package);
			FPaths::NormalizeFilename(Filename);
			AllowedFiles.Add(Filename.ToLower());
			// 仅允许对应已知包的常规 sidecar，不能收养其他资产。
			AllowedFiles.Add(FPaths::ChangeExtension(Filename, TEXT("uexp")).ToLower());
			AllowedFiles.Add(FPaths::ChangeExtension(Filename, TEXT("ubulk")).ToLower());
		}
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Directory, TEXT("*"), true, false);
		for (FString Filename : Files)
		{
			Filename = FPaths::ConvertRelativePathToFull(Filename);
			FPaths::NormalizeFilename(Filename);
			if (!AllowedFiles.Contains(Filename.ToLower()))
			{
				Error = TEXT("验证目录包含未归属本工具的文件，拒绝覆盖：") + Filename;
				return false;
			}
		}
		return true;
	}

	bool FixtureSaveAsset(UObject* Asset, const TArray<FString>& Packages, FString& Error)
	{
		const FString PackageName = Asset->GetOutermost()->GetName();
		if (!Packages.Contains(PackageName) || Asset->GetName() != FPackageName::GetLongPackageAssetName(PackageName))
		{
			Error = TEXT("拒绝保存白名单之外或命名不匹配的包：") + PackageName;
			return false;
		}
		const FString Filename = FixturePackageFilename(PackageName);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		Asset->SetFlags(RF_Public | RF_Standalone);
		Asset->ClearFlags(RF_Transient);
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
		{
			Error = TEXT("验证包保存失败：") + PackageName;
			return false;
		}
		FAssetRegistryModule::AssetCreated(Asset);
		return true;
	}

	bool FixtureRgbUnchanged(const UToonProfile* Source, const UToonProfile* Copy)
	{
		const FRuntimeCurveLinearColor* SourceRamps[] = { &Source->Settings.DiffuseRamp, &Source->Settings.SpecularRamp };
		const FRuntimeCurveLinearColor* CopyRamps[] = { &Copy->Settings.DiffuseRamp, &Copy->Settings.SpecularRamp };
		for (int32 Ramp = 0; Ramp < UE_ARRAY_COUNT(SourceRamps); ++Ramp)
		{
			if (SourceRamps[Ramp]->ExternalCurve != CopyRamps[Ramp]->ExternalCurve) return false;
			for (int32 Channel = 0; Channel < 3; ++Channel)
			{
				const FRichCurve& A = SourceRamps[Ramp]->ColorCurves[Channel];
				const FRichCurve& B = CopyRamps[Ramp]->ColorCurves[Channel];
				if (A.GetConstRefOfKeys() != B.GetConstRefOfKeys() || A.DefaultValue != B.DefaultValue
					|| A.PreInfinityExtrap != B.PreInfinityExtrap || A.PostInfinityExtrap != B.PostInfinityExtrap) return false;
			}
		}
		return true;
	}

	UToonProfile* FixtureCopyProfile(UToonProfile* Source, int32 Role, FString& Error)
	{
		if (Source->Settings.DiffuseRamp.ExternalCurve)
		{
			Error = TEXT("固定六角色源 Profile 应使用内嵌曲线；外部曲线需要独立保存策略：") + Source->GetPathName();
			return nullptr;
		}
		UToonProfile* Copy = DuplicateObject<UToonProfile>(Source, GetTransientPackage());
		if (!Copy) { Error = TEXT("无法复制 Profile：") + Source->GetPathName(); return nullptr; }
		Copy->SetFlags(RF_Transient);
		Copy->ClearFlags(RF_Public | RF_Standalone);
		Copy->Guid = FGuid::NewGuid();
		if (!MMDEndfieldNativeProfile::MarkTransient(Copy, static_cast<EMMDMaterialRole>(Role)) || !FixtureRgbUnchanged(Source, Copy))
		{
			Error = TEXT("Profile 角色标记失败或改变了 RGB：") + Source->GetPathName();
			return nullptr;
		}
		const FString PackageName = FixtureProfilePackage(Role);
		UPackage* Package = CreatePackage(*PackageName);
		const FName Name(*FPackageName::GetLongPackageAssetName(PackageName));
		if (FindObject<UObject>(Package, *Name.ToString())
			|| !Copy->Rename(*Name.ToString(), Package, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
		{
			Error = TEXT("验证 Profile 目标在内存中已存在或 Rename 失败：") + PackageName;
			return nullptr;
		}
		Copy->ClearFlags(RF_Transient);
		Copy->SetFlags(RF_Public | RF_Standalone);
		Copy->PostEditChange();
		return Copy;
	}

	UMaterialInstanceConstant* FixtureCreateMaterial(UMaterial* Parent, UTexture2D* White, UToonProfile* Profile,
		const FMMDStageToonRoleSettings& Settings, int32 Role, FString& Error)
	{
		const FString PackageName = FixtureMaterialPackage(Role);
		UPackage* Package = CreatePackage(*PackageName);
		const FName Name(*FPackageName::GetLongPackageAssetName(PackageName));
		if (FindObject<UObject>(Package, *Name.ToString())) { Error = TEXT("验证 MIC 在内存中已存在：") + PackageName; return nullptr; }
		UMaterialInstanceConstant* Material = NewObject<UMaterialInstanceConstant>(Package, Name, RF_Public | RF_Standalone);
		Material->SetParentEditorOnly(Parent);
		// 清空必须先于 Profile override；引擎 Clear 会重置 Profile 引用。
		Material->ClearParameterValuesEditorOnly();
		Material->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("base_color_tex")), White);
		Material->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColor")), FixtureColors[Role]);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Opacity")), 1.0f);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonRoughness")), Settings.Roughness);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonSpecular")), Settings.Specular);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonMetallic")), Settings.Metallic);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonAnisotropy")), Settings.Anisotropy);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonSphereBlend")), 0.0f);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonFaceSdfEnabled")), 0.0f);
		Material->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ToonFaceShadowSuppress")), 0.0f);
		Material->ToonProfileOverride = Profile;
		Material->bOverrideToonProfile = true;
		// 公开编辑器更新入口会在引擎内部调用受保护的 InitResources。
		Material->PostEditChange();
		return Material;
	}

	TSharedRef<FJsonObject> FixtureCompileState(UMaterialInterface* Material, bool& bReady)
	{
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		const FMaterialShaderMap* Map = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
		TArray<TSharedPtr<FJsonValue>> Errors;
		if (Resource) for (const FString& Error : Resource->GetCompileErrors()) Errors.Add(MakeShared<FJsonValueString>(Error));
		const bool bFinished = Resource && Resource->IsCompilationFinished();
		const bool bSuccess = Map && Map->CompiledSuccessfully();
		const bool bFinalized = Map && Map->IsCompilationFinalized();
		const int32 ShaderCount = Map ? Map->GetShaderNum() : 0;
		const bool bRenderingValid = Map && Map->IsValidForRendering(false);
		bReady = bFinished && bSuccess && bFinalized && ShaderCount > 0 && bRenderingValid && Errors.IsEmpty();
		TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
		State->SetStringField(TEXT("asset"), Material->GetPathName());
		State->SetBoolField(TEXT("finished"), bFinished);
		State->SetBoolField(TEXT("success"), bSuccess);
		State->SetBoolField(TEXT("finalized"), bFinalized);
		State->SetNumberField(TEXT("shader_count"), ShaderCount);
		State->SetBoolField(TEXT("rendering_valid"), bRenderingValid);
		State->SetBoolField(TEXT("MaterialCompileReady"), bReady);
		State->SetArrayField(TEXT("errors"), Errors);
		return State;
	}

	AStaticMeshActor* FixturePrimitive(UWorld* World, UStaticMesh* Mesh, UMaterialInterface* Material,
		const TCHAR* Label, const FVector& Location, const FVector& Scale)
	{
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>();
		if (!Actor) return nullptr;
		Actor->SetActorLabel(Label);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		Actor->GetStaticMeshComponent()->SetMaterial(0, Material);
		Actor->GetStaticMeshComponent()->SetCastShadow(true);
		Actor->SetActorLocation(Location);
		Actor->SetActorScale3D(Scale);
		return Actor;
	}

	bool FixturePopulateWorld(UWorld* World, UStaticMesh* Cube, UStaticMesh* Sphere, UMaterialInterface* Environment,
		UTextureCube* SkyCubemap, const TArray<TStrongObjectPtr<UMaterialInstanceConstant>>& Materials, FString& Error)
	{
		for (int32 Role = 0; Role < Materials.Num(); ++Role)
		{
			const FVector Position(-220.0 + (Role % 3) * 220.0, Role < 3 ? -130.0 : 150.0, 110.0);
			const FString SphereLabel = FString::Printf(TEXT("Toon_%s_Sphere"), FixtureRoleNames[Role]);
			const FString CubeLabel = FString::Printf(TEXT("Toon_%s_Cube"), FixtureRoleNames[Role]);
			if (!FixturePrimitive(World, Sphere, Materials[Role].Get(), *SphereLabel, Position, FVector(1.0))
				|| !FixturePrimitive(World, Cube, Materials[Role].Get(), *CubeLabel, FVector(Position.X, Position.Y, 30.0), FVector(1.25, 1.25, 0.6)))
			{
				Error = TEXT("角色验证 primitive 生成失败");
				return false;
			}
		}
		if (!FixturePrimitive(World, Cube, Environment, TEXT("DefaultLit_Floor"), FVector(0, 0, -10), FVector(10, 9, 0.2))
			|| !FixturePrimitive(World, Cube, Environment, TEXT("DefaultLit_BackWall"), FVector(0, 370, 170), FVector(10, 0.2, 3.6))
			|| !FixturePrimitive(World, Cube, Environment, TEXT("DefaultLit_LeftWall"), FVector(-470, 0, 170), FVector(0.2, 7.2, 3.6))
			|| !FixturePrimitive(World, Sphere, Environment, TEXT("DefaultLit_ControlSphere"), FVector(-370, -260, 70), FVector(1.4))
			|| !FixturePrimitive(World, Cube, Environment, TEXT("DefaultLit_ControlCube"), FVector(335, 230, 65), FVector(1.3)))
		{
			Error = TEXT("环境控制 primitive 生成失败");
			return false;
		}
		ADirectionalLight* Directional = World->SpawnActor<ADirectionalLight>();
		APointLight* Point = World->SpawnActor<APointLight>();
		ASkyLight* Sky = World->SpawnActor<ASkyLight>();
		APostProcessVolume* PostProcess = World->SpawnActor<APostProcessVolume>();
		AMMDEndfieldNativeBackendValidationActor* Validation = World->SpawnActor<AMMDEndfieldNativeBackendValidationActor>();
		if (!Directional || !Point || !Sky || !PostProcess || !Validation)
		{
			Error = TEXT("验证灯光、后处理或运行时验证 Actor 生成失败");
			return false;
		}
		Directional->SetActorLabel(TEXT("NativeShadow_Directional"));
		Directional->SetActorRotation(FRotator(-45, -35, 0));
		UDirectionalLightComponent* DirectionalComponent = CastChecked<UDirectionalLightComponent>(Directional->GetLightComponent());
		DirectionalComponent->SetMobility(EComponentMobility::Movable);
		DirectionalComponent->SetIntensity(4.0f);
		DirectionalComponent->SetLightColor(FLinearColor(1.0f, 0.88f, 0.75f));
		DirectionalComponent->SetCastShadows(true);
		Point->SetActorLabel(TEXT("NativeShadow_Point"));
		Point->SetActorLocation(FVector(-100, -150, 300));
		UPointLightComponent* PointComponent = CastChecked<UPointLightComponent>(Point->GetLightComponent());
		PointComponent->SetMobility(EComponentMobility::Movable);
		PointComponent->SetIntensityUnits(ELightUnits::Lumens);
		PointComponent->SetIntensity(6000.0f);
		PointComponent->SetAttenuationRadius(650.0f);
		PointComponent->SetLightColor(FLinearColor(0.65f, 0.80f, 1.0f));
		PointComponent->SetCastShadows(true);
		Sky->SetActorLabel(TEXT("NativeEnvironment_Sky"));
		Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Sky->GetLightComponent()->SourceType = SLS_SpecifiedCubemap;
		Sky->GetLightComponent()->Cubemap = SkyCubemap;
		Sky->GetLightComponent()->SetIntensity(0.3f);
		Sky->GetLightComponent()->SetCaptureIsDirty();
		PostProcess->SetActorLabel(TEXT("Validation_Lumen_FixedExposure"));
		PostProcess->bUnbound = true;
		FPostProcessSettings& PP = PostProcess->Settings;
		PP.bOverride_DynamicGlobalIlluminationMethod = true;
		PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
		PP.bOverride_ReflectionMethod = true;
		PP.ReflectionMethod = EReflectionMethod::Lumen;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 0.25f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.0f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.0f;
		PP.bOverride_DepthOfFieldScale = true;
		PP.DepthOfFieldScale = 0.0f;
		Validation->SetActorLabel(TEXT("EndfieldNativeBackend_RuntimeValidation"));
		Validation->CameraLocation = FVector(450, -650, 400);
		Validation->CameraLookAt = FVector(0, 0, 100);
		Validation->CameraViewTarget = nullptr;
		World->UpdateWorldComponents(false, false);
		return true;
	}
}

UMMDEndfieldNativeBackendFixtureCommandlet::UMMDEndfieldNativeBackendFixtureCommandlet()
{
	IsClient = true;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UMMDEndfieldNativeBackendFixtureCommandlet::Main(const FString& Params)
{
	const TArray<FString> Packages = FixturePackages();
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MMDEndfieldNativeBackendValidation")));
	const FString ManifestPath = FPaths::Combine(Directory, TEXT("fixture-manifest.json"));
	const FString DefaultReportPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EndfieldNativeBackendValidation/fixture-report.json")));
	FString ReportPath;
	if (!FParse::Value(*Params, TEXT("Report="), ReportPath)) ReportPath = DefaultReportPath;
	ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
	const bool bReportPathAllowed = FPaths::IsUnderDirectory(ReportPath, FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));
	if (!bReportPathAllowed) ReportPath = DefaultReportPath;
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("tool"), FixtureTool);
	Report->SetNumberField(TEXT("fixture_version"), FixtureVersion);
	Report->SetStringField(TEXT("captured_at_utc"), FDateTime::UtcNow().ToIso8601());
	Report->SetStringField(TEXT("package_root"), FixtureRoot);
	Report->SetStringField(TEXT("map_package"), FixtureMap);
	Report->SetStringField(TEXT("ownership_manifest"), ManifestPath);
	Report->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
	Report->SetStringField(TEXT("hash_scope"), TEXT("raw-file-bytes/integrity-guard"));
	Report->SetArrayField(TEXT("owned_packages"), FixtureJsonStrings(Packages));
	Report->SetNumberField(TEXT("expected_package_count"), Packages.Num());
	Report->SetBoolField(TEXT("visual_accepted"), false);
	Report->SetBoolField(TEXT("cook_verified"), false);
	Report->SetBoolField(TEXT("packaged_runtime_verified"), false);
	Report->SetStringField(TEXT("scope"), TEXT("Cook setup only: six role-marker profiles preserving source diffuse RGB, six native Toon MICs, one primitive map; no copied character or Engine/BasicShapes assets."));
	Report->SetStringField(TEXT("limits"), TEXT("MaterialCompileReady and SourceSignature validate fixture preparation only. Lumen, reflection, shadow response and cooked backend routing require actual cook/game screenshots and controls. Fixture colors are diagnostic colors, not client reproduction evidence. Project default map is unchanged."));
	TArray<FString> Errors, Written;
	TArray<TSharedPtr<FJsonValue>> ProfileReports, CompileReports;
	TSharedRef<FJsonObject> SourceHashes = MakeShared<FJsonObject>();
	FString Error, SignatureBundle;
	bool bOwned = false;
	bool bCompileReady = false;
	UWorld* World = nullptr;
	ON_SCOPE_EXIT
	{
		if (World)
		{
			if (GEngine) GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	do
	{
		if (!bReportPathAllowed) { Errors.Add(TEXT("生成报告只允许保存到项目 Saved 目录；已使用默认 Saved 报告路径")); break; }
		if (!FixtureCheckOwnership(Directory, ManifestPath, Packages, Error)) { Errors.Add(Error); break; }
		if (!GEngine || !Substrate::IsToonProfileEnabled()) { Errors.Add(TEXT("需启用 Substrate Toon Profile 并允许 commandlet rendering")); break; }
		TStrongObjectPtr<UMaterial> Parent(LoadObject<UMaterial>(nullptr, *FixtureObjectPath(FixtureParent)));
		TStrongObjectPtr<UMMDStageRenderPreset> Preset(LoadObject<UMMDStageRenderPreset>(nullptr, *FixtureObjectPath(FixturePreset)));
		TStrongObjectPtr<UTexture2D> White(LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
		TStrongObjectPtr<UStaticMesh> Cube(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		TStrongObjectPtr<UStaticMesh> Sphere(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		TStrongObjectPtr<UMaterialInterface> Environment(LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
		TStrongObjectPtr<UTextureCube> SkyCubemap(LoadObject<UTextureCube>(nullptr, TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")));
		if (!Parent.IsValid() || !Preset.IsValid() || !White.IsValid() || !Cube.IsValid() || !Sphere.IsValid() || !Environment.IsValid() || !SkyCubemap.IsValid())
		{
			Errors.Add(TEXT("缺少固定 EndfieldV2 源资产或引擎基础几何/环境资产"));
			break;
		}
		TArray<TStrongObjectPtr<UToonProfile>> Sources, Profiles;
		TArray<TStrongObjectPtr<UMaterialInstanceConstant>> Materials;
		TArray<FString> SourcePackages = { FString(FixtureParent), FString(FixturePreset) };
		for (int32 Role = 0; Role < UE_ARRAY_COUNT(FixtureRoleNames); ++Role)
		{
			const FString SourcePackage = FString::Printf(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_%s"), FixtureRoleNames[Role]);
			Sources.Emplace(LoadObject<UToonProfile>(nullptr, *FixtureObjectPath(SourcePackage)));
			SourcePackages.Add(SourcePackage);
			if (!Sources.Last().IsValid() || !Preset->CharacterToonRoles.Contains(static_cast<EMMDMaterialRole>(Role)))
			{
				Errors.Add(TEXT("缺少固定角色源 Profile/预设设置：") + SourcePackage);
				break;
			}
		}
		if (!Errors.IsEmpty()) break;
		for (const FString& SourcePackage : SourcePackages)
		{
			FString Hash;
			const FString Filename = FPackageName::LongPackageNameToFilename(SourcePackage, FPackageName::GetAssetPackageExtension());
			if (!MMDEndfieldNativeBackend::ComputeFileHash(Filename, Hash)) { Errors.Add(TEXT("源资产 hash 读取失败：") + Filename); break; }
			SourceHashes->SetStringField(SourcePackage, Hash);
			SignatureBundle += SourcePackage + TEXT("=") + Hash + TEXT("\n");
		}
		if (!Errors.IsEmpty()) break;
		const MMDEndfieldNativeBackend::FStatus& Backend = MMDEndfieldNativeBackend::GetStatus();
		SignatureBundle += FString::Printf(TEXT("fixture_version=%d\n"), FixtureVersion) + Backend.PluginShaderHash + TEXT("\n") + Backend.Signature;
		FTCHARToUTF8 SignatureBytes(*SignatureBundle);
		FSHAHash Signature;
		FSHA1::HashBuffer(SignatureBytes.Get(), SignatureBytes.Length(), Signature.Hash);
		Report->SetStringField(TEXT("SourceSignature"), Signature.ToString());
		Report->SetObjectField(TEXT("source_asset_hashes"), SourceHashes);
		Report->SetBoolField(TEXT("backend_mapping_active_during_setup"), Backend.bActive);
		Report->SetNumberField(TEXT("backend_mode_during_setup"), Backend.Mode);
		Report->SetStringField(TEXT("backend_signature_during_setup"), Backend.Signature);
		Report->SetStringField(TEXT("backend_plugin_shader_hash"), Backend.PluginShaderHash);
		Report->SetBoolField(TEXT("success"), false);
		Report->SetStringField(TEXT("generation_state"), TEXT("reserved"));
		if (!FixtureWriteJson(ManifestPath, Report)) { Errors.Add(TEXT("无法写入归属 manifest，拒绝保存包")); break; }
		bOwned = true;
		for (int32 Role = 0; Role < Sources.Num(); ++Role)
		{
			Profiles.Emplace(FixtureCopyProfile(Sources[Role].Get(), Role, Error));
			if (!Profiles.Last().IsValid()) { Errors.Add(Error); break; }
			const FMMDStageToonRoleSettings& Settings = Preset->CharacterToonRoles.FindChecked(static_cast<EMMDMaterialRole>(Role));
			Materials.Emplace(FixtureCreateMaterial(Parent.Get(), White.Get(), Profiles.Last().Get(), Settings, Role, Error));
			if (!Materials.Last().IsValid()) { Errors.Add(Error); break; }
			if (!Materials.Last()->bOverrideToonProfile || Materials.Last()->GetToonProfileOverride_Internal() != Profiles.Last().Get())
			{
				Errors.Add(TEXT("MIC 的角色 Profile override 未保留：") + Materials.Last()->GetPathName());
				break;
			}
			TSharedRef<FJsonObject> Profile = MakeShared<FJsonObject>();
			Profile->SetStringField(TEXT("role"), FixtureRoleNames[Role]);
			Profile->SetStringField(TEXT("source"), Sources[Role]->GetPathName());
			Profile->SetStringField(TEXT("asset"), Profiles.Last()->GetPathName());
			Profile->SetBoolField(TEXT("source_diffuse_rgb_preserved"), FixtureRgbUnchanged(Sources[Role].Get(), Profiles.Last().Get()));
			Profile->SetBoolField(TEXT("source_diffuse_and_specular_rgb_preserved"), FixtureRgbUnchanged(Sources[Role].Get(), Profiles.Last().Get()));
			Profile->SetNumberField(TEXT("marker_alpha_first"), Profiles.Last()->Settings.DiffuseRamp.ColorCurves[3].Eval(0.0f));
			Profile->SetNumberField(TEXT("marker_alpha_last"), Profiles.Last()->Settings.DiffuseRamp.ColorCurves[3].Eval(1.0f));
			Profile->SetStringField(TEXT("material"), Materials.Last()->GetPathName());
			ProfileReports.Add(MakeShared<FJsonValueObject>(Profile));
		}
		if (!Errors.IsEmpty()) break;
		Parent->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		FlushRenderingCommands();
		bCompileReady = true;
		bool bReady = false;
		CompileReports.Add(MakeShared<FJsonValueObject>(FixtureCompileState(Parent.Get(), bReady)));
		bCompileReady &= bReady;
		for (const TStrongObjectPtr<UMaterialInstanceConstant>& Material : Materials)
		{
			CompileReports.Add(MakeShared<FJsonValueObject>(FixtureCompileState(Material.Get(), bReady)));
			bCompileReady &= bReady;
		}
		if (!bCompileReady) { Errors.Add(TEXT("源材质或验证 MIC 真实 shader 未就绪，不保存验证资产")); break; }
		const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
			.RequiresHitProxies(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false)
			.ShouldSimulatePhysics(false).SetTransactional(false);
		UPackage* MapPackage = CreatePackage(FixtureMap);
		if (FindObject<UObject>(MapPackage, *FPackageName::GetLongPackageAssetName(FixtureMap))) { Errors.Add(TEXT("目标验证地图在内存中已存在")); break; }
		World = UWorld::CreateWorld(EWorldType::Editor, false, *FPackageName::GetLongPackageAssetName(FixtureMap), MapPackage, false, GMaxRHIFeatureLevel, &Values);
		if (!World) { Errors.Add(TEXT("验证世界创建失败")); break; }
		GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
		if (!FixturePopulateWorld(World, Cube.Get(), Sphere.Get(), Environment.Get(), SkyCubemap.Get(), Materials, Error)) { Errors.Add(Error); break; }
		for (int32 Role = 0; Role < Profiles.Num(); ++Role)
		{
			if (!FixtureSaveAsset(Profiles[Role].Get(), Packages, Error)) { Errors.Add(Error); break; }
			Written.Add(Profiles[Role]->GetOutermost()->GetName());
		}
		if (!Errors.IsEmpty()) break;
		for (const TStrongObjectPtr<UMaterialInstanceConstant>& Material : Materials)
		{
			if (!FixtureSaveAsset(Material.Get(), Packages, Error)) { Errors.Add(Error); break; }
			Written.Add(Material->GetOutermost()->GetName());
		}
		if (!Errors.IsEmpty()) break;
		if (!FixtureSaveAsset(World, Packages, Error)) { Errors.Add(Error); break; }
		Written.Add(FixtureMap);
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Source : SourceHashes->Values)
		{
			FString Hash;
			const FString Filename = FPackageName::LongPackageNameToFilename(Source.Key, FPackageName::GetAssetPackageExtension());
			if (!MMDEndfieldNativeBackend::ComputeFileHash(Filename, Hash) || Hash != Source.Value->AsString()) Errors.Add(TEXT("生成后源资产 hash 发生变化：") + Source.Key);
		}
	} while (false);

	const bool bSuccess = Errors.IsEmpty() && bCompileReady && Written.Num() == Packages.Num();
	Report->SetBoolField(TEXT("success"), bSuccess);
	Report->SetBoolField(TEXT("MaterialCompileReady"), bCompileReady);
	Report->SetNumberField(TEXT("written_package_count"), Written.Num());
	Report->SetStringField(TEXT("generation_state"), bSuccess ? TEXT("complete") : TEXT("failed"));
	Report->SetArrayField(TEXT("written_packages"), FixtureJsonStrings(Written));
	Report->SetArrayField(TEXT("profiles"), ProfileReports);
	Report->SetArrayField(TEXT("material_compile"), CompileReports);
	Report->SetArrayField(TEXT("errors"), FixtureJsonStrings(Errors));
	if (bOwned && !FixtureWriteJson(ManifestPath, Report)) Errors.Add(TEXT("最终归属 manifest 写入失败"));
	Report->SetArrayField(TEXT("errors"), FixtureJsonStrings(Errors));
	Report->SetBoolField(TEXT("success"), bSuccess && Errors.IsEmpty());
	if (!FixtureWriteJson(ReportPath, Report)) Errors.Add(TEXT("生成报告写入失败：") + ReportPath);
	for (const FString& Failure : Errors) UE_LOG(LogTemp, Error, TEXT("Endfield fixture: %s"), *Failure);
	UE_LOG(LogTemp, Display, TEXT("Endfield fixture: saved %d/%d packages; MaterialCompileReady=%d; report=%s"), Written.Num(), Packages.Num(), bCompileReady, *ReportPath);
	return bSuccess && Errors.IsEmpty() ? 0 : 1;
}
