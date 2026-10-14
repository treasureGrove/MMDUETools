#include "MMDStagePresetsCommandlet.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "ContentStreaming.h"
#include "Dom/JsonObject.h"
#include "Editor/MMDStageMaterialBuilder.h"
#include "Editor/MMDReferenceMaterialBuilder.h"
#include "Editor/MMDOutlineMaterial.h"
#include "Editor/MMDStageTextureGenerator.h"
#include "Editor/MMDGardenMaterialBuilder.h"
#include "Editor/MMDGardenMeshBuilder.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Level.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Import/TMMDMeshBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PreviewScene.h"
#include "RenderGraphBuilder.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHIGlobals.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "SceneInterface.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageMaterialState.h"
#include "Stage/MMDStageRenderPreset.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr const TCHAR* StageRoot = TEXT("/Ue5MMDTools/Stages/");
	constexpr const TCHAR* GeneratedActorTag = TEXT("MMDStageGenerated");
	struct FStageDefinition
	{
		const TCHAR* Name;
		const TCHAR* ImageName;
		const TCHAR* DisplayName;
		EMMDStageTheme Theme;
		bool bStage;
	};
	const FStageDefinition Definitions[] =
	{
		{ TEXT("NeonConcert"), TEXT("neon_concert"), TEXT("霓虹演唱会"), EMMDStageTheme::NeonConcert, true },
		{ TEXT("MoonMirror"), TEXT("moon_mirror"), TEXT("月光镜庭"), EMMDStageTheme::MoonMirror, true },
		{ TEXT("SakuraTheater"), TEXT("sakura_theater"), TEXT("樱庭水镜"), EMMDStageTheme::SakuraTheater, true },
		{ TEXT("SilverCity"), TEXT("silver_city"), TEXT("银白柔光"), EMMDStageTheme::MoonMirror, false },
		{ TEXT("OpenWorldToon"), TEXT("open_world_toon"), TEXT("明快卡通"), EMMDStageTheme::SakuraTheater, false },
		{ TEXT("IndustrialAnime"), TEXT("industrial_anime"), TEXT("工业动画"), EMMDStageTheme::NeonConcert, false },
		{ TEXT("CinematicAnime"), TEXT("cinematic_anime"), TEXT("电影冷暖"), EMMDStageTheme::MoonMirror, false }
	};

	// 终末地预设走独立路径：包位于 Presets/Endfield/ 子目录，父材质是 M_MMD_Endfield_*，
	// 与上面七套（扁平路径 + References.Families）不同源，故不进 Definitions[]，只由 -EndfieldCapture 驱动。
	const FStageDefinition EndfieldCaptureDefinition =
		{ TEXT("Endfield"), TEXT("endfield"), TEXT("终末地"), EMMDStageTheme::SakuraTheater, false };
	const TCHAR* EndfieldCaptureParentPaths[] =
	{
		TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Opaque"),
		TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Masked"),
		TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Translucent")
	};
	constexpr const TCHAR* EndfieldCapturePresetPath = TEXT("/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield");

	FString AssetPackage(const TCHAR* Directory, const TCHAR* Prefix, const TCHAR* Name)
	{
		return FString::Printf(TEXT("%s%s/%s%s"), StageRoot, Directory, Prefix, Name);
	}

	FString StagePresetsObjectPath(const FString& PackageName)
	{
		return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
	}

	bool SaveStageAsset(UObject* Asset, bool bMap, FString& Error)
	{
		UPackage* Package = Asset ? Asset->GetOutermost() : nullptr;
		if (!Package || !Package->GetName().StartsWith(StageRoot))
		{
			Error = TEXT("Refused to save an asset outside the new plugin Stages directory");
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(),
			bMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		Asset->SetFlags(RF_Public | RF_Standalone);
		Asset->MarkPackageDirty();
		if (bMap)
		{
			Package->SetPackageFlags(PKG_ContainsMap);
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		Args.Error = GError;
		Args.bWarnOfLongFilename = false;
		if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
		{
			Error = FString::Printf(TEXT("Could not save %s"), *Filename);
			return false;
		}
		return true;
	}

	int32 ReferenceFamilyFor(const FStageDefinition& Definition)
	{
		const FString Name(Definition.Name);
		if (Name == TEXT("MoonMirror") || Name == TEXT("CinematicAnime")) return 1;
		if (Name == TEXT("IndustrialAnime") || Name == TEXT("SakuraTheater")) return 2;
		if (Name == TEXT("SilverCity")) return 3;
		return 0;
	}

	void StagePresetsConfigurePreset(UMMDStageRenderPreset* Preset, const FStageDefinition& Definition,
		const FMMDStageMaterialSet& Materials, const FMMDReferenceMaterialSet& References)
	{
		const int32 Family = ReferenceFamilyFor(Definition);
		Preset->DisplayName = FText::FromString(Definition.DisplayName);
		Preset->Theme = Definition.Theme;
		Preset->bOverrideStageTheme = Definition.bStage;
		Preset->CharacterMaterialOpaque = References.Families[Family].Opaque;
		Preset->CharacterMaterialMasked = References.Families[Family].Masked;
		Preset->CharacterMaterialTranslucent = References.Families[Family].Translucent;
		Preset->SurfaceMaterial = Materials.Floor;
		Preset->PanelMaterial = Materials.Panel;
		Preset->GlowMaterial = Materials.Accent;
		Preset->BeamMaterial = Materials.Beam;
		Preset->BackdropMaterial = Materials.Backdrop;
		Preset->ExposureBias = 0.18f;
		Preset->BloomIntensity = 0.06f;
		Preset->ColorGradingSaturation = 1.0f;
		Preset->ColorGradingContrast = 1.0f;
		Preset->ColorGradingGamma = 1.0f;
		Preset->ColorGradingGain = FLinearColor::White;
		Preset->WhiteTemperature = 6500.0f;
		Preset->FilmToe = 0.25f;
		Preset->ShadowThreshold = 0.45f;
		Preset->ShadowSoftness = 0.12f;
		Preset->DiffuseWrap = 0.10f;
		Preset->ToonAmount = 1.0f;
		Preset->LightScale = 0.95f;
		Preset->RimPower = 3.0f;
		Preset->SpecularPower = 48.0f;
		Preset->SpecularTint = FLinearColor(0.90f, 0.95f, 1.0f);
		Preset->ShadowSaturation = 0.90f;
		Preset->KeyLightColor = FLinearColor(1.0f, 0.95f, 0.90f);
		Preset->FillLightColor = FLinearColor(0.72f, 0.82f, 1.0f);
		Preset->RimLightColor = FLinearColor(0.55f, 0.78f, 1.0f);
		static const FRotator KeyRotations[] = { FRotator(-38, 55, 0), FRotator(-42, 58, 0), FRotator(-46, 52, 0), FRotator(-34, 55, 0) };
		static const FRotator FillRotations[] = { FRotator(-20, 140, 0), FRotator(-16, 150, 0), FRotator(-22, 135, 0), FRotator(-20, 145, 0) };
		static const FRotator RimRotations[] = { FRotator(-12, -90, 0), FRotator(-10, -90, 0), FRotator(-14, -90, 0), FRotator(-10, -90, 0) };
		static const float Key[] = { 3.3f, 3.6f, 3.2f, 3.0f };
		static const float Fill[] = { 0.45f, 0.30f, 0.38f, 0.55f };
		static const float Rim[] = { 0.70f, 1.0f, 0.65f, 0.45f };
		static const float Accent[] = { 0.60f, 0.85f, 0.70f, 0.60f };
		static const float Ambient[] = { 0.14f, 0.10f, 0.12f, 0.18f };
		static const float Saturation[] = { 1.06f, 0.90f, 0.88f, 0.80f };
		static const float Roughness[] = { 0.70f, 0.40f, 0.48f, 0.32f };
		static const float RimStrength[] = { 0.06f, 0.14f, 0.09f, 0.05f };
		static const float BlackLift[] = { 0.028f, 0.024f, 0.030f, 0.034f };
		static const float SoftSpecular[] = { 0.12f, 0.18f, 0.16f, 0.20f };
		static const float Specular[] = { 0.07f, 0.14f, 0.13f, 0.16f };
		static const FLinearColor Shadow[] = { FLinearColor(.58f,.66f,.76f), FLinearColor(.42f,.48f,.62f), FLinearColor(.48f,.54f,.64f), FLinearColor(.66f,.70f,.78f) };
		Preset->KeyLightRotation = KeyRotations[Family];
		Preset->FillLightRotation = FillRotations[Family];
		Preset->RimLightRotation = RimRotations[Family];
		Preset->KeyLightIntensity = Key[Family];
		Preset->FillLightIntensity = Fill[Family];
		Preset->RimLightIntensity = Rim[Family];
		Preset->AccentLightIntensity = Accent[Family];
		Preset->AmbientStrength = Ambient[Family];
		Preset->ColorSaturation = Saturation[Family];
		Preset->Roughness = Roughness[Family];
		Preset->RimStrength = RimStrength[Family];
		Preset->BlackLift = BlackLift[Family];
		Preset->SoftSpecularStrength = SoftSpecular[Family];
		Preset->SpecularStrength = Specular[Family];
		Preset->ShadowTint = Shadow[Family];
		Preset->BaseTint = FLinearColor(0.025f, 0.037f, 0.070f);
		Preset->AccentTint = FLinearColor(0.045f, 0.82f, 1.0f);
		Preset->SecondaryTint = FLinearColor(0.70f, 0.10f, 0.46f);
		const FString Name(Definition.Name);
		if (Name == TEXT("MoonMirror"))
		{
			Preset->BaseTint = FLinearColor(0.038f, 0.075f, 0.18f);
			Preset->AccentTint = FLinearColor(0.42f, 0.75f, 1.0f);
			Preset->SecondaryTint = FLinearColor(0.79f, 0.87f, 1.0f);
		}
		else if (Name == TEXT("SakuraTheater"))
		{
			Preset->BaseTint = FLinearColor(0.12f, 0.065f, 0.034f);
			Preset->AccentTint = FLinearColor(1.0f, 0.43f, 0.63f);
			Preset->SecondaryTint = FLinearColor(1.0f, 0.79f, 0.44f);
			Preset->KeyLightColor = FLinearColor(1.00f, 0.88f, 0.80f);
			Preset->KeyLightRotation = FRotator(-32.0f, 48.0f, 0.0f);
			Preset->KeyLightIntensity = 3.80f;
			Preset->FillLightColor = FLinearColor(0.58f, 0.68f, 0.88f);
			Preset->FillLightRotation = FRotator(-8.0f, 135.0f, 0.0f);
			Preset->FillLightIntensity = 0.28f;
			Preset->RimLightColor = FLinearColor(0.32f, 0.72f, 1.00f);
			Preset->RimLightRotation = FRotator(-9.0f, -100.0f, 0.0f);
			Preset->RimLightIntensity = 0.90f;
			Preset->AccentLightIntensity = 1.50f;
			Preset->ExposureBias = 0.0f;
			Preset->BloomIntensity = 0.25f;
			Preset->AmbientStrength = 0.12f;
			Preset->ColorGradingContrast = 1.03f;
			Preset->ColorGradingSaturation = 1.05f;
			Preset->LightScale = 1.02f;
			Preset->ShadowThreshold = 0.43f;
			Preset->ShadowSoftness = 0.12f;
			Preset->ShadowTint = FLinearColor(0.67f, 0.73f, 0.84f);
			Preset->DiffuseWrap = 0.10f;
			Preset->ToonAmount = 0.65f;
			Preset->SpecularStrength = 0.50f;
			Preset->SpecularPower = 64.0f;
			Preset->SoftSpecularStrength = 0.18f;
			Preset->Roughness = 0.62f;
			Preset->SpecularTint = FLinearColor(0.78f, 0.92f, 1.10f);
			Preset->RimStrength = 0.14f;
			Preset->RimPower = 3.20f;
			Preset->BlackLift = 0.035f;
			Preset->ColorSaturation = 1.02f;
			Preset->OutlinePixelWidth = 0.90f;
		}
		else if (Name == TEXT("SilverCity"))
		{
			Preset->BaseTint = FLinearColor(0.34f, 0.39f, 0.46f);
			Preset->AccentTint = FLinearColor(0.82f, 0.93f, 1.0f);
			Preset->SecondaryTint = FLinearColor(0.58f, 0.67f, 0.81f);
		}
		else if (Name == TEXT("OpenWorldToon"))
		{
			Preset->BaseTint = FLinearColor(0.14f, 0.22f, 0.18f);
			Preset->AccentTint = FLinearColor(0.31f, 0.95f, 0.63f);
			Preset->SecondaryTint = FLinearColor(1.0f, 0.79f, 0.30f);
		}
		else if (Name == TEXT("IndustrialAnime"))
		{
			Preset->BaseTint = FLinearColor(0.052f, 0.061f, 0.078f);
			Preset->AccentTint = FLinearColor(0.32f, 0.58f, 0.80f);
			Preset->SecondaryTint = FLinearColor(0.90f, 0.46f, 0.19f);
		}
		else if (Name == TEXT("CinematicAnime"))
		{
			Preset->BaseTint = FLinearColor(0.033f, 0.042f, 0.069f);
			Preset->AccentTint = FLinearColor(0.17f, 0.66f, 0.82f);
			Preset->SecondaryTint = FLinearColor(1.0f, 0.54f, 0.24f);
		}
	}

	UMMDStageRenderPreset* MakePreset(const FStageDefinition& Definition, const FMMDStageMaterialSet& Materials,
		const FMMDReferenceMaterialSet& References, bool bGenerate, FString& Error)
	{
		const FString PackageName = AssetPackage(TEXT("Presets"), TEXT("DA_Render_"), Definition.Name);
		UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *StagePresetsObjectPath(PackageName));
		if (bGenerate)
		{
			if (!Preset)
			{
				Preset = NewObject<UMMDStageRenderPreset>(CreatePackage(*PackageName),
					*FPackageName::GetLongPackageAssetName(PackageName), RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(Preset);
			}
			StagePresetsConfigurePreset(Preset, Definition, Materials, References);
			if (!SaveStageAsset(Preset, false, Error))
			{
				return nullptr;
			}
		}
		else if (!Preset)
		{
			Error = FString::Printf(TEXT("Preset asset is missing: %s. Run with -Generate first."), *PackageName);
		}
		return Preset;
	}

	// 只读加载终末地父材质与预设，并校验预设确实绑定了这三套新材质；不改动任何资产。
	bool EndfieldCaptureLoadBundle(UMMDStageRenderPreset*& OutPreset, TArray<UMaterialInterface*>& OutParents, FString& Error)
	{
		OutPreset = LoadObject<UMMDStageRenderPreset>(nullptr, *StagePresetsObjectPath(EndfieldCapturePresetPath));
		OutParents.Reset();
		for (const TCHAR* Path : EndfieldCaptureParentPaths)
		{
			OutParents.Add(LoadObject<UMaterialInterface>(nullptr, *StagePresetsObjectPath(Path)));
		}
		if (!OutPreset)
		{
			Error = FString::Printf(TEXT("Endfield preset is missing: %s. Run -run=MMDEndfieldPresets first."), EndfieldCapturePresetPath);
			return false;
		}
		if (!OutParents[0] || !OutParents[1] || !OutParents[2])
		{
			Error = TEXT("The M_MMD_Endfield_* character material family is incomplete");
			return false;
		}
		if (OutPreset->CharacterMaterialOpaque != OutParents[0] || OutPreset->CharacterMaterialMasked != OutParents[1]
			|| OutPreset->CharacterMaterialTranslucent != OutParents[2])
		{
			Error = TEXT("DA_Render_Endfield does not bind the M_MMD_Endfield_* material family");
			return false;
		}
		return true;
	}

	UBlueprint* MakeStageBlueprint(const FStageDefinition& Definition, UMMDStageRenderPreset* Preset,
		bool bGenerate, FString& Error)
	{
		const FString PackageName = AssetPackage(TEXT("Blueprints"), TEXT("BP_MMDStage_"), Definition.Name);
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *StagePresetsObjectPath(PackageName));
		if (!bGenerate)
		{
			return Blueprint;
		}
		if (!Blueprint)
		{
			Blueprint = FKismetEditorUtilities::CreateBlueprint(AMMDStageActor::StaticClass(), CreatePackage(*PackageName),
				*FPackageName::GetLongPackageAssetName(PackageName), BPTYPE_Normal,
				UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
			if (Blueprint)
			{
				FAssetRegistryModule::AssetCreated(Blueprint);
			}
		}
		if (!Blueprint || Blueprint->ParentClass != AMMDStageActor::StaticClass())
		{
			Error = FString::Printf(TEXT("Stage blueprint creation or parent validation failed: %s"), *PackageName);
			return nullptr;
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		AMMDStageActor* DefaultActor = Blueprint->GeneratedClass
			? Cast<AMMDStageActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		if (!DefaultActor || Blueprint->Status == BS_Error)
		{
			Error = FString::Printf(TEXT("Stage blueprint compilation failed: %s"), *PackageName);
			return nullptr;
		}
		DefaultActor->Theme = Definition.Theme;
		DefaultActor->RenderPreset = Preset;
		DefaultActor->BPM = Definition.Theme == EMMDStageTheme::NeonConcert ? 118.0f : 96.0f;
		DefaultActor->bUseManualTime = false;
		DefaultActor->bAnimate = true;
		DefaultActor->bPreviewAnimation = true;
		return SaveStageAsset(Blueprint, false, Error) ? Blueprint : nullptr;
	}

	UWorld* MakeStageMap(const FStageDefinition& Definition, UBlueprint* Blueprint, bool bGenerate, FString& Error)
	{
		const FString PackageName = AssetPackage(TEXT("Maps"), TEXT("MMDStage_"), Definition.Name);
		UWorld* World = LoadObject<UWorld>(nullptr, *StagePresetsObjectPath(PackageName));
		if (!bGenerate)
		{
			return World;
		}
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			Error = TEXT("Map generation requires a compiled stage blueprint");
			return nullptr;
		}
		if (!World)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Editor, false,
				*FPackageName::GetLongPackageAssetName(PackageName), CreatePackage(*PackageName), false,
				GMaxRHIFeatureLevel, &Values);
			if (World)
			{
				FAssetRegistryModule::AssetCreated(World);
			}
		}
		if (!World)
		{
			Error = TEXT("Stage map world creation failed");
			return nullptr;
		}
		if (!World->IsInitialized())
		{
			World->InitWorld(UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false));
		}
		TArray<AActor*> OldGeneratedActors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(GeneratedActorTag))
			{
				OldGeneratedActors.Add(*It);
			}
		}
		for (AActor* Actor : OldGeneratedActors)
		{
			World->DestroyActor(Actor);
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AMMDStageActor* Stage = World->SpawnActor<AMMDStageActor>(Blueprint->GeneratedClass, FTransform::Identity, Spawn);
		if (!Stage)
		{
			Error = TEXT("Saved stage blueprint could not be spawned in the map");
			return nullptr;
		}
		Stage->Tags.Add(GeneratedActorTag);
		Stage->SetActorLabel(Definition.DisplayName);
		Stage->RebuildStage();
		Stage->EvaluateStageAtTime(0.0f);
		ACameraActor* Camera = World->SpawnActor<ACameraActor>();
		APlayerStart* PlayerStart = World->SpawnActor<APlayerStart>();
		if (!Camera || !PlayerStart)
		{
			Error = TEXT("Stage camera or PlayerStart creation failed");
			return nullptr;
		}
		Camera->Tags.Add(GeneratedActorTag);
		Camera->Tags.Add(TEXT("MMDStageCamera"));
		Camera->SetActorLabel(TEXT("MMD 舞台主镜头"));
		const bool bGarden = Definition.Theme == EMMDStageTheme::SakuraTheater;
		const FVector CameraLocation = bGarden ? FVector(65, -440, 145) : FVector(0, -900, 285);
		const FVector CameraTarget = bGarden ? FVector(0, -5, 75) : FVector(0, 150, 170);
		Camera->SetActorLocationAndRotation(CameraLocation, (CameraTarget - CameraLocation).Rotation());
		Camera->GetCameraComponent()->FieldOfView = bGarden ? 50.0f : 55.0f;
		Camera->GetCameraComponent()->AspectRatio = 16.0f / 9.0f;
		if (bGarden)
		{
			FPostProcessSettings& CameraPP = Camera->GetCameraComponent()->PostProcessSettings;
			CameraPP.bOverride_DepthOfFieldFocalDistance = true;
			CameraPP.DepthOfFieldFocalDistance = FVector::Distance(CameraLocation, CameraTarget);
			CameraPP.bOverride_DepthOfFieldFstop = true;
			CameraPP.DepthOfFieldFstop = 4.0f;
			CameraPP.bOverride_DepthOfFieldSensorWidth = true;
			CameraPP.DepthOfFieldSensorWidth = 36.0f;
		}
		if (FByteProperty* Activation = FindFProperty<FByteProperty>(ACameraActor::StaticClass(), TEXT("AutoActivateForPlayer")))
		{
			Activation->SetPropertyValue_InContainer(Camera, static_cast<uint8>(EAutoReceiveInput::Player0));
		}
		PlayerStart->Tags.Add(GeneratedActorTag);
		PlayerStart->SetActorLocation(FVector(0, -460, 110));
		World->UpdateWorldComponents(false, false);
		return SaveStageAsset(World, true, Error) ? World : nullptr;
	}

	struct FCharacterSample
	{
		FString Mount;
		FString Directory;
		FString MeshPath;
		FString AnimationPath;
		FString SourceVMDPath;
		float BeautyAnimationTime = 0.6f;
		float MotionAnimationTime = 1.6f;
		TStrongObjectPtr<USkeletalMesh> Mesh;
		TStrongObjectPtr<UAnimSequence> Animation;
		~FCharacterSample()
		{
			if (!Mount.IsEmpty())
			{
				FPackageName::UnRegisterMountPoint(Mount, Directory);
			}
		}
	};

	bool LoadCharacterSample(FCharacterSample& Sample, FString& Error)
	{
		const FString ImportRoot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/ImportedAssets"));
		TArray<FString> Manifests;
		IFileManager::Get().FindFilesRecursive(Manifests, *ImportRoot, TEXT("manifest.json"), true, false);
		FDateTime LatestTime = FDateTime::MinValue();
		for (const FString& ManifestPath : Manifests)
		{
			FString Json;
			TSharedPtr<FJsonObject> Manifest;
			if (!FFileHelper::LoadFileToString(Json, *ManifestPath)
				|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest.IsValid())
			{
				continue;
			}
			double Vertices = 0.0;
			const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*ManifestPath);
			if (Manifest->TryGetNumberField(TEXT("vertices"), Vertices) && Vertices > 5000.0 && Timestamp > LatestTime)
			{
				LatestTime = Timestamp;
				Sample.Directory = FPaths::GetPath(ManifestPath) + TEXT("/");
				const FString Name = FPaths::GetCleanFilename(FPaths::GetPath(ManifestPath));
				Sample.Mount = FString::Printf(TEXT("/Game/MMDModels/%s/"), *Name);
				Sample.MeshPath = Sample.Mount + TEXT("Model/") + Name + TEXT(".") + Name;
				Manifest->TryGetStringField(TEXT("animation"), Sample.AnimationPath);
				Manifest->TryGetStringField(TEXT("source_vmd"), Sample.SourceVMDPath);
			}
		}
		if (Sample.Mount.IsEmpty())
		{
			Error = TEXT("No Saved/BackgroundValidation character import was found; run import validation before screenshots");
			return false;
		}
		FPackageName::RegisterMountPoint(Sample.Mount, Sample.Directory);
		Sample.Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *Sample.MeshPath));
		if (!Sample.AnimationPath.IsEmpty())
		{
			Sample.Animation.Reset(LoadObject<UAnimSequence>(nullptr, *Sample.AnimationPath));
		}
		if (!Sample.Mesh.IsValid() || !Sample.Animation.IsValid())
		{
			Error = TEXT("Sandbox character mesh or its VMD animation could not be loaded");
			return false;
		}
		// 回归样本只有动作前两秒；另做 Saved 内的八秒预览，展示真正的舞蹈姿态。
		if (!Sample.SourceVMDPath.IsEmpty() && FPaths::FileExists(Sample.SourceVMDPath))
		{
			const FString CachedPackage = Sample.Mount + TEXT("Animation/MMDStagePreview8s");
			FString CachedFilename;
			if (FPackageName::TryConvertLongPackageNameToFilename(CachedPackage, CachedFilename, FPackageName::GetAssetPackageExtension())
				&& FPaths::FileExists(CachedFilename)
				&& IFileManager::Get().GetTimeStamp(*CachedFilename) >= IFileManager::Get().GetTimeStamp(*Sample.SourceVMDPath))
			{
				TStrongObjectPtr<UAnimSequence> Cached(LoadObject<UAnimSequence>(nullptr, *StagePresetsObjectPath(CachedPackage)));
				if (Cached.IsValid())
				{
					Cached->WaitOnExistingCompression();
					if (Cached->IsBoneCompressedDataValid() && Cached->GetPlayLength() >= 6.5f
						&& Cached->GetSkeleton() == Sample.Mesh->GetSkeleton())
					{
						Sample.Animation.Reset(Cached.Get());
						Sample.AnimationPath = Sample.Animation->GetPathName();
						Sample.BeautyAnimationTime = 2.6f;
						Sample.MotionAnimationTime = 6.4f;
						return true;
					}
				}
			}
			TVMDParser Parser;
			if (!Parser.ParseVMDFile(Sample.SourceVMDPath))
			{
				Error = TEXT("Stage preview source VMD could not be parsed");
				return false;
			}
			constexpr uint32 LastFrame = 240;
			VMDData& Motion = Parser.VMDInfo;
			Motion.BoneKeyframes.RemoveAll([](const VMDBoneKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			Motion.MorphKeyframes.RemoveAll([](const VMDMorphKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			Motion.CameraKeyframes.RemoveAll([](const VMDCameraKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			Motion.LightKeyframes.RemoveAll([](const VMDLightKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			Motion.ShadowKeyframes.RemoveAll([](const VMDShadowKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			Motion.IKKeyframes.RemoveAll([](const VMDIKKeyframe& Key) { return Key.FrameNumber > LastFrame; });
			FMMDAnimationImportContext Context;
			FMMDAnimationImportReport ImportReport;
			FMMDAnimationImportSettings Settings;
			const FString LogicalPMX = FPaths::GetCleanFilename(FPaths::GetPath(Sample.Directory)) + TEXT(".pmx");
			// 缓存过期时使用新包名，避免在尚未完整加载的旧包上覆盖保存。
			const FString PreviewName = FPaths::FileExists(CachedFilename)
				? TEXT("MMDStagePreview8s_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) : TEXT("MMDStagePreview8s");
			const FString LogicalVMD = FPaths::GetPath(Sample.SourceVMDPath) / (PreviewName + TEXT(".vmd"));
			FString ProposedFilename;
			if (!TMMDMeshBuilder::BuildAnimationImportContext(Sample.Mesh.Get(), nullptr, LogicalPMX, LogicalVMD, Context, &ImportReport)
				|| !TMMDMeshBuilder::AnalyzeVMDAnimationImport(Motion, Context, Settings, ImportReport)
				|| !ImportReport.PackagePath.StartsWith(Sample.Mount)
				|| !FPackageName::TryConvertLongPackageNameToFilename(ImportReport.PackagePath / ImportReport.AssetName,
					ProposedFilename, FPackageName::GetAssetPackageExtension())
				|| !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(ProposedFilename), Sample.Directory))
			{
				Error = TEXT("Stage preview animation must remain in the Saved character sandbox");
				return false;
			}
			Sample.Animation.Reset(TMMDMeshBuilder::BuildVMDAnimation(Motion, Context, Settings, &ImportReport));
			if (!Sample.Animation.IsValid() || ImportReport.HasErrors())
			{
				Error = TEXT("Stage preview animation could not be built");
				return false;
			}
			FString PreviewFilename;
			if (!FPackageName::TryConvertLongPackageNameToFilename(Sample.Animation->GetOutermost()->GetName(),
				PreviewFilename, FPackageName::GetAssetPackageExtension()))
			{
				Error = TEXT("Stage preview animation package could not be resolved");
				return false;
			}
			PreviewFilename = FPaths::ConvertRelativePathToFull(PreviewFilename);
			FPaths::CollapseRelativeDirectories(PreviewFilename);
			if (!FPaths::IsUnderDirectory(PreviewFilename, Sample.Directory) || !FPaths::FileExists(PreviewFilename))
			{
				Error = TEXT("Stage preview animation was not saved into the character sandbox");
				return false;
			}
			Sample.Animation->WaitOnExistingCompression();
			if (!Sample.Animation->IsBoneCompressedDataValid() || Sample.Animation->GetPlayLength() < 6.5f)
			{
				Error = TEXT("Stage preview animation duration or compression is invalid");
				return false;
			}
			Sample.AnimationPath = Sample.Animation->GetPathName();
			Sample.BeautyAnimationTime = 2.6f;
			Sample.MotionAnimationTime = 6.4f;
		}
		return true;
	}

	TArray<FTransform> ReadStageInstanceTransforms(AMMDStageActor* Stage)
	{
		TArray<FTransform> Transforms;
		TInlineComponentArray<UInstancedStaticMeshComponent*> Components(Stage);
		for (UInstancedStaticMeshComponent* Component : Components)
		{
			for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				if (Component->GetInstanceTransform(Index, Transform, true))
				{
					Transforms.Add(Transform);
				}
			}
		}
		return Transforms;
	}

	void WriteCaptureLighting(USceneCaptureComponent2D* Capture)
	{
		if (UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get())
		{
			// 常规 ViewExtension 排除了 SceneCapture，后台截图显式提交同一世界的灯光。
			const FVector CameraPosition = Capture->GetComponentLocation();
			Lights->CollectLightsForFrame(Capture->GetWorld(), &CameraPosition);
			ENQUEUE_RENDER_COMMAND(MMDStageCaptureLights)(
				[Lights](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("MMDStageCaptureLights"));
					Lights->WriteLightData_RenderThread(GraphBuilder);
					GraphBuilder.Execute();
				});
			FlushRenderingCommands();
		}
	}

	bool ReadCaptureLighting(const UMMDStageRenderPreset* Preset, TSharedRef<FJsonObject> Result)
	{
		UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
		UTextureRenderTarget2D* Target = Lights ? Lights->GetLightDataRenderTarget() : nullptr;
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FLinearColor> Pixels;
		const bool bRead = Resource && Target->SizeX == 64 && Target->SizeY == 2
			&& Resource->ReadLinearColorPixels(Pixels, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), FIntRect(0, 0, 64, 2))
			&& Pixels.Num() == 128;
		int32 DirectionalCount = 0;
		if (bRead)
		{
			for (int32 Slot = 0; Slot < UMMDAnimeLightDataSubsystem::MaxLights; ++Slot)
			{
				const float Type = Pixels[Slot * 4].A;
				DirectionalCount += Type >= 2.5f && Type < 3.5f ? 1 : 0;
			}
		}
		const float ActiveSpan = bRead ? Pixels[64 + 18].R : 0.0f;
		const float MainIntensity = bRead ? Pixels[1].A : 0.0f;
		const bool bPassed = bRead && DirectionalCount == 3 && ActiveSpan >= 3.0f
			&& Pixels[64 + 18].A > 0.5f && Pixels[0].A >= 2.5f && Pixels[0].A < 3.5f
			&& FMath::IsNearlyEqual(MainIntensity, FMath::Clamp(Preset->KeyLightIntensity, 0.0f, 10.0f), 0.0001f);
		Result->SetBoolField(TEXT("gpu_lighting_readback"), bRead);
		Result->SetBoolField(TEXT("gpu_lighting_passed"), bPassed);
		Result->SetNumberField(TEXT("gpu_directional_lights"), DirectionalCount);
		Result->SetNumberField(TEXT("gpu_active_light_span"), ActiveSpan);
		Result->SetNumberField(TEXT("gpu_main_light_intensity"), MainIntensity);
		Result->SetStringField(TEXT("scene_shadow"), TEXT("No main view: custom shadow basis is invalid; native stage shadows and character fallback remain active"));
		return bPassed;
	}

	bool SaveCapture(USceneCaptureComponent2D* Capture, const FString& Filename, TArray<FColor>& Pixels, FString& Error)
	{
		WriteCaptureLighting(Capture);
		Capture->CaptureScene();
		FlushRenderingCommands();
		UTextureRenderTarget2D* Target = Capture->TextureTarget;
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Target->SizeX * Target->SizeY)
		{
			Error = TEXT("Native GPU readback failed");
			return false;
		}
		// 以两倍分辨率真实渲染后在线性空间降采样，保留细发束并减少单帧锯齿。
		const int32 ImageWidth = Target->SizeX / 2;
		const int32 ImageHeight = Target->SizeY / 2;
		TArray<FColor> Downsampled;
		Downsampled.SetNumUninitialized(ImageWidth * ImageHeight);
		for (int32 Y = 0; Y < ImageHeight; ++Y)
		{
			for (int32 X = 0; X < ImageWidth; ++X)
			{
				const int32 SourceIndex = 2 * Y * Target->SizeX + 2 * X;
				const FLinearColor Average = (FLinearColor(Pixels[SourceIndex]) + FLinearColor(Pixels[SourceIndex + 1])
					+ FLinearColor(Pixels[SourceIndex + Target->SizeX])
					+ FLinearColor(Pixels[SourceIndex + Target->SizeX + 1])) * 0.25f;
				Downsampled[Y * ImageWidth + X] = Average.ToFColorSRGB();
			}
		}
		Pixels = MoveTemp(Downsampled);
		int32 MinLuma = 255;
		int32 MaxLuma = 0;
		for (FColor& Pixel : Pixels)
		{
			const int32 Luma = (int32(Pixel.R) + Pixel.G + Pixel.B) / 3;
			MinLuma = FMath::Min(MinLuma, Luma);
			MaxLuma = FMath::Max(MaxLuma, Luma);
			Pixel.A = 255;
		}
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(ImageWidth, ImageHeight,
			TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
		if (MaxLuma - MinLuma < 12 || Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Filename))
		{
			Error = TEXT("Captured image was blank or could not be saved");
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> CapturePreset(const FStageDefinition& Definition, UMMDStageRenderPreset* Preset,
		const FCharacterSample& Sample, const FString& ScreenshotDirectory, bool bComparison = false)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), Definition.Name);
		Result->SetStringField(TEXT("preset"), Preset->GetPathName());
		Result->SetStringField(TEXT("character"), Sample.MeshPath);
		Result->SetStringField(TEXT("animation"), Sample.AnimationPath);
		Result->SetBoolField(TEXT("character_saved_into_plugin"), false);
		Result->SetBoolField(TEXT("controlled_material_comparison"), bComparison);
		FPreviewScene::ConstructionValues Values;
		Values.SetCreateDefaultLighting(false).SetEditor(true).SetCreatePhysicsScene(false).SetTransactional(false);
		FPreviewScene Scene(Values);
		UWorld* World = Scene.GetWorld();
		if (!World || !World->Scene || !World->Scene->GetSkeletalMeshUpdater())
		{
			Result->SetBoolField(TEXT("passed"), false);
			Result->SetStringField(TEXT("error"), TEXT("A rendering scene with skeletal mesh support is unavailable; the commandlet must run as a client with a real RHI"));
			return Result;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags = RF_Transient;
		AMMDStageActor* Stage = World->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Spawn);
		// 材质 usage 编译会重新注册 primitive，必须通过所属 actor 稳定取得预览 World。
		AActor* CharacterOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		AActor* CaptureOwner = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		TStrongObjectPtr<USkeletalMeshComponent> Character(CharacterOwner
			? NewObject<USkeletalMeshComponent>(CharacterOwner, TEXT("StageValidationCharacter"), RF_Transient) : nullptr);
		TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
		TStrongObjectPtr<USceneCaptureComponent2D> Capture(CaptureOwner
			? NewObject<USceneCaptureComponent2D>(CaptureOwner, TEXT("StageValidationCapture"), RF_Transient) : nullptr);
		if (!Stage || !Character.IsValid() || !Target.IsValid() || !Capture.IsValid())
		{
			Result->SetBoolField(TEXT("passed"), false);
			Result->SetStringField(TEXT("error"), TEXT("Preview world or component creation failed"));
			return Result;
		}
		CharacterOwner->SetRootComponent(Character.Get());
		CharacterOwner->AddInstanceComponent(Character.Get());
		CaptureOwner->SetRootComponent(Capture.Get());
		CaptureOwner->AddInstanceComponent(Capture.Get());
		Stage->Theme = Definition.Theme;
		Stage->ApplyStageRenderPreset(Preset);
		Stage->bUseManualTime = true;
		Stage->RebuildStage();
		Character->SetSkeletalMesh(Sample.Mesh.Get());
		Character->SetMobility(EComponentMobility::Movable);
		Character->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Character->SetCastShadow(true);
		Character->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Scene.AddComponent(Character.Get(), FTransform(FRotator(0, 180, 0), FVector(0, -55, 2)));
		Character->SetAnimation(Sample.Animation.Get());
		Character->SetUpdateAnimationInEditor(true);
		const TArray<TObjectPtr<UMaterialInterface>> OriginalOverrides = Character->OverrideMaterials;
		const bool bOriginalShadowInclude = Character->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")));
		const bool bOriginalShadowExclude = Character->ComponentTags.Contains(FName(TEXT("MMDShadowExclude")));
		const bool bCharacterPreset = Stage->ApplyCharacterRenderPreset(Character.Get(), true);
		const bool bShadowOn = Character->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")));
		const bool bSavedSelfShadow = Preset->bCharacterSelfShadow;
		Preset->bCharacterSelfShadow = false;
		const bool bShadowOff = Stage->ApplyCharacterRenderPreset(Character.Get(), true)
			&& !Character->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")))
			&& Character->ComponentTags.Contains(FName(TEXT("MMDShadowExclude")));
		Preset->bCharacterSelfShadow = bSavedSelfShadow;
		const bool bShadowReapplied = Stage->ApplyCharacterRenderPreset(Character.Get(), true)
			&& Character->ComponentTags.Contains(FName(TEXT("MMDShadowInclude"))) == bSavedSelfShadow
			&& Character->ComponentTags.Contains(FName(TEXT("MMDShadowExclude"))) == (bSavedSelfShadow ? bOriginalShadowExclude : true);
		const bool bSelfShadowTogglePassed = bShadowOn == bSavedSelfShadow && bShadowOff && bShadowReapplied;
		Result->SetBoolField(TEXT("self_shadow_toggle_passed"), bSelfShadowTogglePassed);
		UMMDStageMaterialState* RoleState = Cast<UMMDStageMaterialState>(Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
		int32 RoleCounts[6] = {};
		int32 HairSlot = INDEX_NONE;
		int32 ClothSlot = INDEX_NONE;
		TArray<TSharedPtr<FJsonValue>> RoleItems;
		if (RoleState)
		{
			for (int32 Slot = 0; Slot < RoleState->ResolvedRoles.Num(); ++Slot)
			{
				const int32 Role = static_cast<int32>(RoleState->ResolvedRoles[Slot]);
				if (Role >= 0 && Role < 6) ++RoleCounts[Role];
				if (Role == 2 && HairSlot == INDEX_NONE) HairSlot = Slot;
				if (Role == 3 && ClothSlot == INDEX_NONE) ClothSlot = Slot;
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetNumberField(TEXT("slot"), Slot);
				Item->SetNumberField(TEXT("role"), Role);
				Item->SetStringField(TEXT("reason"), RoleState->RoleReasons.IsValidIndex(Slot) ? RoleState->RoleReasons[Slot] : TEXT("missing"));
				Item->SetStringField(TEXT("material"), RoleState->SourceMaterials.IsValidIndex(Slot) && RoleState->SourceMaterials[Slot]
					? RoleState->SourceMaterials[Slot]->GetPathName() : TEXT("missing"));
				RoleItems.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
		TSharedRef<FJsonObject> RoleSummary = MakeShared<FJsonObject>();
		const TCHAR* RoleNames[] = { TEXT("Generic"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"), TEXT("Eye") };
		for (int32 Index = 0; Index < 6; ++Index) RoleSummary->SetNumberField(RoleNames[Index], RoleCounts[Index]);
		Result->SetObjectField(TEXT("material_role_counts"), RoleSummary);
		Result->SetArrayField(TEXT("material_roles"), RoleItems);
		TSharedRef<FJsonObject> ClothParameters = MakeShared<FJsonObject>();
		ClothParameters->SetNumberField(TEXT("slot"), ClothSlot);
		UMaterialInterface* ClothMaterial = ClothSlot != INDEX_NONE ? Character->GetMaterial(ClothSlot) : nullptr;
		for (const TCHAR* Name : { TEXT("BlackLift"), TEXT("SoftSpecularStrength"), TEXT("SpecularStrength"), TEXT("Roughness"), TEXT("AmbientStrength") })
		{
			float Value = 0.0f;
			if (ClothMaterial && ClothMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(Name), Value))
				ClothParameters->SetNumberField(Name, Value);
		}
		Result->SetObjectField(TEXT("cloth_shader_parameters"), ClothParameters);
		Result->SetNumberField(TEXT("film_toe"), Preset->FilmToe);
		const bool bRolesValid = RoleState && RoleCounts[1] > 0 && RoleCounts[2] > 0 && RoleCounts[3] > 0 && RoleCounts[4] > 0 && RoleCounts[5] > 0;
		float OverrideRole = -1.0f;
		float ClearedRole = -1.0f;
		const bool bOverrideSet = HairSlot != INDEX_NONE && UMMDStageLibrary::SetCharacterMaterialRoleOverride(Character.Get(), HairSlot, EMMDMaterialRole::Metal);
		UMaterialInterface* OverrideMaterial = HairSlot != INDEX_NONE ? Character->GetMaterial(HairSlot) : nullptr;
		const bool bOverrideValue = OverrideMaterial && OverrideMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MaterialRole")), OverrideRole)
			&& OverrideRole == 4.0f;
		const bool bOverrideCleared = HairSlot != INDEX_NONE && UMMDStageLibrary::ClearCharacterMaterialRoleOverride(Character.Get(), HairSlot);
		UMaterialInterface* ClearedMaterial = HairSlot != INDEX_NONE ? Character->GetMaterial(HairSlot) : nullptr;
		const bool bClearedValue = ClearedMaterial && ClearedMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MaterialRole")), ClearedRole)
			&& ClearedRole == 2.0f;
		const bool bRoleOverridePassed = bOverrideSet && bOverrideValue && bOverrideCleared && bClearedValue
			&& UMMDStageLibrary::GetResolvedCharacterMaterialRole(Character.Get(), HairSlot) == EMMDMaterialRole::Hair;
		Result->SetBoolField(TEXT("material_roles_passed"), bRolesValid);
		Result->SetBoolField(TEXT("role_override_clear_passed"), bRoleOverridePassed);
		Result->SetNumberField(TEXT("role_override_slot"), HairSlot);
		Result->SetNumberField(TEXT("role_override_mid_value"), OverrideRole);
		Result->SetNumberField(TEXT("role_clear_mid_value"), ClearedRole);
		// 用真实导入槽验证可逆深度策略；软阴影层和整体淡出继续保持源透明语义。
		struct FMaskedHairFixtureSlot
		{
			const TCHAR* Name;
			EMMDMaterialRole Role;
			EBlendMode SourceBlend;
			float SourceAlpha;
			bool bDepthHair;
		};
		const FMaskedHairFixtureSlot HairFixtureSlots[] =
		{
			{ TEXT("Hair01"), EMMDMaterialRole::Hair, BLEND_Translucent, 1.0f, true },
			{ TEXT("Hair03"), EMMDMaterialRole::Hair, BLEND_Translucent, 1.0f, true },
			{ TEXT("Hair02"), EMMDMaterialRole::Hair, BLEND_Opaque, 1.0f, false },
			{ TEXT("eyeball"), EMMDMaterialRole::Eye, BLEND_Opaque, 1.0f, false },
			{ TEXT("Hairshadow"), EMMDMaterialRole::Generic, BLEND_Translucent, 1.0f, false },
			{ TEXT("face02"), EMMDMaterialRole::Skin, BLEND_Translucent, 1.0f, false },
			{ TEXT("Facial01"), EMMDMaterialRole::Skin, BLEND_Translucent, 1.0f, false },
			{ TEXT("Facial02"), EMMDMaterialRole::Skin, BLEND_Translucent, 1.0f, false },
			{ TEXT("hairclip"), EMMDMaterialRole::Metal, BLEND_Translucent, 0.0f, false }
		};
		const TArray<TObjectPtr<UMaterialInterface>> SourceSnapshot = RoleState ? RoleState->SourceMaterials : TArray<TObjectPtr<UMaterialInterface>>();
		const TArray<TObjectPtr<UMaterialInterface>> OverrideSnapshot = RoleState ? RoleState->SourceOverrides : TArray<TObjectPtr<UMaterialInterface>>();
		TArray<int32> HairFixtureSlotIndices;
		TArray<TSharedPtr<FJsonValue>> HairDepthItems;
		bool bMaskedHairDepthPassed = RoleState && GetDefault<UMMDStageRenderPreset>()->bUseMaskedHair && Preset->bUseMaskedHair;
		const auto RecordMaterialBlend = [&](int32 Slot, const TSharedRef<FJsonObject>& Item, const TCHAR* Phase) -> UMaterialInterface*
		{
			UMaterialInterface* Material = Slot != INDEX_NONE ? Character->GetMaterial(Slot) : nullptr;
			Item->SetNumberField(FString(Phase) + TEXT("_blend"), Material ? static_cast<int32>(Material->GetBlendMode()) : INDEX_NONE);
			Item->SetStringField(FString(Phase) + TEXT("_parent"), Material && Material->GetMaterial() ? Material->GetMaterial()->GetPathName() : TEXT("missing"));
			return Material;
		};
		const auto MatchesExpectedParent = [](UMaterialInterface* Material, EBlendMode ExpectedBlend, const UMMDStageRenderPreset* AppliedPreset)
		{
			UMaterialInterface* ExpectedParent = ExpectedBlend == BLEND_Masked ? AppliedPreset->CharacterMaterialMasked.Get()
				: ExpectedBlend == BLEND_Translucent ? AppliedPreset->CharacterMaterialTranslucent.Get() : AppliedPreset->CharacterMaterialOpaque.Get();
			return Material && ExpectedParent && Material->GetMaterial() == ExpectedParent->GetMaterial();
		};
		for (const FMaskedHairFixtureSlot& Fixture : HairFixtureSlots)
		{
			int32 Slot = INDEX_NONE;
			for (int32 Index = 0; Index < Sample.Mesh->GetMaterials().Num(); ++Index)
			{
				const FSkeletalMaterial& MaterialSlot = Sample.Mesh->GetMaterials()[Index];
				UMaterialInterface* Source = SourceSnapshot.IsValidIndex(Index) ? SourceSnapshot[Index].Get() : nullptr;
				if (MaterialSlot.MaterialSlotName.ToString().Equals(Fixture.Name, ESearchCase::IgnoreCase)
					|| (Source && Source->GetName().Equals(Fixture.Name, ESearchCase::IgnoreCase)))
				{
					Slot = Index;
					break;
				}
			}
			HairFixtureSlotIndices.Add(Slot);
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			UMaterialInterface* Source = SourceSnapshot.IsValidIndex(Slot) ? SourceSnapshot[Slot].Get() : nullptr;
			FLinearColor SourceBase = FLinearColor::White;
			const bool bSourceBaseRead = Source && (Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), SourceBase)
				|| Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), SourceBase));
			float SourceOpacity = SourceBase.A;
			const bool bSourceOpacityRead = Source && Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), SourceOpacity);
			const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Character.Get(), Slot);
			Item->SetStringField(TEXT("name"), Fixture.Name);
			Item->SetNumberField(TEXT("slot"), Slot);
			Item->SetStringField(TEXT("source"), Source ? Source->GetPathName() : TEXT("missing"));
			Item->SetNumberField(TEXT("source_blend"), Source ? static_cast<int32>(Source->GetBlendMode()) : INDEX_NONE);
			Item->SetNumberField(TEXT("role"), static_cast<int32>(Role));
			Item->SetNumberField(TEXT("source_base_alpha"), SourceBase.A);
			Item->SetNumberField(TEXT("source_opacity"), SourceOpacity);
			Item->SetBoolField(TEXT("source_base_read"), bSourceBaseRead);
			Item->SetBoolField(TEXT("source_opacity_read"), bSourceOpacityRead);
			const EBlendMode ExpectedBlend = Fixture.bDepthHair ? BLEND_Masked : Fixture.SourceBlend;
			Item->SetNumberField(TEXT("expected_enabled_blend"), static_cast<int32>(ExpectedBlend));
			UMaterialInterface* Applied = RecordMaterialBlend(Slot, Item, TEXT("enabled"));
			const bool bParentMatches = MatchesExpectedParent(Applied, ExpectedBlend, Preset);
			Item->SetBoolField(TEXT("enabled_parent_matches"), bParentMatches);
			const bool bSlotPassed = Source && bSourceBaseRead && bSourceOpacityRead && Source->GetBlendMode() == Fixture.SourceBlend
				&& FMath::IsNearlyEqual(SourceBase.A, Fixture.SourceAlpha) && FMath::IsNearlyEqual(SourceOpacity, Fixture.SourceAlpha)
				&& Role == Fixture.Role && Applied && Applied->GetBlendMode() == ExpectedBlend && bParentMatches;
			Item->SetBoolField(TEXT("enabled_passed"), bSlotPassed);
			bMaskedHairDepthPassed &= bSlotPassed;
			HairDepthItems.Add(MakeShared<FJsonValueObject>(Item));
		}
		TStrongObjectPtr<UMMDStageRenderPreset> HairFadePreset(DuplicateObject<UMMDStageRenderPreset>(Preset, GetTransientPackage()));
		bool bHairFadeApplied = false;
		if (HairFadePreset.IsValid())
		{
			HairFadePreset->SetFlags(RF_Transient);
			HairFadePreset->ClearFlags(RF_Public | RF_Standalone);
			HairFadePreset->bUseMaskedHair = false;
			bHairFadeApplied = UMMDStageLibrary::ApplyCharacterRenderPreset(Character.Get(), HairFadePreset.Get(), true);
		}
		for (int32 Index = 0; Index < HairDepthItems.Num(); ++Index)
		{
			const TSharedRef<FJsonObject> Item = HairDepthItems[Index]->AsObject().ToSharedRef();
			UMaterialInterface* Applied = RecordMaterialBlend(HairFixtureSlotIndices[Index], Item, TEXT("disabled"));
			const bool bParentMatches = HairFadePreset.IsValid() && MatchesExpectedParent(Applied, HairFixtureSlots[Index].SourceBlend, HairFadePreset.Get());
			Item->SetBoolField(TEXT("disabled_parent_matches"), bParentMatches);
			const bool bSlotPassed = bHairFadeApplied && Applied && Applied->GetBlendMode() == HairFixtureSlots[Index].SourceBlend && bParentMatches;
			Item->SetBoolField(TEXT("disabled_passed"), bSlotPassed);
			bMaskedHairDepthPassed &= bSlotPassed;
		}
		// 恢复原预设后再截图，不让验证副本改变后续姿势和风格对比。
		const bool bHairPresetRestored = UMMDStageLibrary::ApplyCharacterRenderPreset(Character.Get(), Preset, true);
		for (int32 Index = 0; Index < HairDepthItems.Num(); ++Index)
		{
			const TSharedRef<FJsonObject> Item = HairDepthItems[Index]->AsObject().ToSharedRef();
			UMaterialInterface* Applied = RecordMaterialBlend(HairFixtureSlotIndices[Index], Item, TEXT("restored"));
			const EBlendMode ExpectedBlend = HairFixtureSlots[Index].bDepthHair ? BLEND_Masked : HairFixtureSlots[Index].SourceBlend;
			const bool bParentMatches = MatchesExpectedParent(Applied, ExpectedBlend, Preset);
			UMaterialInterface* Source = SourceSnapshot.IsValidIndex(HairFixtureSlotIndices[Index]) ? SourceSnapshot[HairFixtureSlotIndices[Index]].Get() : nullptr;
			FLinearColor SourceBase = FLinearColor::White;
			const bool bSourceBaseRead = Source && (Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), SourceBase)
				|| Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), SourceBase));
			float SourceOpacity = SourceBase.A;
			const bool bSourceUnchanged = Source && Source->GetBlendMode() == HairFixtureSlots[Index].SourceBlend && bSourceBaseRead
				&& Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), SourceOpacity)
				&& FMath::IsNearlyEqual(SourceBase.A, HairFixtureSlots[Index].SourceAlpha) && FMath::IsNearlyEqual(SourceOpacity, HairFixtureSlots[Index].SourceAlpha);
			float RestoredRole = -1.0f;
			const bool bRoleRestored = Applied && Applied->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("MaterialRole")), RestoredRole)
				&& RestoredRole == static_cast<float>(HairFixtureSlots[Index].Role);
			Item->SetBoolField(TEXT("restored_parent_matches"), bParentMatches);
			Item->SetBoolField(TEXT("source_semantics_unchanged"), bSourceUnchanged);
			Item->SetNumberField(TEXT("restored_role_value"), RestoredRole);
			const bool bSlotPassed = bHairPresetRestored && Applied && Applied->GetBlendMode() == ExpectedBlend && bParentMatches && bSourceUnchanged && bRoleRestored;
			Item->SetBoolField(TEXT("restored_passed"), bSlotPassed);
			bMaskedHairDepthPassed &= bSlotPassed;
		}
		const bool bHairSourceSnapshotUnchanged = RoleState && RoleState->SourceMaterials == SourceSnapshot && RoleState->SourceOverrides == OverrideSnapshot;
		bMaskedHairDepthPassed &= bHairSourceSnapshotUnchanged;
		Result->SetBoolField(TEXT("masked_hair_depth_enabled"), Preset->bUseMaskedHair);
		Result->SetBoolField(TEXT("masked_hair_depth_disabled_applied"), bHairFadeApplied);
		Result->SetBoolField(TEXT("masked_hair_depth_original_restored"), bHairPresetRestored);
		Result->SetBoolField(TEXT("masked_hair_source_snapshot_unchanged"), bHairSourceSnapshotUnchanged);
		Result->SetArrayField(TEXT("masked_hair_depth_slots"), HairDepthItems);
		Result->SetBoolField(TEXT("masked_hair_depth_passed"), bMaskedHairDepthPassed);
		UMMDOutlineLibrary::ApplyMMDOutlinePreset(Character.Get(), Preset);
		// FinalColorLDR 已由 tonemapper 编码；UNORM 配 2.2 避免 Gamma 1 压暗或 sRGB RTV 重复编码。
		Target->RenderTargetFormat = RTF_RGBA8;
		Target->bForceLinearGamma = true;
		Target->TargetGamma = 2.2f;
		Target->ClearColor = FLinearColor(0.005f, 0.009f, 0.022f, 1.0f);
		Target->InitAutoFormat(2560, 1440);
		Result->SetNumberField(TEXT("capture_display_gamma"), Target->GetDisplayGamma());
		Target->UpdateResourceImmediate(true);
		Capture->TextureTarget = Target.Get();
		Capture->CaptureSource = SCS_FinalColorLDR;
		Capture->bCaptureEveryFrame = false;
		Capture->bCaptureOnMovement = false;
		Capture->bAlwaysPersistRenderingState = true;
		Capture->FOVAngle = 55.0f;
		Capture->PostProcessBlendWeight = 1.0f;
		FPostProcessSettings& PP = Capture->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = Preset->ExposureBias;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = Preset->BloomIntensity;
		PP.bOverride_BloomThreshold = true;
		PP.BloomThreshold = Definition.Theme == EMMDStageTheme::SakuraTheater ? 0.6f : 0.7f;
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FVector4(Preset->ColorGradingSaturation, Preset->ColorGradingSaturation, Preset->ColorGradingSaturation, 1.0f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(Preset->ColorGradingContrast, Preset->ColorGradingContrast, Preset->ColorGradingContrast, 1.0f);
		PP.bOverride_ColorGamma = true;
		PP.ColorGamma = FVector4(Preset->ColorGradingGamma, Preset->ColorGradingGamma, Preset->ColorGradingGamma, 1.0f);
		PP.bOverride_ColorGain = true;
		PP.ColorGain = FVector4(Preset->ColorGradingGain.R, Preset->ColorGradingGain.G, Preset->ColorGradingGain.B, 1.0f);
		PP.bOverride_WhiteTemp = true;
		PP.WhiteTemp = Preset->WhiteTemperature;
		PP.bOverride_FilmToe = true;
		PP.FilmToe = Preset->FilmToe;
		PP.bOverride_FilmSlope = true;
		PP.FilmSlope = 0.86f;
		PP.bOverride_ScreenSpaceReflectionIntensity = true;
		PP.ScreenSpaceReflectionIntensity = 85.0f;
		PP.bOverride_ScreenSpaceReflectionQuality = true;
		PP.ScreenSpaceReflectionQuality = 75.0f;
		PP.bOverride_ScreenSpaceReflectionMaxRoughness = true;
		PP.ScreenSpaceReflectionMaxRoughness = 0.65f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = Definition.Theme == EMMDStageTheme::SakuraTheater ? 0.20f : 0.12f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.0f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
		const bool bGardenCapture = Definition.Theme == EMMDStageTheme::SakuraTheater;
		const FVector CameraLocation = bGardenCapture ? FVector(65, -440, 145) : FVector(0, -900, 285);
		const FVector CameraTarget = bGardenCapture ? FVector(0, -5, 75) : FVector(0, 150, 190);
		Capture->SetWorldLocationAndRotation(CameraLocation, (CameraTarget - CameraLocation).Rotation());
		if (bGardenCapture)
		{
			Capture->FOVAngle = 50.0f;
			PP.bOverride_DepthOfFieldFocalDistance = true;
			PP.DepthOfFieldFocalDistance = FVector::Distance(CameraLocation, CameraTarget);
			PP.bOverride_DepthOfFieldFstop = true;
			PP.DepthOfFieldFstop = 4.0f;
			PP.bOverride_DepthOfFieldSensorWidth = true;
			PP.DepthOfFieldSensorWidth = 36.0f;
			Capture->PostProcessSettings = PP;
		}
		Scene.AddComponent(Capture.Get(), Capture->GetComponentTransform());
		Stage->EvaluateStageAtTime(0.0f);
		Capture->CaptureScene();
		FlushRenderingCommands();
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInterface* Material = Character->GetMaterial(Slot))
			{
				Material->SetForceMipLevelsToBeResident(false, true, 30.0f, 0, true);
			}
		}
		IStreamingManager::Get().StreamAllResources(30.0f);
		if (bGardenCapture && Stage->GardenSkyLight)
		{
			Stage->GardenSkyLight->RecaptureSky();
			USkyLightComponent::UpdateSkyCaptureContents(World);
			FlushRenderingCommands();
			World->SendAllEndOfFrameUpdates();
			FlushRenderingCommands();
			const FTexture* ProcessedSky = Stage->GardenSkyLight->GetProcessedSkyTexture();
			const bool bSkyReady = ProcessedSky && ProcessedSky->TextureRHI.IsValid();
			Result->SetBoolField(TEXT("sky_capture_resource_ready"), bSkyReady);
			if (!bSkyReady)
			{
				Result->SetBoolField(TEXT("passed"), false);
				Result->SetStringField(TEXT("error"), TEXT("庭院天空卷积资源未就绪"));
				return Result;
			}
		}
		Character->MarkRenderStateDirty();
		World->SendAllEndOfFrameUpdates();
		FlushRenderingCommands();
		// Shader usage 刷新可能重新注册组件，编译结束后再次绑定实际 VMD 资产。
		UMMDPhysicsPlaybackAnimInstance* PhysicsPlayback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(
			Character.Get(), Sample.Animation.Get(), false, true);
		Character->SetUpdateAnimationInEditor(true);
		UAnimSingleNodeInstance* AnimationInstance = Character->GetSingleNodeInstance();
		const bool bAnimationInstanceValid = AnimationInstance != nullptr;
		const bool bAnimationAssetBound = AnimationInstance && AnimationInstance->GetCurrentAsset() == Sample.Animation.Get();
		bool bPhysicsInitialized = false;
		int32 PhysicsChangedBones = 0;
		auto EvaluateCharacter = [&Character, PhysicsPlayback](float AnimationTime)
		{
			if (PhysicsPlayback)
			{
				PhysicsPlayback->ResetPhysics();
				PhysicsPlayback->SetPlaying(true);
			}
			// 顺序推进固定步长，截图包含真实 Bullet 摆动，跳帧不会把刚体遗留在旧姿态。
			const int32 Steps = FMath::Max(1, FMath::RoundToInt(AnimationTime * 60.0f));
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				++GFrameCounter;
				Character->SetPosition(AnimationTime * (Step - 1) / Steps, false);
				Character->TickAnimation(AnimationTime / Steps, false);
				Character->RefreshBoneTransforms();
				Character->HandleExistingParallelEvaluationTask(true, true);
			}
			Character->UpdateComponentToWorld();
			Character->MarkRenderDynamicDataDirty();
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Character.Get());
		};
		TArray<TSharedPtr<FJsonValue>> Captures;
		TArray<FColor> FirstPixels;
		TArray<FTransform> FirstTransforms;
		TArray<FTransform> FirstBoneTransforms;
		bool bPassed = bCharacterPreset && bRolesValid && bRoleOverridePassed && bMaskedHairDepthPassed && bAnimationInstanceValid && bAnimationAssetBound && PhysicsPlayback
			&& PhysicsPlayback->IsPhysicsEnabled() && PhysicsPlayback->GetPhysicsRigidBodyCount() > 0;
		int32 ChangedTransforms = 0;
		int32 ChangedBones = 0;
		double ChangedPixelFraction = 0.0;
		const int32 TimeCount = bComparison ? 0 : 2;
		for (int32 TimeIndex = 0; TimeIndex < TimeCount; ++TimeIndex)
		{
			const float Time = TimeIndex == 0 ? 0.0f : 1.6f;
			World->TimeSeconds = Time;
			World->UnpausedTimeSeconds = Time;
			World->RealTimeSeconds = Time;
			Stage->EvaluateStageAtTime(Time);
			const float AnimationTime = TimeIndex == 0 ? Sample.BeautyAnimationTime : Sample.MotionAnimationTime;
			EvaluateCharacter(AnimationTime);
			World->SendAllEndOfFrameUpdates();
			const TArray<FTransform> Transforms = ReadStageInstanceTransforms(Stage);
			const TArray<FTransform>& BoneTransforms = Character->GetComponentSpaceTransforms();
			if (TimeIndex == 0)
			{
				FirstTransforms = Transforms;
				FirstBoneTransforms = BoneTransforms;
				bPhysicsInitialized = PhysicsPlayback && PhysicsPlayback->IsPhysicsInitialized();
				if (PhysicsPlayback)
				{
					PhysicsPlayback->SetPhysicsEnabled(false);
					EvaluateCharacter(AnimationTime);
					const TArray<FTransform>& NoPhysicsBones = Character->GetComponentSpaceTransforms();
					for (int32 Index = 0; Index < FMath::Min(FirstBoneTransforms.Num(), NoPhysicsBones.Num()); ++Index)
					{
						PhysicsChangedBones += !FirstBoneTransforms[Index].Equals(NoPhysicsBones[Index], 0.001f) ? 1 : 0;
					}
					PhysicsPlayback->SetPhysicsEnabled(true);
					EvaluateCharacter(AnimationTime);
					World->SendAllEndOfFrameUpdates();
				}
			}
			else
			{
				for (int32 Index = 0; Index < FMath::Min(FirstTransforms.Num(), Transforms.Num()); ++Index)
				{
					ChangedTransforms += !FirstTransforms[Index].Equals(Transforms[Index], 0.001f) ? 1 : 0;
				}
				for (int32 Index = 0; Index < FMath::Min(FirstBoneTransforms.Num(), BoneTransforms.Num()); ++Index)
				{
					ChangedBones += !FirstBoneTransforms[Index].Equals(BoneTransforms[Index], 0.001f) ? 1 : 0;
				}
			}
			const FString ImageName = FString(Definition.ImageName) + (TimeIndex ? TEXT("_time2") : TEXT("")) + TEXT(".png");
			const FString Filename = FPaths::Combine(ScreenshotDirectory, ImageName);
			FString Error;
			TArray<FColor> Pixels;
			// 两次提交可刷新更新后的 primitive 和 TAA 历史，读回的始终是原生 UE 画面。
			Capture->CaptureScene();
			FlushRenderingCommands();
			const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, Error);
			if (TimeIndex == 0)
			{
				FirstPixels = Pixels;
				bPassed &= ReadCaptureLighting(Preset, Result);
			}
			else if (Pixels.Num() == FirstPixels.Num() && !Pixels.IsEmpty())
			{
				int32 ChangedPixels = 0;
				for (int32 Index = 0; Index < Pixels.Num(); ++Index)
				{
					ChangedPixels += Pixels[Index] != FirstPixels[Index] ? 1 : 0;
				}
				ChangedPixelFraction = double(ChangedPixels) / Pixels.Num();
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("output"), Filename);
			Item->SetNumberField(TEXT("time_seconds"), Time);
			Item->SetNumberField(TEXT("animation_time_seconds"), AnimationTime);
			Item->SetBoolField(TEXT("passed"), bSaved);
			if (!Error.IsEmpty())
			{
				Item->SetStringField(TEXT("error"), Error);
			}
			Captures.Add(MakeShared<FJsonValueObject>(Item));
			bPassed &= bSaved;
		}
		{
			World->TimeSeconds = 0.0f;
			World->UnpausedTimeSeconds = 0.0f;
			World->RealTimeSeconds = 0.0f;
			Stage->EvaluateStageAtTime(0.0f);
			EvaluateCharacter(Sample.BeautyAnimationTime);
			if (bGardenCapture)
			{
				World->SendAllEndOfFrameUpdates();
				const FVector HeroCamera(65, -440, 145), HeroTarget(0, -5, 75);
				Capture->FOVAngle = 50.0f;
				Capture->SetWorldLocationAndRotation(HeroCamera, (HeroTarget - HeroCamera).Rotation());
				PP.bOverride_DepthOfFieldFocalDistance = true;
				PP.DepthOfFieldFocalDistance = FVector::Distance(HeroCamera, HeroTarget);
				PP.bOverride_DepthOfFieldFstop = true;
				PP.DepthOfFieldFstop = 4.0f;
				PP.bOverride_DepthOfFieldSensorWidth = true;
				PP.DepthOfFieldSensorWidth = 36.0f;
				Capture->PostProcessSettings = PP;
				Capture->bCameraCutThisFrame = true;
				const FString Filename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_hero.png"));
				FString CaptureError;
				TArray<FColor> Pixels;
				const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, CaptureError);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("output"), Filename);
				Item->SetStringField(TEXT("framing"), TEXT("hero_full_body"));
				Item->SetNumberField(TEXT("time_seconds"), 0.0f);
				Item->SetNumberField(TEXT("animation_time_seconds"), Sample.BeautyAnimationTime);
				Item->SetBoolField(TEXT("passed"), bSaved);
				if (!CaptureError.IsEmpty()) Item->SetStringField(TEXT("error"), CaptureError);
				Captures.Add(MakeShared<FJsonValueObject>(Item));
				bPassed &= bSaved;
			}
			if (bComparison)
			{
				FirstBoneTransforms = Character->GetComponentSpaceTransforms();
				bPhysicsInitialized = PhysicsPlayback && PhysicsPlayback->IsPhysicsInitialized();
				if (PhysicsPlayback)
				{
					PhysicsPlayback->SetPhysicsEnabled(false);
					EvaluateCharacter(Sample.BeautyAnimationTime);
					const TArray<FTransform>& NoPhysicsBones = Character->GetComponentSpaceTransforms();
					for (int32 Index = 0; Index < FMath::Min(FirstBoneTransforms.Num(), NoPhysicsBones.Num()); ++Index)
						PhysicsChangedBones += !FirstBoneTransforms[Index].Equals(NoPhysicsBones[Index], 0.001f) ? 1 : 0;
					PhysicsPlayback->SetPhysicsEnabled(true);
					EvaluateCharacter(Sample.BeautyAnimationTime);
				}
			}
			World->SendAllEndOfFrameUpdates();
			Capture->FOVAngle = 40.0f;
			const FVector DetailLocation(0, -330, 149);
			Capture->SetWorldLocationAndRotation(DetailLocation, (FVector(0, -55, 124) - DetailLocation).Rotation());
			if (bGardenCapture)
			{
				PP.DepthOfFieldFocalDistance = FVector::Distance(DetailLocation, FVector(0, -55, 124));
				Capture->PostProcessSettings = PP;
			}
			const FString Filename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_detail.png"));
			FString Error;
			TArray<FColor> Pixels;
			const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, Error);
			if (bComparison) bPassed &= ReadCaptureLighting(Preset, Result) && FMath::IsNearlyEqual(Sample.BeautyAnimationTime, 2.6f);
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("output"), Filename);
			Item->SetStringField(TEXT("framing"), TEXT("character_detail"));
			Item->SetNumberField(TEXT("time_seconds"), 0.0f);
			Item->SetNumberField(TEXT("animation_time_seconds"), Sample.BeautyAnimationTime);
			Item->SetBoolField(TEXT("passed"), bSaved);
			if (!Error.IsEmpty())
			{
				Item->SetStringField(TEXT("error"), Error);
			}
			Captures.Add(MakeShared<FJsonValueObject>(Item));
			bPassed &= bSaved;
		}
		if (bComparison || bGardenCapture)
		{
			int32 HeadIndex = INDEX_NONE;
			for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") })
			{
				HeadIndex = Character->GetBoneIndex(FName(Name));
				if (HeadIndex != INDEX_NONE) break;
			}
			bPassed &= HeadIndex != INDEX_NONE;
			if (HeadIndex != INDEX_NONE)
			{
				// 头骨跟随近景用于检查脸颊过渡；两个实际 VMD 姿态分别正视和抬头。
				const float FaceTimes[] = { 2.6f, 7.0f };
				for (int32 PoseIndex = 0; PoseIndex < UE_ARRAY_COUNT(FaceTimes); ++PoseIndex)
				{
					EvaluateCharacter(FaceTimes[PoseIndex]);
					World->SendAllEndOfFrameUpdates();
					const FVector HeadLocation = Character->GetBoneTransform(HeadIndex).GetLocation();
					const FVector FaceCamera = HeadLocation + (bGardenCapture ? FVector(38.0f, -118.0f, 17.0f) : FVector(15.0f, -118.0f, 15.0f));
					Capture->FOVAngle = 35.0f;
					Capture->SetWorldLocationAndRotation(FaceCamera, (HeadLocation + FVector(0.0f, -3.0f, 8.0f) - FaceCamera).Rotation());
					Capture->bCameraCutThisFrame = true;
					if (bGardenCapture)
					{
						PP.bOverride_DepthOfFieldFocalDistance = true;
						PP.DepthOfFieldFocalDistance = FVector::Distance(FaceCamera, HeadLocation + FVector(0, -3, 8));
						PP.bOverride_DepthOfFieldFstop = true;
						PP.DepthOfFieldFstop = 2.8f;
						PP.bOverride_DepthOfFieldSensorWidth = true;
						PP.DepthOfFieldSensorWidth = 36.0f;
					}
					const FString Suffix = PoseIndex == 0 ? TEXT("_face.png") : TEXT("_face_turn.png");
					const FString Filename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + Suffix);
					FString Error;
					TArray<FColor> Pixels;
					const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, Error);
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("output"), Filename);
					Item->SetStringField(TEXT("framing"), PoseIndex == 0 ? TEXT("face_detail") : TEXT("face_turn"));
					Item->SetNumberField(TEXT("time_seconds"), 0.0f);
					Item->SetNumberField(TEXT("animation_time_seconds"), FaceTimes[PoseIndex]);
					Item->SetBoolField(TEXT("passed"), bSaved);
					FLinearColor HeadForward = FLinearColor::Transparent, HeadRight = FLinearColor::Transparent, HeadCenter = FLinearColor::Transparent;
					bool bHeadFrameValid = false;
					for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
					{
						UMaterialInterface* Material = Character->GetMaterial(Slot);
						if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadPositionRadius), HeadCenter))
						{
							bHeadFrameValid = Material->GetVectorParameterValue(FMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), HeadForward)
								&& Material->GetVectorParameterValue(FMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), HeadRight)
								&& FMath::IsFinite(HeadCenter.A) && HeadCenter.A > 0.0f
								&& FMath::Abs(HeadForward.R * HeadRight.R + HeadForward.G * HeadRight.G + HeadForward.B * HeadRight.B) < 0.001f;
							break;
						}
					}
					Item->SetBoolField(TEXT("head_geometry_frame_valid"), bHeadFrameValid);
					Item->SetNumberField(TEXT("head_geometry_radius"), HeadCenter.A);
					Item->SetArrayField(TEXT("head_forward"), { MakeShared<FJsonValueNumber>(HeadForward.R), MakeShared<FJsonValueNumber>(HeadForward.G), MakeShared<FJsonValueNumber>(HeadForward.B) });
					bPassed &= bHeadFrameValid;
					if (!Error.IsEmpty()) Item->SetStringField(TEXT("error"), Error);
					Captures.Add(MakeShared<FJsonValueObject>(Item));
					bPassed &= bSaved;
					if (bGardenCapture && PoseIndex == 0)
					{
						const TArray<FColor> WithDetails = Pixels;
						TArray<FVector2f> OriginalStrengths;
						for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
						{
							FVector2f Strengths(-1.0f, -1.0f);
							if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot)))
							{
								if (Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("HairDetailStrength")), Strengths.X))
									Material->SetScalarParameterValue(TEXT("HairDetailStrength"), 0.0f);
								if (Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ClothDetailStrength")), Strengths.Y))
									Material->SetScalarParameterValue(TEXT("ClothDetailStrength"), 0.0f);
							}
							OriginalStrengths.Add(Strengths);
						}
						const FString DetailOffFilename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_face_details_off.png"));
						const bool bDetailOffSaved = SaveCapture(Capture.Get(), DetailOffFilename, Pixels, Error);
						int32 DetailChangedPixels = 0;
						for (int32 Index = 0; Index < FMath::Min(Pixels.Num(), WithDetails.Num()); ++Index)
							DetailChangedPixels += Pixels[Index] != WithDetails[Index] ? 1 : 0;
						TSharedRef<FJsonObject> DetailOffItem = MakeShared<FJsonObject>();
						DetailOffItem->SetStringField(TEXT("output"), DetailOffFilename);
						DetailOffItem->SetStringField(TEXT("framing"), TEXT("face_details_off"));
						DetailOffItem->SetNumberField(TEXT("detail_changed_pixels"), DetailChangedPixels);
						DetailOffItem->SetBoolField(TEXT("passed"), bDetailOffSaved && DetailChangedPixels > 1000);
						Captures.Add(MakeShared<FJsonValueObject>(DetailOffItem));
						bPassed &= bDetailOffSaved && DetailChangedPixels > 1000;
						for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
							if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot)))
							{
								if (OriginalStrengths[Slot].X >= 0) Material->SetScalarParameterValue(TEXT("HairDetailStrength"), OriginalStrengths[Slot].X);
								if (OriginalStrengths[Slot].Y >= 0) Material->SetScalarParameterValue(TEXT("ClothDetailStrength"), OriginalStrengths[Slot].Y);
							}
						for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
							if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot)))
								Material->SetVectorParameterValue(UMMDHeadBoneSubsystem::ParamHeadPositionRadius, FLinearColor::Transparent);
						const FString FallbackFilename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_geometry_fallback.png"));
						const bool bFallbackSaved = SaveCapture(Capture.Get(), FallbackFilename, Pixels, Error);
						TSharedRef<FJsonObject> FallbackItem = MakeShared<FJsonObject>();
						FallbackItem->SetStringField(TEXT("output"), FallbackFilename);
						FallbackItem->SetStringField(TEXT("framing"), TEXT("geometry_fallback"));
						FallbackItem->SetBoolField(TEXT("passed"), bFallbackSaved);
						Captures.Add(MakeShared<FJsonValueObject>(FallbackItem));
						bPassed &= bFallbackSaved && UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Character.Get());
					}
					if (PoseIndex == 0 && FString(Definition.Name) == TEXT("Reference_Genshin"))
					{
						// 同姿态关描边，区分原材质透明错误和外扩壳的着色影响。
						bPassed &= UMMDOutlineLibrary::ApplyMMDOutline(Character.Get(), false);
						const FString OutlineFilename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_face_no_outline.png"));
						const bool bOutlineSaved = SaveCapture(Capture.Get(), OutlineFilename, Pixels, Error);
						TSharedRef<FJsonObject> OutlineItem = MakeShared<FJsonObject>();
						OutlineItem->SetStringField(TEXT("output"), OutlineFilename);
						OutlineItem->SetStringField(TEXT("framing"), TEXT("face_no_outline"));
						OutlineItem->SetNumberField(TEXT("time_seconds"), 0.0f);
						OutlineItem->SetNumberField(TEXT("animation_time_seconds"), FaceTimes[PoseIndex]);
						OutlineItem->SetBoolField(TEXT("passed"), bOutlineSaved);
						Captures.Add(MakeShared<FJsonValueObject>(OutlineItem));
						bPassed &= bOutlineSaved && UMMDOutlineLibrary::ApplyMMDOutlinePreset(Character.Get(), Preset);
					}
				}
			}
		}
		if (bGardenCapture)
		{
			EvaluateCharacter(Sample.BeautyAnimationTime);
			const FVector ClothCamera(70, -205, 138), ClothTarget(0, -55, 125);
			Capture->SetWorldLocationAndRotation(ClothCamera, (ClothTarget - ClothCamera).Rotation());
			Capture->FOVAngle = 35.0f;
			Capture->bCameraCutThisFrame = true;
			PP.DepthOfFieldFocalDistance = FVector::Distance(ClothCamera, ClothTarget);
			PP.DepthOfFieldFstop = 5.6f;
			TArray<FVector2f> Strengths;
			for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
			{
				FVector2f DetailValues(-1.0f, -1.0f);
				if (UMaterialInterface* Material = Character->GetMaterial(Slot))
				{
					Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("HairDetailStrength")), DetailValues.X);
					Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ClothDetailStrength")), DetailValues.Y);
				}
				Strengths.Add(DetailValues);
			}
			for (int32 Mode = 0; Mode < 2; ++Mode)
			{
				if (Mode == 1)
					for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
						if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot)))
						{
							if (Strengths[Slot].X >= 0) Material->SetScalarParameterValue(TEXT("HairDetailStrength"), 0.0f);
							if (Strengths[Slot].Y >= 0) Material->SetScalarParameterValue(TEXT("ClothDetailStrength"), 0.0f);
						}
				const FString Filename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName)
					+ (Mode == 0 ? TEXT("_cloth.png") : TEXT("_cloth_details_off.png")));
				FString Error;
				TArray<FColor> Pixels;
				const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, Error);
				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("output"), Filename);
				Item->SetStringField(TEXT("framing"), Mode == 0 ? TEXT("cloth_detail") : TEXT("cloth_details_off"));
				Item->SetBoolField(TEXT("passed"), bSaved);
				Captures.Add(MakeShared<FJsonValueObject>(Item));
				bPassed &= bSaved;
			}
			for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
				if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot)))
				{
					if (Strengths[Slot].X >= 0) Material->SetScalarParameterValue(TEXT("HairDetailStrength"), Strengths[Slot].X);
					if (Strengths[Slot].Y >= 0) Material->SetScalarParameterValue(TEXT("ClothDetailStrength"), Strengths[Slot].Y);
				}
		}
		if (bGardenCapture && Stage->GardenSkyLight)
		{
			// 同一姿态逐项关灯，验证真实天空响应以及有效零灯数据不会生成假主光。
			int32 HeadIndex = INDEX_NONE;
			for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") })
			{
				HeadIndex = Character->GetBoneIndex(FName(Name));
				if (HeadIndex != INDEX_NONE) break;
			}
			if (HeadIndex != INDEX_NONE)
			{
				EvaluateCharacter(Sample.BeautyAnimationTime);
				const FVector HeadLocation = Character->GetBoneTransform(HeadIndex).GetLocation();
				const FVector FaceCamera = HeadLocation + FVector(38.0f, -118.0f, 17.0f);
				Capture->FOVAngle = 35.0f;
				Capture->SetWorldLocationAndRotation(FaceCamera, (HeadLocation + FVector(0.0f, -3.0f, 8.0f) - FaceCamera).Rotation());
				TInlineComponentArray<ULightComponent*> DirectLights(Stage);
				TArray<float> OriginalIntensities;
				for (ULightComponent* DirectLight : DirectLights)
				{
					OriginalIntensities.Add(DirectLight->Intensity);
					DirectLight->SetIntensity(0.0f);
				}
				const bool bOriginalSkyVisible = Stage->GardenSkyLight->IsVisible();
				double DiagnosticMeans[2] = {};
				bool bDiagnosticPassed = true;
				for (int32 DiagnosticIndex = 0; DiagnosticIndex < 2; ++DiagnosticIndex)
				{
					Stage->GardenSkyLight->SetVisibility(DiagnosticIndex == 0);
					World->SendAllEndOfFrameUpdates();
					FlushRenderingCommands();
					Capture->bCameraCutThisFrame = true;
					FString Error;
					TArray<FColor> Pixels;
					const FString Framing = DiagnosticIndex == 0 ? TEXT("environment_only") : TEXT("lights_off");
					const FString Filename = FPaths::Combine(ScreenshotDirectory, FString(Definition.ImageName) + TEXT("_") + Framing + TEXT(".png"));
					const bool bSaved = SaveCapture(Capture.Get(), Filename, Pixels, Error);
					const int32 ImageWidth = Target->SizeX / 2;
					const int32 ImageHeight = Target->SizeY / 2;
					int32 SampleCount = 0;
					if (bSaved)
					{
						// 固定样本的鼻翼与脸颊区域，避开天空和自发光灯笼。
						for (int32 Y = FMath::RoundToInt(ImageHeight * 0.61f); Y < FMath::RoundToInt(ImageHeight * 0.66f); ++Y)
						{
							for (int32 X = FMath::RoundToInt(ImageWidth * 0.48f); X < FMath::RoundToInt(ImageWidth * 0.54f); ++X)
							{
								const FColor& Pixel = Pixels[Y * ImageWidth + X];
								DiagnosticMeans[DiagnosticIndex] += 0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B;
								++SampleCount;
							}
						}
					}
					if (SampleCount > 0) DiagnosticMeans[DiagnosticIndex] /= SampleCount;
					UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
					UTextureRenderTarget2D* LightTarget = Lights ? Lights->GetLightDataRenderTarget() : nullptr;
					FTextureRenderTargetResource* LightResource = LightTarget ? LightTarget->GameThread_GetRenderTargetResource() : nullptr;
					TArray<FLinearColor> LightPixels;
					const bool bZeroLightData = LightResource && LightResource->ReadLinearColorPixels(LightPixels,
						FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), FIntRect(0, 0, 64, 2))
						&& LightPixels.Num() == 128 && LightPixels[82].A > 0.5f && LightPixels[82].R < 0.5f;
					TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("output"), Filename);
					Item->SetStringField(TEXT("framing"), Framing);
					Item->SetNumberField(TEXT("face_mean_luminance_srgb_255"), DiagnosticMeans[DiagnosticIndex]);
					Item->SetBoolField(TEXT("valid_zero_direct_lights"), bZeroLightData);
					Item->SetBoolField(TEXT("passed"), bSaved && bZeroLightData && SampleCount > 0);
					if (!Error.IsEmpty()) Item->SetStringField(TEXT("error"), Error);
					Captures.Add(MakeShared<FJsonValueObject>(Item));
					bDiagnosticPassed &= bSaved && bZeroLightData && SampleCount > 0;
				}
				for (int32 Index = 0; Index < DirectLights.Num(); ++Index) DirectLights[Index]->SetIntensity(OriginalIntensities[Index]);
				Stage->GardenSkyLight->SetVisibility(bOriginalSkyVisible);
				World->SendAllEndOfFrameUpdates();
				FlushRenderingCommands();
				bDiagnosticPassed &= DiagnosticMeans[0] > DiagnosticMeans[1] + 5.0 && DiagnosticMeans[1] < 8.0;
				Result->SetNumberField(TEXT("environment_only_face_luminance"), DiagnosticMeans[0]);
				Result->SetNumberField(TEXT("lights_off_face_luminance"), DiagnosticMeans[1]);
				Result->SetBoolField(TEXT("environment_response_passed"), bDiagnosticPassed);
				bPassed &= bDiagnosticPassed;
			}
			else bPassed = false;
		}
		if (Definition.bStage)
		{
			bPassed &= ChangedTransforms > 0 && ChangedPixelFraction > 0.0001 && ChangedBones > 0;
		}
		Result->SetBoolField(TEXT("character_preset_applied"), bCharacterPreset);
		Result->SetBoolField(TEXT("animation_instance_valid"), bAnimationInstanceValid);
		Result->SetBoolField(TEXT("animation_asset_bound"), bAnimationAssetBound);
		Result->SetBoolField(TEXT("physics_initialized"), bPhysicsInitialized);
		Result->SetNumberField(TEXT("physics_rigid_bodies"), PhysicsPlayback ? PhysicsPlayback->GetPhysicsRigidBodyCount() : 0);
		Result->SetNumberField(TEXT("physics_changed_bones"), PhysicsChangedBones);
		bPassed &= bPhysicsInitialized && PhysicsChangedBones > 0;
		Result->SetNumberField(TEXT("sampled_bones"), FirstBoneTransforms.Num());
		Result->SetNumberField(TEXT("animated_bones_changed"), ChangedBones);
		Result->SetNumberField(TEXT("stage_instances"), FirstTransforms.Num());
		Result->SetNumberField(TEXT("animated_instances_changed"), ChangedTransforms);
		Result->SetNumberField(TEXT("changed_pixel_fraction"), ChangedPixelFraction);
		Result->SetArrayField(TEXT("captures"), Captures);
		TArray<TSharedPtr<FJsonValue>> Files;
		for (const TSharedPtr<FJsonValue>& Value : Captures) Files.Add(MakeShared<FJsonValueString>(Value->AsObject()->GetStringField(TEXT("output"))));
		Result->SetArrayField(TEXT("files"), Files);
		const bool bRestored = Stage->RestoreCharacterMaterials(Character.Get()) && Character->OverrideMaterials == OriginalOverrides
			&& Character->ComponentTags.Contains(FName(TEXT("MMDShadowInclude"))) == bOriginalShadowInclude
			&& Character->ComponentTags.Contains(FName(TEXT("MMDShadowExclude"))) == bOriginalShadowExclude;
		Result->SetBoolField(TEXT("original_materials_restored"), bRestored);
		bPassed &= bRestored && bSelfShadowTogglePassed;
		Result->SetBoolField(TEXT("passed"), bPassed);
		Scene.RemoveComponent(Capture.Get());
		Scene.RemoveComponent(Character.Get());
		World->DestroyActor(CaptureOwner);
		World->DestroyActor(CharacterOwner);
		World->DestroyActor(Stage);
		return Result;
	}
}

