#include "Editor/MMDGardenMaterialBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Factories/TextureFactory.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	bool GardenSaveAsset(UObject* Asset, const FString& PackageName, bool bNewAsset, FString& Error)
	{
		Asset->MarkPackageDirty();
		if (bNewAsset) FAssetRegistryModule::AssetCreated(Asset);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
		{
			Error = FString::Printf(TEXT("Cannot save garden asset: %s"), *PackageName);
			return false;
		}
		return true;
	}

	bool EnsureDetailTexture(const FString& ResourcesPath, const TCHAR* SourceName, const TCHAR* AssetName,
		bool bSRGB, UTexture2D* WhiteTexture, UTexture2D*& OutTexture, bool& bAvailable, FString& Error)
	{
		OutTexture = WhiteTexture;
		bAvailable = false;
		const FString PackageName = FString(TEXT("/Ue5MMDTools/Stages/Textures/")) + AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		const bool bSaved = FPackageName::DoesPackageExist(PackageName);
		UTexture2D* Texture = bSaved ? LoadObject<UTexture2D>(nullptr, *ObjectPath) : FindObject<UTexture2D>(nullptr, *ObjectPath);
		bool bNewAsset = false;
		if (!Texture && bSaved)
		{
			Error = FString::Printf(TEXT("Cannot load existing garden texture: %s"), *ObjectPath);
			return false;
		}
		if (!Texture)
		{
			const FString Filename = FPaths::Combine(ResourcesPath, SourceName);
			if (!IFileManager::Get().FileExists(*Filename)) return true;
			TArray<uint8> SourceData;
			if (!FFileHelper::LoadFileToArray(SourceData, *Filename) || SourceData.IsEmpty())
			{
				Error = FString::Printf(TEXT("Cannot read garden detail texture: %s"), *Filename);
				return false;
			}
			UTextureFactory::SuppressImportOverwriteDialog(true);
			UTextureFactory* Factory = NewObject<UTextureFactory>();
			Factory->bCreateMaterial = false;
			UPackage* Package = CreatePackage(*PackageName);
			const uint8* Data = SourceData.GetData();
			Texture = Cast<UTexture2D>(Factory->FactoryCreateBinary(UTexture2D::StaticClass(), Package,
				FName(AssetName), RF_Public | RF_Standalone, nullptr, TEXT("png"), Data, Data + SourceData.Num(), GWarn));
			if (!Texture)
			{
				Error = FString::Printf(TEXT("Cannot import garden detail texture: %s"), *Filename);
				return false;
			}
			bNewAsset = true;
		}
		const TextureCompressionSettings Compression = bSRGB ? TC_Default : TC_Masks;
		const bool bChanged = Texture->SRGB != bSRGB || Texture->CompressionSettings != Compression
			|| Texture->AddressX != TA_Wrap || Texture->AddressY != TA_Wrap || Texture->VirtualTextureStreaming;
		if (bNewAsset || bChanged)
		{
			Texture->Modify();
			Texture->SRGB = bSRGB;
			Texture->CompressionSettings = Compression;
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Wrap;
			Texture->VirtualTextureStreaming = false;
			Texture->LODGroup = TEXTUREGROUP_World;
			Texture->PostEditChange();
			if (!GardenSaveAsset(Texture, PackageName, !bSaved, Error)) return false;
		}
		OutTexture = Texture;
		bAvailable = true;
		return true;
	}

	bool EnsureNightSky(const FString& ResourcesPath, FString& Error)
	{
		const FString PackageName = TEXT("/Ue5MMDTools/Stages/Textures/T_Garden_NightSky");
		const FString ObjectPath = PackageName + TEXT(".T_Garden_NightSky");
		const bool bSaved = FPackageName::DoesPackageExist(PackageName);
		UTextureCube* Texture = bSaved ? LoadObject<UTextureCube>(nullptr, *ObjectPath) : FindObject<UTextureCube>(nullptr, *ObjectPath);
		if (!Texture && bSaved)
		{
			Error = FString::Printf(TEXT("Cannot load existing garden sky cube: %s"), *ObjectPath);
			return false;
		}
		bool bNewAsset = false;
		if (!Texture)
		{
			const FString Filename = FPaths::Combine(ResourcesPath, TEXT("qwantani_night_puresky_2k.hdr"));
			if (!IFileManager::Get().FileExists(*Filename)) return true;
			TArray<uint8> SourceData;
			if (!FFileHelper::LoadFileToArray(SourceData, *Filename) || SourceData.IsEmpty())
			{
				Error = FString::Printf(TEXT("Cannot read garden night HDR: %s"), *Filename);
				return false;
			}
			UTextureFactory* Factory = NewObject<UTextureFactory>();
			Factory->bCreateMaterial = false;
			// 明确使用经纬 HDR→TextureCube，避免后台导入弹出类型选择框。
			Factory->HDRImportShouldBeLongLatCubeMap = EAppReturnType::YesAll;
			const uint8* Data = SourceData.GetData();
			Texture = Cast<UTextureCube>(Factory->FactoryCreateBinary(UTextureCube::StaticClass(), CreatePackage(*PackageName),
				TEXT("T_Garden_NightSky"), RF_Public | RF_Standalone, nullptr, TEXT("hdr"), Data, Data + SourceData.Num(), GWarn));
			if (!Texture)
			{
				Error = FString::Printf(TEXT("Cannot import garden HDR as TextureCube: %s"), *Filename);
				return false;
			}
			bNewAsset = true;
		}
		const bool bChanged = Texture->SRGB || Texture->CompressionSettings != TC_HDR_Compressed
			|| Texture->MaxTextureSize != 512 || Texture->LODGroup != TEXTUREGROUP_Skybox;
		if (bNewAsset || bChanged)
		{
			Texture->Modify();
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_HDR_Compressed;
			Texture->MaxTextureSize = 512;
			Texture->LODGroup = TEXTUREGROUP_Skybox;
			Texture->PostEditChange();
			if (!GardenSaveAsset(Texture, PackageName, !bSaved, Error)) return false;
		}
		return true;
	}

	template<typename T>
	T* GardenAddExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	template<typename T>
	T* FindParameter(UMaterial* Material, FName Name)
	{
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			T* Candidate = Cast<T>(Expression);
			if (Candidate && Candidate->ParameterName == Name) return Candidate;
		}
		return nullptr;
	}

	UMaterialExpressionScalarParameter* EnsureScalar(UMaterial* Material, FName Name, float DefaultValue,
		bool& bChanged, float Max = 1.0f)
	{
		UMaterialExpressionScalarParameter* Parameter = FindParameter<UMaterialExpressionScalarParameter>(Material, Name);
		if (!Parameter)
		{
			Parameter = GardenAddExpression<UMaterialExpressionScalarParameter>(Material);
			Parameter->ParameterName = Name;
			Parameter->ExpressionGUID = FGuid::NewGuid();
			Parameter->Group = TEXT("Garden");
			Parameter->DefaultValue = DefaultValue;
			Parameter->SliderMin = 0.0f;
			Parameter->SliderMax = Max;
			bChanged = true;
		}
		return Parameter;
	}

	UMaterialExpressionVectorParameter* EnsureVector(UMaterial* Material, FName Name,
		const FLinearColor& DefaultValue, bool& bChanged)
	{
		UMaterialExpressionVectorParameter* Parameter = FindParameter<UMaterialExpressionVectorParameter>(Material, Name);
		if (!Parameter)
		{
			Parameter = GardenAddExpression<UMaterialExpressionVectorParameter>(Material);
			Parameter->ParameterName = Name;
			Parameter->ExpressionGUID = FGuid::NewGuid();
			Parameter->Group = Name.ToString().StartsWith(TEXT("Stage")) && Name != TEXT("StageColor")
				? TEXT("Stage transform") : TEXT("Garden");
			Parameter->DefaultValue = DefaultValue;
			bChanged = true;
		}
		return Parameter;
	}

	UMaterialExpressionTextureObjectParameter* EnsureTexture(UMaterial* Material, FName Name, UTexture2D* Texture,
		EMaterialSamplerType SamplerType, bool& bChanged)
	{
		UMaterialExpressionTextureObjectParameter* Parameter = FindParameter<UMaterialExpressionTextureObjectParameter>(Material, Name);
		if (!Parameter)
		{
			Parameter = GardenAddExpression<UMaterialExpressionTextureObjectParameter>(Material);
			Parameter->ParameterName = Name;
			Parameter->ExpressionGUID = FGuid::NewGuid();
			Parameter->Group = TEXT("Optional garden textures");
			bChanged = true;
		}
		if (Parameter->Texture != Texture || Parameter->SamplerType != SamplerType)
		{
			Parameter->Texture = Texture;
			Parameter->SamplerType = SamplerType;
			bChanged = true;
		}
		return Parameter;
	}

	void EnsureConnection(FExpressionInput& Input, UMaterialExpression* Expression, int32 OutputIndex, bool& bChanged)
	{
		if (Input.Expression != Expression || Input.OutputIndex != OutputIndex)
		{
			Input.Connect(OutputIndex, Expression);
			bChanged = true;
		}
	}

	void EnsureInput(UMaterialExpressionCustom* Shader, FName Name, UMaterialExpression* Expression, bool& bChanged)
	{
		FCustomInput* Input = Shader->Inputs.FindByPredicate([Name](const FCustomInput& Candidate) { return Candidate.InputName == Name; });
		if (!Input)
		{
			Input = &Shader->Inputs.AddDefaulted_GetRef();
			Input->InputName = Name;
			bChanged = true;
		}
		EnsureConnection(Input->Input, Expression, 0, bChanged);
	}

	void EnsureOutputs(UMaterialExpressionCustom* Shader, bool bWater, bool& bChanged)
	{
		const FName Names[] = { bWater ? TEXT("water_roughness") : TEXT("surface_roughness"),
			bWater ? TEXT("water_normal") : TEXT("surface_normal"), TEXT("surface_ao"), TEXT("surface_specular") };
		const ECustomMaterialOutputType Types[] = { CMOT_Float1, CMOT_Float3, CMOT_Float1, CMOT_Float1 };
		const int32 Count = bWater ? 2 : 4;
		bool bValid = Shader->AdditionalOutputs.Num() == Count;
		for (int32 Index = 0; bValid && Index < Count; ++Index)
		{
			bValid = Shader->AdditionalOutputs[Index].OutputName == Names[Index]
				&& Shader->AdditionalOutputs[Index].OutputType == Types[Index];
		}
		if (!bValid)
		{
			Shader->AdditionalOutputs.Reset();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FCustomOutput& Output = Shader->AdditionalOutputs.AddDefaulted_GetRef();
				Output.OutputName = Names[Index];
				Output.OutputType = Types[Index];
			}
			Shader->RebuildOutputs();
			bChanged = true;
		}
	}

	bool EnsureMaterial(bool bWater, UTexture2D* const* DetailTextures, bool bDetailAvailable,
		UMaterialInterface*& Out, FString& Error)
	{
		const TCHAR* AssetName = bWater ? TEXT("M_MMD_GardenWater") : TEXT("M_MMD_GardenSurface");
		const TCHAR* ShaderFile = bWater ? TEXT("TMMDGardenWater.usf") : TEXT("TMMDGardenSurface.usf");
		const FString PackageName = FString(TEXT("/Ue5MMDTools/Stages/Materials/")) + AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		const bool bSaved = FPackageName::DoesPackageExist(PackageName);
		UMaterial* Material = bSaved ? LoadObject<UMaterial>(nullptr, *ObjectPath) : FindObject<UMaterial>(nullptr, *ObjectPath);
		if (!Material && bSaved)
		{
			Error = FString::Printf(TEXT("Cannot load existing garden material: %s"), *ObjectPath);
			return false;
		}
		bool bChanged = false;
		if (!Material)
		{
			Material = NewObject<UMaterial>(CreatePackage(*PackageName), FName(AssetName), RF_Public | RF_Standalone);
			FArchiveUObject InitialArchive;
			Material->Serialize(InitialArchive);
			bChanged = true;
		}

		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		UMaterialExpressionCustom* Shader = Cast<UMaterialExpressionCustom>(Data->BaseColor.Expression);
		if (!Shader)
		{
			for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
			{
				UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression);
				if (Candidate && Candidate->Code.Contains(ShaderFile)) { Shader = Candidate; break; }
			}
		}
		if (bSaved && (!Shader || !Shader->Code.Contains(ShaderFile)))
		{
			Error = FString::Printf(TEXT("Garden material has an unrelated graph: %s"), *ObjectPath);
			return false;
		}
		if (!Shader)
		{
			Shader = GardenAddExpression<UMaterialExpressionCustom>(Material);
			Shader->Inputs.Reset();
			bChanged = true;
		}
		const FString Code = FString::Printf(TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/%s\"\nreturn 0;"), ShaderFile);
		if (Shader->Code != Code || Shader->OutputType != CMOT_Float3)
		{
			Shader->Code = Code;
			Shader->OutputType = CMOT_Float3;
			Shader->Description = bWater ? TEXT("MMD garden reflective water") : TEXT("MMD garden geometric surface");
			bChanged = true;
		}
		EnsureOutputs(Shader, bWater, bChanged);
		EnsureInput(Shader, TEXT("stage_color"), EnsureVector(Material, TEXT("StageColor"),
			bWater ? FLinearColor(0.018f, 0.045f, 0.05f) : FLinearColor(0.30f, 0.25f, 0.20f), bChanged), bChanged);
		EnsureInput(Shader, TEXT("roughness"), EnsureScalar(Material, TEXT("Roughness"), bWater ? 0.065f : 0.60f, bChanged), bChanged);
		EnsureInput(Shader, TEXT("stage_origin"), EnsureVector(Material, TEXT("StageOrigin"), FLinearColor(0, 0, 0, 0), bChanged), bChanged);
		EnsureInput(Shader, TEXT("stage_right"), EnsureVector(Material, TEXT("StageRight"), FLinearColor(1, 0, 0, 0), bChanged), bChanged);
		EnsureInput(Shader, TEXT("stage_forward"), EnsureVector(Material, TEXT("StageForward"), FLinearColor(0, 1, 0, 0), bChanged), bChanged);
		EnsureInput(Shader, TEXT("stage_up"), EnsureVector(Material, TEXT("StageUp"), FLinearColor(0, 0, 1, 0), bChanged), bChanged);
		EnsureInput(Shader, TEXT("stage_time"), EnsureScalar(Material, TEXT("StageTime"), 0.0f, bChanged, 0.0f), bChanged);
		EnsureInput(Shader, TEXT("use_stage_time"), EnsureScalar(Material, TEXT("UseStageTime"), 0.0f, bChanged), bChanged);
		UMaterialExpressionTime* Time = nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (UMaterialExpressionTime* Candidate = Cast<UMaterialExpressionTime>(Expression)) { Time = Candidate; break; }
		}
		if (!Time) { Time = GardenAddExpression<UMaterialExpressionTime>(Material); bChanged = true; }
		EnsureInput(Shader, TEXT("time_seconds"), Time, bChanged);
		if (!bWater)
		{
			EnsureInput(Shader, TEXT("surface_role"), EnsureScalar(Material, TEXT("SurfaceRole"), 0.0f, bChanged, 5.0f), bChanged);
			UMaterialExpressionScalarParameter* UseDetails = EnsureScalar(Material, TEXT("UseDetailTextures"), bDetailAvailable ? 1.0f : 0.0f, bChanged);
			// 默认随可选素材升级；实例仍可将 UseDetailTextures 设为 0，独立验证过程化退路。
			const float DetailDefault = bDetailAvailable ? 1.0f : 0.0f;
			if (UseDetails->DefaultValue != DetailDefault) { UseDetails->DefaultValue = DetailDefault; bChanged = true; }
			EnsureInput(Shader, TEXT("use_detail_textures"), UseDetails, bChanged);
			const FName TextureNames[] = { TEXT("wood_base_tex"), TEXT("wood_arm_tex"), TEXT("stone_base_tex"), TEXT("stone_arm_tex") };
			for (int32 Index = 0; Index < 4; ++Index)
			{
				EnsureInput(Shader, TextureNames[Index], EnsureTexture(Material, TextureNames[Index], DetailTextures[Index],
					Index % 2 == 0 ? SAMPLERTYPE_Color : SAMPLERTYPE_Masks, bChanged), bChanged);
			}
		}
		EnsureConnection(Data->BaseColor, Shader, 0, bChanged);
		EnsureConnection(Data->Roughness, Shader, 1, bChanged);
		EnsureConnection(Data->Normal, Shader, 2, bChanged);
		EnsureConnection(Data->Metallic, EnsureScalar(Material, TEXT("Metallic"), 0.0f, bChanged), 0, bChanged);
		if (bWater)
		{
			EnsureConnection(Data->Specular, EnsureScalar(Material, TEXT("Specular"), 0.5f, bChanged), 0, bChanged);
		}
		else
		{
			EnsureConnection(Data->AmbientOcclusion, Shader, 3, bChanged);
			EnsureConnection(Data->Specular, Shader, 4, bChanged);
		}
		const bool bTwoSided = !bWater;
		if (Material->MaterialDomain != MD_Surface || Material->BlendMode != BLEND_Opaque
			|| Material->GetShadingModels().GetFirstShadingModel() != MSM_DefaultLit
			|| Material->TwoSided != bTwoSided || Material->bTangentSpaceNormal
			|| !Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
		{
			Material->MaterialDomain = MD_Surface;
			Material->BlendMode = BLEND_Opaque;
			Material->SetShadingModel(MSM_DefaultLit);
			Material->TwoSided = bTwoSided;
			Material->bTangentSpaceNormal = false;
			Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
			bChanged = true;
		}
		const int32 ExpectedInputs = bWater ? 9 : 15;
		if (Shader->Inputs.Num() != ExpectedInputs || Shader->AdditionalOutputs.Num() != (bWater ? 2 : 4)
			|| Shader->Inputs.ContainsByPredicate([](const FCustomInput& Input) { return Input.InputName.IsNone() || !Input.Input.Expression; }))
		{
			Error = FString::Printf(TEXT("Garden material input/output schema is invalid: %s"), *ObjectPath);
			return false;
		}
		if (!bSaved || bChanged)
		{
			Material->Modify();
			Material->PostEditChange();
			if (!GardenSaveAsset(Material, PackageName, !bSaved, Error)) return false;
		}
		Out = Material;
		return true;
	}
}

