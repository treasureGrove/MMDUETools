#include "MMDImportSetting.h"
#include "MMDViewPanel.h"
#include "UI/MMDToolPanelWidget.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "MaterialShared.h"
#include "Framework/Application/SlateApplication.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SWindow.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "MMDPhysicsSimulator.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Blueprint/BlueprintSupport.h"
#include "Curves/RichCurve.h"
#include "Misc/FrameRate.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/Input/SNumericEntryBox.h"
#if WITH_EDITOR
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Channels/MovieSceneChannelHandle.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "LevelSequence.h"
#include "AMMDActor.h"
#include "AMMDLevelSequenceActor.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Modules/ModuleManager.h" 
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#endif // WITH_EDITOR

namespace
{
	constexpr const TCHAR* AuroraJewelOpaquePath = TEXT("/Ue5MMDTools/Resources/MaterialPresets/M_TMMD_AuroraJewelToon.M_TMMD_AuroraJewelToon");
	constexpr const TCHAR* AuroraJewelTranslucentPath = TEXT("/Ue5MMDTools/Resources/MaterialPresets/M_TMMD_AuroraJewelToon_Translucent.M_TMMD_AuroraJewelToon_Translucent");

	bool MMDContainsAny(const FString& Source, const TCHAR* const* Tokens, int32 TokenCount)
	{
		for (int32 Index = 0; Index < TokenCount; ++Index)
		{
			if (Source.Contains(Tokens[Index]))
			{
				return true;
			}
		}
		return false;
	}

	UTexture* MMDGetBaseColorTexture(UMaterialInterface* SourceMaterial)
	{
		if (!SourceMaterial)
		{
			return nullptr;
		}

		static const FName ParameterNames[] = {
			TEXT("BaseColorMap"),
			TEXT("BaseColorTexture"),
			TEXT("BaseColor")
		};
		for (const FName ParameterName : ParameterNames)
		{
			UTexture* Texture = nullptr;
			if (SourceMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterName), Texture) && Texture)
			{
				return Texture;
			}
		}
		return nullptr;
	}

	FLinearColor MMDGetDiffuseColor(UMaterialInterface* SourceMaterial)
	{
		FLinearColor DiffuseColor = FLinearColor::White;
		if (SourceMaterial)
		{
			SourceMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), DiffuseColor);
		}
		return DiffuseColor;
	}

	float MMDGetOpacity(UMaterialInterface* SourceMaterial)
	{
		float Opacity = 1.0f;
		if (SourceMaterial)
		{
			SourceMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity);
		}
		return Opacity;
	}

	float MMDClassifySurfaceProfile(UMaterialInterface* SourceMaterial, UTexture* BaseColorTexture)
	{
		if (!SourceMaterial)
		{
			return 0.0f;
		}

		FString SemanticName = SourceMaterial->GetName().ToLower();
		if (UMaterial* BaseMaterial = SourceMaterial->GetBaseMaterial())
		{
			SemanticName += TEXT(" ") + BaseMaterial->GetName().ToLower();
		}
		const FString TextureName = BaseColorTexture ? BaseColorTexture->GetName().ToLower() : FString();

		static const TCHAR* SkinTokens[] = { TEXT("skin"), TEXT("face"), TEXT("facial"), TEXT("body"), TEXT("hada"), TEXT("肌"), TEXT("顔"), TEXT("脸"), TEXT("身体") };
		static const TCHAR* EyeTokens[] = { TEXT("eye"), TEXT("eyeball"), TEXT("iris"), TEXT("hl"), TEXT("瞳"), TEXT("目") };
		static const TCHAR* HairShadowTokens[] = { TEXT("hairshadow"), TEXT("hair_shadow"), TEXT("髪影"), TEXT("发影") };
		static const TCHAR* HairTokens[] = { TEXT("hair"), TEXT("kami"), TEXT("bang"), TEXT("tail"), TEXT("髪"), TEXT("发") };
		static const TCHAR* DarkTokens[] = { TEXT("black"), TEXT("skirt"), TEXT("dress"), TEXT("ribbon"), TEXT("shoe"), TEXT("metal"), TEXT("gold"), TEXT("スカート"), TEXT("リボン"), TEXT("クロスタイ"), TEXT("ボタン"), TEXT("金属") };
		static const TCHAR* LightTokens[] = { TEXT("white"), TEXT("shirt"), TEXT("blouse"), TEXT("apron"), TEXT("frill"), TEXT("tops"), TEXT("tooth"), TEXT("围裙"), TEXT("白"), TEXT("フリル"), TEXT("レース") };

		if (MMDContainsAny(SemanticName, SkinTokens, UE_ARRAY_COUNT(SkinTokens))) return 1.0f;
		if (MMDContainsAny(SemanticName, EyeTokens, UE_ARRAY_COUNT(EyeTokens))) return 5.0f;
		if (MMDContainsAny(SemanticName, HairShadowTokens, UE_ARRAY_COUNT(HairShadowTokens))) return 6.0f;
		if (MMDContainsAny(SemanticName, HairTokens, UE_ARRAY_COUNT(HairTokens))) return 2.0f;
		if (MMDContainsAny(SemanticName, DarkTokens, UE_ARRAY_COUNT(DarkTokens))) return 4.0f;
		if (MMDContainsAny(SemanticName, LightTokens, UE_ARRAY_COUNT(LightTokens))) return 3.0f;
		if (TextureName.Contains(TEXT("black")) || TextureName.Contains(TEXT("dark"))) return 4.0f;
		if (TextureName.Contains(TEXT("white")) || TextureName.Contains(TEXT("shirt")) || TextureName.Contains(TEXT("blouse")) || TextureName.Contains(TEXT("cloth"))) return 3.0f;
		return 0.0f;
	}

	bool MMDNeedsTranslucentPreset(UMaterialInterface* SourceMaterial, const FLinearColor& DiffuseColor)
	{
		return DiffuseColor.A < 0.98f || (SourceMaterial && IsTranslucentBlendMode(*SourceMaterial));
	}

	UMaterialInstanceDynamic* MMDCreateAuroraJewelInstance(
		UMeshComponent* Component,
		int32 SlotIndex,
		UMaterialInterface* SourceMaterial,
		UMaterialInterface* OpaquePreset,
		UMaterialInterface* TranslucentPreset)
	{
		const FLinearColor DiffuseColor = MMDGetDiffuseColor(SourceMaterial);
		UMaterialInterface* Parent = MMDNeedsTranslucentPreset(SourceMaterial, DiffuseColor) ? TranslucentPreset : OpaquePreset;
		if (!Component || !Parent)
		{
			return nullptr;
		}

		const FName InstanceName = MakeUniqueObjectName(
			Component,
			UMaterialInstanceDynamic::StaticClass(),
			*FString::Printf(TEXT("MID_AuroraJewel_%02d"), SlotIndex));
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Parent, Component, InstanceName);
		if (!Instance)
		{
			return nullptr;
		}

		UTexture* BaseColorTexture = MMDGetBaseColorTexture(SourceMaterial);
		if (BaseColorTexture)
		{
			Instance->SetTextureParameterValue(TEXT("BaseColorMap"), BaseColorTexture);
		}
		Instance->SetVectorParameterValue(TEXT("DiffuseColor"), DiffuseColor);
		Instance->SetScalarParameterValue(TEXT("Opacity"), MMDGetOpacity(SourceMaterial));
		Instance->SetScalarParameterValue(TEXT("shadow_step"), 0.00f);
		Instance->SetScalarParameterValue(TEXT("shadow_softness"), 0.045f);
		Instance->SetScalarParameterValue(TEXT("shadow_strength"), 0.64f);
		Instance->SetScalarParameterValue(TEXT("ambient_strength"), 0.10f);
		Instance->SetScalarParameterValue(TEXT("light_color_influence"), 0.32f);
		Instance->SetScalarParameterValue(TEXT("highlight_step"), 0.82f);
		Instance->SetScalarParameterValue(TEXT("highlight_strength"), 0.28f);
		Instance->SetScalarParameterValue(TEXT("rim_strength"), 0.10f);
		Instance->SetScalarParameterValue(TEXT("rim_power"), 3.20f);
		Instance->SetScalarParameterValue(TEXT("local_light_strength"), 0.72f);
		Instance->SetScalarParameterValue(TEXT("local_specular_strength"), 0.55f);
		Instance->SetScalarParameterValue(TEXT("glamour_rim_strength"), 3.00f);
		Instance->SetScalarParameterValue(TEXT("directional_sheen_strength"), 0.95f);
		Instance->SetScalarParameterValue(TEXT("surface_profile"), MMDClassifySurfaceProfile(SourceMaterial, BaseColorTexture));
		return Instance;
	}
}

#if WITH_EDITOR
static bool MMDSaveAssetPackage(UObject* Asset)
{
	if (!Asset || !Asset->GetOutermost())
	{
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	const FString PackageFilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	return UPackage::SavePackage(Package, Asset, *PackageFilePath, SaveArgs);
}

static UMaterialInstanceConstant* MMDCreateOrUpdateAuroraJewelAssetInstance(
	const FString& AssetFolder,
	const FString& AssetName,
	UMaterialInterface* SourceMaterial,
	UMaterialInterface* OpaquePreset,
	UMaterialInterface* TranslucentPreset,
	FString& OutError)
{
	OutError.Reset();
	const FLinearColor DiffuseColor = MMDGetDiffuseColor(SourceMaterial);
	UMaterialInterface* Parent = MMDNeedsTranslucentPreset(SourceMaterial, DiffuseColor) ? TranslucentPreset : OpaquePreset;
	if (!Parent)
	{
		OutError = TEXT("预设父材质为空");
		return nullptr;
	}

	const FString PackageName = AssetFolder / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *ObjectPath);
	if (!Instance)
	{
		UPackage* Package = CreatePackage(*PackageName);
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
		Factory->InitialParent = Parent;
		Instance = Cast<UMaterialInstanceConstant>(Factory->FactoryCreateNew(
			UMaterialInstanceConstant::StaticClass(),
			Package,
			FName(*AssetName),
			RF_Public | RF_Standalone | RF_Transactional,
			nullptr,
			GWarn));
		if (!Instance)
		{
			OutError = FString::Printf(TEXT("创建材质实例失败：%s"), *ObjectPath);
			return nullptr;
		}
		FAssetRegistryModule::AssetCreated(Instance);
	}

	Instance->SetFlags(RF_Transactional);
	Instance->Modify();
	UMaterialEditingLibrary::SetMaterialInstanceParent(Instance, Parent);
	if (UTexture* BaseColorTexture = MMDGetBaseColorTexture(SourceMaterial))
	{
		UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, TEXT("BaseColorMap"), BaseColorTexture);
	}
	UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, TEXT("DiffuseColor"), DiffuseColor);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("Opacity"), MMDGetOpacity(SourceMaterial));
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("shadow_step"), 0.00f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("shadow_softness"), 0.045f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("shadow_strength"), 0.64f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("ambient_strength"), 0.10f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("light_color_influence"), 0.32f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("highlight_step"), 0.82f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("highlight_strength"), 0.28f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("rim_strength"), 0.10f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("rim_power"), 3.20f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("local_light_strength"), 0.72f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("local_specular_strength"), 0.55f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("glamour_rim_strength"), 3.00f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, TEXT("directional_sheen_strength"), 0.95f);
	UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(
		Instance,
		TEXT("surface_profile"),
		MMDClassifySurfaceProfile(SourceMaterial, MMDGetBaseColorTexture(SourceMaterial)));
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	Instance->MarkPackageDirty();
	return Instance;
}

static UBlueprint* CreateMMDLevelSequenceActorBlueprint(ULevelSequence* LevelSequence, const FString& FolderPath, const FString& AssetName, FString& OutError)
{
	OutError.Reset();
	if (!LevelSequence)
	{
		OutError = TEXT("LevelSequence is null.");
		return nullptr;
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(FolderPath / AssetName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* BlueprintPackage = CreatePackage(*UniquePackageName);
	if (!BlueprintPackage)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UBlueprint* ActorBlueprint = FKismetEditorUtilities::CreateBlueprint(
		AMMDLevelSequenceActor::StaticClass(),
		BlueprintPackage,
		FName(*UniqueAssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());
	if (!ActorBlueprint)
	{
		OutError = FString::Printf(TEXT("Failed to create AMMDLevelSequenceActor blueprint: %s"), *UniqueAssetName);
		return nullptr;
	}

	FKismetEditorUtilities::CompileBlueprint(ActorBlueprint);

	if (UClass* GeneratedClass = ActorBlueprint->GeneratedClass)
	{
		if (AMMDLevelSequenceActor* DefaultActor = Cast<AMMDLevelSequenceActor>(GeneratedClass->GetDefaultObject()))
		{
			DefaultActor->LevelSequenceAsset = LevelSequence;
			DefaultActor->CameraSettings.bOverrideAspectRatioAxisConstraint = true;
			DefaultActor->CameraSettings.AspectRatioAxisConstraint = EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV;
			DefaultActor->MarkPackageDirty();
		}
	}

	FAssetRegistryModule::AssetCreated(ActorBlueprint);
	ActorBlueprint->MarkPackageDirty();
	BlueprintPackage->MarkPackageDirty();

	const FString BlueprintPackageFilePath = FPackageName::LongPackageNameToFilename(BlueprintPackage->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(BlueprintPackage, ActorBlueprint, *BlueprintPackageFilePath, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save actor blueprint: %s"), *BlueprintPackageFilePath);
		return nullptr;
	}

	return ActorBlueprint;
}
#endif

// Define helper now (was accidentally removed)
static UBlueprint* SaveMMDBlueprintAsset(AActor* TargetActor, const FString& FolderPath, const FString& AssetName, bool bReplaceInLevel)
{
	if (!TargetActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDBlueprintAsset: TargetActor is null."));
		return nullptr;
	}
#if WITH_EDITOR
	// 1) 鐢熸垚鍞竴鍖呭悕鍜岃祫婧愬悕
	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const FString TargetLongPackageName = FolderPath / AssetName; // 渚嬪 /Game/MMDModels/Foo
		AssetToolsModule.Get().CreateUniqueAssetName(TargetLongPackageName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	// 2) 鍒涘缓鍖呬笌钃濆浘锛堜娇鐢?CreateBlueprint锛岃閬?CreateBlueprintFromActor 鐨勯噸杞藉樊寮傦級
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *UniquePackageName);
		return nullptr;
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		TargetActor->GetClass(),      // 鐖剁被锛氫笌瀹炰緥鐩稿悓
		Package,                      // 澶栭儴锛氬寘
		*UniqueAssetName,             // 璧勬簮鍚?
		BPTYPE_Normal,                // 钃濆浘绫诲瀷
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);
	if (!NewBP)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBlueprint failed for: %s/%s"), *UniquePackageName, *UniqueAssetName);
		return nullptr;
	}

	// 3) 缂栬瘧锛岀‘淇?GeneratedClass/CDO 鍙敤
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	// 4) 灏嗗疄渚嬪睘鎬у鍒跺埌钃濆浘 CDO锛堝弬鏁伴『搴忥細Old=瀹炰緥锛孨ew=CDO锛?
	if (UClass* GenClass = NewBP->GeneratedClass)
	{
		if (UObject* BPCDO = GenClass->GetDefaultObject(/*bCreateIfNeeded*/true))
		{
			UEngine::FCopyPropertiesForUnrelatedObjectsParams Params; // 鎸夌幇鏈?API锛岀Щ闄や笉瀛樺湪鐨勫瓧娈佃缃?
			UEngine::CopyPropertiesForUnrelatedObjects(/*OldObject=*/TargetActor, /*NewObject=*/BPCDO, Params);
		}
	}

	// 5) 鏍囪鍜屾敞鍐?
	NewBP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBP);

	// 6) 淇濆瓨锛堜娇鐢?FSavePackageArgs 鏂?API锛?
	const EObjectFlags TopLevelFlags = RF_Public | RF_Standalone;
	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = TopLevelFlags;
	SaveArgs.SaveFlags = SAVE_None;               // 瑙嗛渶瑕佽缃?SAVE_NoError 绛?
	SaveArgs.Error = GError;                      // 閿欒杈撳嚭
	SaveArgs.bWarnOfLongFilename = false;

	if (!UPackage::SavePackage(Package, /*InBase*/nullptr, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("SavePackage failed: %s"), *FilePath);
	}

	// 7) 鍏虫敞钃濆浘
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewBP);
	return NewBP;
#else
	UE_LOG(LogTemp, Error, TEXT("SaveActorInstanceAsBlueprint: editor-only function"));
	return nullptr;
#endif
}

static UAnimationAsset* SaveMMDAnimationAsset(UAnimSequence* AnimSeq, const FString& FolderPath, const FString& AssetName)
{
	if (!AnimSeq)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: AnimSeq is null."));
		return nullptr;
	}
	(void)FolderPath;
	(void)AssetName;
	if (!AnimSeq->GetOutermost())
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: AnimSeq has no outer package."));
		return nullptr;
	}
#if WITH_EDITOR
	AnimSeq->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(AnimSeq);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		AnimSeq->GetOutermost()->GetName(),
		FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (!UPackage::SavePackage(AnimSeq->GetOutermost(), AnimSeq, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDAnimationAsset: SavePackage failed: %s"), *FilePath);
	}
#endif
	return AnimSeq;
}