UMMDStagePresetsCommandlet::UMMDStagePresetsCommandlet()
{
	IsClient = true;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMMDStagePresetsCommandlet::Main(const FString& Params)
{
	const bool bGenerate = FParse::Param(*Params, TEXT("Generate"));
	// 独立开关：只截终末地一套，完全不进入上面七套预设与对比流程。
	const bool bEndfieldCapture = FParse::Param(*Params, TEXT("EndfieldCapture"));
	const bool bScreenshots = FParse::Param(*Params, TEXT("Screenshots")) || bEndfieldCapture;
	FString CapturePresetFilter;
	FParse::Value(*Params, TEXT("CapturePreset="), CapturePresetFilter);
	if (!CapturePresetFilter.IsEmpty())
	{
		bool bKnownPreset = false;
		for (const FStageDefinition& Definition : Definitions) bKnownPreset |= CapturePresetFilter == Definition.Name;
		if (!bKnownPreset) return 1;
	}
	FString ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/Stages/stages.json"));
	FString ScreenshotDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/Stages/screenshots"));
	FParse::Value(*Params, TEXT("Report="), ReportPath);
	FParse::Value(*Params, TEXT("ScreenshotDir="), ScreenshotDirectory);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
	TArray<TSharedPtr<FJsonValue>> Assets;
	TArray<TSharedPtr<FJsonValue>> MaterialResults;
	TArray<TSharedPtr<FJsonValue>> CaptureResults;
	TArray<FString> Errors;
	FMMDStageMaterialSet Materials;
	FMMDReferenceMaterialSet References;
	FMMDGardenMaterialSet GardenMaterials;
	FString Error;
	TArray<UTexture2D*> Textures;
	if (bGenerate)
	{
		int32 TextureResolution = 256;
		int32 TextureSeed = 2026;
		FParse::Value(*Params, TEXT("TextureResolution="), TextureResolution);
		FParse::Value(*Params, TEXT("TextureSeed="), TextureSeed);
		if (!UMMDStageTextureGenerator::GenerateGenericStageTextures(Textures, Error, TextureResolution,
			TextureSeed, FParse::Param(*Params, TEXT("RegenerateTextures"))))
		{
			Errors.Add(Error);
		}
	}
	bool bMaterialsReady = EnsureMMDStageMaterials(Materials, Error);
	if (!bMaterialsReady)
	{
		Errors.Add(Error);
	}
	else
	{
		bMaterialsReady = EnsureMMDReferenceCharacterMaterials(Materials, References, Error);
		if (!bMaterialsReady) Errors.Add(Error);
	}
	if (bMaterialsReady)
	{
		bMaterialsReady = EnsureMMDGardenMaterials(GardenMaterials, Error)
			&& FMMDGardenMeshBuilder::BuildAssets(bGenerate, Error);
		if (!bMaterialsReady) Errors.Add(Error);
	}
	if (bMaterialsReady)
	{
		for (const FMMDReferenceMaterialFamily& Family : References.Families)
			bMaterialsReady &= Family.Opaque && Family.Masked && Family.Translucent;
		if (!bMaterialsReady) Errors.Add(TEXT("Reference material families are incomplete; dependent assets were not modified"));
	}
	for (UTexture2D* Texture : Textures)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset"), Texture->GetPathName());
		Item->SetStringField(TEXT("type"), TEXT("Texture"));
		Item->SetNumberField(TEXT("width"), Texture->Source.GetSizeX());
		Item->SetNumberField(TEXT("height"), Texture->Source.GetSizeY());
		Item->SetBoolField(TEXT("passed"), Texture->Source.GetSizeX() > 0 && Texture->Source.GetSizeY() > 0);
		Assets.Add(MakeShared<FJsonValueObject>(Item));
	}
	if (bMaterialsReady)
	{
		for (const TCHAR* Name : { TEXT("T_MMD_GenericHairNormalV2"), TEXT("T_MMD_GenericClothNormalV2"), TEXT("T_MMD_GenericClothDataV2") })
		{
			const FString Path = StagePresetsObjectPath(FString(StageRoot) + TEXT("Textures/") + Name);
			UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Path);
			const bool bNormal = FString(Name).Contains(TEXT("Normal"));
			const bool bValid = Texture && !Texture->SRGB && Texture->Source.GetSizeX() == 512 && Texture->Source.GetSizeY() == 512
				&& Texture->AddressX == TA_Wrap && Texture->AddressY == TA_Wrap
				&& (bNormal ? Texture->CompressionSettings == TC_Normalmap : Texture->CompressionSettings == TC_VectorDisplacementmap);
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Path);
			Item->SetStringField(TEXT("type"), TEXT("GenericSurfaceTexture"));
			Item->SetBoolField(TEXT("normal_map"), bNormal);
			Item->SetBoolField(TEXT("passed"), bValid);
			Assets.Add(MakeShared<FJsonValueObject>(Item));
		}
		const FString BackdropPath = StagePresetsObjectPath(FString(StageRoot) + TEXT("Textures/T_Garden_SakuraBackdropV3"));
		UTexture2D* Backdrop = LoadObject<UTexture2D>(nullptr, *BackdropPath);
		const bool bBackdropValid = Backdrop && Backdrop->SRGB && Backdrop->AddressX == TA_Clamp && Backdrop->AddressY == TA_Clamp
			&& Backdrop->Source.GetSizeX() >= 2048 && Backdrop->Source.GetSizeY() >= 512;
		TSharedRef<FJsonObject> BackdropItem = MakeShared<FJsonObject>();
		BackdropItem->SetStringField(TEXT("asset"), BackdropPath);
		BackdropItem->SetStringField(TEXT("type"), TEXT("GardenBackdrop"));
		BackdropItem->SetBoolField(TEXT("passed"), bBackdropValid);
		Assets.Add(MakeShared<FJsonValueObject>(BackdropItem));
	}
	if (bMaterialsReady)
	{
		FAssetCompilingManager::Get().FinishAllCompilation();
		for (const TCHAR* Name : { TEXT("SM_GardenTreeTrunkV2"), TEXT("SM_GardenBranchV2"), TEXT("SM_GardenBlossomClusterV2"), TEXT("SM_GardenReedClumpV2") })
		{
			const FString Path = StagePresetsObjectPath(FString(StageRoot) + TEXT("Meshes/") + Name);
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Path);
			Item->SetStringField(TEXT("type"), TEXT("GardenMeshV2"));
			const FStaticMeshRenderData* Data = Mesh ? Mesh->GetRenderData() : nullptr;
			bool bValid = Data && Data->LODResources.Num() > 0;
			TArray<TSharedPtr<FJsonValue>> Triangles;
			if (Data)
			{
				for (const FStaticMeshLODResources& LOD : Data->LODResources)
					Triangles.Add(MakeShared<FJsonValueNumber>(LOD.GetNumTriangles()));
				if (FString(Name) == TEXT("SM_GardenBlossomClusterV2"))
					bValid &= Data->LODResources.Num() == 3 && Data->LODResources[0].GetNumTriangles() == 1080
						&& Data->LODResources[1].GetNumTriangles() == 720 && Data->LODResources[2].GetNumTriangles() == 270;
			}
			Item->SetArrayField(TEXT("lod_triangles"), Triangles);
			Item->SetBoolField(TEXT("passed"), bValid);
			Assets.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	if (bMaterialsReady)
	{
		TMap<UMaterial*, int32> ExpressionCounts;
		for (const FMMDReferenceMaterialFamily& Family : References.Families)
			for (UMaterialInterface* Parent : { Family.Opaque, Family.Masked, Family.Translucent })
				ExpressionCounts.Add(Parent->GetMaterial(), Parent->GetMaterial()->GetExpressionCollection().Expressions.Num());
		FMMDReferenceMaterialSet Repeated;
		FString RepeatError;
		bool bIdempotent = EnsureMMDReferenceCharacterMaterials(Materials, Repeated, RepeatError);
		for (const TPair<UMaterial*, int32>& Entry : ExpressionCounts)
		{
			bIdempotent &= Entry.Key->GetExpressionCollection().Expressions.Num() == Entry.Value;
			const UMaterialExpressionCustom* Main = Cast<UMaterialExpressionCustom>(Entry.Key->GetEditorOnlyData()->EmissiveColor.Expression);
			bIdempotent &= Main && Main->Inputs.Num() == 44;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("type"), TEXT("ReferenceGraphIdempotence"));
		Item->SetNumberField(TEXT("main_custom_inputs"), 44);
		Item->SetNumberField(TEXT("parents"), ExpressionCounts.Num());
		Item->SetBoolField(TEXT("passed"), bIdempotent);
		Assets.Add(MakeShared<FJsonValueObject>(Item));
		if (!RepeatError.IsEmpty()) Errors.Add(RepeatError);
	}
	TArray<TStrongObjectPtr<UMMDStageRenderPreset>> Presets;
	UMMDStageRenderPreset* EndfieldCapturePreset = nullptr;
	TArray<UMaterialInterface*> EndfieldCaptureParents;
	for (const FStageDefinition& Definition : Definitions)
	{
		Error.Reset();
		UMMDStageRenderPreset* Preset = bMaterialsReady ? MakePreset(Definition, Materials, References, bGenerate, Error) : nullptr;
		Presets.Emplace(Preset);
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset"), StagePresetsObjectPath(AssetPackage(TEXT("Presets"), TEXT("DA_Render_"), Definition.Name)));
		Item->SetStringField(TEXT("type"), TEXT("RenderPreset"));
		const FMMDReferenceMaterialFamily& Family = References.Families[ReferenceFamilyFor(Definition)];
		const bool bPresetBindingsValid = Preset && Preset->CharacterMaterialOpaque == Family.Opaque
			&& Preset->CharacterMaterialMasked == Family.Masked && Preset->CharacterMaterialTranslucent == Family.Translucent;
		Item->SetNumberField(TEXT("reference_family"), ReferenceFamilyFor(Definition));
		Item->SetBoolField(TEXT("passed"), bPresetBindingsValid);
		Assets.Add(MakeShared<FJsonValueObject>(Item));
		if (!Error.IsEmpty())
		{
			Errors.Add(Error);
		}
		if (Definition.bStage && Preset)
		{
			UBlueprint* Blueprint = MakeStageBlueprint(Definition, Preset, bGenerate, Error);
			UWorld* World = MakeStageMap(Definition, Blueprint, bGenerate, Error);
			TSharedRef<FJsonObject> BlueprintItem = MakeShared<FJsonObject>();
			BlueprintItem->SetStringField(TEXT("asset"), StagePresetsObjectPath(AssetPackage(TEXT("Blueprints"), TEXT("BP_MMDStage_"), Definition.Name)));
			BlueprintItem->SetStringField(TEXT("type"), TEXT("Blueprint"));
			BlueprintItem->SetBoolField(TEXT("passed"), Blueprint && Blueprint->GeneratedClass && Blueprint->Status != BS_Error);
			Assets.Add(MakeShared<FJsonValueObject>(BlueprintItem));
			int32 StageCount = 0;
			int32 CameraCount = 0;
			int32 CharacterCount = 0;
			if (World && World->PersistentLevel)
			{
				for (AActor* Actor : World->PersistentLevel->Actors)
				{
					StageCount += Actor && Actor->IsA<AMMDStageActor>() ? 1 : 0;
					CameraCount += Actor && Actor->IsA<ACameraActor>() ? 1 : 0;
					if (Actor)
					{
						TInlineComponentArray<USkeletalMeshComponent*> Characters(Actor);
						CharacterCount += Characters.Num();
					}
				}
			}
			TSharedRef<FJsonObject> MapItem = MakeShared<FJsonObject>();
			MapItem->SetStringField(TEXT("asset"), StagePresetsObjectPath(AssetPackage(TEXT("Maps"), TEXT("MMDStage_"), Definition.Name)));
			MapItem->SetStringField(TEXT("type"), TEXT("Map"));
			MapItem->SetNumberField(TEXT("stage_count"), StageCount);
			MapItem->SetNumberField(TEXT("camera_count"), CameraCount);
			MapItem->SetNumberField(TEXT("character_count"), CharacterCount);
			MapItem->SetBoolField(TEXT("passed"), World && StageCount == 1 && CameraCount == 1 && CharacterCount == 0);
			Assets.Add(MakeShared<FJsonValueObject>(MapItem));
			if (!Error.IsEmpty())
			{
				Errors.Add(Error);
			}
		}
	}
	TStrongObjectPtr<UMaterial> OutlineParent(GetOrCreateMMDOutlineMaterial());
	TSharedRef<FJsonObject> OutlineValidation = ValidateMMDOutlineSlots();
	OutlineValidation->SetStringField(TEXT("type"), TEXT("OutlinePresetValidation"));
	OutlineValidation->SetBoolField(TEXT("passed"), OutlineParent.IsValid() && OutlineValidation->GetBoolField(TEXT("success")));
	Assets.Add(MakeShared<FJsonValueObject>(OutlineValidation));
	// 终末地包不在上面的预设循环里，单独加载并把它和父材质一起纳入编译等待，
	// 否则首次截图会拿到尚未完成的 shader map。
	const bool bEndfieldReady = !bEndfieldCapture || EndfieldCaptureLoadBundle(EndfieldCapturePreset, EndfieldCaptureParents, Error);
	if (bEndfieldCapture && !bEndfieldReady)
	{
		Errors.Add(Error);
	}
	if (FApp::CanEverRender() && GShaderCompilingManager && !GUsingNullRHI)
	{
		TArray<UMaterialInterface*> Parents = { Materials.CharacterOpaque, Materials.CharacterMasked,
			Materials.CharacterTranslucent, Materials.Floor, Materials.Panel, Materials.Accent, Materials.Beam, Materials.Backdrop,
			GardenMaterials.Surface, GardenMaterials.Water };
		Parents.Add(OutlineParent.Get());
		for (const FMMDReferenceMaterialFamily& Family : References.Families)
		{
			Parents.Add(Family.Opaque);
			Parents.Add(Family.Masked);
			Parents.Add(Family.Translucent);
		}
		Parents.Append(EndfieldCaptureParents);
		for (UMaterialInterface* Parent : Parents)
		{
			if (UMaterial* Material = Parent ? Parent->GetMaterial() : nullptr)
			{
				Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
			}
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		GShaderCompilingManager->FinishAllCompilation();
		for (UMaterialInterface* Parent : Parents)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			TArray<FString> CompileErrors;
			const FMaterialResource* Resource = Parent && Parent->GetMaterial()
				? Parent->GetMaterial()->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
			if (Resource)
			{
				CompileErrors.Append(Resource->GetCompileErrors());
				const FMaterialShaderMap* ShaderMap = Resource->GetGameThreadShaderMap();
				if (!Resource->IsCompilationFinished() || !ShaderMap || !ShaderMap->CompiledSuccessfully()
					|| !ShaderMap->IsCompilationFinalized() || !ShaderMap->IsValidForRendering(false))
				{
					CompileErrors.Add(TEXT("Material shader map is not ready for native rendering"));
				}
			}
			else
			{
				CompileErrors.Add(TEXT("Material resource was unavailable"));
			}
			TArray<TSharedPtr<FJsonValue>> JsonErrors;
			for (const FString& CompileError : CompileErrors)
			{
				JsonErrors.Add(MakeShared<FJsonValueString>(CompileError));
			}
			Item->SetStringField(TEXT("asset"), Parent ? Parent->GetPathName() : TEXT("missing"));
			Item->SetArrayField(TEXT("errors"), JsonErrors);
			Item->SetBoolField(TEXT("passed"), CompileErrors.IsEmpty());
			MaterialResults.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	else if (bScreenshots)
	{
		Errors.Add(TEXT("Screenshots require -AllowCommandletRendering -RenderOffscreen and a real RHI"));
	}
	if (bMaterialsReady && bScreenshots && FApp::CanEverRender() && !GUsingNullRHI)
	{
		FCharacterSample Sample;
		Error.Reset();
		UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
		TStrongObjectPtr<UTextureRenderTarget2D> OriginalLightTarget(Lights ? Lights->GetLightDataRenderTarget() : nullptr);
		TStrongObjectPtr<UTextureRenderTarget2D> SharedLightTarget(LoadObject<UTextureRenderTarget2D>(
			nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT")));
		const int32 OriginalWidth = SharedLightTarget.IsValid() ? SharedLightTarget->SizeX : 0;
		const int32 OriginalHeight = SharedLightTarget.IsValid() ? SharedLightTarget->SizeY : 0;
		const ETextureRenderTargetFormat OriginalFormat = SharedLightTarget.IsValid()
			? SharedLightTarget->RenderTargetFormat.GetValue() : RTF_RGBA32f;
		const bool bOriginalUAV = SharedLightTarget.IsValid() && SharedLightTarget->bCanCreateUAV;
		const bool bOriginalDirty = SharedLightTarget.IsValid() && SharedLightTarget->GetOutermost()->IsDirty();
		ON_SCOPE_EXIT
		{
			if (Lights)
			{
				Lights->SetLightDataRenderTarget(OriginalLightTarget.Get());
			}
			if (SharedLightTarget.IsValid())
			{
				SharedLightTarget->RenderTargetFormat = OriginalFormat;
				SharedLightTarget->bCanCreateUAV = bOriginalUAV;
				SharedLightTarget->InitAutoFormat(OriginalWidth, OriginalHeight);
				SharedLightTarget->UpdateResourceImmediate(true);
				if (!bOriginalDirty)
				{
					SharedLightTarget->GetOutermost()->SetDirtyFlag(false);
				}
			}
			FlushRenderingCommands();
		};
		if (!Lights || !SharedLightTarget.IsValid() || Lights->HasPreviewLightOverride())
		{
			Errors.Add(TEXT("LightData subsystem is unavailable or a preview light override is active"));
		}
		else if (!LoadCharacterSample(Sample, Error))
		{
			Errors.Add(Error);
		}
		else
		{
			Lights->SetLightDataRenderTarget(SharedLightTarget.Get());
			// 离屏没有主视图，清空相机基可让角色可靠回退，而不是复用其他世界的阴影。
			FVector4f EmptyShadowData[18];
			for (FVector4f& Vector : EmptyShadowData)
			{
				Vector = FVector4f(0, 0, 0, 0);
			}
			Lights->SetShadowCameraData(EmptyShadowData);
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Definitions); ++Index)
			{
				if (bEndfieldCapture) break;
				if (Presets[Index].IsValid() && (CapturePresetFilter.IsEmpty() || CapturePresetFilter == Definitions[Index].Name))
				{
					CaptureResults.Add(MakeShared<FJsonValueObject>(CapturePreset(
						Definitions[Index], Presets[Index].Get(), Sample, ScreenshotDirectory)));
				}
			}
			if (bEndfieldCapture)
			{
				// 复用七套完全相同的截图流程（全身 / 细节 / 脸部特写），只是换成新入库的终末地预设。
				if (!bEndfieldReady)
				{
					Errors.Add(TEXT("Endfield capture skipped: the preset or its character materials could not be loaded"));
				}
				else
				{
					CaptureResults.Add(MakeShared<FJsonValueObject>(CapturePreset(
						EndfieldCaptureDefinition, EndfieldCapturePreset, Sample, ScreenshotDirectory)));
				}
			}
			const FStageDefinition Comparisons[] =
			{
				{ TEXT("Reference_Genshin"), TEXT("reference_genshin"), TEXT("原神材质对比"), EMMDStageTheme::NeonConcert, false },
				{ TEXT("Reference_Wuthering"), TEXT("reference_wuthering"), TEXT("鸣潮材质对比"), EMMDStageTheme::NeonConcert, false },
				{ TEXT("Reference_Endfield"), TEXT("reference_endfield"), TEXT("终末地材质对比"), EMMDStageTheme::NeonConcert, false },
				{ TEXT("Reference_SilverCity"), TEXT("reference_silver_city"), TEXT("白银之城材质对比"), EMMDStageTheme::NeonConcert, false }
			};
			const int32 SourcePresetIndices[] = { 4, 6, 5, 3 };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Comparisons); ++Index)
			{
				if (!CapturePresetFilter.IsEmpty() || bEndfieldCapture) continue;
				UMMDStageRenderPreset* Source = Presets[SourcePresetIndices[Index]].Get();
				if (!Source || !Presets[0].IsValid())
				{
					Errors.Add(FString::Printf(TEXT("Comparison source preset is missing: %s"), Comparisons[Index].Name));
					continue;
				}
				// 对比仅改临时副本，四套材质共用舞台、姿势、照明和后处理。
				TStrongObjectPtr<UMMDStageRenderPreset> Comparison(DuplicateObject<UMMDStageRenderPreset>(Source, GetTransientPackage()));
				Comparison->SetFlags(RF_Transient);
				Comparison->ClearFlags(RF_Public | RF_Standalone);
				Comparison->DisplayName = FText::FromString(Comparisons[Index].DisplayName);
				Comparison->Theme = EMMDStageTheme::NeonConcert;
				Comparison->bOverrideStageTheme = false;
				Comparison->BaseTint = Presets[0]->BaseTint;
				Comparison->AccentTint = Presets[0]->AccentTint;
				Comparison->SecondaryTint = Presets[0]->SecondaryTint;
				Comparison->KeyLightColor = Presets[0]->KeyLightColor;
				Comparison->FillLightColor = Presets[0]->FillLightColor;
				Comparison->RimLightColor = Presets[0]->RimLightColor;
				Comparison->KeyLightRotation = FRotator(-38, 55, 0);
				Comparison->FillLightRotation = FRotator(-20, 140, 0);
				Comparison->RimLightRotation = FRotator(-12, -90, 0);
				Comparison->KeyLightIntensity = 3.3f;
				Comparison->FillLightIntensity = 0.4f;
				Comparison->RimLightIntensity = 0.7f;
				Comparison->AccentLightIntensity = 0.6f;
				Comparison->AmbientStrength = 0.14f;
				Comparison->ExposureBias = 0.18f;
				Comparison->BloomIntensity = 0.06f;
				Comparison->ColorGradingSaturation = 1.0f;
				Comparison->ColorGradingContrast = 1.0f;
				Comparison->ColorGradingGamma = 1.0f;
				Comparison->ColorGradingGain = FLinearColor::White;
				Comparison->WhiteTemperature = 6500.0f;
				Comparison->FilmToe = 0.25f;
				CaptureResults.Add(MakeShared<FJsonValueObject>(CapturePreset(
					Comparisons[Index], Comparison.Get(), Sample, ScreenshotDirectory, true)));
			}
		}
	}
	int32 Passed = 0;
	int32 Failed = Errors.Num();
	for (const TArray<TSharedPtr<FJsonValue>>* Items : { &Assets, &MaterialResults, &CaptureResults })
	{
		for (const TSharedPtr<FJsonValue>& Value : *Items)
		{
			if (Value->AsObject()->GetBoolField(TEXT("passed")))
			{
				++Passed;
			}
			else
			{
				++Failed;
			}
		}
	}
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), Failed == 0);
	Report->SetBoolField(TEXT("generated"), bGenerate);
	Report->SetBoolField(TEXT("foreground_opened"), false);
	Report->SetStringField(TEXT("shader_platform"), LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
	Report->SetNumberField(TEXT("passed"), Passed);
	Report->SetNumberField(TEXT("failed"), Failed);
	Report->SetArrayField(TEXT("assets"), Assets);
	Report->SetArrayField(TEXT("materials"), MaterialResults);
	Report->SetArrayField(TEXT("screenshots"), CaptureResults);
	Report->SetArrayField(TEXT("capture_groups"), CaptureResults);
	TArray<TSharedPtr<FJsonValue>> JsonErrors;
	for (const FString& Item : Errors)
	{
		JsonErrors.Add(MakeShared<FJsonValueString>(Item));
		UE_LOG(LogTemp, Error, TEXT("[MMDStagePresets] %s"), *Item);
	}
	Report->SetArrayField(TEXT("errors"), JsonErrors);
	FString Json;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
	if (!FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		++Failed;
		UE_LOG(LogTemp, Error, TEXT("[MMDStagePresets] Cannot write %s"), *ReportPath);
	}
	UE_LOG(LogTemp, Display, TEXT("[MMDStagePresets] %d passed, %d failed. %s"), Passed, Failed, *ReportPath);
	return Failed == 0 ? 0 : 1;
}
