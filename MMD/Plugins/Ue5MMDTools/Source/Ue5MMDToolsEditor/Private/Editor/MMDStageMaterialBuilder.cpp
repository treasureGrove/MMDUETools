#include "Editor/MMDStageMaterialBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	bool EnsureSakuraBackdropTexture(UTexture2D* BlackTexture, UTexture2D*& OutTexture, FString& Error)
	{
		OutTexture = BlackTexture;
		const FString PackageName = TEXT("/Ue5MMDTools/Stages/Textures/T_Garden_SakuraBackdropV3");
		const FString ObjectPath = PackageName + TEXT(".T_Garden_SakuraBackdropV3");
		if (FPackageName::DoesPackageExist(PackageName))
		{
			OutTexture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
			if (!OutTexture) Error = TEXT("无法加载樱庭远景贴图");
			return OutTexture != nullptr;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		if (!Plugin) { Error = TEXT("无法定位舞台插件资源"); return false; }
		const FString Filename = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Garden/sakura_night_backdrop_v3.png"));
		// 分发包未带源 PNG 时仍可使用旧过程远景；角色材质不依赖此资源。
		if (!IFileManager::Get().FileExists(*Filename)) return true;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.IsEmpty())
		{
			Error = TEXT("无法读取樱庭远景 PNG"); return false;
		}
		UTextureFactory::SuppressImportOverwriteDialog(true);
		UTextureFactory* Factory = NewObject<UTextureFactory>();
		Factory->bCreateMaterial = false;
		UPackage* Package = CreatePackage(*PackageName);
		const uint8* Data = Bytes.GetData();
		OutTexture = Cast<UTexture2D>(Factory->FactoryCreateBinary(UTexture2D::StaticClass(), Package,
			TEXT("T_Garden_SakuraBackdropV3"), RF_Public | RF_Standalone, nullptr, TEXT("png"), Data, Data + Bytes.Num(), GWarn));
		if (!OutTexture) { Error = TEXT("樱庭远景 PNG 导入失败"); return false; }
		OutTexture->SRGB = true;
		OutTexture->CompressionSettings = TC_Default;
		OutTexture->AddressX = TA_Clamp;
		OutTexture->AddressY = TA_Clamp;
		OutTexture->LODGroup = TEXTUREGROUP_World;
		OutTexture->VirtualTextureStreaming = false;
		OutTexture->NeverStream = true;
		OutTexture->PostEditChange();
		OutTexture->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(OutTexture);
		const FString AssetFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AssetFilename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, OutTexture, *AssetFilename, Args))
		{
			Error = TEXT("樱庭远景贴图保存失败"); return false;
		}
		return true;
	}

	template<typename T>
	T* StageAddExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	UMaterialExpressionScalarParameter* StageAddScalar(UMaterial* Material, FName Name, float DefaultValue,
		FName Group = TEXT("Rendering"), float Min = 0.0f, float Max = 1.0f)
	{
		UMaterialExpressionScalarParameter* Parameter = StageAddExpression<UMaterialExpressionScalarParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->Group = Group;
		Parameter->DefaultValue = DefaultValue;
		Parameter->SliderMin = Min;
		Parameter->SliderMax = Max;
		return Parameter;
	}

	UMaterialExpressionVectorParameter* StageAddVector(UMaterial* Material, FName Name, const FLinearColor& DefaultValue,
		FName Group = TEXT("Rendering"))
	{
		UMaterialExpressionVectorParameter* Parameter = StageAddExpression<UMaterialExpressionVectorParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->Group = Group;
		Parameter->DefaultValue = DefaultValue;
		return Parameter;
	}

	UMaterialExpressionTextureObjectParameter* StageAddTextureObject(UMaterial* Material, FName Name, UTexture* Texture,
		EMaterialSamplerType SamplerType = SAMPLERTYPE_LinearColor)
	{
		UMaterialExpressionTextureObjectParameter* Parameter = StageAddExpression<UMaterialExpressionTextureObjectParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->Group = TEXT("Optional textures");
		Parameter->Texture = Texture;
		Parameter->SamplerType = SamplerType;
		return Parameter;
	}

	void StageAddCustomInput(UMaterialExpressionCustom* Custom, FName Name, UMaterialExpression* Expression, int32 OutputIndex = 0)
	{
		FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
		Input.InputName = Name;
		Input.Input.Connect(OutputIndex, Expression);
	}

	bool UpgradeBackdropInputs(UMaterial* Material, UTexture2D* BackdropTexture, UTexture2D* BlackTexture)
	{
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Shader || !Shader->Code.Contains(TEXT("TMMDStageBackdrop.usf"))) return false;
		bool bChanged = false;
		if (!Shader->Inputs.ContainsByPredicate([](const FCustomInput& Input) { return Input.InputName == TEXT("backdrop_tex"); }))
		{
			Material->Modify();
			StageAddCustomInput(Shader, TEXT("backdrop_tex"), StageAddTextureObject(Material, TEXT("BackdropTexture"), BackdropTexture, SAMPLERTYPE_Color));
			bChanged = true;
		}
		if (!Shader->Inputs.ContainsByPredicate([](const FCustomInput& Input) { return Input.InputName == TEXT("backdrop_texture_strength"); }))
		{
			Material->Modify();
			StageAddCustomInput(Shader, TEXT("backdrop_texture_strength"), StageAddScalar(Material, TEXT("BackdropTextureStrength"),
				BackdropTexture != BlackTexture ? 1.0f : 0.0f, TEXT("Stage")));
			bChanged = true;
		}
		return bChanged;
	}

	UMaterial* CreateCharacterMaterial(UPackage* Package, FName Name, EBlendMode BlendMode,
		UTexture2D* WhiteTexture, UTexture2D* BlackTexture, UTextureRenderTarget2D* LightData, UTextureRenderTarget2D* ShadowMap)
	{
		UMaterial* Material = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone);
		FArchiveUObject InitialArchive;
		Material->Serialize(InitialArchive);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BlendMode;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
		Material->OpacityMaskClipValue = 0.01f;
		Material->bCastDynamicShadowAsMasked = BlendMode == BLEND_Translucent;

		UMaterialExpressionTextureSampleParameter2D* BaseTexture = StageAddExpression<UMaterialExpressionTextureSampleParameter2D>(Material);
		BaseTexture->ParameterName = TEXT("base_color_tex");
		BaseTexture->ExpressionGUID = FGuid::NewGuid();
		BaseTexture->Group = TEXT("Imported material");
		BaseTexture->Texture = WhiteTexture;
		BaseTexture->SamplerType = SAMPLERTYPE_Color;
		UMaterialExpressionVectorParameter* BaseColor = StageAddVector(Material, TEXT("BaseColor"), FLinearColor::White, TEXT("Imported material"));
		UMaterialExpressionMultiply* TintedBase = StageAddExpression<UMaterialExpressionMultiply>(Material);
		TintedBase->A.Connect(0, BaseTexture);
		TintedBase->B.Connect(0, BaseColor);
		UMaterialExpressionScalarParameter* Opacity = StageAddScalar(Material, TEXT("Opacity"), 1.0f, TEXT("Imported material"));
		UMaterialExpressionMultiply* FinalOpacity = StageAddExpression<UMaterialExpressionMultiply>(Material);
		FinalOpacity->A.Connect(4, BaseTexture);
		FinalOpacity->B.Connect(0, Opacity);

		UMaterialExpressionCustom* Shader = StageAddExpression<UMaterialExpressionCustom>(Material);
		Shader->OutputType = CMOT_Float3;
		Shader->Description = TEXT("MMD stage character lighting");
		Shader->Code = TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDStageToon.usf\"\nreturn 0;");
		Shader->Inputs.Reset();
		StageAddCustomInput(Shader, TEXT("base_color"), TintedBase);
		StageAddCustomInput(Shader, TEXT("light_data_tex"), StageAddTextureObject(Material, TEXT("light_data_tex"), LightData));
		StageAddCustomInput(Shader, TEXT("mmd_shadow_map"), StageAddTextureObject(Material, TEXT("mmd_shadow_map"), ShadowMap));
		StageAddCustomInput(Shader, TEXT("mat_cap"), StageAddTextureObject(Material, TEXT("mat_cap"), BlackTexture, SAMPLERTYPE_Color));
		StageAddCustomInput(Shader, TEXT("shadow_threshold"), StageAddScalar(Material, TEXT("ShadowThreshold"), 0.45f));
		StageAddCustomInput(Shader, TEXT("shadow_softness"), StageAddScalar(Material, TEXT("ShadowSoftness"), 0.12f));
		StageAddCustomInput(Shader, TEXT("shadow_tint"), StageAddVector(Material, TEXT("ShadowTint"), FLinearColor(0.58f, 0.62f, 0.74f)));
		StageAddCustomInput(Shader, TEXT("ambient_strength"), StageAddScalar(Material, TEXT("AmbientStrength"), 0.35f, TEXT("Rendering"), 0.0f, 2.0f));
		StageAddCustomInput(Shader, TEXT("light_scale"), StageAddScalar(Material, TEXT("LightScale"), 0.85f, TEXT("Rendering"), 0.0f, 2.0f));
		StageAddCustomInput(Shader, TEXT("specular_strength"), StageAddScalar(Material, TEXT("SpecularStrength"), 0.08f));
		StageAddCustomInput(Shader, TEXT("specular_power"), StageAddScalar(Material, TEXT("SpecularPower"), 48.0f, TEXT("Rendering"), 1.0f, 128.0f));
		StageAddCustomInput(Shader, TEXT("rim_strength"), StageAddScalar(Material, TEXT("RimStrength"), 0.10f));
		StageAddCustomInput(Shader, TEXT("rim_power"), StageAddScalar(Material, TEXT("RimPower"), 3.0f, TEXT("Rendering"), 1.0f, 8.0f));
		StageAddCustomInput(Shader, TEXT("toon_amount"), StageAddScalar(Material, TEXT("ToonAmount"), 1.0f));
		StageAddCustomInput(Shader, TEXT("diffuse_wrap"), StageAddScalar(Material, TEXT("DiffuseWrap"), 0.10f, TEXT("Rendering"), 0.0f, 0.8f));
		StageAddCustomInput(Shader, TEXT("color_saturation"), StageAddScalar(Material, TEXT("ColorSaturation"), 1.0f, TEXT("Rendering"), 0.0f, 2.0f));
		StageAddCustomInput(Shader, TEXT("shadow_saturation"), StageAddScalar(Material, TEXT("ShadowSaturation"), 1.0f, TEXT("Rendering"), 0.0f, 2.0f));
		StageAddCustomInput(Shader, TEXT("roughness"), StageAddScalar(Material, TEXT("Roughness"), 0.65f));
		StageAddCustomInput(Shader, TEXT("specular_tint"), StageAddVector(Material, TEXT("SpecularTint"), FLinearColor(0.9f, 0.95f, 1.0f)));
		StageAddCustomInput(Shader, TEXT("sphere_mode"), StageAddScalar(Material, TEXT("sphere_mode"), 0.0f, TEXT("Imported material"), 0.0f, 3.0f));
		StageAddCustomInput(Shader, TEXT("sphere_strength"), StageAddScalar(Material, TEXT("SphereStrength"), 0.10f, TEXT("Imported material")));
		StageAddCustomInput(Shader, TEXT("black_lift"), StageAddScalar(Material, TEXT("BlackLift"), 0.018f, TEXT("Rendering"), 0.0f, 0.08f));
		StageAddCustomInput(Shader, TEXT("soft_specular_strength"), StageAddScalar(Material, TEXT("SoftSpecularStrength"), 0.16f));

		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->EmissiveColor.Connect(0, Shader);
		Data->Opacity.Connect(0, FinalOpacity);
		Data->OpacityMask.Connect(0, FinalOpacity);
		return Material;
	}

	UMaterial* CreateBeamMaterial(UPackage* Package, FName Name)
	{
		UMaterial* Material = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone);
		FArchiveUObject InitialArchive;
		Material->Serialize(InitialArchive);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Additive;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		UMaterialExpressionCustom* Shader = StageAddExpression<UMaterialExpressionCustom>(Material);
		Shader->OutputType = CMOT_Float4;
		Shader->Description = TEXT("Geometric stage beam");
		Shader->Code = TEXT("struct TMMDStageBeam\n{\n"
			"float4 OutputColor(FMaterialPixelParameters Parameters, float3 stage_color, float emissive_strength, float opacity)\n{\n"
			"float3 world_pos = LWCToFloat(Parameters.AbsoluteWorldPosition);\n"
			"float3 world_normal = (float3)Parameters.TangentToWorld[2];\n"
			"world_normal *= rsqrt(max(dot(world_normal, world_normal), 1e-6));\n"
			"float3 view_vector = LWCToFloat(GetWorldCameraOrigin(Parameters)) - world_pos;\n"
			"float3 view_dir = view_vector * rsqrt(max(dot(view_vector, view_vector), 1e-6));\n"
			"float edge_fade = pow(saturate(abs(dot(world_normal, view_dir))), 0.65);\n"
			"return float4(max(stage_color, 0.0) * max(emissive_strength, 0.0), saturate(opacity) * edge_fade * 0.65);\n"
			"}\n};\nTMMDStageBeam shader;\nreturn shader.OutputColor(Parameters, stage_color, emissive_strength, opacity);");
		Shader->Inputs.Reset();
		StageAddCustomInput(Shader, TEXT("stage_color"), StageAddVector(Material, TEXT("StageColor"), FLinearColor(0.22f, 0.60f, 1.0f), TEXT("Stage")));
		StageAddCustomInput(Shader, TEXT("emissive_strength"), StageAddScalar(Material, TEXT("EmissiveStrength"), 3.0f, TEXT("Stage"), 0.0f, 16.0f));
		StageAddCustomInput(Shader, TEXT("opacity"), StageAddScalar(Material, TEXT("Opacity"), 0.06f, TEXT("Stage"), 0.0f, 0.3f));
		UMaterialExpressionComponentMask* Color = StageAddExpression<UMaterialExpressionComponentMask>(Material);
		Color->Input.Connect(0, Shader);
		Color->R = Color->G = Color->B = true;
		Color->A = false;
		UMaterialExpressionComponentMask* Alpha = StageAddExpression<UMaterialExpressionComponentMask>(Material);
		Alpha->Input.Connect(0, Shader);
		Alpha->R = Alpha->G = Alpha->B = false;
		Alpha->A = true;
		UMaterialExpressionDepthFade* DepthFade = StageAddExpression<UMaterialExpressionDepthFade>(Material);
		DepthFade->InOpacity.Connect(0, Alpha);
		DepthFade->FadeDistance.Connect(0, StageAddScalar(Material, TEXT("DepthFadeDistance"), 80.0f, TEXT("Stage"), 1.0f, 200.0f));
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->EmissiveColor.Connect(0, Color);
		Data->Opacity.Connect(0, DepthFade);
		return Material;
	}

	UMaterial* CreateBackdropMaterial(UPackage* Package, FName Name)
	{
		UMaterial* Material = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone);
		FArchiveUObject InitialArchive;
		Material->Serialize(InitialArchive);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		UMaterialExpressionCustom* Shader = StageAddExpression<UMaterialExpressionCustom>(Material);
		Shader->OutputType = CMOT_Float3;
		Shader->Description = TEXT("Procedural stage atmosphere");
		Shader->Code = TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDStageBackdrop.usf\"\nreturn 0;");
		Shader->Inputs.Reset();
		StageAddCustomInput(Shader, TEXT("stage_color"), StageAddVector(Material, TEXT("StageColor"), FLinearColor(0.04f, 0.08f, 0.18f), TEXT("Stage")));
		StageAddCustomInput(Shader, TEXT("horizon_color"), StageAddVector(Material, TEXT("HorizonColor"), FLinearColor(0.13f, 0.20f, 0.28f), TEXT("Stage")));
		StageAddCustomInput(Shader, TEXT("backdrop_theme"), StageAddScalar(Material, TEXT("BackdropTheme"), 0.0f, TEXT("Stage"), 0.0f, 2.0f));
		StageAddCustomInput(Shader, TEXT("stage_time"), StageAddScalar(Material, TEXT("StageTime"), 0.0f, TEXT("Stage"), 0.0f, 0.0f));
		StageAddCustomInput(Shader, TEXT("emissive_strength"), StageAddScalar(Material, TEXT("EmissiveStrength"), 1.0f, TEXT("Stage"), 0.0f, 4.0f));
		StageAddCustomInput(Shader, TEXT("stage_origin"), StageAddVector(Material, TEXT("StageOrigin"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_right"), StageAddVector(Material, TEXT("StageRight"), FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_forward"), StageAddVector(Material, TEXT("StageForward"), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_up"), StageAddVector(Material, TEXT("StageUp"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), TEXT("Stage transform")));
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Shader);
		return Material;
	}

	bool UpgradeCharacterInputs(UMaterial* Material)
	{
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Shader || !Shader->Code.Contains(TEXT("TMMDStageToon.usf"))) return false;
		bool bChanged = false;
		auto EnsureScalarInput = [Material, Shader, &bChanged](FName InputName, FName ParameterName, float DefaultValue, float Max)
		{
			if (Shader->Inputs.ContainsByPredicate([InputName](const FCustomInput& Input) { return Input.InputName == InputName; })) return;
			Material->Modify();
			UMaterialExpressionScalarParameter* Parameter = nullptr;
			for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
			{
				UMaterialExpressionScalarParameter* Candidate = Cast<UMaterialExpressionScalarParameter>(Expression);
				if (Candidate && Candidate->ParameterName == ParameterName) { Parameter = Candidate; break; }
			}
			if (!Parameter) Parameter = StageAddScalar(Material, ParameterName, DefaultValue, TEXT("Rendering"), 0.0f, Max);
			StageAddCustomInput(Shader, InputName, Parameter);
			bChanged = true;
		};
		EnsureScalarInput(TEXT("black_lift"), TEXT("BlackLift"), 0.018f, 0.08f);
		EnsureScalarInput(TEXT("soft_specular_strength"), TEXT("SoftSpecularStrength"), 0.16f, 1.0f);
		return bChanged;
	}

	UMaterial* CreateStageMaterial(UPackage* Package, FName Name, int32 Kind)
	{
		UMaterial* Material = NewObject<UMaterial>(Package, Name, RF_Public | RF_Standalone);
		FArchiveUObject InitialArchive;
		Material->Serialize(InitialArchive);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(Kind == 2 ? MSM_Unlit : MSM_DefaultLit);
		Material->TwoSided = Kind == 2;
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		const FLinearColor DefaultColor = Kind == 0 ? FLinearColor(0.025f, 0.038f, 0.060f)
			: Kind == 1 ? FLinearColor(0.035f, 0.12f, 0.20f) : FLinearColor(0.015f, 0.65f, 1.0f);
		UMaterialExpressionVectorParameter* StageColor = StageAddVector(Material, TEXT("StageColor"), DefaultColor, TEXT("Stage"));
		UMaterialExpressionScalarParameter* Roughness = StageAddScalar(Material, TEXT("Roughness"), Kind == 0 ? 0.36f : 0.32f, TEXT("Stage"));
		UMaterialExpressionScalarParameter* Metallic = StageAddScalar(Material, TEXT("Metallic"), Kind == 0 ? 0.65f : 0.4f, TEXT("Stage"));
		UMaterialExpressionCustom* Shader = StageAddExpression<UMaterialExpressionCustom>(Material);
		Shader->OutputType = CMOT_Float3;
		Shader->Description = TEXT("Animated geometric stage light");
		Shader->Code = TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDStageSurface.usf\"\nreturn 0;");
		Shader->Inputs.Reset();
		StageAddCustomInput(Shader, TEXT("stage_color"), StageColor);
		StageAddCustomInput(Shader, TEXT("emissive_strength"), StageAddScalar(Material, TEXT("EmissiveStrength"), Kind == 0 ? 0.08f : Kind == 1 ? 0.4f : 4.0f, TEXT("Stage"), 0.0f, 16.0f));
		StageAddCustomInput(Shader, TEXT("pulse_speed"), StageAddScalar(Material, TEXT("PulseSpeed"), 0.8f, TEXT("Stage"), 0.0f, 4.0f));
		StageAddCustomInput(Shader, TEXT("pulse_amount"), StageAddScalar(Material, TEXT("PulseAmount"), Kind == 0 ? 0.0f : 0.45f, TEXT("Stage")));
		StageAddCustomInput(Shader, TEXT("pattern_scale"), StageAddScalar(Material, TEXT("PatternScale"), 0.02f, TEXT("Stage"), 0.0f, 0.1f));
		StageAddCustomInput(Shader, TEXT("time_seconds"), StageAddExpression<UMaterialExpressionTime>(Material));
		StageAddCustomInput(Shader, TEXT("stage_time"), StageAddScalar(Material, TEXT("StageTime"), 0.0f, TEXT("Stage"), 0.0f, 0.0f));
		StageAddCustomInput(Shader, TEXT("use_stage_time"), StageAddScalar(Material, TEXT("UseStageTime"), 0.0f, TEXT("Stage")));
		StageAddCustomInput(Shader, TEXT("stage_origin"), StageAddVector(Material, TEXT("StageOrigin"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_right"), StageAddVector(Material, TEXT("StageRight"), FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_forward"), StageAddVector(Material, TEXT("StageForward"), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), TEXT("Stage transform")));
		StageAddCustomInput(Shader, TEXT("stage_up"), StageAddVector(Material, TEXT("StageUp"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), TEXT("Stage transform")));
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		Data->BaseColor.Connect(0, StageColor);
		Data->Roughness.Connect(0, Roughness);
		Data->Metallic.Connect(0, Metallic);
		Data->EmissiveColor.Connect(0, Shader);
		return Material;
	}

	bool SaveMaterial(UMaterial* Material, const FString& PackageName, FString& OutError, bool bNewAsset)
	{
		Material->PostEditChange();
		Material->MarkPackageDirty();
		if (bNewAsset)
		{
			FAssetRegistryModule::AssetCreated(Material);
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Material->GetOutermost(), Material, *Filename, Args))
		{
			OutError = FString::Printf(TEXT("Cannot save stage material %s"), *PackageName);
			return false;
		}
		return true;
	}

	bool ValidateMaterial(const UMaterial* Material, bool bCharacter, bool bCheckUsage = true, bool bBeam = false, bool bBackdrop = false)
	{
		const UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		if (bBeam)
		{
			const UMaterialExpressionComponentMask* Color = Data ? Cast<UMaterialExpressionComponentMask>(Data->EmissiveColor.Expression) : nullptr;
			const UMaterialExpressionCustom* Shader = Color ? Cast<UMaterialExpressionCustom>(Color->Input.Expression) : nullptr;
			return Material && Material->MaterialDomain == MD_Surface && Material->BlendMode == BLEND_Additive
				&& Material->TwoSided && Material->GetShadingModels().GetFirstShadingModel() == MSM_Unlit
				&& Shader && Shader->Code.Contains(TEXT("TMMDStageBeam")) && Shader->Inputs.Num() == 3
				&& Cast<UMaterialExpressionDepthFade>(Data->Opacity.Expression)
				&& (!bCheckUsage || Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
		}
		const UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (bBackdrop)
		{
			return Material && Material->MaterialDomain == MD_Surface && Material->BlendMode == BLEND_Opaque
				&& Material->TwoSided && Material->GetShadingModels().GetFirstShadingModel() == MSM_Unlit
				&& Shader && Shader->Code.Contains(TEXT("TMMDStageBackdrop.usf")) && Shader->Inputs.Num() == 11
				&& (!bCheckUsage || Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
		}
		return Material && Material->MaterialDomain == MD_Surface && Shader
			&& Shader->Code.Contains(bCharacter ? TEXT("TMMDStageToon.usf") : TEXT("TMMDStageSurface.usf"))
			&& Shader->Inputs.Num() == (bCharacter ? 23 : 12)
			&& (!bCheckUsage || Material->GetUsageByFlag(bCharacter ? MATUSAGE_MorphTargets : MATUSAGE_InstancedStaticMeshes))
			&& (!bCharacter || (Material->GetShadingModels().GetFirstShadingModel() == MSM_Unlit
				&& Material->GetUsageByFlag(MATUSAGE_SkeletalMesh) && Data->Opacity.Expression && Data->OpacityMask.Expression));
	}
}

bool EnsureMMDStageMaterials(FMMDStageMaterialSet& OutMaterials, FString& OutError)
{
	OutMaterials = FMMDStageMaterialSet();
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Stage materials must be created on the game thread");
		return false;
	}
	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	UTexture2D* BlackTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
	UTexture2D* PearlTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Ue5MMDTools/Stages/Textures/T_StageMatCapPearl.T_StageMatCapPearl"));
	UTextureRenderTarget2D* LightData = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT"));
	UTextureRenderTarget2D* ShadowMap = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT"));
	if (!WhiteTexture || !BlackTexture || !LightData || !ShadowMap)
	{
		OutError = TEXT("Cannot load stage material default textures or MMD render targets");
		return false;
	}
	UTexture2D* BackdropTexture = nullptr;
	if (!EnsureSakuraBackdropTexture(BlackTexture, BackdropTexture, OutError)) return false;
	const TCHAR* Names[] = {
		TEXT("M_MMD_StageToon_Opaque"), TEXT("M_MMD_StageToon_Masked"), TEXT("M_MMD_StageToon_Translucent"),
		TEXT("M_MMD_StageFloor"), TEXT("M_MMD_StagePanel"), TEXT("M_MMD_StageAccent"), TEXT("M_MMD_StageBeam"), TEXT("M_MMD_StageBackdrop") };
	UMaterialInterface** Outputs[] = { &OutMaterials.CharacterOpaque, &OutMaterials.CharacterMasked,
		&OutMaterials.CharacterTranslucent, &OutMaterials.Floor, &OutMaterials.Panel, &OutMaterials.Accent, &OutMaterials.Beam, &OutMaterials.Backdrop };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Names); ++Index)
	{
		const FString PackageName = FString(TEXT("/Ue5MMDTools/Stages/Materials/")) + Names[Index];
		const FString ObjectPath = PackageName + TEXT(".") + Names[Index];
		const bool bSaved = FPackageName::DoesPackageExist(PackageName);
		UMaterial* Material = bSaved ? LoadObject<UMaterial>(nullptr, *ObjectPath) : FindObject<UMaterial>(nullptr, *ObjectPath);
		bool bGraphChanged = false;
		if (Material)
		{
			if (Index < 3) bGraphChanged = UpgradeCharacterInputs(Material);
			if (Index == 7) bGraphChanged = UpgradeBackdropInputs(Material, BackdropTexture, BlackTexture);
			if (!ValidateMaterial(Material, Index < 3, false, Index == 6, Index == 7))
			{
				OutError = FString::Printf(TEXT("Stage material graph is invalid: %s"), *ObjectPath);
				return false;
			}
		}
		else if (bSaved)
		{
			OutError = FString::Printf(TEXT("Cannot load existing stage material: %s"), *ObjectPath);
			return false;
		}
		else
		{
			UPackage* Package = CreatePackage(*PackageName);
			Material = Index < 3 ? CreateCharacterMaterial(Package, Names[Index],
				Index == 0 ? BLEND_Opaque : Index == 1 ? BLEND_Masked : BLEND_Translucent,
				WhiteTexture, PearlTexture ? PearlTexture : BlackTexture, LightData, ShadowMap)
				: Index == 6 ? CreateBeamMaterial(Package, Names[Index])
				: Index == 7 ? CreateBackdropMaterial(Package, Names[Index]) : CreateStageMaterial(Package, Names[Index], Index - 3);
		}
		if (!Material)
		{
			OutError = FString::Printf(TEXT("Cannot create stage material: %s"), *ObjectPath);
			return false;
		}
		if (Index == 7)
		{
			bGraphChanged |= UpgradeBackdropInputs(Material, BackdropTexture, BlackTexture);
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
			FString Source;
			if (!Plugin || !FFileHelper::LoadFileToString(Source, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/TMMDShader/TMMDStageBackdrop.usf"))))
			{
				OutError = TEXT("Cannot read stage backdrop shader source");
				return false;
			}
			FTCHARToUTF8 SourceUtf8(*Source);
			const FString Signature = FMD5::HashBytes(reinterpret_cast<const uint8*>(SourceUtf8.Get()), SourceUtf8.Length());
			const FString Code = TEXT("// 远景源码签名：") + Signature
				+ TEXT("\n#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDStageBackdrop.usf\"\nreturn 0;");
			UMaterialExpressionCustom* Shader = Cast<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression);
			if (Shader->Code != Code)
			{
				// include 文本不变时旧资产仍可能命中旧 shader map，源码签名使更新可靠且幂等。
				Material->Modify();
				Shader->Code = Code;
				bGraphChanged = true;
			}
		}
		// 仅补当前舞台资产的必要变体，已有材质图与参数不变。
		const EMaterialUsage RequiredUsage = Index < 3 ? MATUSAGE_MorphTargets : MATUSAGE_InstancedStaticMeshes;
		const bool bUsageChanged = !Material->GetUsageByFlag(RequiredUsage);
		if (bUsageChanged)
		{
			Material->Modify();
			Material->SetUsageByFlag(RequiredUsage, true);
		}
		if (!ValidateMaterial(Material, Index < 3, true, Index == 6, Index == 7))
		{
			OutError = FString::Printf(TEXT("Stage material graph or usage is invalid: %s"), *ObjectPath);
			return false;
		}
		if ((!bSaved || bUsageChanged || bGraphChanged) && !SaveMaterial(Material, PackageName, OutError, !bSaved))
		{
			return false;
		}
		*Outputs[Index] = Material;
	}
	return true;
}