#if WITH_EDITOR
static FString SanitizeMMDAssetName(const FString& InName)
{
	FString Result = InName;
	const TCHAR InvalidChars[] = TEXT(" .,:;!@#$%^&*()+={}[]|\\/'\"<>?");
	for (const TCHAR InvalidChar : InvalidChars)
	{
		if (InvalidChar == TEXT('\0'))
		{
			break;
		}
		Result.ReplaceCharInline(InvalidChar, TEXT('_'));
	}
	return Result.IsEmpty() ? TEXT("VMD") : Result;
}

static FString SanitizeMMDModelFolderName(const FString& InName)
{
	FString Result = SanitizeMMDAssetName(InName);
	while (Result.Contains(TEXT("__")))
	{
		Result.ReplaceInline(TEXT("__"), TEXT("_"));
	}
	if (!Result.IsEmpty() && !FChar::IsAlpha(Result[0]))
	{
		Result = TEXT("M_") + Result;
	}
	return Result.IsEmpty() ? TEXT("M_Unknown") : Result;
}

static FString GetMMDModelLevelSequenceFolder(AActor* Actor, USkeletalMeshComponent* SkelComp)
{
	const FString MMDModelsRoot = TEXT("/Game/MMDModels");

	if (const AMMDActor* MMDActor = Cast<AMMDActor>(Actor))
	{
		if (!MMDActor->SourcePMXFilePath.IsEmpty())
		{
			return MMDModelsRoot / SanitizeMMDModelFolderName(FPaths::GetBaseFilename(MMDActor->SourcePMXFilePath)) / TEXT("LevelSequence");
		}

		if (Actor->GetClass())
		{
			if (const AMMDActor* CDO = Cast<AMMDActor>(Actor->GetClass()->GetDefaultObject()))
			{
				if (!CDO->SourcePMXFilePath.IsEmpty())
				{
					return MMDModelsRoot / SanitizeMMDModelFolderName(FPaths::GetBaseFilename(CDO->SourcePMXFilePath)) / TEXT("LevelSequence");
				}
			}
		}
	}

	if (SkelComp)
	{
		if (USkeletalMesh* SkeletalMesh = SkelComp->GetSkeletalMeshAsset())
		{
			const FString MeshPackageName = SkeletalMesh->GetOutermost()->GetName();
			const FString MeshPackagePath = FPackageName::GetLongPackagePath(MeshPackageName);
			const FString ModelPathPrefix = MMDModelsRoot / TEXT("");
			if (MeshPackagePath.StartsWith(ModelPathPrefix))
			{
				FString RelativePath = MeshPackagePath.RightChop(ModelPathPrefix.Len());
				FString ModelFolder;
				if (RelativePath.Split(TEXT("/"), &ModelFolder, nullptr))
				{
					if (!ModelFolder.IsEmpty())
					{
						return MMDModelsRoot / ModelFolder / TEXT("LevelSequence");
					}
				}
				else if (!RelativePath.IsEmpty())
				{
					return MMDModelsRoot / RelativePath / TEXT("LevelSequence");
				}
			}
		}
	}

	return MMDModelsRoot / TEXT("LevelSequence");
}

static FVector ConvertMMDPositionToUnrealCamera(const FVector& InPosition, double Scale)
{
	return FVector(InPosition.X * Scale, -InPosition.Z * Scale, InPosition.Y * Scale);
}

static FVector ConvertMMDDirectionToUnrealCamera(const FVector& InDirection)
{
	return FVector(InDirection.X, -InDirection.Z, InDirection.Y);
}

static FQuat MakeMMDCameraOrbitRotation(const FVector& InRotationRadians)
{
	const FQuat Yaw(FVector(0.0, 1.0, 0.0), -InRotationRadians.Y);
	const FQuat Pitch(FVector(1.0, 0.0, 0.0), -InRotationRadians.X);
	const FQuat Roll(FVector(0.0, 0.0, 1.0), -InRotationRadians.Z);
	return (Roll * Pitch * Yaw).GetNormalized();
}

static FTransform ConvertVMDCameraKeyToUnrealTransform(const VMDCameraKeyframe& Keyframe, double Scale)
{
	const FVector TargetMMD = Keyframe.Interest;
	const FQuat CameraOrbitRotationMMD = MakeMMDCameraOrbitRotation(Keyframe.Rotation);
	const FVector CameraOffsetMMD = CameraOrbitRotationMMD.RotateVector(FVector(0.0, 0.0, Keyframe.Distance));
	const FVector CameraPositionMMD = TargetMMD + CameraOffsetMMD;

	const FVector Location = ConvertMMDPositionToUnrealCamera(CameraPositionMMD, Scale);
	const FVector Forward = ConvertMMDDirectionToUnrealCamera(CameraOrbitRotationMMD.RotateVector(FVector(0.0, 0.0, 1.0))).GetSafeNormal();
	const FVector Up = ConvertMMDDirectionToUnrealCamera(CameraOrbitRotationMMD.RotateVector(FVector(0.0, 1.0, 0.0))).GetSafeNormal();
	const FRotator Rotation = Forward.IsNearlyZero()
		? FRotator::ZeroRotator
		: FRotationMatrix::MakeFromXZ(Forward, Up.IsNearlyZero() ? FVector::UpVector : Up).Rotator();

	return FTransform(Rotation, Location, FVector::OneVector);
}

static float ConvertMMDVerticalFOVToUnrealHorizontalFOV(float VerticalFOVDegrees, float AspectRatio = 16.0f / 9.0f)
{
	const float ClampedVerticalFOV = FMath::Clamp(VerticalFOVDegrees, 1.0f, 179.0f);
	const float HalfVerticalRadians = FMath::DegreesToRadians(ClampedVerticalFOV) * 0.5f;
	return FMath::RadiansToDegrees(2.0f * FMath::Atan(FMath::Tan(HalfVerticalRadians) * AspectRatio));
}

static void AddDoubleKey(TMovieSceneChannelHandle<FMovieSceneDoubleChannel> ChannelHandle, FFrameNumber Frame, double Value)
{
	if (FMovieSceneDoubleChannel* Channel = ChannelHandle.Get())
	{
		Channel->AddCubicKey(Frame, Value);
	}
}

static void AddCameraTransformKey(UMovieScene3DTransformSection* Section, FFrameNumber Frame, const FTransform& Transform)
{
	if (!Section)
	{
		return;
	}

	const FVector Location = Transform.GetLocation();
	const FRotator Rotation = Transform.Rotator();
	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();

	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Location.X")), Frame, Location.X);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Location.Y")), Frame, Location.Y);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Location.Z")), Frame, Location.Z);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Rotation.X")), Frame, Rotation.Roll);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Rotation.Y")), Frame, Rotation.Pitch);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Rotation.Z")), Frame, Rotation.Yaw);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Scale.X")), Frame, 1.0);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Scale.Y")), Frame, 1.0);
	AddDoubleKey(ChannelProxy.GetChannelByName<FMovieSceneDoubleChannel>(TEXT("Scale.Z")), Frame, 1.0);
}

static void AddIdentityTransformTrack(UMovieScene* MovieScene, const FGuid& ObjectGuid, int32 DurationFrames)
{
	if (!MovieScene || !ObjectGuid.IsValid())
	{
		return;
	}

	if (UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(ObjectGuid))
	{
		TransformTrack->SetPropertyNameAndPath(TEXT("Transform"), TEXT("Transform"));
		if (UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection()))
		{
			TransformTrack->AddSection(*TransformSection);
			TransformSection->SetRange(TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(FMath::Max(DurationFrames, 1)))));
			AddCameraTransformKey(TransformSection, FFrameNumber(0), FTransform::Identity);
		}
	}
}

static void ConfigureMMDMovieSceneTiming(UMovieScene* MovieScene, int32 DurationFrames)
{
	if (!MovieScene)
	{
		return;
	}

	const FFrameRate FrameRate(30, 1);
	MovieScene->SetDisplayRate(FrameRate);
	MovieScene->SetTickResolutionDirectly(FrameRate);
	MovieScene->SetEvaluationType(EMovieSceneEvaluationType::FrameLocked);
	MovieScene->SetPlaybackRange(FFrameNumber(0), FMath::Max(DurationFrames, 1));
}

static USkeletalMeshComponent* FindSelectedSkeletalMeshComponent()
{
	if (!GEditor)
	{
		return nullptr;
	}
	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		return nullptr;
	}
	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		if (AActor* Actor = Cast<AActor>(*Iter))
		{
			if (USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>())
			{
				return SkelComp;
			}
		}
	}
	return nullptr;
}

struct FMMDSelectedAnimationTarget
{
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
	USkeletalMesh* SkeletalMesh = nullptr;
	FString SourcePMXFilePath;
};

static bool TryGetAnimationTargetFromActor(AActor* Actor, FMMDSelectedAnimationTarget& OutTarget)
{
	if (!Actor)
	{
		return false;
	}

	OutTarget = FMMDSelectedAnimationTarget{};
	if (AMMDActor* MMDActor = Cast<AMMDActor>(Actor))
	{
		OutTarget.SkeletalMeshComponent = MMDActor->GetMeshComponent();
		OutTarget.SourcePMXFilePath = MMDActor->SourcePMXFilePath;
	}
	else if (USkeletalMeshComponent* SkelComp = Actor->FindComponentByClass<USkeletalMeshComponent>())
	{
		OutTarget.SkeletalMeshComponent = SkelComp;
	}

	if (OutTarget.SourcePMXFilePath.IsEmpty() && Actor->GetClass())
	{
		if (const AMMDActor* CDO = Cast<AMMDActor>(Actor->GetClass()->GetDefaultObject()))
		{
			OutTarget.SourcePMXFilePath = CDO->SourcePMXFilePath;
		}
	}

	if (OutTarget.SkeletalMeshComponent)
	{
		OutTarget.SkeletalMesh = OutTarget.SkeletalMeshComponent->GetSkeletalMeshAsset();
	}

	return OutTarget.SkeletalMeshComponent && OutTarget.SkeletalMesh;
}

static bool TryGetAnimationTargetFromActorClass(UClass* ActorClass, FMMDSelectedAnimationTarget& OutTarget)
{
	if (!ActorClass || !ActorClass->IsChildOf(AMMDActor::StaticClass()))
	{
		return false;
	}

	AMMDActor* CDO = Cast<AMMDActor>(ActorClass->GetDefaultObject());
	if (!CDO)
	{
		return false;
	}

	OutTarget = FMMDSelectedAnimationTarget{};
	OutTarget.SkeletalMeshComponent = CDO->GetMeshComponent();
	OutTarget.SourcePMXFilePath = CDO->SourcePMXFilePath;
	if (OutTarget.SkeletalMeshComponent)
	{
		OutTarget.SkeletalMesh = OutTarget.SkeletalMeshComponent->GetSkeletalMeshAsset();
	}

	return OutTarget.SkeletalMeshComponent && OutTarget.SkeletalMesh;
}

static UClass* ResolveMMDActorClassFromAsset(UObject* Asset)
{
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		return Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AMMDActor::StaticClass())
			? Blueprint->GeneratedClass
			: nullptr;
	}

	if (UClass* ClassAsset = Cast<UClass>(Asset))
	{
		return ClassAsset->IsChildOf(AMMDActor::StaticClass()) ? ClassAsset : nullptr;
	}

	return nullptr;
}

static FString GetBlueprintGeneratedClassPathFromAssetData(const FAssetData& AssetData)
{
	FString GeneratedClassPath;
	if (!AssetData.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClassPath))
	{
		AssetData.GetTagValue(TEXT("GeneratedClass"), GeneratedClassPath);
	}

	if (GeneratedClassPath.StartsWith(TEXT("BlueprintGeneratedClass'")) && GeneratedClassPath.EndsWith(TEXT("'")))
	{
		GeneratedClassPath = GeneratedClassPath.Mid(24, GeneratedClassPath.Len() - 25);
	}
	else
	{
		GeneratedClassPath = FPackageName::ExportTextPathToObjectPath(GeneratedClassPath);
	}

	return GeneratedClassPath;
}

static UClass* ResolveMMDActorClassFromAssetData(const FAssetData& AssetData)
{
	const FString GeneratedClassPath = GetBlueprintGeneratedClassPathFromAssetData(AssetData);
	if (!GeneratedClassPath.IsEmpty())
	{
		if (UClass* GeneratedClass = LoadClass<UObject>(nullptr, *GeneratedClassPath))
		{
			return GeneratedClass->IsChildOf(AMMDActor::StaticClass()) ? GeneratedClass : nullptr;
		}
	}

	return ResolveMMDActorClassFromAsset(AssetData.GetAsset());
}

static bool IsMMDActorBlueprintAssetData(const FAssetData& AssetData)
{
	const FString GeneratedClassPath = GetBlueprintGeneratedClassPathFromAssetData(AssetData);
	if (GeneratedClassPath.IsEmpty())
	{
		return false;
	}

	const FSoftClassPath GeneratedSoftClassPath(GeneratedClassPath);
	const FTopLevelAssetPath GeneratedClassAssetPath = GeneratedSoftClassPath.GetAssetPath();
	const FTopLevelAssetPath MMDActorClassPath = AMMDActor::StaticClass()->GetClassPathName();

	static TSet<FTopLevelAssetPath> DerivedClassNames;
	static bool bDerivedClassNamesInitialized = false;
	if (!bDerivedClassNamesInitialized)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FTopLevelAssetPath> BaseClassNames;
		BaseClassNames.Add(MMDActorClassPath);
		TSet<FTopLevelAssetPath> ExcludedClassNames;
		AssetRegistry.GetDerivedClassNames(BaseClassNames, ExcludedClassNames, DerivedClassNames);
		bDerivedClassNamesInitialized = true;
	}

	return GeneratedClassAssetPath == MMDActorClassPath || DerivedClassNames.Contains(GeneratedClassAssetPath);
}

static FString GetMMDModelAnimationFolder(UClass* ActorClass)
{
	FMMDSelectedAnimationTarget Target;
	if (!TryGetAnimationTargetFromActorClass(ActorClass, Target))
	{
		return FString();
	}

	FString LevelSequenceFolder = GetMMDModelLevelSequenceFolder(nullptr, Target.SkeletalMeshComponent);
	if (LevelSequenceFolder.RemoveFromEnd(TEXT("/LevelSequence")))
	{
		return LevelSequenceFolder / TEXT("Animation");
	}
	return FString();
}

static bool ShouldFilterAnimationOutsideCurrentMMDModel(const FAssetData& AssetData, UClass* ActorClass)
{
	const FString AnimationFolder = GetMMDModelAnimationFolder(ActorClass);
	return !AnimationFolder.IsEmpty() && !AssetData.PackagePath.ToString().StartsWith(AnimationFolder);
}

static bool ShouldFilterAnimationOutsideFolder(const FAssetData& AssetData, const FString& AnimationFolder)
{
	return !AnimationFolder.IsEmpty() && !AssetData.PackagePath.ToString().StartsWith(AnimationFolder);
}

static bool TryGetSelectedAnimationTarget(FMMDSelectedAnimationTarget& OutTarget, FString& OutError)
{
	OutTarget = FMMDSelectedAnimationTarget{};
	OutError.Reset();

	if (!GEditor)
	{
		OutError = TEXT("GEditor is unavailable.");
		return false;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!SelectedActors || SelectedActors->Num() == 0)
	{
		OutError = TEXT("Please select an MMD Actor or an Actor with SkeletalMesh in the level.");
		return false;
	}

	for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (!Actor)
		{
			continue;
		}

		if (TryGetAnimationTargetFromActor(Actor, OutTarget))
		{
			return true;
		}
	}

	OutError = TEXT("Selected Actor has no usable SkeletalMesh.");
	return false;
}

static UAnimSequence* BuildAnimSequenceFromVMD(const VMDData& VMDInfo, USkeletalMesh* SkeletalMesh, const FString& VMDFilePath)
{
	UE_LOG(LogTemp, Warning, TEXT("BuildAnimSequenceFromVMD: This function is deprecated. Use ImportVMDAnimation instead."));
	return nullptr;
}

