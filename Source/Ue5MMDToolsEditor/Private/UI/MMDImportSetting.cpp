#include "MMDImportSetting.h"
#include "MMDViewPanel.h"
#include "UI/MMDToolPanelStyle.h"
#include "UI/SMMDStudioMark.h"
#include "UI/SMMDAnimatedButton.h"
#include "UI/SMMDTabIndicator.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/MMDToolPanelWidget.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
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
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SExpandableArea.h"
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
#include "Import/MMDModelDataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
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
#include "MovieSceneTimeHelpers.h"
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
#include "UObject/StrongObjectPtr.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#endif // WITH_EDITOR
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#endif

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
static UBlueprint* SaveMMDBlueprintAsset(AActor* TargetActor, const FString& FolderPath, const FString& AssetName, bool bFocusAsset)
{
	if (!TargetActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveMMDBlueprintAsset: TargetActor is null."));
		return nullptr;
	}
#if WITH_EDITOR
	// 1) 生成唯一包名和资源名
	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		const FString TargetLongPackageName = FolderPath / AssetName; // 渚嬪 /Game/MMDModels/Foo
		AssetToolsModule.Get().CreateUniqueAssetName(TargetLongPackageName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	// 2) 创建包与蓝图（使用 CreateBlueprint，规避 CreateBlueprintFromActor 的重载差异）
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *UniquePackageName);
		return nullptr;
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		TargetActor->GetClass(),      // 父类：与实例相同
		Package,                      // 澶栭儴锛氬寘
		*UniqueAssetName,             // 资源名
		BPTYPE_Normal,                // 蓝图类型
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);
	if (!NewBP)
	{
		UE_LOG(LogTemp, Error, TEXT("CreateBlueprint failed for: %s/%s"), *UniquePackageName, *UniqueAssetName);
		return nullptr;
	}

	// 3) 编译，确保 GeneratedClass/CDO 可用
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	// 4) 将实例属性复制到蓝图 CDO（参数顺序：Old=实例，New=CDO）
	if (UClass* GenClass = NewBP->GeneratedClass)
	{
		if (UObject* BPCDO = GenClass->GetDefaultObject(/*bCreateIfNeeded*/true))
		{
			UEngine::FCopyPropertiesForUnrelatedObjectsParams Params; // 鎸夌幇鏈?API锛岀Щ闄や笉瀛樺湪鐨勫瓧娈佃缃?
			UEngine::CopyPropertiesForUnrelatedObjects(/*OldObject=*/TargetActor, /*NewObject=*/BPCDO, Params);
		}
	}

	// 5) 标记和注册
	NewBP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBP);

	// 6) 保存（使用 FSavePackageArgs 新 API）
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

	// 7) 保存并关注蓝图
	if (bFocusAsset) FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewBP);
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

static bool GetMMDPhysicsBakeData(USkeletalMesh* SkeletalMesh, const FString& SourcePMXFilePath,
	TArray<FMMDPhysicsRigidBodyData>& OutRigids, TArray<FMMDPhysicsJointData>& OutJoints, FString& OutError)
{
	OutRigids.Reset();
	OutJoints.Reset();
	OutError.Reset();
	// 已导入的模型数据随资产保存，移动工程后不应再依赖原始 PMX 路径。
	const UMMDSkeletalMeshUserData* UserData = SkeletalMesh
		? Cast<UMMDSkeletalMeshUserData>(SkeletalMesh->GetAssetUserDataOfClass(UMMDSkeletalMeshUserData::StaticClass()))
		: nullptr;
	if (UserData && UserData->ModelDataAsset && !UserData->ModelDataAsset->Rigids.IsEmpty())
	{
		OutRigids = UserData->ModelDataAsset->Rigids;
		OutJoints = UserData->ModelDataAsset->Joints;
		return true;
	}

	if (SourcePMXFilePath.IsEmpty() || !FPaths::FileExists(SourcePMXFilePath))
	{
		OutError = TEXT("The model has no saved rigid body data and no valid source PMX file for physics bake.");
		return false;
	}
	const PMXDatas* ParsedPMX = TMMDMeshBuilder::GetOrParsePMXData(SourcePMXFilePath);
	if (!ParsedPMX)
	{
		OutError = TEXT("Failed to parse PMX physics data.");
		return false;
	}
	ConvertPMXPhysicsToAnimNodeData(*ParsedPMX, OutRigids, OutJoints);
	if (OutRigids.IsEmpty())
	{
		OutError = TEXT("The model has no rigid body data to bake.");
		return false;
	}
	return true;
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
	const FLinearColor GMMDColorBlue = MMDPanelStyle::Text;
	const FLinearColor GMMDColorMint = MMDPanelStyle::Text;
	const FLinearColor GMMDPanelBG = MMDPanelStyle::Canvas;
	const FLinearColor GMMDSectionBG = MMDPanelStyle::Panel;
	const FLinearColor GMMDTextDim = MMDPanelStyle::Muted;

	const FButtonStyle& MMDGetPrimaryButtonStyle() { return *MMDPanelStyle::Primary(); }
	const FButtonStyle& MMDGetSecondaryButtonStyle() { return *MMDPanelStyle::Secondary(); }
	const FButtonStyle& MMDGetAccentButtonStyle() { return *MMDPanelStyle::Primary(); }
	const FButtonStyle& MMDGetWarnButtonStyle() { return *MMDPanelStyle::Secondary(); }

	TSharedRef<SWidget> MMDMakeButton(const FText& Label, const FButtonStyle& Style, FOnClicked Handler, TAttribute<bool> MotionEnabled)
	{
		const FLinearColor TextColor = &Style == MMDPanelStyle::Primary() ? MMDPanelStyle::PrimaryText : MMDPanelStyle::Text;
		return SNew(SMMDAnimatedButton, MotionEnabled).ButtonStyle(&Style).OnClicked(Handler).HAlign(HAlign_Center)
		[SNew(STextBlock).Text(Label).ColorAndOpacity(TextColor).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))];
	}

	TSharedRef<SWidget> MMDMakeSectionHeader(const FText& Title, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(Title).Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
			.ColorAndOpacity(Color).Margin(FMargin(2, 4));
	}

	TSharedRef<SWidget> MMDMakeSolidPanel(const FLinearColor& Color, TSharedRef<SWidget> Content, FMargin Padding = FMargin(10))
	{
		static const FSlateRoundedBoxBrush White(FLinearColor::White, 8.0f);
		return SNew(SBorder).BorderImage(&White).BorderBackgroundColor(Color).Padding(Padding)[Content];
	}
}

