#include "Editor/MMDReferenceMaterialBuilder.h"

#include "Editor/MMDStageMaterialBuilder.h"
#include "Editor/MMDReferenceTextureGenerator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSkyLightEnvMapSample.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* ReferenceInputNames[] = {
		TEXT("base_color"), TEXT("light_data_tex"), TEXT("mmd_shadow_map"), TEXT("mat_cap"),
		TEXT("shadow_threshold"), TEXT("shadow_softness"), TEXT("shadow_tint"), TEXT("ambient_strength"),
		TEXT("light_scale"), TEXT("specular_strength"), TEXT("specular_power"), TEXT("rim_strength"), TEXT("rim_power"),
		TEXT("toon_amount"), TEXT("diffuse_wrap"), TEXT("color_saturation"), TEXT("shadow_saturation"),
		TEXT("roughness"), TEXT("specular_tint"), TEXT("sphere_mode"), TEXT("sphere_strength"),
		TEXT("black_lift"), TEXT("soft_specular_strength"), TEXT("style_family"), TEXT("material_role"),
		TEXT("environment_reflection"), TEXT("environment_specular_strength"),
		TEXT("hair_shift_tex"), TEXT("hair_shift_strength"), TEXT("hair_shift_scale"),
		TEXT("head_forward_ws"), TEXT("head_right_ws"), TEXT("head_position_radius"),
		TEXT("hair_detail_normal"), TEXT("hair_detail_strength"), TEXT("cloth_detail_normal"), TEXT("cloth_detail_strength"),
		TEXT("cloth_detail_data"), TEXT("cloth_detail_scale"), TEXT("hair_detail_scale"),
		TEXT("surface_diffuse_normal"), TEXT("surface_specular_normal_roughness"),
		TEXT("surface_hair_tangent_strand"), TEXT("surface_detail_response") };

	bool EnsureGenericHairShiftTexture(UTexture2D*& OutTexture, FString& OutError)
	{
		const FString PackageName = TEXT("/Ue5MMDTools/Stages/Textures/T_MMD_GenericHairShift");
		const FString ObjectPath = PackageName + TEXT(".T_MMD_GenericHairShift");
		const bool bSaved = FPackageName::DoesPackageExist(PackageName);
		UTexture2D* Texture = bSaved ? LoadObject<UTexture2D>(nullptr, *ObjectPath) : FindObject<UTexture2D>(nullptr, *ObjectPath);
		if (bSaved && !Texture)
		{
			OutError = FString::Printf(TEXT("Cannot load generic hair shift texture: %s"), *ObjectPath);
			return false;
		}
		bool bChanged = false;
		if (!Texture)
		{
			Texture = NewObject<UTexture2D>(CreatePackage(*PackageName), TEXT("T_MMD_GenericHairShift"), RF_Public | RF_Standalone);
			bChanged = true;
		}
		constexpr int32 Resolution = 128;
		if (Texture->Source.GetSizeX() != Resolution || Texture->Source.GetSizeY() != Resolution || Texture->Source.GetFormat() != TSF_G8)
		{
			TArray<uint8> Pixels;
			Pixels.SetNumUninitialized(Resolution * Resolution);
			// 原创确定性整数周期函数：平滑发束与变频噪声可平铺，不包含模型 UV/部位信息。
			for (int32 Y = 0; Y < Resolution; ++Y)
			{
				for (int32 X = 0; X < Resolution; ++X)
				{
					const double U = (static_cast<double>(X) + 0.5) / Resolution;
					const double V = (static_cast<double>(Y) + 0.5) / Resolution;
					const double Turn = 2.0 * UE_PI;
					const double Warp = 1.1 * FMath::Sin(Turn * V) + 0.30 * FMath::Sin(Turn * 3.0 * V);
					const double Strands = FMath::Sin(Turn * 17.0 * U + Warp);
					const double FineStrands = FMath::Sin(Turn * (31.0 * U + V) + Warp * 0.4);
					const double Noise = FMath::Sin(Turn * (7.0 * U + 3.0 * V) + FMath::Sin(Turn * 2.0 * U) * 0.8)
						* FMath::Sin(Turn * (5.0 * U - 2.0 * V) + FMath::Sin(Turn * V) * 0.5);
					const double Value = 0.5 + Strands * 0.22 + FineStrands * 0.10 + Noise * 0.07
						+ FMath::Sin(Turn * (3.0 * U + 2.0 * V)) * 0.045;
					Pixels[Y * Resolution + X] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0), 0, 255));
				}
			}
			Texture->Source.Init(Resolution, Resolution, 1, 1, TSF_G8, Pixels.GetData());
			bChanged = true;
		}
		if (Texture->SRGB || Texture->CompressionSettings != TC_Grayscale || Texture->AddressX != TA_Wrap
			|| Texture->AddressY != TA_Wrap || Texture->Filter != TF_Trilinear || Texture->VirtualTextureStreaming)
		{
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Grayscale;
			Texture->AddressX = TA_Wrap;
			Texture->AddressY = TA_Wrap;
			Texture->Filter = TF_Trilinear;
			Texture->VirtualTextureStreaming = false;
			Texture->LODGroup = TEXTUREGROUP_Character;
			Texture->MipGenSettings = TMGS_FromTextureGroup;
			bChanged = true;
		}
		if (!bSaved || bChanged)
		{
			Texture->PostEditChange();
			Texture->MarkPackageDirty();
			if (!bSaved) FAssetRegistryModule::AssetCreated(Texture);
			const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			Args.SaveFlags = SAVE_NoError;
			if (!UPackage::SavePackage(Texture->GetOutermost(), Texture, *Filename, Args))
			{
				OutError = FString::Printf(TEXT("Cannot save generic hair shift texture: %s"), *PackageName);
				return false;
			}
		}
		OutTexture = Texture;
		return true;
	}

	bool BuildReferenceShaderCode(FString& OutCode, FString& OutSignature, FString& OutError)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		if (!Plugin)
		{
			OutError = TEXT("Cannot locate Ue5MMDTools to read the reference shader source");
			return false;
		}
		const TCHAR* SourcePaths[] = { TEXT("Shaders/TMMDShader/TMMDReferenceCharacter.usf"), TEXT("Shaders/Core/MMDToonLighting.ush"),
			TEXT("Shaders/Core/TMMDReferenceBRDF.ush") };
		FString SourceBundle;
		for (const TCHAR* RelativePath : SourcePaths)
		{
			const FString SourcePath = FPaths::Combine(Plugin->GetBaseDir(), RelativePath);
			FString Source;
			if (!FFileHelper::LoadFileToString(Source, *SourcePath) || Source.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Cannot read reference shader source: %s"), *SourcePath);
				return false;
			}
			SourceBundle += FString(RelativePath) + TEXT("\n") + Source + TEXT("\n");
		}
		// 实际入口和 Core 依赖一起进入材质键；只在源码改变时更新已有 shader map。
		FTCHARToUTF8 SourceUtf8(*SourceBundle);
		const FString SourceHash = FMD5::HashBytes(reinterpret_cast<const uint8*>(SourceUtf8.Get()), SourceUtf8.Length());
		OutSignature = TEXT("// 源码签名：") + SourceHash;
		OutCode = OutSignature + TEXT("\n#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDReferenceCharacter.usf\"\nreturn 0;");
		return true;
	}

	template<typename T>
	T* AddReferenceExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	void EnsureReferenceConnection(FExpressionInput& Input, UMaterialExpression* Expression, bool& bChanged, int32 OutputIndex = 0)
	{
		if (Input.Expression != Expression || Input.OutputIndex != OutputIndex)
		{
			Input.Connect(OutputIndex, Expression);
			bChanged = true;
		}
	}

	void EnsureReferenceInput(UMaterialExpressionCustom* Shader, FName InputName, UMaterialExpression* Expression, bool& bChanged,
		int32 OutputIndex = 0)
	{
		FCustomInput* Input = Shader->Inputs.FindByPredicate([InputName](const FCustomInput& Candidate) { return Candidate.InputName == InputName; });
		if (!Input)
		{
			Input = &Shader->Inputs.AddDefaulted_GetRef();
			Input->InputName = InputName;
			bChanged = true;
		}
		EnsureReferenceConnection(Input->Input, Expression, bChanged, OutputIndex);
	}

	bool UpgradeReferenceEnvironment(UMaterial* Material, bool& bChanged)
	{
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Shader || !Shader->Code.Contains(TEXT("TMMDReferenceCharacter.usf"))
			|| (Shader->Inputs.Num() != 25 && Shader->Inputs.Num() != 27 && Shader->Inputs.Num() != 30
				&& Shader->Inputs.Num() != 33 && Shader->Inputs.Num() != 40 && Shader->Inputs.Num() != 44)) return false;
		for (int32 InputIndex = 0; InputIndex < 25; ++InputIndex)
			if (Shader->Inputs[InputIndex].InputName != FName(ReferenceInputNames[InputIndex])
				|| !Shader->Inputs[InputIndex].Input.Expression) return false;

		UMaterialExpressionCustom* RoleRoughness = nullptr;
		UMaterialExpressionReflectionVectorWS* Reflection = nullptr;
		UMaterialExpressionSkyLightEnvMapSample* Environment = nullptr;
		UMaterialExpressionScalarParameter* Strength = nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression))
				if (Candidate != Shader && Candidate->Code.Contains(TEXT("GetRoleRoughness"))) RoleRoughness = Candidate;
			if (UMaterialExpressionReflectionVectorWS* Candidate = Cast<UMaterialExpressionReflectionVectorWS>(Expression)) Reflection = Candidate;
			if (UMaterialExpressionSkyLightEnvMapSample* Candidate = Cast<UMaterialExpressionSkyLightEnvMapSample>(Expression)) Environment = Candidate;
			if (UMaterialExpressionScalarParameter* Candidate = Cast<UMaterialExpressionScalarParameter>(Expression))
				if (Candidate->ParameterName == TEXT("EnvironmentSpecularStrength")) Strength = Candidate;
		}
		if (!RoleRoughness)
		{
			RoleRoughness = AddReferenceExpression<UMaterialExpressionCustom>(Material);
			RoleRoughness->Inputs.Reset();
			bChanged = true;
		}
		const FString RoughnessCode = TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDReferenceBRDF.ush\"\n")
			TEXT("TMMDReferenceBRDF brdf;\nreturn brdf.GetRoleRoughness(roughness, material_role, style_family, specular_power);");
		if (RoleRoughness->Code != RoughnessCode || RoleRoughness->OutputType != CMOT_Float1)
		{
			RoleRoughness->Code = RoughnessCode;
			RoleRoughness->OutputType = CMOT_Float1;
			RoleRoughness->Description = TEXT("MMD reference role roughness");
			bChanged = true;
		}
		EnsureReferenceInput(RoleRoughness, TEXT("roughness"), Shader->Inputs[17].Input.Expression, bChanged);
		EnsureReferenceInput(RoleRoughness, TEXT("material_role"), Shader->Inputs[24].Input.Expression, bChanged);
		EnsureReferenceInput(RoleRoughness, TEXT("style_family"), Shader->Inputs[23].Input.Expression, bChanged);
		EnsureReferenceInput(RoleRoughness, TEXT("specular_power"), Shader->Inputs[10].Input.Expression, bChanged);
		if (!Reflection) { Reflection = AddReferenceExpression<UMaterialExpressionReflectionVectorWS>(Material); bChanged = true; }
		if (!Environment) { Environment = AddReferenceExpression<UMaterialExpressionSkyLightEnvMapSample>(Material); bChanged = true; }
		EnsureReferenceConnection(Environment->Direction, Reflection, bChanged);
		if (Shader->Inputs.Num() < 44 || !Environment->Roughness.Expression)
			EnsureReferenceConnection(Environment->Roughness, RoleRoughness, bChanged);
		if (!Strength)
		{
			Strength = AddReferenceExpression<UMaterialExpressionScalarParameter>(Material);
			Strength->ParameterName = TEXT("EnvironmentSpecularStrength");
			Strength->ExpressionGUID = FGuid::NewGuid();
			Strength->Group = TEXT("Rendering");
			Strength->DefaultValue = 1.0f;
			Strength->SliderMin = 0.0f;
			Strength->SliderMax = 2.0f;
			bChanged = true;
		}
		// 使用真实图节点，让引擎负责预过滤天空资源及采样变体；不直接访问私有绑定。
		EnsureReferenceInput(Shader, TEXT("environment_reflection"), Environment, bChanged);
		EnsureReferenceInput(Shader, TEXT("environment_specular_strength"), Strength, bChanged);
		return true;
	}

	bool UpgradeReferenceHairShift(UMaterial* Material, UTexture2D* DefaultTexture, bool& bChanged)
	{
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Shader || (Shader->Inputs.Num() != 27 && Shader->Inputs.Num() != 30 && Shader->Inputs.Num() != 33
			&& Shader->Inputs.Num() != 40 && Shader->Inputs.Num() != 44)
			|| !DefaultTexture) return false;
		UMaterialExpressionTextureObjectParameter* ShiftTexture = nullptr;
		UMaterialExpressionScalarParameter* ShiftStrength = nullptr;
		UMaterialExpressionScalarParameter* ShiftScale = nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (UMaterialExpressionTextureObjectParameter* Candidate = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
				if (Candidate->ParameterName == TEXT("HairShiftTexture")) ShiftTexture = Candidate;
			if (UMaterialExpressionScalarParameter* Candidate = Cast<UMaterialExpressionScalarParameter>(Expression))
			{
				if (Candidate->ParameterName == TEXT("HairShiftStrength")) ShiftStrength = Candidate;
				if (Candidate->ParameterName == TEXT("HairShiftScale")) ShiftScale = Candidate;
			}
		}
		if (!ShiftTexture)
		{
			ShiftTexture = AddReferenceExpression<UMaterialExpressionTextureObjectParameter>(Material);
			ShiftTexture->ParameterName = TEXT("HairShiftTexture");
			ShiftTexture->ExpressionGUID = FGuid::NewGuid();
			ShiftTexture->Group = TEXT("Optional generic hair detail");
			ShiftTexture->Texture = DefaultTexture;
			ShiftTexture->SamplerType = SAMPLERTYPE_LinearGrayscale;
			bChanged = true;
		}
		if (!ShiftTexture->Texture)
		{
			ShiftTexture->Texture = DefaultTexture;
			ShiftTexture->SamplerType = SAMPLERTYPE_LinearGrayscale;
			bChanged = true;
		}
		if (!ShiftStrength)
		{
			ShiftStrength = AddReferenceExpression<UMaterialExpressionScalarParameter>(Material);
			ShiftStrength->ParameterName = TEXT("HairShiftStrength");
			ShiftStrength->ExpressionGUID = FGuid::NewGuid();
			ShiftStrength->Group = TEXT("Optional generic hair detail");
			ShiftStrength->DefaultValue = 0.50f;
			ShiftStrength->SliderMin = 0.0f;
			ShiftStrength->SliderMax = 1.0f;
			bChanged = true;
		}
		if (!ShiftScale)
		{
			ShiftScale = AddReferenceExpression<UMaterialExpressionScalarParameter>(Material);
			ShiftScale->ParameterName = TEXT("HairShiftScale");
			ShiftScale->ExpressionGUID = FGuid::NewGuid();
			ShiftScale->Group = TEXT("Optional generic hair detail");
			ShiftScale->DefaultValue = 0.045f;
			ShiftScale->SliderMin = 0.0001f;
			ShiftScale->SliderMax = 0.20f;
			bChanged = true;
		}
		// 升级旧父材质默认值；保留用户已定制的其它默认值及所有 MID 参数。
		if (FMath::IsNearlyEqual(ShiftStrength->DefaultValue, 0.35f))
		{
			ShiftStrength->DefaultValue = 0.50f;
			bChanged = true;
		}
		if (FMath::IsNearlyEqual(ShiftScale->DefaultValue, 0.02f))
		{
			ShiftScale->DefaultValue = 0.045f;
			bChanged = true;
		}
		EnsureReferenceInput(Shader, TEXT("hair_shift_tex"), ShiftTexture, bChanged);
		EnsureReferenceInput(Shader, TEXT("hair_shift_strength"), ShiftStrength, bChanged);
		EnsureReferenceInput(Shader, TEXT("hair_shift_scale"), ShiftScale, bChanged);
		const TCHAR* HeadParameters[] = { TEXT("MMDHeadForward"), TEXT("MMDHeadRight"), TEXT("MMDHeadPositionRadius") };
		UMaterialExpressionVectorParameter* HeadExpressions[3] = {};
		for (int32 HeadIndex = 0; HeadIndex < UE_ARRAY_COUNT(HeadParameters); ++HeadIndex)
		{
			for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
			{
				UMaterialExpressionVectorParameter* Candidate = Cast<UMaterialExpressionVectorParameter>(Expression);
				if (Candidate && Candidate->ParameterName == FName(HeadParameters[HeadIndex])) HeadExpressions[HeadIndex] = Candidate;
			}
			if (!HeadExpressions[HeadIndex])
			{
				UMaterialExpressionVectorParameter* Head = AddReferenceExpression<UMaterialExpressionVectorParameter>(Material);
				Head->ParameterName = HeadParameters[HeadIndex];
				Head->ExpressionGUID = FGuid::NewGuid();
				Head->Group = TEXT("Optional geometric head frame");
				Head->DefaultValue = FLinearColor(0, 0, 0, 0);
				HeadExpressions[HeadIndex] = Head;
				bChanged = true;
			}
			// VectorParameter 输出 5 是 RGBA；必须保留半径 W，不能使用默认 RGB 输出。
			EnsureReferenceInput(Shader, ReferenceInputNames[30 + HeadIndex], HeadExpressions[HeadIndex], bChanged, 5);
		}
		return true;
	}

	bool UpgradeReferenceSurfaceDetail(UMaterial* Material, UTexture2D* HairNormal, UTexture2D* ClothNormal,
		UTexture2D* ClothData, bool& bChanged)
	{
		UMaterialExpressionCustom* Shader = Material ? Cast<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression) : nullptr;
		if (!Shader || (Shader->Inputs.Num() != 33 && Shader->Inputs.Num() != 40 && Shader->Inputs.Num() != 44)
			|| !HairNormal || !ClothNormal || !ClothData) return false;
		const TCHAR* ParameterNames[] = { TEXT("HairDetailNormal"), TEXT("HairDetailStrength"), TEXT("ClothDetailNormal"),
			TEXT("ClothDetailStrength"), TEXT("ClothDetailData"), TEXT("ClothDetailScale"), TEXT("HairDetailScale") };
		const float Defaults[] = { 0.0f, 0.20f, 0.0f, 0.40f, 0.0f, 0.010f, 0.040f };
		for (int32 DetailIndex = 0; DetailIndex < UE_ARRAY_COUNT(ParameterNames); ++DetailIndex)
		{
			const bool bTexture = DetailIndex == 0 || DetailIndex == 2 || DetailIndex == 4;
			UMaterialExpression* Parameter = nullptr;
			for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
			{
				if (bTexture)
				{
					UMaterialExpressionTextureObjectParameter* Candidate = Cast<UMaterialExpressionTextureObjectParameter>(Expression);
					if (Candidate && Candidate->ParameterName == FName(ParameterNames[DetailIndex])) Parameter = Candidate;
				}
				else
				{
					UMaterialExpressionScalarParameter* Candidate = Cast<UMaterialExpressionScalarParameter>(Expression);
					if (Candidate && Candidate->ParameterName == FName(ParameterNames[DetailIndex])) Parameter = Candidate;
				}
			}
			if (bTexture)
			{
				UTexture2D* DefaultTexture = DetailIndex == 0 ? HairNormal : (DetailIndex == 2 ? ClothNormal : ClothData);
				const EMaterialSamplerType SamplerType = DetailIndex == 4 ? SAMPLERTYPE_LinearColor : SAMPLERTYPE_Normal;
				const TCHAR* LegacyName = DetailIndex == 0 ? TEXT("T_MMD_GenericHairNormal")
					: (DetailIndex == 2 ? TEXT("T_MMD_GenericClothNormal") : TEXT("T_MMD_GenericClothData"));
				const FString LegacyPath = FString(TEXT("/Ue5MMDTools/Stages/Textures/")) + LegacyName + TEXT(".") + LegacyName;
				UMaterialExpressionTextureObjectParameter* Texture = Cast<UMaterialExpressionTextureObjectParameter>(Parameter);
				if (!Texture)
				{
					Texture = AddReferenceExpression<UMaterialExpressionTextureObjectParameter>(Material);
					Texture->ParameterName = ParameterNames[DetailIndex];
					Texture->ExpressionGUID = FGuid::NewGuid();
					Texture->Group = TEXT("Optional universal surface detail");
					Texture->Texture = DefaultTexture;
					Texture->SamplerType = SamplerType;
					Parameter = Texture;
					bChanged = true;
				}
				// 只迁移上一版插件默认图的精确路径；艺术家自选图和实例覆盖保持原样。
				const bool bLegacyDefault = Texture->Texture && Texture->Texture->GetPathName() == LegacyPath;
				if (!Texture->Texture || bLegacyDefault || Texture->SamplerType != SamplerType)
				{
					if (!Texture->Texture || bLegacyDefault) Texture->Texture = DefaultTexture;
					Texture->SamplerType = SamplerType;
					bChanged = true;
				}
			}
			else if (!Parameter)
			{
				UMaterialExpressionScalarParameter* Scalar = AddReferenceExpression<UMaterialExpressionScalarParameter>(Material);
				Scalar->ParameterName = ParameterNames[DetailIndex];
				Scalar->ExpressionGUID = FGuid::NewGuid();
				Scalar->Group = TEXT("Optional universal surface detail");
				Scalar->DefaultValue = Defaults[DetailIndex];
				Scalar->SliderMin = DetailIndex >= 5 ? 0.0001f : 0.0f;
				Scalar->SliderMax = DetailIndex >= 5 ? 0.20f : 1.0f;
				Parameter = Scalar;
				bChanged = true;
			}
			// 只收敛上一版的精确默认强度；用户自定义数值和 MID 覆盖不变。
			if (UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Parameter))
			{
				if ((DetailIndex == 1 && Scalar->DefaultValue == 0.45f)
					|| (DetailIndex == 3 && Scalar->DefaultValue == 0.65f))
				{
					Scalar->DefaultValue = Defaults[DetailIndex];
					bChanged = true;
				}
			}
			EnsureReferenceInput(Shader, ReferenceInputNames[33 + DetailIndex], Parameter, bChanged);
		}
		UMaterialExpressionCustom* Shared = nullptr;
		UMaterialExpressionComponentMask* SpecularNormal = nullptr;
		UMaterialExpressionComponentMask* SurfaceRoughness = nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expression))
				if (Candidate != Shader && (Candidate->Code.Contains(TEXT("PrepareSurfaceDetail")) || Candidate->Code.Contains(TEXT("PrepareHair")))) Shared = Candidate;
			if (UMaterialExpressionComponentMask* Candidate = Cast<UMaterialExpressionComponentMask>(Expression))
			{
				if (Candidate->Desc == TEXT("MMD shared specular normal")) SpecularNormal = Candidate;
				if (Candidate->Desc == TEXT("MMD shared surface roughness")) SurfaceRoughness = Candidate;
			}
		}
		if (!Shared)
		{
			Shared = AddReferenceExpression<UMaterialExpressionCustom>(Material);
			Shared->Inputs.Reset();
			bChanged = true;
		}
		const FString SharedCode = TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDReferenceBRDF.ush\"\n")
			TEXT("TMMDReferenceBRDF brdf;\n")
			TEXT("float3 world_normal = brdf.SafeNormalize((float3)Parameters.TangentToWorld[2] * Parameters.TwoSidedSign, float3(0,0,1));\n")
			TEXT("TMMDReferenceSurfaceDetail surface = brdf.PrepareSurfaceDetail(Parameters, world_normal, material_role, roughness, style_family, specular_power,\n")
			TEXT("hair_shift_tex, hair_shift_texSampler, hair_shift_strength, hair_shift_scale, head_forward_ws, head_right_ws, head_position_radius,\n")
			TEXT("hair_detail_normal, hair_detail_normalSampler, hair_detail_strength, cloth_detail_normal, cloth_detail_normalSampler, cloth_detail_strength,\n")
			TEXT("cloth_detail_data, cloth_detail_dataSampler, cloth_detail_scale, hair_detail_scale);\n")
			TEXT("surface_specular_normal_roughness = float4(surface.specular_normal, surface.roughness);\n")
			TEXT("surface_hair_tangent_strand = float4(surface.hair_tangent, surface.strand_mask);\n")
			TEXT("surface_detail_response = float3(surface.cloth_cavity, surface.cloth_weave, surface.face_mask);\n")
			TEXT("return surface.diffuse_normal;");
		if (Shared->Code != SharedCode || Shared->OutputType != CMOT_Float3)
		{
			Shared->Code = SharedCode;
			Shared->OutputType = CMOT_Float3;
			Shared->Description = TEXT("MMD shared surface detail");
			bChanged = true;
		}
		const int32 SharedInputs[] = { 24, 27, 28, 29, 30, 31, 32, 17, 23, 10, 33, 34, 35, 36, 37, 38, 39 };
		for (int32 InputIndex : SharedInputs)
			EnsureReferenceInput(Shared, ReferenceInputNames[InputIndex], Shader->Inputs[InputIndex].Input.Expression,
				bChanged, Shader->Inputs[InputIndex].Input.OutputIndex);
		const ECustomMaterialOutputType OutputTypes[] = { CMOT_Float4, CMOT_Float4, CMOT_Float3 };
		bool bOutputsChanged = Shared->AdditionalOutputs.Num() != 3;
		if (!bOutputsChanged)
			for (int32 OutputIndex = 0; OutputIndex < 3; ++OutputIndex)
				bOutputsChanged |= Shared->AdditionalOutputs[OutputIndex].OutputName != FName(ReferenceInputNames[41 + OutputIndex])
					|| Shared->AdditionalOutputs[OutputIndex].OutputType != OutputTypes[OutputIndex];
		if (bOutputsChanged)
		{
			Shared->AdditionalOutputs.Reset();
			for (int32 OutputIndex = 0; OutputIndex < 3; ++OutputIndex)
			{
				FCustomOutput& Output = Shared->AdditionalOutputs.AddDefaulted_GetRef();
				Output.OutputName = ReferenceInputNames[41 + OutputIndex];
				Output.OutputType = OutputTypes[OutputIndex];
			}
			Shared->RebuildOutputs();
			bChanged = true;
		}
		for (int32 OutputIndex = 0; OutputIndex < 4; ++OutputIndex)
			EnsureReferenceInput(Shader, ReferenceInputNames[40 + OutputIndex], Shared, bChanged, OutputIndex);
		if (!SpecularNormal)
		{
			SpecularNormal = AddReferenceExpression<UMaterialExpressionComponentMask>(Material);
			SpecularNormal->Desc = TEXT("MMD shared specular normal");
			bChanged = true;
		}
		if (!SurfaceRoughness)
		{
			SurfaceRoughness = AddReferenceExpression<UMaterialExpressionComponentMask>(Material);
			SurfaceRoughness->Desc = TEXT("MMD shared surface roughness");
			bChanged = true;
		}
		if (!SpecularNormal->R || !SpecularNormal->G || !SpecularNormal->B || SpecularNormal->A
			|| SurfaceRoughness->R || SurfaceRoughness->G || SurfaceRoughness->B || !SurfaceRoughness->A)
		{
			SpecularNormal->R = SpecularNormal->G = SpecularNormal->B = true;
			SpecularNormal->A = false;
			SurfaceRoughness->R = SurfaceRoughness->G = SurfaceRoughness->B = false;
			SurfaceRoughness->A = true;
			bChanged = true;
		}
		EnsureReferenceConnection(SpecularNormal->Input, Shared, bChanged, 1);
		EnsureReferenceConnection(SurfaceRoughness->Input, Shared, bChanged, 1);
		UMaterialExpressionSkyLightEnvMapSample* Environment = Cast<UMaterialExpressionSkyLightEnvMapSample>(Shader->Inputs[25].Input.Expression);
		UMaterialExpressionReflectionVectorWS* Reflection = Environment ? Cast<UMaterialExpressionReflectionVectorWS>(Environment->Direction.Expression) : nullptr;
		if (!Reflection) return false;
		EnsureReferenceConnection(Reflection->CustomWorldNormal, SpecularNormal, bChanged);
		EnsureReferenceConnection(Environment->Roughness, SurfaceRoughness, bChanged);
		if (!Reflection->bNormalizeCustomWorldNormal) { Reflection->bNormalizeCustomWorldNormal = true; bChanged = true; }
		return true;
	}

	bool ValidateReferenceMaterial(const UMaterial* Material, int32 FamilyIndex, EBlendMode BlendMode)
	{
		const UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		const UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Material || Material->MaterialDomain != MD_Surface || Material->BlendMode != BlendMode || !Material->TwoSided
			|| Material->GetShadingModels().GetFirstShadingModel() != MSM_Unlit
			|| !Material->GetUsageByFlag(MATUSAGE_SkeletalMesh) || !Material->GetUsageByFlag(MATUSAGE_MorphTargets)
			|| !Data->Opacity.Expression || !Data->OpacityMask.Expression || !Shader || Shader->OutputType != CMOT_Float3
			|| !Shader->Code.Contains(TEXT("/Plugin/Ue5MMDTools/TMMDShader/TMMDReferenceCharacter.usf"))
			|| Shader->Inputs.Num() != UE_ARRAY_COUNT(ReferenceInputNames))
		{
			return false;
		}
		for (int32 InputIndex = 0; InputIndex < UE_ARRAY_COUNT(ReferenceInputNames); ++InputIndex)
		{
			const FCustomInput& Input = Shader->Inputs[InputIndex];
			if (Input.InputName != FName(ReferenceInputNames[InputIndex]) || !Input.Input.Expression)
				return false;
		}
		const UMaterialExpressionConstant* Style = Cast<UMaterialExpressionConstant>(Shader->Inputs[23].Input.Expression);
		const UMaterialExpressionScalarParameter* Role = Cast<UMaterialExpressionScalarParameter>(Shader->Inputs[24].Input.Expression);
		const UMaterialExpressionSkyLightEnvMapSample* Environment = Cast<UMaterialExpressionSkyLightEnvMapSample>(Shader->Inputs[25].Input.Expression);
		const UMaterialExpressionScalarParameter* EnvironmentStrength = Cast<UMaterialExpressionScalarParameter>(Shader->Inputs[26].Input.Expression);
		const UMaterialExpressionCustom* Shared = Cast<UMaterialExpressionCustom>(Shader->Inputs[40].Input.Expression);
		const UMaterialExpressionReflectionVectorWS* Reflection = Environment ? Cast<UMaterialExpressionReflectionVectorWS>(Environment->Direction.Expression) : nullptr;
		const UMaterialExpressionComponentMask* SpecularNormal = Reflection ? Cast<UMaterialExpressionComponentMask>(Reflection->CustomWorldNormal.Expression) : nullptr;
		const UMaterialExpressionComponentMask* SurfaceRoughness = Environment ? Cast<UMaterialExpressionComponentMask>(Environment->Roughness.Expression) : nullptr;
		if (!Style || !FMath::IsNearlyEqual(Style->R, static_cast<float>(FamilyIndex))
			|| !Role || Role->ParameterName != TEXT("MaterialRole") || !FMath::IsNearlyZero(Role->DefaultValue)
			|| !Environment || !EnvironmentStrength || EnvironmentStrength->ParameterName != TEXT("EnvironmentSpecularStrength")
			|| !Reflection || !Reflection->bNormalizeCustomWorldNormal
			|| !SpecularNormal || !SpecularNormal->R || !SpecularNormal->G || !SpecularNormal->B || SpecularNormal->A
			|| !SurfaceRoughness || SurfaceRoughness->R || SurfaceRoughness->G || SurfaceRoughness->B || !SurfaceRoughness->A
			|| !Shared || Shared->OutputType != CMOT_Float3 || Shared->Inputs.Num() != 17 || Shared->AdditionalOutputs.Num() != 3
			|| !Shared->Code.Contains(TEXT("PrepareSurfaceDetail"))
			|| SpecularNormal->Input.Expression != Shared || SpecularNormal->Input.OutputIndex != 1
			|| SurfaceRoughness->Input.Expression != Shared || SurfaceRoughness->Input.OutputIndex != 1) return false;
		const int32 SharedInputs[] = { 24, 27, 28, 29, 30, 31, 32, 17, 23, 10, 33, 34, 35, 36, 37, 38, 39 };
		for (int32 SharedIndex = 0; SharedIndex < UE_ARRAY_COUNT(SharedInputs); ++SharedIndex)
		{
			const int32 SourceIndex = SharedInputs[SharedIndex];
			const FCustomInput& Input = Shared->Inputs[SharedIndex];
			if (Input.InputName != FName(ReferenceInputNames[SourceIndex])
				|| Input.Input.Expression != Shader->Inputs[SourceIndex].Input.Expression
				|| Input.Input.OutputIndex != Shader->Inputs[SourceIndex].Input.OutputIndex) return false;
		}
		for (int32 OutputIndex = 0; OutputIndex < 4; ++OutputIndex)
			if (Shader->Inputs[40 + OutputIndex].Input.Expression != Shared
				|| Shader->Inputs[40 + OutputIndex].Input.OutputIndex != OutputIndex) return false;
		const ECustomMaterialOutputType OutputTypes[] = { CMOT_Float4, CMOT_Float4, CMOT_Float3 };
		for (int32 OutputIndex = 0; OutputIndex < 3; ++OutputIndex)
			if (Shared->AdditionalOutputs[OutputIndex].OutputName != FName(ReferenceInputNames[41 + OutputIndex])
				|| Shared->AdditionalOutputs[OutputIndex].OutputType != OutputTypes[OutputIndex]) return false;
		const TCHAR* HeadNames[] = { TEXT("MMDHeadForward"), TEXT("MMDHeadRight"), TEXT("MMDHeadPositionRadius") };
		for (int32 HeadIndex = 0; HeadIndex < UE_ARRAY_COUNT(HeadNames); ++HeadIndex)
		{
			const UMaterialExpressionVectorParameter* Head = Cast<UMaterialExpressionVectorParameter>(Shader->Inputs[30 + HeadIndex].Input.Expression);
			if (!Head || Head->ParameterName != FName(HeadNames[HeadIndex]) || Shader->Inputs[30 + HeadIndex].Input.OutputIndex != 5) return false;
		}
		const int32 TextureIndices[] = { 27, 33, 35, 37 };
		const TCHAR* TextureNames[] = { TEXT("HairShiftTexture"), TEXT("HairDetailNormal"), TEXT("ClothDetailNormal"), TEXT("ClothDetailData") };
		for (int32 TextureIndex = 0; TextureIndex < UE_ARRAY_COUNT(TextureIndices); ++TextureIndex)
		{
			const UMaterialExpressionTextureObjectParameter* Texture = Cast<UMaterialExpressionTextureObjectParameter>(Shader->Inputs[TextureIndices[TextureIndex]].Input.Expression);
			if (!Texture || !Texture->Texture || Texture->ParameterName != FName(TextureNames[TextureIndex])) return false;
			const EMaterialSamplerType ExpectedType = TextureIndex == 0 ? SAMPLERTYPE_LinearGrayscale
				: (TextureIndex == 3 ? SAMPLERTYPE_LinearColor : SAMPLERTYPE_Normal);
			if (Texture->SamplerType != ExpectedType) return false;
		}
		const int32 ScalarIndices[] = { 28, 29, 34, 36, 38, 39 };
		const TCHAR* ScalarNames[] = { TEXT("HairShiftStrength"), TEXT("HairShiftScale"), TEXT("HairDetailStrength"),
			TEXT("ClothDetailStrength"), TEXT("ClothDetailScale"), TEXT("HairDetailScale") };
		for (int32 ScalarIndex = 0; ScalarIndex < UE_ARRAY_COUNT(ScalarIndices); ++ScalarIndex)
		{
			const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Shader->Inputs[ScalarIndices[ScalarIndex]].Input.Expression);
			if (!Scalar || Scalar->ParameterName != FName(ScalarNames[ScalarIndex])) return false;
		}
		return true;
	}

	UMaterial* DuplicateReferenceMaterial(UMaterial* Source, UPackage* Package, FName Name, int32 FamilyIndex, const FString& ShaderCode)
	{
		UMaterial* Material = DuplicateObject<UMaterial>(Source, Package, Name);
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		UMaterialExpressionCustom* Shader = Data ? Cast<UMaterialExpressionCustom>(Data->EmissiveColor.Expression) : nullptr;
		if (!Shader || Shader->Inputs.Num() != 23 || !Shader->Code.Contains(TEXT("TMMDStageToon.usf")))
			return nullptr;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (Expression && Expression->GetTypedOuter<UMaterial>() != Material)
				return nullptr;
		}
		Material->ClearFlags(RF_Transient);
		Material->SetFlags(RF_Public | RF_Standalone);
		Material->StateId = FGuid::NewGuid();
		Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (Expression)
				Expression->Material = Material;
		}
		Shader->Code = ShaderCode;
		Shader->Description = TEXT("MMD reference character lighting");

		// 风格是父材质的编译常量；角色分类由运行时 MID 设置，不创建静态变体。
		UMaterialExpressionConstant* Style = NewObject<UMaterialExpressionConstant>(Material);
		Style->Material = Material;
		Style->R = static_cast<float>(FamilyIndex);
		Material->GetExpressionCollection().Expressions.Add(Style);
		FCustomInput& StyleInput = Shader->Inputs.AddDefaulted_GetRef();
		StyleInput.InputName = TEXT("style_family");
		StyleInput.Input.Connect(0, Style);

		UMaterialExpressionScalarParameter* Role = NewObject<UMaterialExpressionScalarParameter>(Material);
		Role->Material = Material;
		Role->ParameterName = TEXT("MaterialRole");
		Role->ExpressionGUID = FGuid::NewGuid();
		Role->Group = TEXT("Material role");
		Role->DefaultValue = 0.0f;
		Role->SliderMin = 0.0f;
		Role->SliderMax = 5.0f;
		Material->GetExpressionCollection().Expressions.Add(Role);
		FCustomInput& RoleInput = Shader->Inputs.AddDefaulted_GetRef();
		RoleInput.InputName = TEXT("material_role");
		RoleInput.Input.Connect(0, Role);
		return Material;
	}

	bool SaveReferenceMaterial(UMaterial* Material, const FString& PackageName, FString& OutError, bool bNewAsset)
	{
		Material->PostEditChange();
		Material->MarkPackageDirty();
		if (bNewAsset)
			FAssetRegistryModule::AssetCreated(Material);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Material->GetOutermost(), Material, *Filename, Args))
		{
			OutError = FString::Printf(TEXT("Cannot save reference material: %s"), *PackageName);
			return false;
		}
		return true;
	}
}