static void ConvertPMXPhysicsToAnimNodeData(const PMXDatas& PMXData, TArray<FMMDPhysicsRigidBodyData>& OutRigids, TArray<FMMDPhysicsJointData>& OutJoints)
{
	OutRigids.Reset(PMXData.ModelRigids.Num());
	for (const FPMXRigid& Rigid : PMXData.ModelRigids)
	{
		FMMDPhysicsRigidBodyData& RigidData = OutRigids.AddDefaulted_GetRef();
		RigidData.Name = Rigid.NameJP;
		RigidData.NameEN = Rigid.NameEN;
		RigidData.RelatedBoneIndex = Rigid.RelatedBoneIndex;
		RigidData.ShapeType = Rigid.ShapeType;
		RigidData.ShapeSize = Rigid.Size;
		RigidData.ShapePosition = Rigid.Position;
		RigidData.ShapeRotation = Rigid.Rotation;
		RigidData.Mass = Rigid.Mass;
		RigidData.Friction = Rigid.Friction;
		RigidData.Restitution = Rigid.Restitution;
		RigidData.CollisionGroup = Rigid.Group;
		RigidData.CollisionMask = Rigid.CollisionMask;
		RigidData.PhysicsMode = Rigid.PhysicsMode;
		RigidData.LinearDamping = Rigid.LinearDamping;
		RigidData.AngularDamping = Rigid.AngularDamping;
	}

	OutJoints.Reset(PMXData.ModelJoints.Num());
	for (const FPMXJoint& Joint : PMXData.ModelJoints)
	{
		FMMDPhysicsJointData& JointData = OutJoints.AddDefaulted_GetRef();
		JointData.Name = Joint.NameJP;
		JointData.NameEN = Joint.NameEN;
		JointData.JointType = Joint.JointType;
		JointData.RigidBodyIndexA = Joint.RigidA;
		JointData.RigidBodyIndexB = Joint.RigidB;
		JointData.Position = Joint.Position;
		JointData.Rotation = Joint.Rotation;
		JointData.LimitPositionMin = Joint.LimitPosLower;
		JointData.LimitPositionMax = Joint.LimitPosUpper;
		JointData.LimitRotationMin = Joint.LimitRotLower;
		JointData.LimitRotationMax = Joint.LimitRotUpper;
		JointData.SpringPosition = Joint.SpringPos;
		JointData.SpringRotation = Joint.SpringRot;
	}
}

static bool BuildLocalTransformsFromAnimSequence(UAnimSequence* AnimSequence, const FReferenceSkeleton& RefSkeleton, float BakeFrameRate, int32 NumKeys, TArray<TArray<FTransform>>& OutLocalTransforms, FString& OutError)
{
	const IAnimationDataModel* DataModel = AnimSequence ? AnimSequence->GetDataModel() : nullptr;
	if (!DataModel)
	{
		OutError = TEXT("Input AnimSequence has no data model.");
		return false;
	}

	const int32 NumBones = RefSkeleton.GetNum();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	OutLocalTransforms.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		OutLocalTransforms[BoneIndex].SetNum(NumKeys);
		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			OutLocalTransforms[BoneIndex][FrameIndex] = DefaultTransform;
		}
	}

	const float SourceLength = FMath::Max(DataModel->GetPlayLength(), KINDA_SMALL_NUMBER);
	TArray<FName> TrackNames;
	DataModel->GetBoneTrackNames(TrackNames);
	for (const FName& BoneName : TrackNames)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			const double Time = FMath::Clamp(
				static_cast<double>(FrameIndex) / FMath::Max(static_cast<double>(BakeFrameRate), static_cast<double>(KINDA_SMALL_NUMBER)),
				0.0,
				static_cast<double>(SourceLength));
			OutLocalTransforms[BoneIndex][FrameIndex] = DataModel->EvaluateBoneTrackTransform(
				BoneName,
				DataModel->GetFrameRate().AsFrameTime(Time),
				EAnimInterpolationType::Linear);
			if (OutLocalTransforms[BoneIndex][FrameIndex].ContainsNaN())
			{
				OutLocalTransforms[BoneIndex][FrameIndex] = DefaultTransform;
			}
		}
	}

	return true;
}

static void BuildComponentTransformsForFrame(const FReferenceSkeleton& RefSkeleton, const TArray<TArray<FTransform>>& LocalTransforms, int32 FrameIndex, TArray<FTransform>& OutComponentTransforms)
{
	const int32 NumBones = RefSkeleton.GetNum();
	OutComponentTransforms.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FTransform& LocalTransform = LocalTransforms[BoneIndex][FrameIndex];
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		OutComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
			? LocalTransform * OutComponentTransforms[ParentIndex]
			: LocalTransform;
	}
}

static void WriteComponentTransformsToLocalFrame(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& ComponentTransforms, int32 FrameIndex, TArray<TArray<FTransform>>& InOutLocalTransforms)
{
	for (int32 BoneIndex = 0; BoneIndex < ComponentTransforms.Num(); ++BoneIndex)
	{
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		InOutLocalTransforms[BoneIndex][FrameIndex] = ParentIndex != INDEX_NONE && ComponentTransforms.IsValidIndex(ParentIndex)
			? ComponentTransforms[BoneIndex].GetRelativeTransform(ComponentTransforms[ParentIndex])
			: ComponentTransforms[BoneIndex];
	}
}

static UAnimSequence* CreatePhysicsBakedAnimSequence(UAnimSequence* SourceAnim, USkeletalMesh* SkeletalMesh, const FReferenceSkeleton& RefSkeleton, const TArray<TArray<FTransform>>& LocalTransforms, float BakeFrameRate, int32 NumFrames, FString& OutError)
{
	if (!SourceAnim || !SkeletalMesh || !SkeletalMesh->GetSkeleton())
	{
		OutError = TEXT("Invalid source animation or skeletal mesh.");
		return nullptr;
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const FString SourcePath = FPackageName::GetLongPackagePath(SourceAnim->GetOutermost()->GetName());
		const FString BaseAssetPath = SourcePath / (SourceAnim->GetName() + TEXT("_MMDPhys"));
		AssetToolsModule.Get().CreateUniqueAssetName(BaseAssetPath, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	UAnimSequence* BakedAnim = Package ? NewObject<UAnimSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone) : nullptr;
	if (!BakedAnim)
	{
		OutError = TEXT("Failed to create baked AnimSequence package.");
		return nullptr;
	}

	BakedAnim->SetSkeleton(SkeletalMesh->GetSkeleton());
	BakedAnim->SetPreviewMesh(SkeletalMesh);

	IAnimationDataController& Controller = BakedAnim->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Bake MMD Physics")));
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(FMath::RoundToInt32(BakeFrameRate), 1), true);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), true);

	const int32 NumKeys = NumFrames + 1;
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		const FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;
		PosKeys.SetNum(NumKeys);
		RotKeys.SetNum(NumKeys);
		ScaleKeys.SetNum(NumKeys);

		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			const FTransform& LocalTransform = LocalTransforms[BoneIndex][FrameIndex];
			PosKeys[FrameIndex] = FVector3f(LocalTransform.GetTranslation());
			RotKeys[FrameIndex] = FQuat4f(LocalTransform.GetRotation().GetNormalized());
			ScaleKeys[FrameIndex] = FVector3f(LocalTransform.GetScale3D());
		}

		Controller.AddBoneCurve(BoneName, false);
		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	if (const IAnimationDataModel* SourceDataModel = SourceAnim->GetDataModel())
	{
		for (const FFloatCurve& FloatCurve : SourceDataModel->GetFloatCurves())
		{
			const FName CurveName = FloatCurve.GetName();
			if (CurveName == NAME_None)
			{
				continue;
			}

			const FAnimationCurveIdentifier CurveId(CurveName, ERawCurveTrackTypes::RCT_Float);
			Controller.AddCurve(CurveId, FloatCurve.GetCurveTypeFlags(), false);
			Controller.SetCurveKeys(CurveId, FloatCurve.FloatCurve.GetConstRefOfKeys(), false);
			if (USkeleton* Skeleton = SkeletalMesh->GetSkeleton())
			{
				Skeleton->AccumulateCurveMetaData(CurveName, false, true);
			}
		}
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket();

	BakedAnim->PostEditChange();
	BakedAnim->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BakedAnim);
	Package->MarkPackageDirty();

	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(Package, BakedAnim, *FilePath, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save baked animation: %s"), *FilePath);
		return nullptr;
	}

	return BakedAnim;
}
#endif


TWeakPtr<MMDImportSetting> MMDImportSetting::CurrentInstance = nullptr; // 闈欐€佹垚鍛樺垵濮嬪寲

void MMDImportSetting::RegisterInstance(const TSharedRef<MMDImportSetting>& InstanceRef)
{
	CurrentInstance = InstanceRef;
}

namespace
{
	// ===== 二次元主题配色（马卡龙 + 深紫底）=====
	const FLinearColor GMMDColorPink   = FLinearColor(1.00f, 0.42f, 0.62f, 1.0f);  // 樱花粉
	const FLinearColor GMMDColorBlue   = FLinearColor(0.35f, 0.72f, 1.00f, 1.0f);  // 天蓝
	const FLinearColor GMMDColorMint   = FLinearColor(0.24f, 0.90f, 0.78f, 1.0f);  // 薄荷
	const FLinearColor GMMDColorPurple = FLinearColor(0.66f, 0.47f, 1.00f, 1.0f);  // 紫

	const FLinearColor GMMDPanelBG   = FLinearColor(0.10f, 0.07f, 0.16f, 1.0f);   // 深紫底
	const FLinearColor GMMDSectionBG = FLinearColor(0.14f, 0.10f, 0.22f, 1.0f);   // 分区底
	const FLinearColor GMMDHeaderBG  = FLinearColor(0.17f, 0.12f, 0.27f, 1.0f);   // 顶栏/状态栏底
	const FLinearColor GMMDTextDim   = FLinearColor(0.72f, 0.68f, 0.85f, 1.0f);   // 灰紫文字
	const FLinearColor GMMDTextBright= FLinearColor(0.96f, 0.95f, 1.00f, 1.0f);   // 亮文字

	// 二次元大圆角
	constexpr float GMMDRadius = 10.0f;

	// 按钮样式工厂：普通/悬停/按下 三态 + 大圆角
	FButtonStyle MMDMakeButtonStyle(const FLinearColor& Normal, const FLinearColor& Hovered, const FLinearColor& Pressed)
	{
		FButtonStyle S;
		S.SetNormal(FSlateRoundedBoxBrush(Normal, GMMDRadius));
		S.SetHovered(FSlateRoundedBoxBrush(Hovered, GMMDRadius));
		S.SetPressed(FSlateRoundedBoxBrush(Pressed, GMMDRadius));
		S.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.20f, 0.17f, 0.28f, 1.0f), GMMDRadius));
		S.SetNormalPadding(FMargin(14.0f, 7.0f));
		S.SetPressedPadding(FMargin(14.0f, 8.0f, 14.0f, 6.0f));
		return S;
	}

	const FButtonStyle& MMDGetPrimaryButtonStyle()
	{
		static const FButtonStyle Style = MMDMakeButtonStyle(
			FLinearColor(0.85f, 0.28f, 0.52f, 1.0f),   // 樱花粉（加深，白字可读）
			FLinearColor(0.95f, 0.40f, 0.64f, 1.0f),
			FLinearColor(0.70f, 0.22f, 0.42f, 1.0f));
		return Style;
	}

	const FButtonStyle& MMDGetSecondaryButtonStyle()
	{
		static const FButtonStyle Style = MMDMakeButtonStyle(
			FLinearColor(0.30f, 0.42f, 0.78f, 1.0f),   // 蓝紫
			FLinearColor(0.42f, 0.56f, 0.92f, 1.0f),
			FLinearColor(0.22f, 0.32f, 0.62f, 1.0f));
		return Style;
	}

	const FButtonStyle& MMDGetAccentButtonStyle()
	{
		static const FButtonStyle Style = MMDMakeButtonStyle(
			FLinearColor(0.08f, 0.50f, 0.42f, 1.0f),   // 薄荷绿（加深，白字可读）
			FLinearColor(0.14f, 0.64f, 0.54f, 1.0f),
			FLinearColor(0.05f, 0.40f, 0.34f, 1.0f));
		return Style;
	}

	const FButtonStyle& MMDGetWarnButtonStyle()
	{
		static const FButtonStyle Style = MMDMakeButtonStyle(
			FLinearColor(0.82f, 0.30f, 0.42f, 1.0f),   // 红（清除）
			FLinearColor(0.95f, 0.42f, 0.54f, 1.0f),
			FLinearColor(0.66f, 0.22f, 0.32f, 1.0f));
		return Style;
	}

	TSharedRef<SWidget> MMDMakeButton(const FText& Label, const FButtonStyle& Style, FOnClicked Handler, const FLinearColor& TextColor = FLinearColor::White)
	{
		return SNew(SButton)
			.ButtonStyle(&Style)
			.OnClicked(Handler)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
				.ColorAndOpacity(FSlateColor(TextColor))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			];
	}

	TSharedRef<SWidget> MMDMakeSectionHeader(const FText& Title, const FLinearColor& Color)
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString(TEXT("◆ ")) + Title.ToString()))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(Color))
				.Margin(FMargin(2.0f, 2.0f))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator)
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f)))
			];
	}

	TSharedRef<SWidget> MMDMakeSolidPanel(const FLinearColor& Color, TSharedRef<SWidget> Content, FMargin Padding = FMargin(8.0f))
	{
		return SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(Color))
			.Padding(Padding)
			[
				Content
			];
	}
}

void MMDImportSetting::Construct(const FArguments& InArgs)
{
	ViewPanel = InArgs._ViewPanel;
	if (!ViewPanel.IsValid())
	{
		SAssignNew(ViewPanel, MMDViewPanel);
	}

	// ================= 左侧边栏 =================
	TSharedRef<SVerticalBox> Sidebar = SNew(SVerticalBox);

	auto AddSection = [&Sidebar](const FText& Title, const FLinearColor& Color) -> TSharedRef<SVerticalBox>
	{
		Sidebar->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 14.0f, 0.0f, 2.0f)
			[
				MMDMakeSectionHeader(Title, Color)
			];

		TSharedRef<SVerticalBox> Section = SNew(SVerticalBox);
		Sidebar->AddSlot()
			.AutoHeight()
			.Padding(8.0f, 2.0f, 8.0f, 4.0f)
			[
				Section
			];
		return Section;
	};

	auto AddButton = [](TSharedRef<SVerticalBox> Section, const FText& Label, const FButtonStyle& Style, FOnClicked Handler)
	{
		Section->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				MMDMakeButton(Label, Style, Handler)
			];
	};

	// ---- 模型导入 ----
	{
		TSharedRef<SVerticalBox> Sec = AddSection(FText::FromString(TEXT("模型导入")), GMMDColorPink);
		AddButton(Sec, FText::FromString(TEXT("导入 PMX 模型")), MMDGetPrimaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportModelClicked));
		AddButton(Sec, FText::FromString(TEXT("导入 VMD 动作")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportVMDClicked));
		AddButton(Sec, FText::FromString(TEXT("追加表情 VMD")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnAppendFacialVMDClicked));
		AddButton(Sec, FText::FromString(TEXT("导入 VMD 相机")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportVMDCameraClicked));
	}

	// ---- 工具 ----
	{
		TSharedRef<SVerticalBox> Sec = AddSection(FText::FromString(TEXT("工具")), GMMDColorBlue);
		AddButton(Sec, FText::FromString(TEXT("加载 MMD 角色")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateLambda([this]() { LoadSelectedMMDActor(); return FReply::Handled(); }));
		AddButton(Sec, FText::FromString(TEXT("序列合成")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnOpenSequenceComposerClicked));
		AddButton(Sec, FText::FromString(TEXT("烘焙物理")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnOpenPhysicsBakeClicked));
	}

	// ---- 着色器预设 ----
	{
		TSharedRef<SVerticalBox> Sec = AddSection(FText::FromString(TEXT("着色器预设")), GMMDColorPurple);
		Sec->AddSlot()
			.AutoHeight()
			.Padding(2.0f, 1.0f, 2.0f, 5.0f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("TMMD Aurora Jewel Toon · R56")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(GMMDTextDim))
			];
		AddButton(Sec, FText::FromString(TEXT("应用并保存到 Actor 资产")), MMDGetPrimaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnApplyAuroraJewelToonClicked));
		AddButton(Sec, FText::FromString(TEXT("撤回 Actor 资产材质")), MMDGetWarnButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnUndoShaderPresetClicked));
	}

	// ---- 光照环境（场景切换：每个环境是一个可编辑的 .umap，点"打开"切换到该场景调灯）----
	auto AddLightingEnvRow = [this](TSharedRef<SVerticalBox> Sec, EMMDLightingEnvironment Env)
	{
		const FString Name = UMMDLightingEnvironmentLibrary::GetEnvironmentDisplayName(Env);
		const FString EnName = UMMDLightingEnvironmentLibrary::GetEnvironmentAssetName(Env);
		const bool bSpecial = UMMDLightingEnvironmentLibrary::IsSpecialEnvironment(Env);
		const FString Label = bSpecial ? (Name + TEXT("（特殊）")) : Name;

		Sec->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(2.0f).Padding(0.0f, 0.0f, 2.0f, 0.0f)
				[
					MMDMakeButton(FText::FromString(FString::Printf(TEXT("Panel 预览：%s"), *Label)), MMDGetAccentButtonStyle(),
						FOnClicked::CreateLambda([this, Env, Name]()
						{
							if (ViewPanel.IsValid())
							{
								ViewPanel->ApplyLightingEnvironment(Env);
								ViewPanel->ResetPreviewCamera();
								ShowImportProgress(FString::Printf(TEXT("Panel 已切换：%s"), *Name), EMMDMessageType::Success);
							}
							else
							{
								ShowImportProgress(TEXT("Panel 预览视口尚未初始化"), EMMDMessageType::Error);
							}
							return FReply::Handled();
						}))
				]
				+ SHorizontalBox::Slot().FillWidth(0.85f).Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					MMDMakeButton(FText::FromString(FString::Printf(TEXT("打开关卡 (%s)"), *EnName)), MMDGetSecondaryButtonStyle(),
						FOnClicked::CreateLambda([this, Env, Name]()
						{
							const bool bOpened = UMMDLightingEnvironmentLibrary::OpenEnvironmentLevel(Env);
							if (bOpened && ViewPanel.IsValid())
							{
								ViewPanel->ApplyLightingEnvironment(Env);
								ViewPanel->ResetPreviewCamera();
							}
							ShowImportProgress(bOpened
								? FString::Printf(TEXT("已打开光照关卡并同步 Panel：%s"), *Name)
								: FString::Printf(TEXT("打开光照关卡失败：%s"), *Name),
								bOpened ? EMMDMessageType::Success : EMMDMessageType::Error);
							return FReply::Handled();
						}))
				]
			];
	};

	{
		TSharedRef<SVerticalBox> Sec = AddSection(FText::FromString(TEXT("光照环境 · 标准")), GMMDColorMint);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::Studio3Point);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::Daylight);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::OvercastSoft);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::IndoorWarm);
	}

	{
		TSharedRef<SVerticalBox> Sec = AddSection(FText::FromString(TEXT("光照环境 · 特殊")), GMMDColorPurple);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::NeonNight);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::RimSilhouette);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::GoldenHour);
		AddLightingEnvRow(Sec, EMMDLightingEnvironment::HorrorGreen);

		AddButton(Sec, FText::FromString(TEXT("生成全部场景资产")), MMDGetSecondaryButtonStyle(),
			FOnClicked::CreateLambda([this]()
			{
				const int32 Count = UMMDLightingEnvironmentLibrary::CreateAllEnvironmentLevelAssets();
				ShowImportProgress(FString::Printf(TEXT("已生成 %d 个光照环境关卡资产"), Count),
					Count > 0 ? EMMDMessageType::Success : EMMDMessageType::Warning);
				return FReply::Handled();
			}));

		AddButton(Sec, FText::FromString(TEXT("重建全部 LookDev 场景")), MMDGetPrimaryButtonStyle(),
			FOnClicked::CreateLambda([this]()
			{
				const int32 Count = UMMDLightingEnvironmentLibrary::RebuildAllEnvironmentLevelAssets();
				ShowImportProgress(FString::Printf(TEXT("已重建 %d/8 个 LookDev 场景（舞台、相机、曝光与灯光）"), Count),
					Count == 8 ? EMMDMessageType::Success : EMMDMessageType::Warning);
				return FReply::Handled();
			}));
	}

	// ================= 右侧预览区 =================
	TSharedRef<SWidget> PreviewArea =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SAssignNew(SelectedModelText, STextBlock)
			.Text(FText::FromString(TEXT("模型：未选择")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(GMMDTextDim))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)
		[
			SAssignNew(SelectedAnimText, STextBlock)
			.Text(FText::FromString(TEXT("动画：未选择")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(GMMDTextDim))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			ViewPanel.ToSharedRef()
		];

	// ================= 头部 =================
	TSharedRef<SWidget> Header =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(GMMDHeaderBG))
		.Padding(FMargin(12.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("MMD 工具")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
					.ColorAndOpacity(FSlateColor(GMMDColorPink))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Ue5MMDTools · PMX/VMD 导入")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(GMMDTextDim))
			]
		];

	// ================= 状态栏 =================
	TSharedRef<SWidget> StatusBar =
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FSlateColor(FLinearColor(0.20f, 0.14f, 0.32f, 1.0f)))
		.Padding(FMargin(12.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("状态： ")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(GMMDTextDim))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SAssignNew(StatusText, STextBlock)
				.Text(FText::FromString(TEXT("就绪")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(GMMDTextDim))
			]
		];

	// ================= 整体布局 =================
	TSharedRef<SSplitter> Splitter =
		SNew(SSplitter)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.36f).MinSize(260.0f)
		[
			MMDMakeSolidPanel(GMMDSectionBG,
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					Sidebar
				])
		]
		+ SSplitter::Slot().Value(0.66f)
		[
			PreviewArea
		];

	ChildSlot
	[
		MMDMakeSolidPanel(GMMDPanelBG,
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				Header
			]
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 2.0f, 0.0f, 2.0f)
			[
				Splitter
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				StatusBar
			],
			FMargin(2.0f))
	];
}