void MMDImportSetting::Construct(const FArguments& InArgs)
{
	GConfig->GetBool(TEXT("MMDToolPanel"), TEXT("EnableUIAnimations"), bUIAnimationsEnabled, GEditorPerProjectIni);
	PageTransition.JumpToEnd();
	StatusTransition.JumpToEnd();
	ViewPanel = InArgs._ViewPanel;
	if (!ViewPanel.IsValid())
	{
		SAssignNew(ViewPanel, MMDViewPanel);
	}

	TSharedRef<SVerticalBox> ImportPage = SNew(SVerticalBox);
	auto AddSection = [&ImportPage](const FText& Title, const FLinearColor& Color) -> TSharedRef<SVerticalBox>
	{
		ImportPage->AddSlot().AutoHeight().Padding(0, 8, 0, 6)[MMDMakeSectionHeader(Title, Color)];
		TSharedRef<SVerticalBox> Section = SNew(SVerticalBox);
		ImportPage->AddSlot().AutoHeight().Padding(0, 0, 0, 12)[Section];
		return Section;
	};
	auto AddButton = [this](TSharedRef<SVerticalBox> Section, const FText& Label, const FButtonStyle& Style, FOnClicked Handler)
	{
		Section->AddSlot().AutoHeight().Padding(0, 3)[MMDMakeButton(Label, Style, Handler,
			TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))];
	};

	TSharedRef<SVerticalBox> ModelSection = AddSection(FText::FromString(TEXT("01  选择角色")), GMMDColorBlue);
	AddButton(ModelSection, FText::FromString(TEXT("导入 PMX 模型")), MMDGetPrimaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportModelClicked));
	AddButton(ModelSection, FText::FromString(TEXT("加载已有角色")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateLambda([this]() { LoadSelectedMMDActor(); return FReply::Handled(); }));

	TSharedRef<SVerticalBox> MotionSection = AddSection(FText::FromString(TEXT("02  姿势与动作")), GMMDColorBlue);
	MotionSection->AddSlot().AutoHeight()[BuildMotionPresetPanel()];
	TSharedRef<SVerticalBox> MotionImport = SNew(SVerticalBox);
	MotionSection->AddSlot().AutoHeight().Padding(0, 6)
	[
		SNew(SExpandableArea).InitiallyCollapsed(true).Style(MMDPanelStyle::Expandable())
		.BorderImage(MMDPanelStyle::CardBrush()).HeaderPadding(FMargin(10, 8)).Padding(FMargin(8))
		.HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("导入自己的 VMD／表情／相机"))).ColorAndOpacity(GMMDTextDim)]
		.BodyContent()[MotionImport]
	];
	MotionSection = MotionImport;
	AddButton(MotionSection, FText::FromString(TEXT("导入 VMD 动作")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportVMDClicked));
	AddButton(MotionSection, FText::FromString(TEXT("追加表情")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnAppendFacialVMDClicked));
	AddButton(MotionSection, FText::FromString(TEXT("导入 VMD 相机")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnImportVMDCameraClicked));

	TSharedRef<SVerticalBox> OutputSection = AddSection(FText::FromString(TEXT("03  制作与输出")), GMMDColorMint);
	AddButton(OutputSection, FText::FromString(TEXT("合成关卡序列")), MMDGetAccentButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnOpenSequenceComposerClicked));
	AddButton(OutputSection, FText::FromString(TEXT("烘焙物理动画")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnOpenPhysicsBakeClicked));

	TSharedRef<SVerticalBox> Advanced = SNew(SVerticalBox);
	Advanced->AddSlot().AutoHeight().Padding(0, 6)
	[
		SNew(SCheckBox)
		.IsChecked_Lambda([]() { return TMMDMeshBuilder::IsUseStandardMaterials() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([](ECheckBoxState State) { TMMDMeshBuilder::SetUseStandardMaterials(State == ECheckBoxState::Checked); })
		[SNew(STextBlock).Text(FText::FromString(TEXT("导入时使用 MMD Standard 材质"))).ColorAndOpacity(MMDPanelStyle::Text).AutoWrapText(true)]
	];
	Advanced->AddSlot().AutoHeight().Padding(0, 6)
	[
		SNew(STextBlock).Text(FText::FromString(TEXT("Aurora Jewel · 旧版资产方案\n以下操作会保存到角色蓝图资产。")))
		.AutoWrapText(true).ColorAndOpacity(GMMDTextDim)
	];
	AddButton(Advanced, FText::FromString(TEXT("Aurora Jewel：应用并保存资产")), MMDGetSecondaryButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnApplyAuroraJewelToonClicked));
	AddButton(Advanced, FText::FromString(TEXT("撤回 Aurora Jewel 资产修改")), MMDGetWarnButtonStyle(), FOnClicked::CreateRaw(this, &MMDImportSetting::OnUndoShaderPresetClicked));
	ImportPage->AddSlot().AutoHeight().Padding(0, 12)
	[
		SNew(SExpandableArea).InitiallyCollapsed(true).Style(MMDPanelStyle::Expandable())
		.BorderImage(MMDPanelStyle::CardBrush()).HeaderPadding(FMargin(10, 8)).Padding(FMargin(8))
		.HeaderContent()[SNew(STextBlock).Text(FText::FromString(TEXT("高级选项与旧版工具"))).ColorAndOpacity(GMMDTextDim)]
		.BodyContent()[Advanced]
	];

	SAssignNew(WorkflowSwitcher, SWidgetSwitcher);
	auto AddPage = [this](TSharedRef<SWidget> Content)
	{
		WorkflowSwitcher->AddSlot()
		[SNew(SScrollBox).Style(MMDPanelStyle::ScrollBox()).ScrollBarStyle(MMDPanelStyle::ScrollBar()).ScrollBarThickness(FVector2D(4, 4))
			+ SScrollBox::Slot().Padding(12)[Content]];
	};
	AddPage(ImportPage);
	AddPage(BuildRenderPresetPanel());
	AddPage(BuildScenePresetPanel());

	TSharedRef<SHorizontalBox> Navigation = SNew(SHorizontalBox);
	const TCHAR* TabNames[] = { TEXT("角色"), TEXT("风格"), TEXT("场景") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Navigation->AddSlot().FillWidth(1).Padding(2, 0)
		[
			SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
			.Tag(FName(*FString::Printf(TEXT("MMDWorkflowTab%d"), Index)))
			.ButtonStyle(MMDPanelStyle::Tab()).HAlign(HAlign_Fill).ContentPadding(FMargin(0))
			.OnClicked_Lambda([this, Index]() { return SelectWorkflowPage(Index); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(8, 12)
				[SNew(STextBlock).Text(FText::FromString(TabNames[Index])).ColorAndOpacity_Lambda([this, Index]() { return WorkflowSwitcher->GetActiveWidgetIndex() == Index ? MMDPanelStyle::Accent : MMDPanelStyle::Muted; })
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))]
			]
		];
	}
	TSharedRef<SWidget> Sidebar = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 4, 10, 0)[Navigation]
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 0)
		[SNew(SMMDTabIndicator).Position(this, &MMDImportSetting::GetNavigationPosition)]
		+ SVerticalBox::Slot().FillHeight(1)
		[SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)
			[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(0)
				.ColorAndOpacity_Lambda([this]() { return FLinearColor(1, 1, 1, FMath::Lerp(0.72f, 1.0f, GetPageTransitionAlpha())); })
				.RenderTransform_Lambda([this]() { return FSlateRenderTransform(FVector2D(0, 7.0f * (1.0f - GetPageTransitionAlpha()))); })
				[WorkflowSwitcher.ToSharedRef()]]];

	// ================= 右侧预览区 =================
	TSharedRef<SWidget> PreviewArea =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 10, 10, 6)
		[SNew(SBorder).BorderImage(MMDPanelStyle::HeaderBrush()).Padding(FMargin(12, 8))
			[SNew(STextBlock).Text_Lambda([this]() { return ViewPanel->GetLookDevDescription(); }).ColorAndOpacity(MMDPanelStyle::Accent).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))]]
		+ SVerticalBox::Slot().AutoHeight().Padding(10, 2)
		[
			SAssignNew(SelectedModelText, STextBlock)
			.Text(FText::FromString(TEXT("模型：未选择")))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(GMMDTextDim))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f, 0.0f, 8.0f, 6.0f)
		[
			SAssignNew(SelectedAnimText, STextBlock)
			.Text_Lambda([this]() { return FText::FromString(TEXT("动画：") + ViewPanel->GetPreviewAnimationLabel().ToString()); })
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(GMMDTextDim))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 0.0f, 8.0f, 8.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ViewPanel.ToSharedRef()]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[SNew(SBorder).BorderImage(MMDPanelStyle::HeaderBrush()).Padding(FMargin(20, 16))
				.Visibility_Lambda([this]() { return ViewPanel->HasPreviewModel() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				[SNew(STextBlock).Text(FText::FromString(TEXT("从左侧导入 PMX 或加载已有角色\n加载后自动进入待机，可随时换动作与视角")))
					.Justification(ETextJustify::Center).ColorAndOpacity(MMDPanelStyle::Text)]]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)[BuildPreviewControls()];

	// ================= 头部 =================
	TSharedRef<SWidget> Header =
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SMMDStudioHeaderArt).Visibility(EVisibility::HitTestInvisible)]
		+ SOverlay::Slot().Padding(FMargin(18.0f, 12.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)[SNew(SMMDStudioMark)]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[SNew(STextBlock).Text(FText::FromString(TEXT("MMD STUDIO")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 19)).ColorAndOpacity(MMDPanelStyle::Text)]
				+ SVerticalBox::Slot().AutoHeight().Padding(1, 3, 0, 0)
				[SNew(STextBlock).Text(FText::FromString(TEXT("角 色 工 坊")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(MMDPanelStyle::Muted)]
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
			[BuildPreviewMovieControls()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SCheckBox).Tag(TEXT("MMDUIAnimations"))
				.IsChecked_Lambda([this]() { return bUIAnimationsEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { SetUIAnimationsEnabled(State == ECheckBoxState::Checked); })
				.ToolTipText(FText::FromString(TEXT("按钮与页签使用短过渡；关闭后即时切换。此偏好会记住，角色动画和物理继续运行。")))
				[SNew(STextBlock).Text(FText::FromString(TEXT("界面动效"))).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9)).ColorAndOpacity(GMMDTextDim)]
			]
		];

	// ================= 状态栏 =================
	TSharedRef<SWidget> StatusBar =
		SNew(SBorder)
		.BorderImage(MMDPanelStyle::HeaderBrush())
		.Padding(FMargin(12.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SAssignNew(StatusTypeText, STextBlock)
				.Text(FText::FromString(TEXT("提示  ")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(GMMDTextDim))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("NoBrush")).Padding(0)
				.ColorAndOpacity_Lambda([this]() { return FLinearColor(1, 1, 1, FMath::Lerp(0.8f, 1.0f, GetStatusTransitionAlpha())); })
				[
				SAssignNew(StatusText, STextBlock)
				.Text(FText::FromString(TEXT("先导入或加载角色，再选择风格与场景。")))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(GMMDTextDim))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[BuildPreviewMovieResultControls()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
			[SNew(SMMDAnimatedButton, TAttribute<bool>::CreateSP(this, &MMDImportSetting::AreUIAnimationsEnabled))
				.ButtonStyle(MMDPanelStyle::Tab()).ContentPadding(FMargin(6, 3)).ForegroundColor(GMMDTextDim)
				.Text(FText::FromString(TEXT("复制"))).ToolTipText(FText::FromString(TEXT("复制完整操作结果，便于查找资产或反馈问题。")))
				.IsEnabled_Lambda([this]() { return !CurrentStatusMessage.IsEmpty(); })
				.OnClicked_Lambda([this]() { FPlatformApplicationMisc::ClipboardCopy(*CurrentStatusMessage); return FReply::Handled(); })]
		];

	// ================= 整体布局 =================
	TSharedRef<SSplitter> Splitter =
		SNew(SSplitter)
		.Style(MMDPanelStyle::Splitter()).PhysicalSplitterHandleSize(2.0f).HitDetectionSplitterHandleSize(8.0f)
		.Orientation(Orient_Horizontal)
		+ SSplitter::Slot().Value(0.34f).MinSize(350.0f)
		[
			MMDMakeSolidPanel(GMMDSectionBG, Sidebar, FMargin(0))
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
			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8.0f, 6.0f, 6.0f, 6.0f)
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
		SelectedModelText->SetToolTipText(Text);
		SelectedModelText->SetColorAndOpacity(FSlateColor(MMDPanelStyle::Text));
	}
}