bool EnsureMMDReferenceCharacterMaterials(const FMMDStageMaterialSet& BaseMaterials,
	FMMDReferenceMaterialSet& OutMaterials, FString& OutError)
{
	OutMaterials = FMMDReferenceMaterialSet();
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("Reference materials must be created on the game thread");
		return false;
	}
	FString ShaderCode;
	FString ShaderSignature;
	if (!BuildReferenceShaderCode(ShaderCode, ShaderSignature, OutError))
		return false;
	UTexture2D* HairShiftTexture = nullptr;
	if (!EnsureGenericHairShiftTexture(HairShiftTexture, OutError)) return false;
	UTexture2D* HairNormal = nullptr;
	UTexture2D* ClothNormal = nullptr;
	UTexture2D* ClothData = nullptr;
	if (!EnsureMMDReferenceDetailTextures(HairNormal, ClothNormal, ClothData, OutError)) return false;
	UMaterial* Sources[] = { Cast<UMaterial>(BaseMaterials.CharacterOpaque), Cast<UMaterial>(BaseMaterials.CharacterMasked),
		Cast<UMaterial>(BaseMaterials.CharacterTranslucent) };
	const EBlendMode BlendModes[] = { BLEND_Opaque, BLEND_Masked, BLEND_Translucent };
	const TCHAR* BlendNames[] = { TEXT("Opaque"), TEXT("Masked"), TEXT("Translucent") };
	const TCHAR* FamilyNames[] = { TEXT("Genshin"), TEXT("Wuthering"), TEXT("Endfield"), TEXT("SilverCity") };
	for (int32 BlendIndex = 0; BlendIndex < UE_ARRAY_COUNT(Sources); ++BlendIndex)
	{
		if (!Sources[BlendIndex] || Sources[BlendIndex]->BlendMode != BlendModes[BlendIndex])
		{
			OutError = TEXT("Reference materials require the three base stage character parent materials");
			return false;
		}
	}
	for (int32 FamilyIndex = 0; FamilyIndex < UE_ARRAY_COUNT(FamilyNames); ++FamilyIndex)
	{
		FMMDReferenceMaterialFamily& Family = OutMaterials.Families[FamilyIndex];
		UMaterialInterface** Outputs[] = { &Family.Opaque, &Family.Masked, &Family.Translucent };
		for (int32 BlendIndex = 0; BlendIndex < UE_ARRAY_COUNT(BlendNames); ++BlendIndex)
		{
			const FString Name = FString::Printf(TEXT("M_MMD_Reference_%s_%s"), FamilyNames[FamilyIndex], BlendNames[BlendIndex]);
			const FString PackageName = TEXT("/Ue5MMDTools/Stages/Materials/") + Name;
			const FString ObjectPath = PackageName + TEXT(".") + Name;
			const bool bSaved = FPackageName::DoesPackageExist(PackageName);
			UMaterial* Material = bSaved ? LoadObject<UMaterial>(nullptr, *ObjectPath) : FindObject<UMaterial>(nullptr, *ObjectPath);
			if (!Material && bSaved)
			{
				OutError = FString::Printf(TEXT("Cannot load existing reference material: %s"), *ObjectPath);
				return false;
			}
			if (!Material)
				Material = DuplicateReferenceMaterial(Sources[BlendIndex], CreatePackage(*PackageName), FName(*Name), FamilyIndex, ShaderCode);
			bool bGraphChanged = false;
			if (!UpgradeReferenceEnvironment(Material, bGraphChanged) || !UpgradeReferenceHairShift(Material, HairShiftTexture, bGraphChanged)
				|| !UpgradeReferenceSurfaceDetail(Material, HairNormal, ClothNormal, ClothData, bGraphChanged)
				|| !ValidateReferenceMaterial(Material, FamilyIndex, BlendModes[BlendIndex]))
			{
				OutError = FString::Printf(TEXT("Reference material graph or usage is invalid: %s"), *ObjectPath);
				return false;
			}
			UMaterialExpressionCustom* Shader = CastChecked<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression);
			UMaterialExpressionScalarParameter* Role = CastChecked<UMaterialExpressionScalarParameter>(Shader->Inputs[24].Input.Expression);
			if (Role->SliderMax == 4.0f)
			{
				Role->SliderMax = 5.0f;
				bGraphChanged = true;
			}
			const bool bSourceChanged = !Shader->Code.Contains(ShaderSignature);
			if (bSourceChanged || bGraphChanged)
			{
				// 只更新已通过图契约检查的 Reference 父材质；相同源码签名完全不重存。
				Material->GetOutermost()->FullyLoad();
				Material->Modify();
				Shader->Modify();
				Shader->Code = ShaderCode;
				Material->StateId = FGuid::NewGuid();
			}
			if ((!bSaved || bSourceChanged || bGraphChanged) && !SaveReferenceMaterial(Material, PackageName, OutError, !bSaved))
				return false;
			*Outputs[BlendIndex] = Material;
		}
	}
	return true;
}