void MMDImportSetting::SetSelectedModelTextUI(const FText& Text)
{
	if (SelectedModelText.IsValid())
	{
		SelectedModelText->SetText(FText::FromString(TEXT("模型：") + Text.ToString()));
		SelectedModelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.80f, 0.85f, 0.92f, 1.0f)));
	}
}

void MMDImportSetting::SetSelectedAnimTextUI(const FText& Text)
{
	if (SelectedAnimText.IsValid())
	{
		SelectedAnimText->SetText(FText::FromString(TEXT("动画：") + Text.ToString()));
		SelectedAnimText->SetColorAndOpacity(FSlateColor(FLinearColor(0.80f, 0.85f, 0.92f, 1.0f)));
	}
}

bool MMDImportSetting::SetPreviewActorClassUI(UClass* InClass)
{
	return ViewPanel.IsValid() ? ViewPanel->CreatePreviewActor(InClass) : false;
}

void MMDImportSetting::SetPreviewSkeletalMeshUI(USkeletalMesh* InMesh)
{
	if (ViewPanel.IsValid())
	{
		ViewPanel->ShowImportedSkeletalMesh(InMesh);
	}
}

void MMDImportSetting::SetPreviewAnimationUI(UAnimSequence* InAnim)
{
	if (ViewPanel.IsValid())
	{
		ViewPanel->SetPreviewAnimation(InAnim);
	}
}
FReply MMDImportSetting::OnImportModelClicked()
{
	ImportMMDModel();
	return FReply::Handled();
}
FReply MMDImportSetting::OnImportVMDClicked()
{
	ImportVMDAnimation();
	return FReply::Handled();
}
FReply MMDImportSetting::OnAppendFacialVMDClicked()
{
	AppendFacialVMDToAnimation();
	return FReply::Handled();
}
FReply MMDImportSetting::OnImportVMDCameraClicked()
{
	ImportVMDCameraAnimation();
	return FReply::Handled();
}
FReply MMDImportSetting::OnOpenSequenceComposerClicked()
{
	OpenSequenceComposerWindow();
	return FReply::Handled();
}

FReply MMDImportSetting::OnOpenPhysicsBakeClicked()
{
	OpenPhysicsBakeWindow();
	return FReply::Handled();
}

FReply MMDImportSetting::OnApplyAuroraJewelToonClicked()
{
#if WITH_EDITOR
	UClass* ActorClass = LastLoadedMMDActorClass.Get();
	if (!ActorClass)
	{
		TArray<FAssetData> SelectedAssets;
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		for (const FAssetData& AssetData : SelectedAssets)
		{
			ActorClass = ResolveMMDActorClassFromAssetData(AssetData);
			if (ActorClass)
			{
				break;
			}
		}
	}
	if (!ActorClass && LastImportedMMDActor.IsValid())
	{
		ActorClass = LastImportedMMDActor->GetClass();
	}
	if (!ActorClass && GEditor && GEditor->GetSelectedActors())
	{
		for (FSelectionIterator Iterator(*GEditor->GetSelectedActors()); Iterator; ++Iterator)
		{
			if (AActor* SelectedActor = Cast<AActor>(*Iterator))
			{
				if (SelectedActor->IsA<AMMDActor>())
				{
					ActorClass = SelectedActor->GetClass();
					break;
				}
			}
		}
	}

	UBlueprint* ActorBlueprint = ActorClass ? Cast<UBlueprint>(ActorClass->ClassGeneratedBy) : nullptr;
	FMMDSelectedAnimationTarget Target;
	if (!ActorBlueprint || !TryGetAnimationTargetFromActorClass(ActorClass, Target) || !Target.SkeletalMeshComponent)
	{
		ShowImportProgress(TEXT("请先加载或在内容浏览器选择一个 MMD Actor 蓝图资产"), EMMDMessageType::Warning);
		return FReply::Handled();
	}
	USkeletalMeshComponent* Component = Target.SkeletalMeshComponent;

	UMaterialInterface* OpaquePreset = LoadObject<UMaterialInterface>(nullptr, AuroraJewelOpaquePath);
	UMaterialInterface* TranslucentPreset = LoadObject<UMaterialInterface>(nullptr, AuroraJewelTranslucentPath);
	if (!OpaquePreset || !TranslucentPreset)
	{
		ShowImportProgress(TEXT("Aurora Jewel Toon 预设资产缺失，请重新生成插件预设"), EMMDMessageType::Error);
		return FReply::Handled();
	}

	if (Component->GetNumMaterials() <= 0)
	{
		ShowImportProgress(TEXT("当前 MMD Actor 资产没有可切换的材质槽"), EMMDMessageType::Warning);
		return FReply::Handled();
	}

	bool bAlreadyApplied = true;
	for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
	{
		UMaterialInstance* Instance = Cast<UMaterialInstance>(Component->GetMaterial(SlotIndex));
		if (!Instance || (!Instance->IsChildOf(OpaquePreset) && !Instance->IsChildOf(TranslucentPreset)))
		{
			bAlreadyApplied = false;
			break;
		}
	}
	if (bAlreadyApplied)
	{
		ShowImportProgress(TEXT("当前 MMD Actor 资产已经使用 TMMD Aurora Jewel Toon"), EMMDMessageType::Info);
		return FReply::Handled();
	}

	const FString ActorFolder = FPackageName::GetLongPackagePath(ActorBlueprint->GetOutermost()->GetName());
	const FString MaterialFolder = ActorFolder / TEXT("Materials/AuroraJewel");
	TArray<UMaterialInterface*> ReplacementMaterials;
	TArray<UMaterialInstanceConstant*> CreatedOrUpdatedInstances;
	for (int32 SlotIndex = 0; SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
	{
		UMaterialInterface* SourceMaterial = Component->GetMaterial(SlotIndex);
		const FString AssetName = FString::Printf(
			TEXT("MI_%s_AuroraJewel_%02d"),
			*ActorBlueprint->GetName(),
			SlotIndex);
		FString CreateError;
		UMaterialInstanceConstant* Replacement = MMDCreateOrUpdateAuroraJewelAssetInstance(
			MaterialFolder,
			AssetName,
			SourceMaterial,
			OpaquePreset,
			TranslucentPreset,
			CreateError);
		if (!Replacement)
		{
			ShowImportProgress(CreateError, EMMDMessageType::Error);
			return FReply::Handled();
		}
		ReplacementMaterials.Add(Replacement);
		CreatedOrUpdatedInstances.Add(Replacement);
	}

	for (UMaterialInstanceConstant* Instance : CreatedOrUpdatedInstances)
	{
		if (!MMDSaveAssetPackage(Instance))
		{
			ShowImportProgress(FString::Printf(TEXT("保存材质实例失败：%s"), *Instance->GetPathName()), EMMDMessageType::Error);
			return FReply::Handled();
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("应用 TMMD Aurora Jewel Toon 到 Actor 资产")));
	FMMDMaterialPresetUndoEntry UndoEntry;
	UndoEntry.Component = Component;
	for (UMaterialInterface* OverrideMaterial : Component->OverrideMaterials)
	{
		UndoEntry.Materials.Add(OverrideMaterial);
	}

	MaterialPresetUndoEntries.Reset();
	ActorBlueprint->Modify();
	Component->SetFlags(RF_Transactional);
	Component->Modify();
	for (int32 SlotIndex = 0; SlotIndex < ReplacementMaterials.Num(); ++SlotIndex)
	{
		Component->SetMaterial(SlotIndex, ReplacementMaterials[SlotIndex]);
	}
	Component->MarkRenderStateDirty();
	FBlueprintEditorUtils::MarkBlueprintAsModified(ActorBlueprint);
	MaterialPresetUndoEntries.Add(MoveTemp(UndoEntry));

	if (!MMDSaveAssetPackage(ActorBlueprint))
	{
		ShowImportProgress(TEXT("材质已生成，但保存 MMD Actor 蓝图资产失败"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	LastLoadedMMDActorClass = ActorClass;
	SetPreviewActorClassUI(ActorClass);

	ShowImportProgress(
		FString::Printf(
			TEXT("已保存到 Actor 资产 %s：%d 个材质槽；实例位于 %s，可点击面板撤回"),
			*ActorBlueprint->GetName(),
			ReplacementMaterials.Num(),
			*MaterialFolder),
		EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("着色器预设切换仅在编辑器中可用"), EMMDMessageType::Error);
#endif
	return FReply::Handled();
}

FReply MMDImportSetting::OnUndoShaderPresetClicked()
{
#if WITH_EDITOR
	if (MaterialPresetUndoEntries.IsEmpty())
	{
		ShowImportProgress(TEXT("没有可撤回的材质切换"), EMMDMessageType::Info);
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("撤回 MMD 着色器预设")));
	int32 RestoredCount = 0;
	UBlueprint* RestoredBlueprint = nullptr;
	UClass* RestoredActorClass = nullptr;
	for (FMMDMaterialPresetUndoEntry& Entry : MaterialPresetUndoEntries)
	{
		UMeshComponent* Component = Entry.Component.Get();
		if (!Component)
		{
			continue;
		}

		AActor* Owner = Component->GetOwner();
		RestoredActorClass = Owner ? Owner->GetClass() : nullptr;
		RestoredBlueprint = RestoredActorClass ? Cast<UBlueprint>(RestoredActorClass->ClassGeneratedBy) : nullptr;
		if (RestoredBlueprint)
		{
			RestoredBlueprint->Modify();
		}
		Component->SetFlags(RF_Transactional);
		Component->Modify();
		Component->EmptyOverrideMaterials();
		for (int32 SlotIndex = 0; SlotIndex < Entry.Materials.Num(); ++SlotIndex)
		{
			Component->SetMaterial(SlotIndex, Entry.Materials[SlotIndex].Get());
			++RestoredCount;
		}
		Component->MarkRenderStateDirty();
		if (RestoredBlueprint)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(RestoredBlueprint);
			MMDSaveAssetPackage(RestoredBlueprint);
		}
	}
	MaterialPresetUndoEntries.Reset();
	if (RestoredActorClass)
	{
		SetPreviewActorClassUI(RestoredActorClass);
	}

	ShowImportProgress(
		RestoredCount > 0
			? FString::Printf(TEXT("已恢复并保存 Actor 资产原来的材质覆盖（%d 项）"), RestoredCount)
			: TEXT("已恢复 Actor 资产原来的空材质覆盖列表"),
		RestoredCount > 0 ? EMMDMessageType::Success : EMMDMessageType::Warning);
#else
	ShowImportProgress(TEXT("着色器预设撤回仅在编辑器中可用"), EMMDMessageType::Error);
#endif
	return FReply::Handled();
}

void MMDImportSetting::LoadSelectedMMDActor()
{
#if WITH_EDITOR
	if (UClass* PickedClass = PickMMDActorClassFromContentBrowser())
	{
		SetCurrentMMDActorClass(PickedClass, PickedClass->GetName());
	}
#else
	ShowImportProgress(TEXT("Loading AMMDActor is editor-only."), EMMDMessageType::Error);
#endif
}

bool MMDImportSetting::SetCurrentMMDActorClass(UClass* ActorClass, const FString& DisplayName)
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!TryGetAnimationTargetFromActorClass(ActorClass, Target))
	{
		ShowImportProgress(TEXT("Selected asset is not a usable AMMDActor Blueprint with SkeletalMesh."), EMMDMessageType::Error);
		return false;
	}

	LastLoadedMMDActorClass = ActorClass;
	LastImportedMMDActor = nullptr;
	ComposerActor = nullptr;
	ComposerSkeletalMeshComponent = nullptr;
	PhysicsBakeActor = nullptr;
	PhysicsBakeSkeletalMeshComponent = nullptr;
	RefreshSequenceComposerLabels();
	RefreshPhysicsBakeLabels();

	{
		const FString Label = DisplayName.IsEmpty() ? ActorClass->GetName() : DisplayName;
		SetSelectedModelTextUI(FText::FromString(Label));
		if (!SetPreviewActorClassUI(ActorClass))
		{
			SetPreviewSkeletalMeshUI(Target.SkeletalMesh);
		}
	}

	ShowImportProgress(FString::Printf(TEXT("Loaded MMD actor asset: %s"), DisplayName.IsEmpty() ? *ActorClass->GetName() : *DisplayName), EMMDMessageType::Success);
	return true;
#else
	ShowImportProgress(TEXT("Loading AMMDActor is editor-only."), EMMDMessageType::Error);
	return false;
#endif
}