void MMDImportSetting::SetSelectedAnimTextUI(const FText& Text)
{
	if (SelectedAnimText.IsValid())
	{
		SelectedAnimText->SetToolTipText(Text);
		SelectedAnimText->SetColorAndOpacity(FSlateColor(MMDPanelStyle::Text));
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
	SetCurrentMMDActorClass(ActorClass, ActorBlueprint->GetName());

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

	++ModelSelectionRevision;
	LastLoadedMMDActorClass = ActorClass;
	LastImportedMMDActor = nullptr;
	ComposerActor = nullptr;
	ComposerSkeletalMeshComponent = nullptr;
	PhysicsBakeActor = nullptr;
	PhysicsBakeSkeletalMeshComponent = nullptr;
	const bool bClearedAnimation = (ComposerAnimSequence.IsValid() && !IsAnimationCompatibleWithCurrentModel(ComposerAnimSequence.Get()))
		|| (PhysicsBakeAnimSequence.IsValid() && !IsAnimationCompatibleWithCurrentModel(PhysicsBakeAnimSequence.Get()));
	if (!IsAnimationCompatibleWithCurrentModel(ComposerAnimSequence.Get())) ComposerAnimSequence.Reset();
	if (!IsAnimationCompatibleWithCurrentModel(PhysicsBakeAnimSequence.Get())) PhysicsBakeAnimSequence.Reset();
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

	ShowImportProgress(FString::Printf(TEXT("已加载角色：%s%s"), DisplayName.IsEmpty() ? *ActorClass->GetName() : *DisplayName,
		bClearedAnimation ? TEXT("。已清除与此角色不兼容的制作动画，请重新选择。") : TEXT("")), EMMDMessageType::Success);
	return true;
#else
	ShowImportProgress(TEXT("Loading AMMDActor is editor-only."), EMMDMessageType::Error);
	return false;
#endif
}

uint64 MMDImportSetting::BeginModelImport(AActor* Actor)
{
	LastImportedMMDActor = Actor;
	return ++ModelSelectionRevision;
}

bool MMDImportSetting::AdoptImportedActorClass(uint64 RequestRevision, UClass* ActorClass, const FString& DisplayName)
{
	// 旧导入仍保存资产，但不能覆盖后来选择的角色。
	return RequestRevision == ModelSelectionRevision && SetCurrentMMDActorClass(ActorClass, DisplayName);
}

bool MMDImportSetting::IsAnimationCompatibleWithCurrentModel(const UAnimSequence* Animation) const
{
	FMMDSelectedAnimationTarget Target;
	if (!Animation || !Animation->GetSkeleton() || !TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target)) return false;
	const USkeleton* Skeleton = Target.SkeletalMesh->GetSkeleton();
	return Skeleton && (Skeleton == Animation->GetSkeleton() || Skeleton->IsCompatibleForEditor(Animation->GetSkeleton()));
}