bool EnsureMMDGardenMaterials(FMMDGardenMaterialSet& Out, FString& Error)
{
	Out = FMMDGardenMaterialSet();
	Error.Reset();
	if (!IsInGameThread())
	{
		Error = TEXT("Garden materials must be created on the game thread");
		return false;
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!Plugin.IsValid() || !WhiteTexture)
	{
		Error = TEXT("Cannot load garden plugin path or fallback texture");
		return false;
	}
	const FString ResourcesPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Garden"));
	const TCHAR* SourceNames[] = { TEXT("wood_floor_deck_diff_1k.png"), TEXT("wood_floor_deck_arm_1k.png"),
		TEXT("monastery_stone_floor_diff_1k.png"), TEXT("monastery_stone_floor_arm_1k.png") };
	const TCHAR* AssetNames[] = { TEXT("T_Garden_Wood_Base"), TEXT("T_Garden_Wood_ARM"),
		TEXT("T_Garden_Stone_Base"), TEXT("T_Garden_Stone_ARM") };
	UTexture2D* DetailTextures[4] = {};
	bool bAllDetailsAvailable = true;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		bool bAvailable = false;
		if (!EnsureDetailTexture(ResourcesPath, SourceNames[Index], AssetNames[Index], Index % 2 == 0,
			WhiteTexture, DetailTextures[Index], bAvailable, Error)) return false;
		bAllDetailsAvailable &= bAvailable;
	}
	if (!EnsureNightSky(ResourcesPath, Error)) return false;
	return EnsureMaterial(false, DetailTextures, bAllDetailsAvailable, Out.Surface, Error)
		&& EnsureMaterial(true, DetailTextures, bAllDetailsAvailable, Out.Water, Error);
}