UClass* MMDImportSetting::PickMMDActorClassFromContentBrowser()
{
#if WITH_EDITOR
	TArray<FAssetData> SelectedAssets;
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);

	for (const FAssetData& AssetData : SelectedAssets)
	{
		if (UClass* ActorClass = ResolveMMDActorClassFromAssetData(AssetData))
		{
			return ActorClass;
		}
	}

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([](const FAssetData& AssetData)
	{
		return !IsMMDActorBlueprintAssetData(AssetData);
	});

	FAssetData PickedAsset;
	TSharedPtr<SWindow> PickerWindow;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([&PickedAsset, &PickerWindow](const FAssetData& AssetData)
	{
		if (IsMMDActorBlueprintAssetData(AssetData))
		{
			PickedAsset = AssetData;
			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
		}
	});
	AssetPickerConfig.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateLambda([&PickedAsset, &PickerWindow](const FAssetData& AssetData)
	{
		if (IsMMDActorBlueprintAssetData(AssetData))
		{
			PickedAsset = AssetData;
			if (PickerWindow.IsValid())
			{
				PickerWindow->RequestDestroyWindow();
			}
		}
	});

	PickerWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Select AMMDActor Blueprint")))
		.ClientSize(FVector2D(760.0f, 520.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	PickerWindow->SetContent(ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig));
	FSlateApplication::Get().AddModalWindow(PickerWindow.ToSharedRef(), FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr));

	UClass* PickedClass = ResolveMMDActorClassFromAssetData(PickedAsset);
	if (!PickedClass)
	{
		ShowImportProgress(TEXT("No AMMDActor Blueprint selected."), EMMDMessageType::Warning);
	}
	return PickedClass;
#else
	return nullptr;
#endif
}

UAnimSequence* MMDImportSetting::PickAnimSequenceFromContentBrowser()
{
#if WITH_EDITOR
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	const FString AnimationFilterFolder = GetMMDModelAnimationFolder(LastLoadedMMDActorClass.Get());

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([AnimationFilterFolder](const FAssetData& AssetData)
	{
		return ShouldFilterAnimationOutsideFolder(AssetData, AnimationFilterFolder);
	});

	FAssetData PickedAsset;
	TSharedPtr<SWindow> PickerWindow;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([&PickedAsset, &PickerWindow](const FAssetData& AssetData)
	{
		PickedAsset = AssetData;
		if (PickerWindow.IsValid())
		{
			PickerWindow->RequestDestroyWindow();
		}
	});
	AssetPickerConfig.OnAssetDoubleClicked = FOnAssetDoubleClicked::CreateLambda([&PickedAsset, &PickerWindow](const FAssetData& AssetData)
	{
		PickedAsset = AssetData;
		if (PickerWindow.IsValid())
		{
			PickerWindow->RequestDestroyWindow();
		}
	});

	PickerWindow = SNew(SWindow)
		.Title(FText::FromString(TEXT("Select Target AnimSequence")))
		.ClientSize(FVector2D(760.0f, 520.0f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	PickerWindow->SetContent(ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig));
	FSlateApplication::Get().AddModalWindow(PickerWindow.ToSharedRef(), FSlateApplication::Get().FindBestParentWindowForDialogs(nullptr));

	UAnimSequence* PickedAnimSequence = Cast<UAnimSequence>(PickedAsset.GetAsset());
	if (!PickedAnimSequence)
	{
		ShowImportProgress(TEXT("No AnimSequence selected."), EMMDMessageType::Warning);
	}
	return PickedAnimSequence;
#else
	return nullptr;
#endif
}

bool MMDImportSetting::EnsureCurrentMMDActorTarget()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		return true;
	}

	UClass* PickedClass = PickMMDActorClassFromContentBrowser();
	return PickedClass && SetCurrentMMDActorClass(PickedClass, PickedClass->GetName());
#else
	return false;
#endif
}

void MMDImportSetting::ImportMMDModel()
{
	ShowImportProgress(TEXT("鎵撳紑鏂囦欢閫夋嫨瀵硅瘽妗?.."));

	// 浣跨敤鏂囦欢瀵硅瘽妗嗛€夋嫨MMD妯″瀷鏂囦欢
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OpenedFiles;
		const FString FileTypes = TEXT("MMD Model Files (*.pmx;*.pmd;*.fbx)|*.pmx;*.pmd;*.fbx|PMX Files (*.pmx)|*.pmx|PMD Files (*.pmd)|*.pmd|FBX Files (*.fbx)|*.fbx|All Files (*.*)|*.*");

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("瀵煎叆MMD妯″瀷"),
			TEXT(""), // 榛樿璺緞
			TEXT(""), // 榛樿鏂囦欢鍚?
			FileTypes,
			EFileDialogFlags::None,
			OpenedFiles);

		if (bOpened && OpenedFiles.Num() > 0)
		{
			FString SelectedFile = OpenedFiles[0];
			FString FileName = FPaths::GetCleanFilename(SelectedFile);

			ShowImportProgress(FString::Printf(TEXT("宸查€夋嫨鏂囦欢: %s"), *FileName));
			SetSelectedModelTextUI(FText::FromString(FileName));

			if (ViewPanel.IsValid())
			{
				ViewPanel->LoadMMDModel(SelectedFile);
			}

			ShowImportProgress(FString::Printf(TEXT("姝ｅ湪鍔犺浇妯″瀷: %s"), *FileName));

			if (FPaths::GetExtension(SelectedFile).Equals(TEXT("pmx"), ESearchCase::IgnoreCase))
			{
				ShowImportProgress(TEXT("寮€濮嬭В鏋怭MX鏂囦欢..."));
				UE_LOG(LogTemp, Warning, TEXT("寮€濮嬭В鏋怭MX鏂囦欢: %s"), *SelectedFile);
#if WITH_EDITOR
				UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (!EditorWorld) {
					ShowImportProgress(TEXT("鏈壘鍒扮紪杈戝櫒涓栫晫锛屾棤娉曠敓鎴怉ctor"), EMMDMessageType::Error);
					return;
				}
				FActorSpawnParameters SpawnParams;
				SpawnParams.Name = MakeUniqueObjectName(EditorWorld, AMMDActor::StaticClass(), FName(TEXT("MMDActor")));
				SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
				AMMDActor* NewMMDActor = EditorWorld->SpawnActor<AMMDActor>(AMMDActor::StaticClass(), FTransform::Identity, SpawnParams);
				if (!NewMMDActor)
				{
					ShowImportProgress(TEXT("鐢熸垚AMMDActor澶辫触"), EMMDMessageType::Error);
					return;
				}
				LastImportedMMDActor = NewMMDActor;
				NewMMDActor->SetupComponents(SelectedFile);
				// 淇濆瓨涓鸿摑鍥捐祫浜?
				FString AssetFolder = TEXT("/Game/MMDModels");
				FString AssetName = FPaths::GetBaseFilename(FileName);
				if (UBlueprint* NewBP = SaveMMDBlueprintAsset(NewMMDActor, AssetFolder + TEXT("/") + AssetName + TEXT("/BluePrint"), AssetName, true))
				{
					if (UClass* GenClass = NewBP->GeneratedClass)
					{
						// 鍏抽敭锛氭妸 PMX 婧愯矾寰勮缃埌钃濆浘 CDO锛屼繚璇佽摑鍥惧疄渚?OnConstruction 鑳芥嬁鍒?
						if (AActor* CDOActor = Cast<AActor>(GenClass->GetDefaultObject()))
						{
							if (AMMDActor* CDO = Cast<AMMDActor>(CDOActor))
							{
								CDO->SourcePMXFilePath = SelectedFile; // 缁濆璺緞
								CDO->Modify(true);
								UE_LOG(LogTemp, Log, TEXT("[Import] Set CDO SourcePMXFilePath: %s"), *SelectedFile);
							}
						}

						// 閲嶆柊缂栬瘧锛屼娇榛樿鍊肩敓鏁?
						FKismetEditorUtilities::CompileBlueprint(NewBP);

						// 棰勮涓敓鎴愬疄渚嬶紙鍏?OnConstruction 灏嗗熀浜?CDO 鐨勮矾寰勮嚜鍔ㄥ垵濮嬪寲锛?
						if (ViewPanel.IsValid())
						{
							ViewPanel->CreatePreviewActor(GenClass);
						}
						SetPreviewActorClassUI(GenClass);
						LastLoadedMMDActorClass = GenClass;
					}
				}

				GEditor->SelectNone(false, true);
				GEditor->SelectActor(NewMMDActor, true, true);
				GEditor->MoveViewportCamerasToActor(*NewMMDActor, false);


				ShowImportProgress(TEXT("宸插湪鍏冲崱涓敓鎴怉MMDActor骞跺姞杞絇MX"), EMMDMessageType::Success);

#else
				ShowImportProgress(TEXT("浠呭湪缂栬緫鍣ㄤ腑鍙敓鎴怉ctor"), EMMDMessageType::Warning);
#endif
			}
			else
			{
				ShowImportProgress(FString::Printf(TEXT("鏂囦欢绫诲瀷: %s (闈濸MX)"), *FPaths::GetExtension(SelectedFile)));
			}
		}
	}
	else
	{
		ShowImportProgress(TEXT("Cannot open file dialog."));
	}
}

void MMDImportSetting::ImportStaticMesh()
{
	ShowImportProgress(TEXT("鎵撳紑闈欐€佺綉鏍奸€夋嫨瀵硅瘽妗?.."));

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OpenedFiles;
		const FString FileTypes = TEXT("Static Mesh Files (*.fbx;*.obj;*.3ds)|*.fbx;*.obj;*.3ds|FBX Files (*.fbx)|*.fbx|OBJ Files (*.obj)|*.obj|All Files (*.*)|*.*");

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Import Static Mesh"),
			TEXT(""),
			TEXT(""),
			FileTypes,
			EFileDialogFlags::None,
			OpenedFiles);

		if (bOpened && OpenedFiles.Num() > 0)
		{
			FString SelectedFile = OpenedFiles[0];
			FString FileName = FPaths::GetCleanFilename(SelectedFile);

			ShowImportProgress(FString::Printf(TEXT("宸查€夋嫨闈欐€佺綉鏍? %s"), *FileName));

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					-1,
					10.0f,
					FColor::Blue,
					FString::Printf(TEXT("闈欐€佺綉鏍煎鍏? %s"), *SelectedFile));
			}
		}
		else
		{
			ShowImportProgress(TEXT("Import canceled."));
		}
	}
}
void MMDImportSetting::ImportVMDAnimation()
{
	ShowImportProgress(TEXT("鎵撳紑VMD鍔ㄧ敾閫夋嫨瀵硅瘽妗?.."));

#if WITH_EDITOR
	FMMDSelectedAnimationTarget InitialTarget;
	if (!TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), InitialTarget))
	{
		UClass* PickedClass = PickMMDActorClassFromContentBrowser();
		if (!PickedClass || !SetCurrentMMDActorClass(PickedClass, PickedClass->GetName()))
		{
			ShowImportProgress(TEXT("VMD import needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
			return;
		}
	}
#endif

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		ShowImportProgress(TEXT("Cannot open file selection dialog."), EMMDMessageType::Error);
		return;
	}

	TArray<FString> OpenedFiles;
	const FString FileTypes = TEXT("VMD Files (*.vmd)|*.vmd|All Files (*.*)|*.*");
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("瀵煎叆VMD鍔ㄧ敾"),
		TEXT(""),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OpenedFiles);

	if (!bOpened || OpenedFiles.Num() == 0)
	{
		ShowImportProgress(TEXT("VMD animation import canceled."), EMMDMessageType::Warning);
		return;
	}

	const FString SelectedFile = OpenedFiles[0];
	const FString FileName = FPaths::GetCleanFilename(SelectedFile);
	ShowImportProgress(FString::Printf(TEXT("姝ｅ湪瑙ｆ瀽VMD鏂囦欢: %s"), *FileName));
	SetSelectedAnimTextUI(FText::FromString(FileName));

	TVMDParser VMDParser;
	if (!VMDParser.ParseVMDFile(SelectedFile))
	{
		ShowImportProgress(TEXT("Failed to parse VMD file."), EMMDMessageType::Error);
		return;
	}

#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("VMD import needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
		return;
	}

	if (!Target.SkeletalMesh || !Target.SkeletalMesh->GetSkeleton())
	{
		ShowImportProgress(TEXT("Selected SkeletalMesh has no Skeleton, cannot import VMD."), EMMDMessageType::Error);
		return;
	}

	TPMXParser PMXParser;
	const PMXDatas* PMXData = nullptr;
	if (!Target.SourcePMXFilePath.IsEmpty() && FPaths::FileExists(Target.SourcePMXFilePath))
	{
		if (PMXParser.ParsePMXFile(Target.SourcePMXFilePath))
		{
			PMXData = &PMXParser.PMXInfo;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Failed to parse source PMX for IK bake: %s"), *Target.SourcePMXFilePath);
		}
	}

	FMMDAnimationImportContext ImportContext;
	FMMDAnimationImportReport ImportReport;
	if (!TMMDMeshBuilder::BuildAnimationImportContext(Target.SkeletalMesh, PMXData, Target.SourcePMXFilePath, SelectedFile, ImportContext, &ImportReport))
	{
		const FString ErrorMessage = ImportReport.Errors.Num() > 0 ? ImportReport.Errors[0] : TEXT("Failed to create VMD import context.");
		ShowImportProgress(ErrorMessage, EMMDMessageType::Error);
		return;
	}

	FMMDAnimationImportSettings ImportSettings;
	ImportSettings.bImportBoneTracks = true;
	ImportSettings.bImportMorphCurves = true;

	UAnimSequence* AnimSequence = TMMDMeshBuilder::BuildVMDAnimation(VMDParser.VMDInfo, ImportContext, ImportSettings, &ImportReport);
	if (!AnimSequence)
	{
		const FString ErrorMessage = ImportReport.Errors.Num() > 0 ? ImportReport.Errors[0] : TEXT("Failed to generate VMD skeletal animation.");
		ShowImportProgress(ErrorMessage, EMMDMessageType::Error);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Generated %s | Bone matched %d/%d | Morph matched %d/%d | MaxFrame=%d"),
		*AnimSequence->GetPathName(),
		ImportReport.MatchedBoneTrackCount,
		ImportReport.SourceBoneTrackCount,
		ImportReport.MatchedMorphTrackCount,
		ImportReport.SourceMorphTrackCount,
		ImportReport.MaxFrame);

	int32 LoggedMatchedTracks = 0;
	int32 LoggedUnmatchedTracks = 0;
	for (const FMMDResolvedBoneTrack& Track : ImportReport.BoneTracks)
	{
		if (Track.bMatched && LoggedMatchedTracks < 12)
		{
			UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Matched bone: %s -> %s (%d keys)"),
				*Track.SourceBoneName,
				*Track.TargetBoneName.ToString(),
				Track.KeyCount);
			++LoggedMatchedTracks;
		}
		else if (!Track.bMatched && LoggedUnmatchedTracks < 24)
		{
			UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Unmatched bone: %s (%d keys)"),
				*Track.SourceBoneName,
				Track.KeyCount);
			++LoggedUnmatchedTracks;
		}
	}

	if (Target.SkeletalMeshComponent)
	{
		Target.SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Target.SkeletalMeshComponent->SetAnimation(AnimSequence);
		Target.SkeletalMeshComponent->Play(true);
	}
	SetSelectedAnimTextUI(FText::FromString(AnimSequence->GetName()));
	SetPreviewAnimationUI(AnimSequence);
	ComposerAnimSequence = AnimSequence;
	PhysicsBakeAnimSequence = AnimSequence;

	for (const FString& Warning : ImportReport.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMD Import] %s"), *Warning);
	}

	ShowImportProgress(
		FString::Printf(
			TEXT("VMD楠ㄩ/琛ㄦ儏鍔ㄧ敾瀵煎叆骞舵挱鏀? %s | Bone %d/%d | Morph %d/%d | MaxFrame=%d"),
			*AnimSequence->GetPathName(),
			ImportReport.MatchedBoneTrackCount,
			ImportReport.SourceBoneTrackCount,
			ImportReport.MatchedMorphTrackCount,
			ImportReport.SourceMorphTrackCount,
			ImportReport.MaxFrame),
		EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("VMD import is editor-only."), EMMDMessageType::Error);
#endif
}

#if WITH_EDITOR
static UObject* GetFirstSelectedEditorObjectOfClass(UClass* Class);
#endif