bool MMDImportSetting::ShouldFilterAnimationForCurrentModel(const FAssetData& AssetData) const
{
	FMMDSelectedAnimationTarget Target;
	if (!TryGetAnimationTargetFromActorClass(LastLoadedMMDActorClass.Get(), Target) || !Target.SkeletalMesh->GetSkeleton()) return true;
	const FString Folder = GetMMDModelAnimationFolder(LastLoadedMMDActorClass.Get());
	const FString AssetFolder = AssetData.PackagePath.ToString();
	if (!Folder.IsEmpty() && AssetFolder != Folder && !AssetFolder.StartsWith(Folder + TEXT("/"))) return true;
	return !Target.SkeletalMesh->GetSkeleton()->IsCompatibleForEditor(AssetData);
}

bool MMDImportSetting::SelectProductionAnimation(UAnimSequence* Animation, bool bForPhysicsBake)
{
	if (Animation && !IsAnimationCompatibleWithCurrentModel(Animation))
	{
		ShowImportProgress(TEXT("动画骨架与当前角色不兼容，请选择此角色的动画或先完成重定向；原选择已保留。"), EMMDMessageType::Warning);
		return false;
	}
	if (bForPhysicsBake) PhysicsBakeAnimSequence = Animation;
	else ComposerAnimSequence = Animation;
	RefreshSequenceComposerLabels();
	RefreshPhysicsBakeLabels();
	return true;
}