void MMDImportSetting::AppendFacialVMDToAnimation()
{
	ShowImportProgress(TEXT("Selecting target AnimSequence..."));

#if WITH_EDITOR
	FMMDSelectedAnimationTarget InitialTarget;
	if (!TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), InitialTarget))
	{
		UClass* PickedClass = PickMMDActorClassFromContentBrowser();
		if (!PickedClass || !SetCurrentMMDActorClass(PickedClass, PickedClass->GetName()))
		{
			ShowImportProgress(TEXT("Append facial VMD needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
			return;
		}
	}

	UAnimSequence* TargetAnimSequence = PickAnimSequenceFromContentBrowser();
	if (!TargetAnimSequence)
	{
		return;
	}

	ComposerAnimSequence = TargetAnimSequence;
	PhysicsBakeAnimSequence = TargetAnimSequence;
	SetSelectedAnimTextUI(FText::FromString(TargetAnimSequence->GetName()));
	SetPreviewAnimationUI(TargetAnimSequence);
	RefreshSequenceComposerLabels();
	RefreshPhysicsBakeLabels();

	ShowImportProgress(FString::Printf(TEXT("Selected target AnimSequence: %s. Opening facial VMD file dialog..."), *TargetAnimSequence->GetName()));

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		ShowImportProgress(TEXT("Cannot open file selection dialog."), EMMDMessageType::Error);
		return;
	}

	TArray<FString> OpenedFiles;
	const FString FileTypes = TEXT("VMD Files (*.vmd)|*.vmd|All Files (*.*)|*.*");
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Append Facial VMD"),
		TEXT(""),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OpenedFiles);

	if (!bOpened || OpenedFiles.Num() == 0)
	{
		ShowImportProgress(TEXT("Append facial VMD canceled."), EMMDMessageType::Warning);
		return;
	}

	const FString SelectedFile = OpenedFiles[0];
	const FString FileName = FPaths::GetCleanFilename(SelectedFile);
	ShowImportProgress(FString::Printf(TEXT("Parsing facial VMD: %s"), *FileName));

	TVMDParser VMDParser;
	if (!VMDParser.ParseVMDFile(SelectedFile))
	{
		ShowImportProgress(TEXT("Failed to parse facial VMD file."), EMMDMessageType::Error);
		return;
	}
	if (VMDParser.VMDInfo.MorphKeyframes.Num() == 0)
	{
		ShowImportProgress(TEXT("Selected VMD has no facial morph keyframes."), EMMDMessageType::Warning);
		return;
	}

	FMMDSelectedAnimationTarget Target;
	if (!TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("Append facial VMD needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
		return;
	}
	if (!Target.SkeletalMesh || !Target.SkeletalMesh->GetSkeleton())
	{
		ShowImportProgress(TEXT("Current MMDActor SkeletalMesh has no Skeleton."), EMMDMessageType::Error);
		return;
	}

	TPMXParser PMXParser;
	const PMXDatas* PMXData = nullptr;
	if (!Target.SourcePMXFilePath.IsEmpty() && FPaths::FileExists(Target.SourcePMXFilePath))
	{
		if (PMXParser.ParsePMXFile(Target.SourcePMXFilePath))
		{
			PMXData = &PMXParser.PMXInfo;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[VMD Facial Append] Failed to parse source PMX: %s"), *Target.SourcePMXFilePath);
		}
	}

	FMMDAnimationImportContext ImportContext;
	FMMDAnimationImportReport ImportReport;
	if (!TMMDMeshBuilder::BuildAnimationImportContext(Target.SkeletalMesh, PMXData, Target.SourcePMXFilePath, SelectedFile, ImportContext, &ImportReport))
	{
		const FString ErrorMessage = ImportReport.Errors.Num() > 0 ? ImportReport.Errors[0] : TEXT("Failed to create facial VMD import context.");
		ShowImportProgress(ErrorMessage, EMMDMessageType::Error);
		return;
	}

	FMMDAnimationImportSettings ImportSettings;
	ImportSettings.bImportBoneTracks = false;
	ImportSettings.bImportMorphCurves = true;
	ImportSettings.bBakeMMDIKToFK = false;

	if (!TMMDMeshBuilder::AppendVMDMorphCurvesToAnimSequence(TargetAnimSequence, VMDParser.VMDInfo, ImportContext, ImportSettings, &ImportReport))
	{
		const FString ErrorMessage = ImportReport.Errors.Num() > 0 ? ImportReport.Errors[0] : TEXT("Failed to append facial VMD morph curves.");
		ShowImportProgress(ErrorMessage, EMMDMessageType::Error);
		return;
	}

	ComposerAnimSequence = TargetAnimSequence;
	PhysicsBakeAnimSequence = TargetAnimSequence;
	SetSelectedAnimTextUI(FText::FromString(TargetAnimSequence->GetName()));
	SetPreviewAnimationUI(TargetAnimSequence);
	RefreshSequenceComposerLabels();
	RefreshPhysicsBakeLabels();

	ShowImportProgress(
		FString::Printf(TEXT("Facial VMD appended to %s: morph matched %d/%d"),
			*TargetAnimSequence->GetName(),
			ImportReport.MatchedMorphTrackCount,
			ImportReport.SourceMorphTrackCount),
		EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("Append facial VMD is editor-only."), EMMDMessageType::Error);
#endif
}

#if WITH_EDITOR
static UObject* GetFirstSelectedEditorObjectOfClass(UClass* Class)
{
	if (!GEditor || !Class)
	{
		return nullptr;
	}

	USelection* SelectedObjects = GEditor->GetSelectedObjects();
	if (!SelectedObjects)
	{
		return nullptr;
	}

	for (FSelectionIterator Iter(*SelectedObjects); Iter; ++Iter)
	{
		UObject* Object = *Iter;
		if (Object && Object->IsA(Class))
		{
			return Object;
		}
	}
	return nullptr;
}

static void CopyMovieSceneBindingTracks(
	UMovieScene* SourceMovieScene,
	const FGuid& SourceGuid,
	UMovieScene* TargetMovieScene,
	const FGuid& TargetGuid)
{
	if (!SourceMovieScene || !TargetMovieScene)
	{
		return;
	}

	const FMovieSceneBinding* SourceBinding = SourceMovieScene->FindBinding(SourceGuid);
	FMovieSceneBinding* TargetBinding = TargetMovieScene->FindBinding(TargetGuid);
	if (!SourceBinding || !TargetBinding)
	{
		return;
	}

	for (UMovieSceneTrack* SourceTrack : SourceBinding->GetTracks())
	{
		if (!SourceTrack)
		{
			continue;
		}

		UMovieSceneTrack* NewTrack = DuplicateObject<UMovieSceneTrack>(SourceTrack, TargetMovieScene);
		if (NewTrack)
		{
			if (UMovieSceneSpawnTrack* SpawnTrack = Cast<UMovieSceneSpawnTrack>(NewTrack))
			{
				SpawnTrack->SetObjectId(TargetGuid);
			}
			TargetBinding->AddTrack(*NewTrack, TargetMovieScene);
		}
	}
}

static FMovieSceneDoubleChannel* FindTransformDoubleChannel(UMovieScene3DTransformSection* Section, const TCHAR* ChannelName)
{
	return Section ? Section->GetChannelProxy().GetChannelByName<FMovieSceneDoubleChannel>(ChannelName).Get() : nullptr;
}

static double GetDoubleChannelValueAtIndex(FMovieSceneDoubleChannel* Channel, int32 Index, double DefaultValue)
{
	if (!Channel)
	{
		return DefaultValue;
	}

	TArrayView<FMovieSceneDoubleValue> Values = Channel->GetData().GetValues();
	return Index >= 0 && Index < Values.Num() ? Values[Index].Value : DefaultValue;
}

static void SetDoubleChannelValueAtIndex(FMovieSceneDoubleChannel* Channel, int32 Index, double Value)
{
	if (!Channel)
	{
		return;
	}

	TArrayView<FMovieSceneDoubleValue> Values = Channel->GetData().GetValues();
	if (Index >= 0 && Index < Values.Num())
	{
		Values[Index].Value = Value;
	}
}

static void ApplyRootTransformToCameraTransformTracks(UMovieScene* MovieScene, const FGuid& CameraGuid, const FTransform& RootTransform)
{
	if (!MovieScene || RootTransform.Equals(FTransform::Identity))
	{
		return;
	}

	FMovieSceneBinding* CameraBinding = MovieScene->FindBinding(CameraGuid);
	if (!CameraBinding)
	{
		return;
	}

	for (UMovieSceneTrack* Track : CameraBinding->GetTracks())
	{
		UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
		if (!TransformTrack)
		{
			continue;
		}

		for (UMovieSceneSection* Section : TransformTrack->GetAllSections())
		{
			UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
			if (!TransformSection)
			{
				continue;
			}

			FMovieSceneDoubleChannel* LocationX = FindTransformDoubleChannel(TransformSection, TEXT("Location.X"));
			FMovieSceneDoubleChannel* LocationY = FindTransformDoubleChannel(TransformSection, TEXT("Location.Y"));
			FMovieSceneDoubleChannel* LocationZ = FindTransformDoubleChannel(TransformSection, TEXT("Location.Z"));
			FMovieSceneDoubleChannel* RotationX = FindTransformDoubleChannel(TransformSection, TEXT("Rotation.X"));
			FMovieSceneDoubleChannel* RotationY = FindTransformDoubleChannel(TransformSection, TEXT("Rotation.Y"));
			FMovieSceneDoubleChannel* RotationZ = FindTransformDoubleChannel(TransformSection, TEXT("Rotation.Z"));
			if (!LocationX || !LocationY || !LocationZ || !RotationX || !RotationY || !RotationZ)
			{
				continue;
			}

			const int32 KeyCount = LocationX->GetData().GetValues().Num();
			for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
			{
				const FVector LocalLocation(
					GetDoubleChannelValueAtIndex(LocationX, KeyIndex, 0.0),
					GetDoubleChannelValueAtIndex(LocationY, KeyIndex, 0.0),
					GetDoubleChannelValueAtIndex(LocationZ, KeyIndex, 0.0));
				const FRotator LocalRotation(
					GetDoubleChannelValueAtIndex(RotationY, KeyIndex, 0.0),
					GetDoubleChannelValueAtIndex(RotationZ, KeyIndex, 0.0),
					GetDoubleChannelValueAtIndex(RotationX, KeyIndex, 0.0));
				const FTransform WorldTransform = FTransform(LocalRotation, LocalLocation, FVector::OneVector) * RootTransform;
				const FRotator WorldRotation = WorldTransform.Rotator();

				SetDoubleChannelValueAtIndex(LocationX, KeyIndex, WorldTransform.GetLocation().X);
				SetDoubleChannelValueAtIndex(LocationY, KeyIndex, WorldTransform.GetLocation().Y);
				SetDoubleChannelValueAtIndex(LocationZ, KeyIndex, WorldTransform.GetLocation().Z);
				SetDoubleChannelValueAtIndex(RotationX, KeyIndex, WorldRotation.Roll);
				SetDoubleChannelValueAtIndex(RotationY, KeyIndex, WorldRotation.Pitch);
				SetDoubleChannelValueAtIndex(RotationZ, KeyIndex, WorldRotation.Yaw);
			}
		}
	}
}

static bool InlineCameraSequenceIntoMaster(ULevelSequence* CameraSequence, ULevelSequence* MasterSequence, int32 DurationFrames, const FTransform& RootTransform, FString& OutError)
{
	OutError.Reset();
	if (!CameraSequence || !MasterSequence)
	{
		OutError = TEXT("Invalid camera or master LevelSequence.");
		return false;
	}

	UMovieScene* SourceMovieScene = CameraSequence->GetMovieScene();
	UMovieScene* TargetMovieScene = MasterSequence->GetMovieScene();
	if (!SourceMovieScene || !TargetMovieScene)
	{
		OutError = TEXT("Camera or master sequence has no MovieScene.");
		return false;
	}

	FGuid SourceCameraGuid;
	const FMovieSceneSpawnable* SourceCameraSpawnable = nullptr;
	for (int32 Index = 0; Index < SourceMovieScene->GetSpawnableCount(); ++Index)
	{
		const FMovieSceneSpawnable& Spawnable = SourceMovieScene->GetSpawnable(Index);
		if (Spawnable.GetObjectTemplate() && Spawnable.GetObjectTemplate()->IsA<ACameraActor>())
		{
			SourceCameraGuid = Spawnable.GetGuid();
			SourceCameraSpawnable = &Spawnable;
			break;
		}
	}

	if (!SourceCameraSpawnable || !SourceCameraGuid.IsValid())
	{
		OutError = TEXT("Selected camera LevelSequence has no spawnable CameraActor.");
		return false;
	}

	UObject* SourceTemplate = const_cast<UObject*>(SourceCameraSpawnable->GetObjectTemplate());
	UObject* NewTemplate = SourceTemplate ? DuplicateObject<UObject>(SourceTemplate, TargetMovieScene) : nullptr;
	if (!NewTemplate)
	{
		OutError = TEXT("Failed to copy camera spawnable template.");
		return false;
	}

	const FGuid TargetCameraGuid = TargetMovieScene->AddSpawnable(SourceCameraSpawnable->GetName(), *NewTemplate);
	if (!TargetCameraGuid.IsValid())
	{
		OutError = TEXT("Failed to add camera spawnable to master sequence.");
		return false;
	}

	CopyMovieSceneBindingTracks(SourceMovieScene, SourceCameraGuid, TargetMovieScene, TargetCameraGuid);
	ApplyRootTransformToCameraTransformTracks(TargetMovieScene, TargetCameraGuid, RootTransform);

	FMovieSceneSpawnable* TargetCameraSpawnable = TargetMovieScene->FindSpawnable(TargetCameraGuid);
	ACameraActor* TargetCameraTemplateActor = Cast<ACameraActor>(NewTemplate);
	if (TargetCameraSpawnable && TargetCameraTemplateActor)
	{
		for (const FGuid& SourceChildGuid : SourceCameraSpawnable->GetChildPossessables())
		{
			const FMovieScenePossessable* SourceChildPossessable = SourceMovieScene->FindPossessable(SourceChildGuid);
			if (!SourceChildPossessable)
			{
				continue;
			}

			const FGuid TargetChildGuid = TargetMovieScene->AddPossessable(SourceChildPossessable->GetName(), const_cast<UClass*>(SourceChildPossessable->GetPossessedObjectClass()));
			if (!TargetChildGuid.IsValid())
			{
				continue;
			}

			if (FMovieScenePossessable* TargetChildPossessable = TargetMovieScene->FindPossessable(TargetChildGuid))
			{
				TargetChildPossessable->SetParent(TargetCameraGuid, TargetMovieScene);
			}
			TargetCameraSpawnable->AddChildPossessable(TargetChildGuid);

			if (UCameraComponent* CameraTemplateComponent = TargetCameraTemplateActor->GetCameraComponent())
			{
				MasterSequence->BindPossessableObject(TargetChildGuid, *CameraTemplateComponent, TargetCameraTemplateActor);
			}

			CopyMovieSceneBindingTracks(SourceMovieScene, SourceChildGuid, TargetMovieScene, TargetChildGuid);
		}
	}

	if (UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(TargetMovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass())))
	{
		UMovieSceneCameraCutSection* CameraCutSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack->CreateNewSection());
		if (CameraCutSection)
		{
			CameraCutTrack->AddSection(*CameraCutSection);
			CameraCutSection->SetCameraGuid(TargetCameraGuid);
			CameraCutSection->SetRange(TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
		}
	}

	return true;
}
#endif

void MMDImportSetting::OpenPhysicsBakeWindow()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (EnsureCurrentMMDActorTarget() && TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		PhysicsBakeSkeletalMeshComponent = Target.SkeletalMeshComponent;
		PhysicsBakeActor = nullptr;
	}

	if (UAnimSequence* SelectedAnim = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass())))
	{
		PhysicsBakeAnimSequence = SelectedAnim;
	}
	const FString AnimationFilterFolder = GetMMDModelAnimationFolder(LastLoadedMMDActorClass.Get());

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("MMD Physics Bake")))
		.ClientSize(FVector2D(620.0f, 260.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Bake MMD rigid body physics into a deterministic AnimSequence for LevelSequence.")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Model")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SAssignNew(PhysicsBakeActorText, STextBlock)
					.Text(FText::FromString(TEXT("Model: none")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Load Actor Asset")))
					.ToolTipText(FText::FromString(TEXT("Load the AMMDActor Blueprint used by the tool preview.")))
					.OnClicked(this, &MMDImportSetting::CapturePhysicsBakeActor)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Animation")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UAnimSequence::StaticClass())
					.ObjectPath(this, &MMDImportSetting::GetPhysicsBakeAnimationPath)
					.OnObjectChanged(this, &MMDImportSetting::OnPhysicsBakeAnimationChanged)
					.OnShouldFilterAsset(FOnShouldFilterAsset::CreateLambda([AnimationFilterFolder](const FAssetData& AssetData)
					{
						return ShouldFilterAnimationOutsideFolder(AssetData, AnimationFilterFolder);
					}))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Use Selected")))
					.OnClicked(this, &MMDImportSetting::CapturePhysicsBakeAnimation)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Append Facial VMD")))
					.OnClicked(this, &MMDImportSetting::OnAppendFacialVMDClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Frame Rate")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SNumericEntryBox<float>)
					.MinValue(1.0f)
					.MaxValue(240.0f)
					.Value(this, &MMDImportSetting::GetPhysicsBakeFrameRate)
					.OnValueChanged(this, &MMDImportSetting::OnPhysicsBakeFrameRateChanged)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(18.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Warmup Frames")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SNumericEntryBox<int32>)
					.MinValue(0)
					.MaxValue(300)
					.Value(this, &MMDImportSetting::GetPhysicsBakeWarmupFrames)
					.OnValueChanged(this, &MMDImportSetting::OnPhysicsBakeWarmupFramesChanged)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f)
			[
				SAssignNew(PhysicsBakeAnimText, STextBlock)
				.Text(FText::FromString(TEXT("Animation: none")))
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SSpacer)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Bake AnimSequence")))
				.OnClicked(this, &MMDImportSetting::BakePhysicsAnimation)
			]
		]);

	RefreshPhysicsBakeLabels();
	FSlateApplication::Get().AddWindow(Window);
#else
	ShowImportProgress(TEXT("Physics bake is editor-only."), EMMDMessageType::Error);
#endif
}