bool MMDImportSetting::UseBakedAnimationResult(UAnimSequence* Animation, uint64 RequestRevision)
{
	// 烘焙期间会泵 Slate 消息；用户若换角色，完成结果不抢新的制作选择。
	return Animation && RequestRevision == ModelSelectionRevision && SelectProductionAnimation(Animation, false);
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

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &MMDImportSetting::ShouldFilterAnimationForCurrentModel);

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
	ShowImportProgress(TEXT("打开文件选择对话框..."));

	// 使用文件对话框选择MMD模型文件
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		TArray<FString> OpenedFiles;
		const FString FileTypes = TEXT("MMD Model Files (*.pmx;*.pmd;*.fbx)|*.pmx;*.pmd;*.fbx|PMX Files (*.pmx)|*.pmx|PMD Files (*.pmd)|*.pmd|FBX Files (*.fbx)|*.fbx|All Files (*.*)|*.*");

		bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("导入MMD模型"),
			TEXT(""), // 默认路径
			TEXT(""), // 默认文件名
			FileTypes,
			EFileDialogFlags::None,
			OpenedFiles);

		if (bOpened && OpenedFiles.Num() > 0)
		{
			FString SelectedFile = OpenedFiles[0];
			FString FileName = FPaths::GetCleanFilename(SelectedFile);

			ShowImportProgress(FString::Printf(TEXT("已选择文件: %s"), *FileName));
			SetSelectedModelTextUI(FText::FromString(FileName));

			if (ViewPanel.IsValid())
			{
				ViewPanel->LoadMMDModel(SelectedFile);
			}

			ShowImportProgress(FString::Printf(TEXT("正在加载模型: %s"), *FileName));

			if (FPaths::GetExtension(SelectedFile).Equals(TEXT("pmx"), ESearchCase::IgnoreCase))
			{
				ShowImportProgress(TEXT("开始解析PMX文件..."));
				UE_LOG(LogTemp, Warning, TEXT("开始解析PMX文件: %s"), *SelectedFile);
#if WITH_EDITOR
				UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (!EditorWorld) {
					ShowImportProgress(TEXT("未找到编辑器世界，无法生成Actor"), EMMDMessageType::Error);
					return;
				}
				FActorSpawnParameters SpawnParams;
				SpawnParams.Name = MakeUniqueObjectName(EditorWorld, AMMDActor::StaticClass(), FName(TEXT("MMDActor")));
				SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
				AMMDActor* NewMMDActor = EditorWorld->SpawnActor<AMMDActor>(AMMDActor::StaticClass(), FTransform::Identity, SpawnParams);
				if (!NewMMDActor)
				{
					ShowImportProgress(TEXT("生成AMMDActor失败"), EMMDMessageType::Error);
					return;
				}
				const uint64 RequestRevision = BeginModelImport(NewMMDActor);

				// 异步导入完成（成功/失败）后在游戏线程回调里保存蓝图并刷新预览；
				// 此时 SkeletalMesh/动画蓝图已装配到 Actor 上，CDO 才能带上网格引用。
				{
					TWeakPtr<MMDImportSetting> WeakUI = SharedThis(this);
					TWeakObjectPtr<AMMDActor> WeakActor(NewMMDActor);
					const FString FileCopy = SelectedFile;
					const FString FolderCopy = TEXT("/Game/MMDModels");
					const FString NameCopy = FPaths::GetBaseFilename(FileName);
					NewMMDActor->OnSetupFinished().AddLambda(
						[WeakUI, WeakActor, FileCopy, FolderCopy, NameCopy, RequestRevision](AMMDActor* Actor)
						{
							TSharedPtr<MMDImportSetting> UI = WeakUI.Pin();
							AMMDActor* MMD = WeakActor.Get();
							if (!UI.IsValid() || !MMD || MMD != Actor)
							{
								return;
							}

							USkeletalMeshComponent* MeshComp = MMD->GetMeshComponent();
							if (!MeshComp || !MeshComp->GetSkeletalMeshAsset())
							{
								if (UI->ModelSelectionRevision == RequestRevision)
									UI->ShowImportProgress(TEXT("MMD 模型导入失败，跳过蓝图保存"), EMMDMessageType::Error);
								return;
							}

							// 保存为蓝图资产（文件级函数，同 TU 内可见）
							if (UBlueprint* NewBP = SaveMMDBlueprintAsset(MMD, FolderCopy + TEXT("/") + NameCopy + TEXT("/BluePrint"), NameCopy, UI->ModelSelectionRevision == RequestRevision))
							{
								if (UClass* GenClass = NewBP->GeneratedClass)
								{
									// 关键：把 PMX 源路径设置到蓝图 CDO，保证蓝图实例 OnConstruction 能拿到
									if (AActor* CDOActor = Cast<AActor>(GenClass->GetDefaultObject()))
									{
										if (AMMDActor* CDO = Cast<AMMDActor>(CDOActor))
										{
											CDO->SourcePMXFilePath = FileCopy; // 绝对路径
											CDO->Modify(true);
											UE_LOG(LogTemp, Log, TEXT("[Import] Set CDO SourcePMXFilePath: %s"), *FileCopy);
										}
									}

									// 重新编译，使默认值生效
									FKismetEditorUtilities::CompileBlueprint(NewBP);

									UI->AdoptImportedActorClass(RequestRevision, GenClass, NameCopy);
								}
							}
						});
				}

				ShowImportProgress(TEXT("正在后台导入 PMX，完成后保存角色蓝图；若切换角色，将保留后来选择的预览。"));
				NewMMDActor->SetupComponents(SelectedFile);

				GEditor->SelectNone(false, true);
				GEditor->SelectActor(NewMMDActor, true, true);
				GEditor->MoveViewportCamerasToActor(*NewMMDActor, false);



#else
				ShowImportProgress(TEXT("仅在编辑器中可生成Actor"), EMMDMessageType::Warning);
#endif
			}
			else
			{
				ShowImportProgress(FString::Printf(TEXT("文件类型: %s (非PMX)"), *FPaths::GetExtension(SelectedFile)));
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
	ShowImportProgress(TEXT("打开静态网格选择对话框..."));

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

			ShowImportProgress(FString::Printf(TEXT("已选择静态网格: %s"), *FileName));

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
	ShowImportProgress(TEXT("打开VMD动画选择对话框..."));

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
		TEXT("导入VMD动画"),
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
	ShowImportProgress(FString::Printf(TEXT("正在解析VMD文件: %s"), *FileName));
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

	const PMXDatas* PMXData = TMMDMeshBuilder::GetOrParsePMXData(Target.SourcePMXFilePath);
	if (!PMXData && !Target.SourcePMXFilePath.IsEmpty() && FPaths::FileExists(Target.SourcePMXFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Failed to parse source PMX for IK bake: %s"), *Target.SourcePMXFilePath);
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

	SetSelectedAnimTextUI(FText::FromString(AnimSequence->GetName()));
	SetPreviewAnimationUI(AnimSequence);
	ComposerAnimSequence = AnimSequence;
	PhysicsBakeAnimSequence = AnimSequence;
	RefreshSequenceComposerLabels();
	RefreshPhysicsBakeLabels();

	for (const FString& Warning : ImportReport.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMD Import] %s"), *Warning);
	}

	ShowImportProgress(
		FString::Printf(
			TEXT("VMD骨骼/表情动画导入并播放: %s | Bone %d/%d | Morph %d/%d | MaxFrame=%d"),
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

	const PMXDatas* PMXData = TMMDMeshBuilder::GetOrParsePMXData(Target.SourcePMXFilePath);
	if (!PMXData && !Target.SourcePMXFilePath.IsEmpty() && FPaths::FileExists(Target.SourcePMXFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[VMD Facial Append] Failed to parse source PMX: %s"), *Target.SourcePMXFilePath);
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
	const FGuid& TargetGuid,
	FFrameNumber SourceStartFrame)
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
			for (UMovieSceneSection* Section : NewTrack->GetAllSections())
			{
				const bool bWasLocked = Section->IsLocked();
				Section->SetIsLocked(false);
				Section->MoveSection(-SourceStartFrame);
				Section->SetIsLocked(bWasLocked);
			}
			if (UMovieSceneSpawnTrack* SpawnTrack = Cast<UMovieSceneSpawnTrack>(NewTrack))
			{
				SpawnTrack->SetObjectId(TargetGuid);
			}
			TargetBinding->AddTrack(*NewTrack, TargetMovieScene);
		}
	}
}

static int32 GetMMDSequenceDurationFrames(const UMovieScene* MovieScene, FFrameRate TargetRate, int32 FallbackFrames)
{
	if (MovieScene)
	{
		const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
		if (Range.HasLowerBound() && Range.HasUpperBound())
		{
			const FFrameNumber Start = FFrameRate::TransformTime(UE::MovieScene::DiscreteInclusiveLower(Range),
				MovieScene->GetTickResolution(), TargetRate).RoundToFrame();
			const FFrameNumber End = FFrameRate::TransformTime(UE::MovieScene::DiscreteExclusiveUpper(Range),
				MovieScene->GetTickResolution(), TargetRate).RoundToFrame();
			return FMath::Max(1, (End - Start).Value);
		}
	}
	return FallbackFrames;
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
	// 在临时副本上由引擎迁移轨道、通道及切线的时间基，源镜头资产保持原样。
	TStrongObjectPtr<UMovieScene> CameraTimingCopy(DuplicateObject<UMovieScene>(SourceMovieScene, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UMovieScene::StaticClass(), TEXT("MMDCameraTimingCopy"))));
	SourceMovieScene = CameraTimingCopy.Get();
	if (!SourceMovieScene)
	{
		OutError = TEXT("Failed to copy camera MovieScene for timing conversion.");
		return false;
	}
	if (SourceMovieScene->GetTickResolution() != TargetMovieScene->GetTickResolution())
	{
		UE::MovieScene::TimeHelpers::MigrateFrameTimes(
			SourceMovieScene->GetTickResolution(), TargetMovieScene->GetTickResolution(), SourceMovieScene);
	}
	const TRange<FFrameNumber> SourcePlaybackRange = SourceMovieScene->GetPlaybackRange();
	const FFrameNumber SourceStartFrame = SourcePlaybackRange.HasLowerBound()
		? UE::MovieScene::DiscreteInclusiveLower(SourcePlaybackRange) : FFrameNumber(0);

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

	CopyMovieSceneBindingTracks(SourceMovieScene, SourceCameraGuid, TargetMovieScene, TargetCameraGuid, SourceStartFrame);
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

			CopyMovieSceneBindingTracks(SourceMovieScene, SourceChildGuid, TargetMovieScene, TargetChildGuid, SourceStartFrame);
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
		if (!PhysicsBakeAnimSequence.IsValid()) SelectProductionAnimation(SelectedAnim, true);
	}

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
					.OnShouldFilterAsset(FOnShouldFilterAsset::CreateSP(this, &MMDImportSetting::ShouldFilterAnimationForCurrentModel))
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
		if (!ComposerAnimSequence.IsValid()) SelectProductionAnimation(SelectedAnim, false);
	}
	if (ULevelSequence* SelectedCameraSequence = Cast<ULevelSequence>(GetFirstSelectedEditorObjectOfClass(ULevelSequence::StaticClass())))
	{
		if (!ComposerCameraSequence.IsValid()) ComposerCameraSequence = SelectedCameraSequence;
	}

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
					.OnShouldFilterAsset(FOnShouldFilterAsset::CreateSP(this, &MMDImportSetting::ShouldFilterAnimationForCurrentModel))
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
	UAnimSequence* SelectedAnim = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass()));
	if (!SelectedAnim)
	{
		ShowImportProgress(TEXT("Select an AnimSequence asset in the Content Browser first."), EMMDMessageType::Warning);
	}
	else SelectProductionAnimation(SelectedAnim, false);
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
		++ModelSelectionRevision;
		LastLoadedMMDActorClass.Reset();
		ComposerSkeletalMeshComponent.Reset();
		PhysicsBakeSkeletalMeshComponent.Reset();
		ComposerAnimSequence.Reset();
		PhysicsBakeAnimSequence.Reset();
		RefreshSequenceComposerLabels();
		RefreshPhysicsBakeLabels();
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
	SelectProductionAnimation(Cast<UAnimSequence>(AssetData.GetAsset()), false);
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
	UAnimSequence* SelectedAnim = Cast<UAnimSequence>(GetFirstSelectedEditorObjectOfClass(UAnimSequence::StaticClass()));
	if (!SelectedAnim)
	{
		ShowImportProgress(TEXT("Select an AnimSequence asset in the Content Browser first."), EMMDMessageType::Warning);
	}
	else SelectProductionAnimation(SelectedAnim, true);
	RefreshPhysicsBakeLabels();