void MMDImportSetting::OpenSequenceComposerWindow()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (EnsureCurrentMMDActorTarget() && TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ComposerActor = nullptr;
		ComposerSkeletalMeshComponent = Target.SkeletalMeshComponent;
	}

	if (UAnimSequence* SelectedAnim = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass())))
	{
		ComposerAnimSequence = SelectedAnim;
	}
	if (ULevelSequence* SelectedCameraSequence = Cast<ULevelSequence>(GetFirstSelectedEditorObjectOfClass(ULevelSequence::StaticClass())))
	{
		ComposerCameraSequence = SelectedCameraSequence;
	}
	const FString AnimationFilterFolder = GetMMDModelAnimationFolder(LastLoadedMMDActorClass.Get());

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(FText::FromString(TEXT("MMD Sequence Composer")))
		.ClientSize(FVector2D(560.0f, 240.0f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	Window->SetContent(
		SNew(SBorder)
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Create a master LevelSequence from explicit selections.")))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Model")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UBlueprint::StaticClass())
					.ObjectPath(this, &MMDImportSetting::GetComposerActorPath)
					.OnObjectChanged(this, &MMDImportSetting::OnComposerActorChanged)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Load Actor Asset")))
					.OnClicked(this, &MMDImportSetting::CaptureSequenceComposerActor)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Animation")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UAnimSequence::StaticClass())
					.ObjectPath(this, &MMDImportSetting::GetComposerAnimationPath)
					.OnObjectChanged(this, &MMDImportSetting::OnComposerAnimationChanged)
					.OnShouldFilterAsset(FOnShouldFilterAsset::CreateLambda([AnimationFilterFolder](const FAssetData& AssetData)
					{
						return ShouldFilterAnimationOutsideFolder(AssetData, AnimationFilterFolder);
					}))
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(FText::FromString(TEXT("Append Facial VMD")))
					.OnClicked(this, &MMDImportSetting::OnAppendFacialVMDClicked)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Camera")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(ULevelSequence::StaticClass())
					.ObjectPath(this, &MMDImportSetting::GetComposerCameraPath)
					.OnObjectChanged(this, &MMDImportSetting::OnComposerCameraChanged)
				]
			]
			+ SVerticalBox::Slot().FillHeight(1.0f)
			[
				SNew(SSpacer)
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Create Master Sequence")))
				.OnClicked(this, &MMDImportSetting::CreateComposedLevelSequence)
			]
		]);

	RefreshSequenceComposerLabels();
	FSlateApplication::Get().AddWindow(Window);
#else
	ShowImportProgress(TEXT("Sequence composer is editor-only."), EMMDMessageType::Error);
#endif
}

FReply MMDImportSetting::CaptureSequenceComposerActor()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!EnsureCurrentMMDActorTarget() || !TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("Sequence composer needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
		return FReply::Handled();
	}
	ComposerSkeletalMeshComponent = Target.SkeletalMeshComponent;
	ComposerActor = nullptr;
	RefreshSequenceComposerLabels();
#endif
	return FReply::Handled();
}

FReply MMDImportSetting::CaptureSequenceComposerAnimation()
{
#if WITH_EDITOR
	ComposerAnimSequence = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass()));
	if (!ComposerAnimSequence.IsValid())
	{
		ShowImportProgress(TEXT("Select an AnimSequence asset in the Content Browser first."), EMMDMessageType::Warning);
	}
	RefreshSequenceComposerLabels();
#endif
	return FReply::Handled();
}

FReply MMDImportSetting::CaptureSequenceComposerCamera()
{
#if WITH_EDITOR
	ComposerCameraSequence = Cast<ULevelSequence>(GetFirstSelectedEditorObjectOfClass(ULevelSequence::StaticClass()));
	if (!ComposerCameraSequence.IsValid())
	{
		ShowImportProgress(TEXT("Select a camera LevelSequence asset in the Content Browser first."), EMMDMessageType::Warning);
	}
	RefreshSequenceComposerLabels();
#endif
	return FReply::Handled();
}

void MMDImportSetting::OnComposerActorChanged(const FAssetData& AssetData)
{
#if WITH_EDITOR
	UClass* ActorClass = ResolveMMDActorClassFromAssetData(AssetData);
	if (!SetCurrentMMDActorClass(ActorClass, ActorClass ? ActorClass->GetName() : FString()))
	{
		LastLoadedMMDActorClass.Reset();
		ComposerSkeletalMeshComponent.Reset();
		RefreshSequenceComposerLabels();
		return;
	}

	FMMDSelectedAnimationTarget Target;
	if (TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ComposerSkeletalMeshComponent = Target.SkeletalMeshComponent;
	}
	RefreshSequenceComposerLabels();
#endif
}

void MMDImportSetting::OnComposerAnimationChanged(const FAssetData& AssetData)
{
	ComposerAnimSequence = Cast<UAnimSequence>(AssetData.GetAsset());
	RefreshSequenceComposerLabels();
}

void MMDImportSetting::OnComposerCameraChanged(const FAssetData& AssetData)
{
	ComposerCameraSequence = Cast<ULevelSequence>(AssetData.GetAsset());
	RefreshSequenceComposerLabels();
}

FString MMDImportSetting::GetComposerActorPath() const
{
	return LastLoadedMMDActorClass.IsValid() ? LastLoadedMMDActorClass->GetPathName() : FString();
}

FString MMDImportSetting::GetComposerAnimationPath() const
{
	return ComposerAnimSequence.IsValid() ? ComposerAnimSequence->GetPathName() : FString();
}

FString MMDImportSetting::GetComposerCameraPath() const
{
	return ComposerCameraSequence.IsValid() ? ComposerCameraSequence->GetPathName() : FString();
}

void MMDImportSetting::RefreshSequenceComposerLabels()
{
	if (ComposerActorText.IsValid())
	{
		const FString Label = LastLoadedMMDActorClass.IsValid()
			? FString::Printf(TEXT("Model: %s"), *LastLoadedMMDActorClass->GetName())
			: TEXT("Model: none");
		ComposerActorText->SetText(FText::FromString(Label));
	}
	if (ComposerAnimText.IsValid())
	{
		const FString Label = ComposerAnimSequence.IsValid()
			? FString::Printf(TEXT("Animation: %s"), *ComposerAnimSequence->GetPathName())
			: TEXT("Animation: none");
		ComposerAnimText->SetText(FText::FromString(Label));
	}
	if (ComposerCameraText.IsValid())
	{
		const FString Label = ComposerCameraSequence.IsValid()
			? FString::Printf(TEXT("Camera: %s"), *ComposerCameraSequence->GetPathName())
			: TEXT("Camera: none");
		ComposerCameraText->SetText(FText::FromString(Label));
	}
}

FReply MMDImportSetting::CapturePhysicsBakeActor()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!EnsureCurrentMMDActorTarget() || !TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("Physics bake needs a loaded AMMDActor Blueprint asset."), EMMDMessageType::Error);
		return FReply::Handled();
	}
	PhysicsBakeSkeletalMeshComponent = Target.SkeletalMeshComponent;
	PhysicsBakeActor = nullptr;
	RefreshPhysicsBakeLabels();
#endif
	return FReply::Handled();
}

FReply MMDImportSetting::CapturePhysicsBakeAnimation()
{
#if WITH_EDITOR
	PhysicsBakeAnimSequence = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass()));
	if (!PhysicsBakeAnimSequence.IsValid())
	{
		ShowImportProgress(TEXT("Select an AnimSequence asset in the Content Browser first."), EMMDMessageType::Warning);
	}
	RefreshPhysicsBakeLabels();
#endif
	return FReply::Handled();
}

void MMDImportSetting::OnPhysicsBakeAnimationChanged(const FAssetData& AssetData)
{
	PhysicsBakeAnimSequence = Cast<UAnimSequence>(AssetData.GetAsset());
	RefreshPhysicsBakeLabels();
}

FString MMDImportSetting::GetPhysicsBakeAnimationPath() const
{
	return PhysicsBakeAnimSequence.IsValid() ? PhysicsBakeAnimSequence->GetPathName() : FString();
}

TOptional<float> MMDImportSetting::GetPhysicsBakeFrameRate() const
{
	return PhysicsBakeFrameRate;
}

void MMDImportSetting::OnPhysicsBakeFrameRateChanged(float NewValue)
{
	PhysicsBakeFrameRate = FMath::Clamp(NewValue, 1.0f, 240.0f);
}

TOptional<int32> MMDImportSetting::GetPhysicsBakeWarmupFrames() const
{
	return PhysicsBakeWarmupFrames;
}

void MMDImportSetting::OnPhysicsBakeWarmupFramesChanged(int32 NewValue)
{
	PhysicsBakeWarmupFrames = FMath::Clamp(NewValue, 0, 300);
}

void MMDImportSetting::RefreshPhysicsBakeLabels()
{
	if (PhysicsBakeActorText.IsValid())
	{
		const FString Label = LastLoadedMMDActorClass.IsValid()
			? FString::Printf(TEXT("Model: %s"), *LastLoadedMMDActorClass->GetName())
			: TEXT("Model: none");
		PhysicsBakeActorText->SetText(FText::FromString(Label));
	}
	if (PhysicsBakeAnimText.IsValid())
	{
		const FString Label = PhysicsBakeAnimSequence.IsValid()
			? FString::Printf(TEXT("Animation: %s"), *PhysicsBakeAnimSequence->GetPathName())
			: TEXT("Animation: none");
		PhysicsBakeAnimText->SetText(FText::FromString(Label));
	}
}

FReply MMDImportSetting::BakePhysicsAnimation()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!EnsureCurrentMMDActorTarget() || !TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("Physics bake needs the current RenderTarget AMMDActor asset."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	USkeletalMeshComponent* SkelComp = Target.SkeletalMeshComponent;
	UAnimSequence* SourceAnim = PhysicsBakeAnimSequence.Get();
	if (!SkelComp || !Target.SkeletalMesh || !SourceAnim)
	{
		ShowImportProgress(TEXT("Load the current AMMDActor asset and an AnimSequence before baking."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	if (Target.SourcePMXFilePath.IsEmpty() || !FPaths::FileExists(Target.SourcePMXFilePath))
	{
		ShowImportProgress(TEXT("The current AMMDActor asset has no valid source PMX path for physics data."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	TPMXParser Parser;
	if (!Parser.ParsePMXFile(Target.SourcePMXFilePath))
	{
		ShowImportProgress(TEXT("Failed to parse PMX physics data."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	TArray<FMMDPhysicsRigidBodyData> Rigids;
	TArray<FMMDPhysicsJointData> Joints;
	ConvertPMXPhysicsToAnimNodeData(Parser.PMXInfo, Rigids, Joints);
	if (Rigids.Num() == 0 || Joints.Num() == 0)
	{
		ShowImportProgress(TEXT("PMX has no rigid body or joint data to bake."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	USkeletalMesh* SkeletalMesh = Target.SkeletalMesh;
	USkeleton* Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;
	if (!Skeleton)
	{
		ShowImportProgress(TEXT("Selected SkeletalMesh has no Skeleton."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	const float BakeRate = FMath::Clamp(PhysicsBakeFrameRate, 1.0f, 240.0f);
	const int32 NumFrames = FMath::Max(1, FMath::CeilToInt(SourceAnim->GetPlayLength() * BakeRate));
	const int32 NumKeys = NumFrames + 1;

	TArray<TArray<FTransform>> LocalTransforms;
	FString Error;
	if (!BuildLocalTransformsFromAnimSequence(SourceAnim, RefSkeleton, BakeRate, NumKeys, LocalTransforms, Error))
	{
		ShowImportProgress(Error, EMMDMessageType::Error);
		return FReply::Handled();
	}

	FMMDPhysicsSimulator Simulator;
	if (!Simulator.InitializeFromPMX(Rigids, Joints, SkelComp))
	{
		ShowImportProgress(TEXT("Failed to initialize MMD physics simulator."), EMMDMessageType::Error);
		return FReply::Handled();
	}
	constexpr float MMDPhysicsFixedTimeStep = 1.0f / 60.0f;
	const int32 MMDPhysicsMaxSubSteps = FMath::Clamp(FMath::CeilToInt((1.0f / BakeRate) / MMDPhysicsFixedTimeStep) + 1, 4, 16);
	Simulator.ConfigureSimulation(MMDPhysicsFixedTimeStep, MMDPhysicsMaxSubSteps);

	FScopedSlowTask SlowTask(static_cast<float>(NumKeys + PhysicsBakeWarmupFrames), FText::FromString(TEXT("Baking MMD physics")));
	SlowTask.MakeDialog(true);

	TArray<FTransform> ComponentTransforms;
	const FTransform ComponentToWorld = FTransform::Identity;
	const bool bPreviewBake = ViewPanel.IsValid();
	if (bPreviewBake)
	{
		ViewPanel->BeginPhysicsBakePreview(SkeletalMesh);
	}
	auto PumpBakePreview = [this, bPreviewBake, &RefSkeleton](const TArray<FTransform>& PreviewComponentTransforms, int32 FrameIndex)
	{
		if (!bPreviewBake || !ViewPanel.IsValid())
		{
			return;
		}

		const int32 PreviewStride = 2;
		if ((FrameIndex % PreviewStride) != 0)
		{
			return;
		}

		ViewPanel->PreviewPhysicsBakeFrame(RefSkeleton, PreviewComponentTransforms);
		FSlateApplication::Get().PumpMessages();
		FSlateApplication::Get().Tick();
	};

	BuildComponentTransformsForFrame(RefSkeleton, LocalTransforms, 0, ComponentTransforms);
	for (int32 WarmupIndex = 0; WarmupIndex < PhysicsBakeWarmupFrames; ++WarmupIndex)
	{
		if (SlowTask.ShouldCancel())
		{
			if (bPreviewBake && ViewPanel.IsValid())
			{
				ViewPanel->EndPhysicsBakePreview();
			}
			ShowImportProgress(TEXT("Physics bake canceled."), EMMDMessageType::Warning);
			return FReply::Handled();
		}
		SlowTask.EnterProgressFrame(1.0f);
		Simulator.TickMMDPhysicsOnComponentTransforms(ComponentTransforms, ComponentToWorld, 1.0f / BakeRate);
		PumpBakePreview(ComponentTransforms, WarmupIndex);
	}

	for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
	{
		if (SlowTask.ShouldCancel())
		{
			if (bPreviewBake && ViewPanel.IsValid())
			{
				ViewPanel->EndPhysicsBakePreview();
			}
			ShowImportProgress(TEXT("Physics bake canceled."), EMMDMessageType::Warning);
			return FReply::Handled();
		}
		SlowTask.EnterProgressFrame(1.0f);
		BuildComponentTransformsForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
		Simulator.TickMMDPhysicsOnComponentTransforms(ComponentTransforms, ComponentToWorld, 1.0f / BakeRate);
		WriteComponentTransformsToLocalFrame(RefSkeleton, ComponentTransforms, FrameIndex, LocalTransforms);
		PumpBakePreview(ComponentTransforms, FrameIndex);
	}

	UAnimSequence* BakedAnim = CreatePhysicsBakedAnimSequence(SourceAnim, SkeletalMesh, RefSkeleton, LocalTransforms, BakeRate, NumFrames, Error);
	if (bPreviewBake && ViewPanel.IsValid())
	{
		ViewPanel->EndPhysicsBakePreview();
	}
	if (!BakedAnim)
	{
		ShowImportProgress(Error.IsEmpty() ? TEXT("Failed to create baked AnimSequence.") : Error, EMMDMessageType::Error);
		return FReply::Handled();
	}

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(BakedAnim);
	}

	ShowImportProgress(FString::Printf(TEXT("Physics baked: %s"), *BakedAnim->GetPathName()), EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("Physics bake is editor-only."), EMMDMessageType::Error);
#endif
	return FReply::Handled();
}

FReply MMDImportSetting::CreateComposedLevelSequence()
{
#if WITH_EDITOR
	FMMDSelectedAnimationTarget Target;
	if (!EnsureCurrentMMDActorTarget() || !TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target))
	{
		ShowImportProgress(TEXT("Sequence composer needs the current RenderTarget AMMDActor asset."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	USkeletalMeshComponent* SkelComp = Target.SkeletalMeshComponent;
	UAnimSequence* AnimSequence = ComposerAnimSequence.Get();
	ULevelSequence* CameraSequence = ComposerCameraSequence.Get();
	if (!SkelComp || !LastLoadedMMDActorClass.IsValid() || !AnimSequence || !CameraSequence)
	{
		ShowImportProgress(TEXT("Load an AMMDActor asset, AnimSequence, and camera LevelSequence before composing."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	FString SequenceFolder;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		SequenceFolder = GetMMDModelLevelSequenceFolder(nullptr, SkelComp);
		const FString BaseAssetPath = SequenceFolder / (FString(TEXT("LS_")) + SanitizeMMDAssetName(AnimSequence->GetName()) + TEXT("_Master"));
		AssetToolsModule.Get().CreateUniqueAssetName(BaseAssetPath, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	ULevelSequence* MasterSequence = Package
		? NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone)
		: nullptr;
	if (!MasterSequence)
	{
		ShowImportProgress(TEXT("Failed to create master LevelSequence."), EMMDMessageType::Error);
		return FReply::Handled();
	}
	MasterSequence->Initialize();

	UMovieScene* MovieScene = MasterSequence->GetMovieScene();
	if (!MovieScene)
	{
		ShowImportProgress(TEXT("Failed to create master MovieScene."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	const int32 AnimDurationFrames = FMath::Max(1, FMath::CeilToInt(AnimSequence->GetPlayLength() * 30.0f));
	int32 CameraDurationFrames = AnimDurationFrames;
	if (UMovieScene* CameraMovieScene = CameraSequence->GetMovieScene())
	{
		const TRange<FFrameNumber> CameraRange = CameraMovieScene->GetPlaybackRange();
		if (CameraRange.HasLowerBound() && CameraRange.HasUpperBound())
		{
			CameraDurationFrames = FMath::Max(1, CameraRange.GetUpperBoundValue().Value - CameraRange.GetLowerBoundValue().Value);
		}
	}
	const int32 DurationFrames = FMath::Max(AnimDurationFrames, CameraDurationFrames);
	ConfigureMMDMovieSceneTiming(MovieScene, DurationFrames);

	const FName ActorTemplateName = MakeUniqueObjectName(MovieScene, LastLoadedMMDActorClass.Get(), TEXT("MMDModelSpawnableTemplate"));
	UObject* ActorTemplate = NewObject<UObject>(MovieScene, LastLoadedMMDActorClass.Get(), ActorTemplateName, RF_Transactional);
	if (!ActorTemplate)
	{
		ShowImportProgress(TEXT("Failed to create model spawnable template."), EMMDMessageType::Error);
		return FReply::Handled();
	}

	const FGuid ActorGuid = MovieScene->AddSpawnable(LastLoadedMMDActorClass->GetName(), *ActorTemplate);
	if (!ActorGuid.IsValid())
	{
		ShowImportProgress(TEXT("Failed to create model spawnable binding."), EMMDMessageType::Error);
		return FReply::Handled();
	}
	if (UMovieSceneSpawnTrack* SpawnTrack = MovieScene->AddTrack<UMovieSceneSpawnTrack>(ActorGuid))
	{
		SpawnTrack->SetObjectId(ActorGuid);
		if (UMovieSceneSection* SpawnSection = SpawnTrack->CreateNewSection())
		{
			SpawnTrack->AddSection(*SpawnSection);
			SpawnSection->SetRange(TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
		}
	}
	AddIdentityTransformTrack(MovieScene, ActorGuid, DurationFrames);

	const FGuid SkelCompGuid = MovieScene->AddPossessable(SkelComp->GetName(), SkelComp->GetClass());
	if (FMovieScenePossessable* SkelCompPossessable = MovieScene->FindPossessable(SkelCompGuid))
	{
		SkelCompPossessable->SetParent(ActorGuid, MovieScene);
	}
	if (FMovieSceneSpawnable* ActorSpawnable = MovieScene->FindSpawnable(ActorGuid))
	{
		ActorSpawnable->AddChildPossessable(SkelCompGuid);
	}
	AActor* ActorTemplateAsActor = Cast<AActor>(ActorTemplate);
	USkeletalMeshComponent* TemplateSkelComp = ActorTemplateAsActor ? ActorTemplateAsActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	if (TemplateSkelComp)
	{
		MasterSequence->BindPossessableObject(SkelCompGuid, *TemplateSkelComp, ActorTemplateAsActor);
	}

	if (UMovieSceneSkeletalAnimationTrack* AnimTrack = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(SkelCompGuid))
	{
		if (UMovieSceneSection* AnimSection = AnimTrack->AddNewAnimation(FFrameNumber(0), AnimSequence))
		{
			AnimSection->SetRange(TRange<FFrameNumber>(
				TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)),
				TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(AnimDurationFrames))));
		}
	}

	FString CameraInlineError;
	const FTransform ModelRootTransform = FTransform::Identity;
	if (!InlineCameraSequenceIntoMaster(CameraSequence, MasterSequence, CameraDurationFrames, ModelRootTransform, CameraInlineError))
	{
		ShowImportProgress(CameraInlineError, EMMDMessageType::Error);
		return FReply::Handled();
	}

	FAssetRegistryModule::AssetCreated(MasterSequence);
	MasterSequence->MarkPackageDirty();
	Package->MarkPackageDirty();

	const FString PackageFilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	UPackage::SavePackage(Package, MasterSequence, *PackageFilePath, SaveArgs);

	FString ActorBlueprintError;
	UBlueprint* SequenceActorBlueprint = CreateMMDLevelSequenceActorBlueprint(
		MasterSequence,
		SequenceFolder,
		FString::Printf(TEXT("BP_%s_Actor"), *UniqueAssetName),
		ActorBlueprintError);

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(MasterSequence);
	}

	if (SequenceActorBlueprint)
	{
		TArray<UObject*> AssetsToSync;
		AssetsToSync.Add(SequenceActorBlueprint);
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(AssetsToSync);
	}

	if (SequenceActorBlueprint)
	{
		ShowImportProgress(
			FString::Printf(TEXT("Master Sequence created: %s. Drag actor blueprint into the level: %s"),
				*MasterSequence->GetPathName(),
				*SequenceActorBlueprint->GetPathName()),
			EMMDMessageType::Success);
	}
	else
	{
		ShowImportProgress(
			FString::Printf(TEXT("Master Sequence created: %s, but actor blueprint creation failed: %s"),
				*MasterSequence->GetPathName(),
				ActorBlueprintError.IsEmpty() ? TEXT("unknown error") : *ActorBlueprintError),
			EMMDMessageType::Warning);
	}
#endif
	return FReply::Handled();
}

void MMDImportSetting::ImportVMDCameraAnimation()
{
	ShowImportProgress(TEXT("Opening VMD camera file dialog..."));

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		ShowImportProgress(TEXT("Cannot open file dialog."), EMMDMessageType::Error);
		return;
	}

	TArray<FString> OpenedFiles;
	const FString FileTypes = TEXT("VMD Files (*.vmd)|*.vmd|All Files (*.*)|*.*");
	const bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Import VMD Camera"),
		TEXT(""),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OpenedFiles);

	if (!bOpened || OpenedFiles.Num() == 0)
	{
		ShowImportProgress(TEXT("VMD camera import canceled."), EMMDMessageType::Warning);
		return;
	}

	const FString SelectedFile = OpenedFiles[0];
	const FString FileName = FPaths::GetCleanFilename(SelectedFile);
	ShowImportProgress(FString::Printf(TEXT("Parsing VMD camera: %s"), *FileName));
	SetSelectedAnimTextUI(FText::FromString(FileName));

	TVMDParser VMDParser;
	if (!VMDParser.ParseVMDFile(SelectedFile))
	{
		ShowImportProgress(TEXT("Failed to parse VMD file."), EMMDMessageType::Error);
		return;
	}

#if WITH_EDITOR
	if (VMDParser.VMDInfo.CameraKeyframes.Num() == 0)
	{
		ShowImportProgress(TEXT("This VMD file has no camera keyframes."), EMMDMessageType::Warning);
		return;
	}

	TArray<VMDCameraKeyframe> CameraKeys = VMDParser.VMDInfo.CameraKeyframes;
	CameraKeys.Sort([](const VMDCameraKeyframe& A, const VMDCameraKeyframe& B)
	{
		return A.FrameNumber < B.FrameNumber;
	});

	int32 MaxFrame = 0;
	for (const VMDCameraKeyframe& Keyframe : CameraKeys)
	{
		MaxFrame = FMath::Max(MaxFrame, static_cast<int32>(Keyframe.FrameNumber));
	}
	const int32 DurationFrames = FMath::Max(MaxFrame + 1, 1);
	const FString CleanBaseName = SanitizeMMDAssetName(FPaths::GetBaseFilename(SelectedFile));

	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		ShowImportProgress(TEXT("No active editor level found for VMD camera import."), EMMDMessageType::Error);
		return;
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const FString BaseAssetPath = FString(TEXT("/Game/MMDModels/LevelSequence/LS_")) + CleanBaseName + TEXT("_Camera");
		AssetToolsModule.Get().CreateUniqueAssetName(BaseAssetPath, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		ShowImportProgress(TEXT("Failed to create LevelSequence package."), EMMDMessageType::Error);
		return;
	}

	ULevelSequence* LevelSequence = NewObject<ULevelSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
	if (!LevelSequence)
	{
		ShowImportProgress(TEXT("Failed to create LevelSequence asset."), EMMDMessageType::Error);
		return;
	}
	LevelSequence->Initialize();

	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (!MovieScene)
	{
		ShowImportProgress(TEXT("Failed to create MovieScene."), EMMDMessageType::Error);
		return;
	}

	ConfigureMMDMovieSceneTiming(MovieScene, DurationFrames);

	const FTransform FirstTransform = ConvertVMDCameraKeyToUnrealTransform(CameraKeys[0], 8.0);

	{
		const FName CameraTemplateName = MakeUniqueObjectName(MovieScene, ACameraActor::StaticClass(), FName(*FString::Printf(TEXT("MMD_%s_Camera"), *CleanBaseName)));
		ACameraActor* CameraTemplateActor = NewObject<ACameraActor>(MovieScene, ACameraActor::StaticClass(), CameraTemplateName, RF_Transactional);
		if (!CameraTemplateActor)
		{
			ShowImportProgress(TEXT("Failed to create spawnable camera template."), EMMDMessageType::Error);
			return;
		}
		CameraTemplateActor->SetActorLabel(FString::Printf(TEXT("MMD_%s_Camera"), *CleanBaseName));
		CameraTemplateActor->SetActorTransform(FirstTransform);

		UCameraComponent* CameraComponent = CameraTemplateActor->GetCameraComponent();
		if (!CameraComponent)
		{
			ShowImportProgress(TEXT("Failed to find camera component."), EMMDMessageType::Error);
			return;
		}
		CameraComponent->SetAspectRatio(16.0f / 9.0f);
		CameraComponent->SetAspectRatioAxisConstraint(EAspectRatioAxisConstraint::AspectRatio_MaintainYFOV);
		CameraComponent->bOverrideAspectRatioAxisConstraint = true;
		CameraComponent->SetFieldOfView(ConvertMMDVerticalFOVToUnrealHorizontalFOV(CameraKeys[0].ViewAngle));

		const FGuid CameraGuid = MovieScene->AddSpawnable(CameraTemplateActor->GetActorLabel(), *CameraTemplateActor);
		if (!CameraGuid.IsValid())
		{
			ShowImportProgress(TEXT("Failed to create spawnable camera binding."), EMMDMessageType::Error);
			return;
		}

		if (UMovieSceneSpawnTrack* SpawnTrack = MovieScene->AddTrack<UMovieSceneSpawnTrack>(CameraGuid))
		{
			SpawnTrack->SetObjectId(CameraGuid);
			if (UMovieSceneSection* SpawnSection = SpawnTrack->CreateNewSection())
			{
				SpawnTrack->AddSection(*SpawnSection);
				SpawnSection->SetRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)), TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
			}
		}

		UMovieScene3DTransformSection* TransformSection = nullptr;
		if (UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(CameraGuid))
		{
			TransformTrack->SetPropertyNameAndPath(TEXT("Transform"), TEXT("Transform"));
			TransformSection = Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
			if (TransformSection)
			{
				TransformTrack->AddSection(*TransformSection);
				TransformSection->SetRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)), TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
			}
		}

		UMovieSceneFloatSection* FOVSection = nullptr;
		if (UCameraComponent* CameraTemplateComponent = CameraTemplateActor->GetCameraComponent())
		{
			const FGuid CameraComponentGuid = MovieScene->AddPossessable(CameraTemplateComponent->GetName(), CameraTemplateComponent->GetClass());
			if (FMovieScenePossessable* CameraComponentPossessable = MovieScene->FindPossessable(CameraComponentGuid))
			{
				CameraComponentPossessable->SetParent(CameraGuid, MovieScene);
			}
			if (FMovieSceneSpawnable* CameraSpawnable = MovieScene->FindSpawnable(CameraGuid))
			{
				CameraSpawnable->AddChildPossessable(CameraComponentGuid);
			}
			LevelSequence->BindPossessableObject(CameraComponentGuid, *CameraTemplateComponent, CameraTemplateActor);

			if (UMovieSceneFloatTrack* FOVTrack = MovieScene->AddTrack<UMovieSceneFloatTrack>(CameraComponentGuid))
			{
				FOVTrack->SetPropertyNameAndPath(TEXT("FieldOfView"), TEXT("FieldOfView"));
				FOVSection = Cast<UMovieSceneFloatSection>(FOVTrack->CreateNewSection());
				if (FOVSection)
				{
					FOVTrack->AddSection(*FOVSection);
					FOVSection->SetRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)), TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
				}
			}
		}

		for (const VMDCameraKeyframe& Keyframe : CameraKeys)
		{
			const FFrameNumber Frame(static_cast<int32>(Keyframe.FrameNumber));
			AddCameraTransformKey(TransformSection, Frame, ConvertVMDCameraKeyToUnrealTransform(Keyframe, 8.0));
			if (FOVSection)
			{
				FOVSection->GetChannel().AddCubicKey(Frame, ConvertMMDVerticalFOVToUnrealHorizontalFOV(Keyframe.ViewAngle));
			}
		}

		if (UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass())))
		{
			if (UMovieSceneCameraCutSection* CameraCutSection = CameraCutTrack->AddNewCameraCut(UE::MovieScene::FRelativeObjectBindingID(CameraGuid), FFrameNumber(0)))
			{
				CameraCutSection->SetRange(TRange<FFrameNumber>(TRangeBound<FFrameNumber>::Inclusive(FFrameNumber(0)), TRangeBound<FFrameNumber>::Exclusive(FFrameNumber(DurationFrames))));
			}
		}
	}

	FAssetRegistryModule::AssetCreated(LevelSequence);
	LevelSequence->MarkPackageDirty();
	Package->MarkPackageDirty();

	const FString PackageFilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	UPackage::SavePackage(Package, LevelSequence, *PackageFilePath, SaveArgs);

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(LevelSequence);
	}

	ShowImportProgress(
		FString::Printf(TEXT("VMD camera imported: %s | CameraKeys=%d | MaxFrame=%d"), *LevelSequence->GetPathName(), CameraKeys.Num(), MaxFrame),
		EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("VMD camera import is editor-only."), EMMDMessageType::Error);
#endif
}
void MMDImportSetting::ShowImportProgress(const FString& Message, EMMDMessageType Type)
{
	if (UMMDToolPanelWidget* ToolPanel = ToolPanelWidget.Get())
	{
		FLinearColor MessageColor = FLinearColor(0.09f, 0.72f, 0.82f, 1.0f);
		switch (Type)
		{
		case EMMDMessageType::Warning:
			MessageColor = FLinearColor(1.0f, 0.70f, 0.16f, 1.0f);
			break;
		case EMMDMessageType::Error:
			MessageColor = FLinearColor(1.0f, 0.24f, 0.32f, 1.0f);
			break;
		case EMMDMessageType::Success:
			MessageColor = FLinearColor(0.12f, 0.74f, 0.46f, 1.0f);
			break;
		case EMMDMessageType::Info:
		default:
			break;
		}
		ToolPanel->SetStatusText(FText::FromString(Message), MessageColor);
	}

	if (StatusText.IsValid())
	{
		switch (Type)
		{
		case EMMDMessageType::Info:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Warning:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Yellow));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Error:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
			StatusText->SetText(FText::FromString(Message));
			break;

		case EMMDMessageType::Success:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::Green));
			StatusText->SetText(FText::FromString(Message));
			break;
		default:
			StatusText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			StatusText->SetText(FText::FromString("No Type: " + Message));
			break;
		}
	}
}
void MMDImportSetting::ShowGlobalImportProgress(const FString& Message, EMMDMessageType Type)
{
	TSharedPtr<MMDImportSetting> Instance = CurrentInstance.Pin();

	if (Instance.IsValid())
	{
		// 濡傛灉瀹炰緥瀛樺湪锛岃皟鐢ㄥ疄渚嬫柟娉?
		Instance->ShowImportProgress(Message, Type);
	}
	else
	{
		// 濡傛灉瀹炰緥涓嶅瓨鍦紝鑷冲皯杈撳嚭鍒版棩蹇?
		switch (Type)
		{
		case EMMDMessageType::Info:
			UE_LOG(LogTemp, Log, TEXT("[MMD瀵煎叆] %s"), *Message);
			break;
		case EMMDMessageType::Warning:
			UE_LOG(LogTemp, Warning, TEXT("[MMD瀵煎叆] %s"), *Message);
			break;
		case EMMDMessageType::Error:
			UE_LOG(LogTemp, Error, TEXT("[MMD瀵煎叆] %s"), *Message);
			break;
		case EMMDMessageType::Success:
			UE_LOG(LogTemp, Warning, TEXT("[MMD瀵煎叆鎴愬姛] %s"), *Message);
			break;
		}
	}
}