#endif
	return FReply::Handled();
}

void MMDImportSetting::OnPhysicsBakeAnimationChanged(const FAssetData& AssetData)
{
	SelectProductionAnimation(Cast<UAnimSequence>(AssetData.GetAsset()), true);
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
	if (!IsAnimationCompatibleWithCurrentModel(SourceAnim))
	{
		ShowImportProgress(TEXT("无法烘焙：动画骨架与当前角色不兼容，请重新选择或先完成重定向。"), EMMDMessageType::Error);
		return FReply::Handled();
	}
	const uint64 RequestRevision = ModelSelectionRevision;

	TArray<FMMDPhysicsRigidBodyData> Rigids;
	TArray<FMMDPhysicsJointData> Joints;
	FString PhysicsDataError;
	if (!GetMMDPhysicsBakeData(Target.SkeletalMesh, Target.SourcePMXFilePath, Rigids, Joints, PhysicsDataError))
	{
		ShowImportProgress(PhysicsDataError, EMMDMessageType::Error);
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

	const bool bUsingBakedAnimation = UseBakedAnimationResult(BakedAnim, RequestRevision);
	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(BakedAnim);
	}

	ShowImportProgress(FString::Printf(TEXT("物理已烘焙：%s。%s"), *BakedAnim->GetPathName(),
		bUsingBakedAnimation ? TEXT("合成器已改用烘焙结果；烘焙输入仍保留原动作。") : TEXT("当前角色已变化，未替换新的制作选择。")), EMMDMessageType::Success);
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
	if (!IsAnimationCompatibleWithCurrentModel(AnimSequence))
	{
		ShowImportProgress(TEXT("无法合成：动画骨架与当前角色不兼容，请重新选择或先完成重定向。"), EMMDMessageType::Error);
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
	const int32 CameraDurationFrames = GetMMDSequenceDurationFrames(CameraSequence->GetMovieScene(), FFrameRate(30, 1), AnimDurationFrames);
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
	if (!UPackage::SavePackage(Package, MasterSequence, *PackageFilePath, SaveArgs))
	{
		ShowImportProgress(FString::Printf(TEXT("Failed to save master LevelSequence: %s. Sequence actor creation was stopped."),
			*PackageFilePath), EMMDMessageType::Error);
		return FReply::Handled();
	}

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
	if (!UPackage::SavePackage(Package, LevelSequence, *PackageFilePath, SaveArgs))
	{
		ShowImportProgress(FString::Printf(TEXT("Failed to save VMD camera LevelSequence: %s."), *PackageFilePath), EMMDMessageType::Error);
		return;
	}
	OnComposerCameraChanged(FAssetData(LevelSequence));

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditorSubsystem->OpenEditorForAsset(LevelSequence);
	}

	ShowImportProgress(
		FString::Printf(TEXT("VMD 相机已导入并用于合成：%s | CameraKeys=%d | MaxFrame=%d"), *LevelSequence->GetPathName(), CameraKeys.Num(), MaxFrame),
		EMMDMessageType::Success);
#else
	ShowImportProgress(TEXT("VMD camera import is editor-only."), EMMDMessageType::Error);
#endif
}
void MMDImportSetting::ShowImportProgress(const FString& Message, EMMDMessageType Type)
{
	const bool bChanged = CurrentStatusMessage != Message || CurrentStatusType != Type;
	CurrentStatusMessage = Message;
	CurrentStatusType = Type;
	FLinearColor Color = MMDPanelStyle::Accent;
	const TCHAR* Label = TEXT("提示  ");
	switch (Type)
	{
	case EMMDMessageType::Warning: Color = MMDPanelStyle::Warning; Label = TEXT("注意  "); break;
	case EMMDMessageType::Error: Color = MMDPanelStyle::Error; Label = TEXT("失败  "); break;
	case EMMDMessageType::Success: Color = MMDPanelStyle::Success; Label = TEXT("完成  "); break;
	default: break;
	}
	if (UMMDToolPanelWidget* ToolPanel = ToolPanelWidget.Get())
	{
		ToolPanel->SetStatusText(FText::FromString(Message), Color);
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
		StatusText->SetToolTipText(FText::FromString(Message));
		StatusText->SetColorAndOpacity(Color);
	}
	if (StatusTypeText)
	{
		StatusTypeText->SetText(FText::FromString(Label));
		StatusTypeText->SetColorAndOpacity(Color);
	}
	// 连续导入日志保持稳定，只给最终结果轻量反馈。
	if (bChanged && bUIAnimationsEnabled && Type != EMMDMessageType::Info) StatusTransition.Play(AsShared());
	else StatusTransition.JumpToEnd();
}
void MMDImportSetting::ShowGlobalImportProgress(const FString& Message, EMMDMessageType Type)
{
	TSharedPtr<MMDImportSetting> Instance = CurrentInstance.Pin();

	if (Instance.IsValid())
	{
		// 如果实例存在，调用实例方法
		Instance->ShowImportProgress(Message, Type);
	}
	else
	{
		// 如果实例不存在，至少输出到日志
		switch (Type)
		{
		case EMMDMessageType::Info:
			UE_LOG(LogTemp, Log, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Warning:
			UE_LOG(LogTemp, Warning, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Error:
			UE_LOG(LogTemp, Error, TEXT("[MMD导入] %s"), *Message);
			break;
		case EMMDMessageType::Success:
			UE_LOG(LogTemp, Warning, TEXT("[MMD导入成功] %s"), *Message);
			break;
		}
	}
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPhysicsBakeSavedDataTest, "Ue5MMDTools.Creation.PhysicsBakeSavedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDPhysicsBakeSavedDataTest::RunTest(const FString& Parameters)
{
	USkeletalMesh* Mesh = NewObject<USkeletalMesh>();
	UMMDSkeletalMeshUserData* UserData = NewObject<UMMDSkeletalMeshUserData>(Mesh);
	UMMDModelDataAsset* ModelData = NewObject<UMMDModelDataAsset>();
	UserData->ModelDataAsset = ModelData;
	Mesh->AddAssetUserData(UserData);
	FMMDPhysicsRigidBodyData& Rigid = ModelData->Rigids.AddDefaulted_GetRef();
	Rigid.Name = TEXT("SavedRigid");
	Rigid.ShapeType = 0;
	Rigid.ShapeSize = FVector(1.0f);
	Rigid.PhysicsMode = 0;

	TArray<FMMDPhysicsRigidBodyData> Rigids;
	TArray<FMMDPhysicsJointData> Joints;
	FString Error;
	TestTrue(TEXT("源文件已丢失时使用资产内刚体"), GetMMDPhysicsBakeData(Mesh, FString(), Rigids, Joints, Error));
	TestEqual(TEXT("独立刚体无需关节即可烘焙"), Joints.Num(), 0);
	TestEqual(TEXT("读取保存的刚体数"), Rigids.Num(), 1);
	if (!Rigids.IsEmpty())
	{
		Rigids[0].Mass = 99.0f;
		TestNotEqual(TEXT("烘焙数据副本不修改资产"), ModelData->Rigids[0].Mass, 99.0f);
	}
	ModelData->Rigids.Reset();
	TestFalse(TEXT("没有保存数据和源文件时拒绝烘焙"), GetMMDPhysicsBakeData(Mesh, FString(), Rigids, Joints, Error));
	TestFalse(TEXT("缺少数据时返回具体错误"), Error.IsEmpty());
	TestTrue(TEXT("失败不遗留上次刚体"), Rigids.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDComposerCameraTimingTest, "Ue5MMDTools.Creation.CameraTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDComposerCameraTimingTest::RunTest(const FString& Parameters)
{
	ULevelSequence* Source = NewObject<ULevelSequence>();
	ULevelSequence* Target = NewObject<ULevelSequence>();
	Source->Initialize();
	Target->Initialize();
	UMovieScene* SourceScene = Source->GetMovieScene();
	UMovieScene* TargetScene = Target->GetMovieScene();
	SourceScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
	SourceScene->SetDisplayRate(FFrameRate(60, 1));
	SourceScene->SetPlaybackRange(FFrameNumber(68), 859);
	TestEqual(TEXT("亚帧边界时片长与引擎迁移后的边界一致"), GetMMDSequenceDurationFrames(SourceScene, FFrameRate(30, 1), 1), 1);
	SourceScene->SetPlaybackRange(FFrameNumber(24000), 48000);
	ConfigureMMDMovieSceneTiming(TargetScene, 60);
	TestEqual(TEXT("24k 时间基的两秒镜头转换为 60 帧"), GetMMDSequenceDurationFrames(SourceScene, FFrameRate(30, 1), 1), 60);

	ACameraActor* Camera = NewObject<ACameraActor>(SourceScene);
	const FGuid CameraGuid = SourceScene->AddSpawnable(TEXT("TimingCamera"), *Camera);
	UMovieScene3DTransformTrack* Track = SourceScene->AddTrack<UMovieScene3DTransformTrack>(CameraGuid);
	Track->SetPropertyNameAndPath(TEXT("Transform"), TEXT("Transform"));
	UMovieScene3DTransformSection* Section = CastChecked<UMovieScene3DTransformSection>(Track->CreateNewSection());
	Track->AddSection(*Section);
	Section->SetRange(TRange<FFrameNumber>(FFrameNumber(24000), FFrameNumber(72000)));
	AddCameraTransformKey(Section, FFrameNumber(24000), FTransform(FVector(10.0, 0.0, 0.0)));
	AddCameraTransformKey(Section, FFrameNumber(48000), FTransform(FVector(20.0, 0.0, 0.0)));
	Section->SetIsLocked(true);

	UCameraComponent* Component = Camera->GetCameraComponent();
	const FGuid ComponentGuid = SourceScene->AddPossessable(Component->GetName(), Component->GetClass());
	SourceScene->FindPossessable(ComponentGuid)->SetParent(CameraGuid, SourceScene);
	SourceScene->FindSpawnable(CameraGuid)->AddChildPossessable(ComponentGuid);
	Source->BindPossessableObject(ComponentGuid, *Component, Camera);
	UMovieSceneFloatTrack* FOVTrack = SourceScene->AddTrack<UMovieSceneFloatTrack>(ComponentGuid);
	FOVTrack->SetPropertyNameAndPath(TEXT("FieldOfView"), TEXT("FieldOfView"));
	UMovieSceneFloatSection* FOVSection = CastChecked<UMovieSceneFloatSection>(FOVTrack->CreateNewSection());
	FOVTrack->AddSection(*FOVSection);
	FOVSection->SetRange(Section->GetRange());
	FOVSection->GetChannel().AddCubicKey(FFrameNumber(24000), 35.0f);
	FOVSection->GetChannel().AddCubicKey(FFrameNumber(48000), 55.0f);

	FString Error;
	if (!TestTrue(TEXT("不同时间基且非零起点的镜头可合成"), InlineCameraSequenceIntoMaster(Source, Target, 60, FTransform::Identity, Error)))
	{
		AddError(Error);
		return false;
	}
	bool bFoundTransform = false;
	bool bFoundFOV = false;
	for (const FMovieSceneBinding& Binding : TargetScene->GetBindings())
	{
		for (UMovieSceneTrack* CopiedTrack : Binding.GetTracks())
		{
			for (UMovieSceneSection* CopiedSection : CopiedTrack->GetAllSections())
			{
				TestEqual(TEXT("镜头片段起点归零"), CopiedSection->GetInclusiveStartFrame().Value, 0);
				TestEqual(TEXT("镜头片段持续两秒"), CopiedSection->GetExclusiveEndFrame().Value, 60);
				if (UMovieScene3DTransformSection* Transform = Cast<UMovieScene3DTransformSection>(CopiedSection))
				{
					bFoundTransform = true;
					FMovieSceneDoubleChannel* X = FindTransformDoubleChannel(Transform, TEXT("Location.X"));
					double Value = 0.0;
					TestTrue(TEXT("转换后的位置通道可求值"), X && X->Evaluate(FFrameTime(30), Value));
					TestEqual(TEXT("一秒处仍为第二个位置关键帧"), Value, 20.0);
					TestTrue(TEXT("保留源片段锁定状态"), Transform->IsLocked());
				}
				if (UMovieSceneFloatSection* FloatSection = Cast<UMovieSceneFloatSection>(CopiedSection))
				{
					bFoundFOV = true;
					float Value = 0.0f;
					TestTrue(TEXT("转换后的 FOV 通道可求值"), FloatSection->GetChannel().Evaluate(FFrameTime(30), Value));
					TestEqual(TEXT("一秒处 FOV 与位置同步"), Value, 55.0f);
				}
			}
		}
	}
	TestTrue(TEXT("复制相机位置轨道"), bFoundTransform);
	TestTrue(TEXT("复制相机组件 FOV 轨道"), bFoundFOV);
	TestEqual(TEXT("源镜头时间基未修改"), SourceScene->GetTickResolution(), FFrameRate(24000, 1));
	TestEqual(TEXT("源镜头起点未修改"), Section->GetInclusiveStartFrame().Value, 24000);
	const FMovieSceneDoubleChannel* SourceX = FindTransformDoubleChannel(Section, TEXT("Location.X"));
	TestEqual(TEXT("源关键帧未修改"), SourceX->GetData().GetTimes()[1].Value, 48000);
	return true;
}
#endif


