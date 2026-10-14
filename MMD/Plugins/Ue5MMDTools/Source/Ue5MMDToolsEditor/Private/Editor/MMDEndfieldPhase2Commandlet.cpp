#include "MMDEndfieldPhase2Commandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSkyLightEnvMapSample.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageRenderPreset.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* Phase2AllowedPackages[] = {
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Opaque"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Masked"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldPhase2/M_MMD_EndfieldPhase2_Translucent"),
		TEXT("/Ue5MMDTools/Stages/Presets/EndfieldPhase2/DA_Render_EndfieldPhase2") };
	const TCHAR* DedicatedCore = TEXT("/Plugin/Ue5MMDTools/Core/TMMDEndfieldPhase2BRDF.ush");
	const TCHAR* LegacyCore = TEXT("/Plugin/Ue5MMDTools/Core/TMMDReferenceBRDF.ush");
	const TCHAR* DedicatedCharacter = TEXT("/Plugin/Ue5MMDTools/TMMDShader/TMMDEndfieldPhase2Character.usf");
	const TCHAR* InputNames[] = {
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

	FString Phase2ObjectPath(const FString& Package)
	{
		return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	}

	bool SaveDedicatedAsset(UObject* Asset, TArray<TSharedPtr<FJsonValue>>& Written, FString& Error)
	{
		const FString Package = Asset ? Asset->GetOutermost()->GetName() : FString();
		bool bAllowed = false;
		for (const TCHAR* Allowed : Phase2AllowedPackages) bAllowed |= Package == Allowed;
		if (!bAllowed) { Error = TEXT("Refused a save outside the exact four-package EndfieldPhase2 whitelist"); return false; }
		const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		Asset->SetFlags(RF_Public | RF_Standalone);
		Asset->ClearFlags(RF_Transient);
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
		{ Error = TEXT("Cannot save dedicated asset: ") + Package; return false; }
		Written.Add(MakeShared<FJsonValueString>(Package));
		return true;
	}

	bool Phase2BuildSignature(FString& Signature, FString& Error)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		if (!Plugin) { Error = TEXT("Ue5MMDTools plugin is unavailable"); return false; }
		FString Bundle;
		for (const TCHAR* Path : { TEXT("Shaders/TMMDShader/TMMDEndfieldPhase2Character.usf"),
			TEXT("Shaders/Core/MMDToonLighting.ush"), TEXT("Shaders/Core/TMMDEndfieldPhase2BRDF.ush") })
		{
			FString Code;
			if (!FFileHelper::LoadFileToString(Code, *FPaths::Combine(Plugin->GetBaseDir(), Path)) || Code.IsEmpty())
			{ Error = TEXT("Cannot read dedicated dependency: ") + FString(Path); return false; }
			Bundle += FString(Path) + TEXT("\n") + Code + TEXT("\n");
		}
		FTCHARToUTF8 Utf8(*Bundle);
		Signature = TEXT("// EndfieldPhase2源码签名：") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		return true;
	}

	bool CheckGraph(UMaterial* Material, EBlendMode Blend, bool bDedicated, FString& Error)
	{
		UMaterialExpressionCustom* Shader = Material ? Cast<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression) : nullptr;
		if (!Material || Material->BlendMode != Blend || Material->MaterialDomain != MD_Surface || !Material->TwoSided
			|| Material->GetShadingModels().GetFirstShadingModel() != MSM_Unlit
			|| !Material->GetUsageByFlag(MATUSAGE_SkeletalMesh) || !Material->GetUsageByFlag(MATUSAGE_MorphTargets)
			|| !Shader || Shader->OutputType != CMOT_Float3 || Shader->Inputs.Num() != 44)
		{ Error = TEXT("Dedicated template blend/usage/main graph is invalid"); return false; }
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(InputNames); ++Index)
			if (Shader->Inputs[Index].InputName != FName(InputNames[Index]) || !Shader->Inputs[Index].Input.Expression)
			{ Error = TEXT("Dedicated template input name/order/connection is invalid"); return false; }
		if (!Material->GetEditorOnlyData()->Opacity.Expression || !Material->GetEditorOnlyData()->OpacityMask.Expression)
		{ Error = TEXT("Dedicated opacity connections are missing"); return false; }
		const UMaterialExpressionConstant* Style = Cast<UMaterialExpressionConstant>(Shader->Inputs[23].Input.Expression);
		const UMaterialExpressionScalarParameter* Role = Cast<UMaterialExpressionScalarParameter>(Shader->Inputs[24].Input.Expression);
		const UMaterialExpressionCustom* Shared = Cast<UMaterialExpressionCustom>(Shader->Inputs[40].Input.Expression);
		if (!Style || !FMath::IsNearlyEqual(Style->R, 2.0f) || !Role || Role->ParameterName != TEXT("MaterialRole")
			|| !Shared || Shared->Inputs.Num() != 17 || Shared->AdditionalOutputs.Num() != 3 || Shared->OutputType != CMOT_Float3
			|| !Shared->Code.Contains(TEXT("PrepareSurfaceDetail")))
		{ Error = TEXT("Dedicated template style/role/shared surface graph is invalid"); return false; }
		const int32 SharedInputs[] = { 24, 27, 28, 29, 30, 31, 32, 17, 23, 10, 33, 34, 35, 36, 37, 38, 39 };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(SharedInputs); ++Index)
		{
			const FCustomInput& Input = Shared->Inputs[Index];
			const FCustomInput& Main = Shader->Inputs[SharedInputs[Index]];
			if (Input.InputName != Main.InputName || Input.Input.Expression != Main.Input.Expression || Input.Input.OutputIndex != Main.Input.OutputIndex)
			{ Error = TEXT("Dedicated shared input does not match its source"); return false; }
		}
		for (int32 Index = 0; Index < 4; ++Index)
			if (Shader->Inputs[40 + Index].Input.Expression != Shared || Shader->Inputs[40 + Index].Input.OutputIndex != Index)
			{ Error = TEXT("Dedicated shared outputs do not match the lighting inputs"); return false; }
		const ECustomMaterialOutputType OutputTypes[] = { CMOT_Float4, CMOT_Float4, CMOT_Float3 };
		for (int32 Index = 0; Index < 3; ++Index)
			if (Shared->AdditionalOutputs[Index].OutputName != FName(InputNames[41 + Index]) || Shared->AdditionalOutputs[Index].OutputType != OutputTypes[Index])
			{ Error = TEXT("Dedicated shared output name/type is invalid"); return false; }
		const UMaterialExpressionSkyLightEnvMapSample* Environment = Cast<UMaterialExpressionSkyLightEnvMapSample>(Shader->Inputs[25].Input.Expression);
		const UMaterialExpressionReflectionVectorWS* Reflection = Environment ? Cast<UMaterialExpressionReflectionVectorWS>(Environment->Direction.Expression) : nullptr;
		const UMaterialExpressionComponentMask* SpecularNormal = Reflection ? Cast<UMaterialExpressionComponentMask>(Reflection->CustomWorldNormal.Expression) : nullptr;
		const UMaterialExpressionComponentMask* SurfaceRoughness = Environment ? Cast<UMaterialExpressionComponentMask>(Environment->Roughness.Expression) : nullptr;
		if (!Reflection || !Reflection->bNormalizeCustomWorldNormal || !SpecularNormal || !SpecularNormal->R || !SpecularNormal->G
			|| !SpecularNormal->B || SpecularNormal->A || !SurfaceRoughness || SurfaceRoughness->R || SurfaceRoughness->G
			|| SurfaceRoughness->B || !SurfaceRoughness->A || SpecularNormal->Input.Expression != Shared || SpecularNormal->Input.OutputIndex != 1
			|| SurfaceRoughness->Input.Expression != Shared || SurfaceRoughness->Input.OutputIndex != 1)
		{ Error = TEXT("Dedicated environment does not consume the shared normal/roughness"); return false; }
		int32 RoughnessNodes = 0, SurfaceNodes = 0;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			if (Expression && Expression->GetTypedOuter<UMaterial>() != Material)
			{ Error = TEXT("Dedicated clone still owns expressions from a different material"); return false; }
			if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
			{
				if (Custom->Code.Contains(TEXT("GetRoleRoughness")))
				{
					++RoughnessNodes;
					if (Custom->OutputType != CMOT_Float1 || Custom->Inputs.Num() != 4
						|| (bDedicated && !Custom->Code.Contains(DedicatedCore)))
					{ Error = TEXT("Dedicated role roughness node is invalid"); return false; }
				}
				if (Custom->Code.Contains(TEXT("PrepareSurfaceDetail"))) ++SurfaceNodes;
				if (bDedicated && (Custom->Code.Contains(LegacyCore) || Custom->Code.Contains(TEXT("TMMDReferenceCharacter.usf"))))
				{ Error = TEXT("Dedicated graph still includes a legacy character/core shader"); return false; }
			}
		}
		if (RoughnessNodes != 1 || SurfaceNodes != 1 || (bDedicated && (!Shader->Code.Contains(DedicatedCharacter) || !Shared->Code.Contains(DedicatedCore))))
		{ Error = TEXT("Dedicated shader dependencies are incomplete"); return false; }
		return true;
	}

	bool AllCustomSignaturesMatch(UMaterial* Material, const FString& Signature)
	{
		int32 Nodes = 0;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
			if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
				if (Custom->Code.Contains(DedicatedCore) || Custom->Code.Contains(DedicatedCharacter))
				{ ++Nodes; if (!Custom->Code.StartsWith(Signature + TEXT("\n"))) return false; }
		return Nodes == 3;
	}

	void ConfigureDedicatedPreset(UMMDStageRenderPreset* Preset, const TArray<TStrongObjectPtr<UMaterial>>& Parents)
	{
		Preset->DisplayName = FText::FromString(TEXT("终末地人物（二期）"));
		Preset->Theme = EMMDStageTheme::SakuraTheater;
		Preset->bOverrideStageTheme = false;
		Preset->CharacterMaterialOpaque = Parents[0].Get();
		Preset->CharacterMaterialMasked = Parents[1].Get();
		Preset->CharacterMaterialTranslucent = Parents[2].Get();
		// 首轮固定已实测 R4 参数作为人物验证基线，资源引用来自只读舞台模板。
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
		Preset->ColorGradingGamma = 1.0f;
		Preset->ColorGradingGain = FLinearColor::White;
		Preset->WhiteTemperature = 6500.0f;
		Preset->FilmToe = 0.25f;
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
		Preset->ShadowSaturation = 0.90f;
		Preset->OutlinePixelWidth = 0.90f;
	}

	bool PresetMatches(const UMMDStageRenderPreset* Preset, const UMMDStageRenderPreset* Expected)
	{
		for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
			if (!It->Identical_InContainer(Preset, Expected)) return false;
		return true;
	}

	bool RewriteDedicatedCode(UMaterial* Material, const FString& Signature, FString& Error)
	{
		UMaterialExpressionCustom* Main = CastChecked<UMaterialExpressionCustom>(Material->GetEditorOnlyData()->EmissiveColor.Expression);
		int32 CoreNodes = 0;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
			if (!Custom || Custom == Main) continue;
			if (Custom->Code.Contains(LegacyCore) || Custom->Code.Contains(DedicatedCore))
			{
				FString Body = Custom->Code;
				if (Body.StartsWith(TEXT("// EndfieldPhase2源码签名：")))
				{ int32 Newline = INDEX_NONE; if (Body.FindChar(TEXT('\n'), Newline)) Body = Body.Mid(Newline + 1); }
				Body.ReplaceInline(LegacyCore, DedicatedCore);
				Body.ReplaceInline(TEXT("TMMDReferenceBRDF"), TEXT("TMMDEndfieldPhase2BRDF"));
				Body.ReplaceInline(TEXT("TMMDReferenceSurfaceDetail"), TEXT("TMMDEndfieldPhase2SurfaceDetail"));
				Custom->Code = Signature + TEXT("\n") + Body;
				++CoreNodes;
			}
		}
		if (CoreNodes != 2) { Error = TEXT("Exactly surface detail and role roughness must use the dedicated core"); return false; }
		Main->Code = Signature + TEXT("\n#include \"") + DedicatedCharacter + TEXT("\"\nreturn 0;");
		Main->Description = TEXT("EndfieldPhase2 专属人物着色");
		Material->StateId = FGuid::NewGuid();
		return true;
	}
}

UMMDEndfieldPhase2Commandlet::UMMDEndfieldPhase2Commandlet()
{
	IsClient = true;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMMDEndfieldPhase2Commandlet::Main(const FString& Params)
{
	const bool bGenerate = FParse::Param(*Params, TEXT("Generate"));
	FString ReportPath;
	if (!FParse::Value(*Params, TEXT("Report="), ReportPath) || ReportPath.IsEmpty()) return 1;
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Written, Materials, Assets;
	TArray<TStrongObjectPtr<UMaterial>> Parents;
	FString Signature, Error;
	const TCHAR* Blends[] = { TEXT("Opaque"), TEXT("Masked"), TEXT("Translucent") };
	const EBlendMode Modes[] = { BLEND_Opaque, BLEND_Masked, BLEND_Translucent };
	if (!Phase2BuildSignature(Signature, Error)) Errors.Add(Error);
	for (int32 Index = 0; Index < 3 && Errors.IsEmpty(); ++Index)
	{
		const FString Package = Phase2AllowedPackages[Index];
		const bool bExists = FPackageName::DoesPackageExist(Package);
		UMaterial* Material = bExists ? LoadObject<UMaterial>(nullptr, *Phase2ObjectPath(Package)) : nullptr;
		if (bExists && (!Material || !CheckGraph(Material, Modes[Index], true, Error)))
		{ Errors.Add(TEXT("Existing dedicated asset is invalid; refusing replacement: ") + Package + TEXT(" ") + Error); break; }
		if (!Material)
		{
			if (!bGenerate) { Errors.Add(TEXT("Dedicated asset is missing: ") + Package); break; }
			const FString Template = FString(TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_Reference_Endfield_")) + Blends[Index];
			UMaterial* Source = LoadObject<UMaterial>(nullptr, *Phase2ObjectPath(Template));
			if (!CheckGraph(Source, Modes[Index], false, Error)) { Errors.Add(Template + TEXT(" ") + Error); break; }
			Material = DuplicateObject<UMaterial>(Source, CreatePackage(*Package), *FPackageName::GetLongPackageAssetName(Package));
			if (!Material) { Errors.Add(TEXT("Cannot duplicate dedicated material: ") + Package); break; }
			FAssetRegistryModule::AssetCreated(Material);
		}
		if (!bExists || !AllCustomSignaturesMatch(Material, Signature))
		{
			if (!bGenerate) { Errors.Add(TEXT("Dedicated source signature is stale: ") + Package); break; }
			if (!RewriteDedicatedCode(Material, Signature, Error) || !CheckGraph(Material, Modes[Index], true, Error)
				|| !AllCustomSignaturesMatch(Material, Signature))
			{ Errors.Add(Package + TEXT(" ") + Error); break; }
			Material->PostEditChange();
			if (!SaveDedicatedAsset(Material, Written, Error)) { Errors.Add(Error); break; }
		}
		Parents.Emplace(Material);
	}
	TStrongObjectPtr<UMMDStageRenderPreset> Preset;
	if (Errors.IsEmpty() && Parents.Num() == 3)
	{
		const FString Package = Phase2AllowedPackages[3];
		const bool bExists = FPackageName::DoesPackageExist(Package);
		Preset.Reset(bExists ? LoadObject<UMMDStageRenderPreset>(nullptr, *Phase2ObjectPath(Package)) : nullptr);
		if (bExists && (!Preset.IsValid() || Preset->CharacterMaterialOpaque != Parents[0].Get()
			|| Preset->CharacterMaterialMasked != Parents[1].Get() || Preset->CharacterMaterialTranslucent != Parents[2].Get()))
			Errors.Add(TEXT("Existing dedicated preset has different bindings; refusing replacement"));
		else
		{
			UMMDStageRenderPreset* Source = LoadObject<UMMDStageRenderPreset>(nullptr,
				TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater.DA_Render_SakuraTheater"));
			if (!Source) Errors.Add(TEXT("Sakura validation scene settings are missing"));
			else
			{
				TStrongObjectPtr<UMMDStageRenderPreset> Expected(DuplicateObject<UMMDStageRenderPreset>(Source, GetTransientPackage()));
				if (!Expected.IsValid()) Errors.Add(TEXT("Cannot construct the dedicated configuration"));
				else
				{
					ConfigureDedicatedPreset(Expected.Get(), Parents);
					if (!Preset.IsValid() || !PresetMatches(Preset.Get(), Expected.Get()))
					{
						if (!bGenerate) Errors.Add(TEXT("Dedicated preset is missing or has stale configuration"));
						else
						{
							if (!Preset.IsValid()) Preset.Reset(DuplicateObject<UMMDStageRenderPreset>(Expected.Get(), CreatePackage(*Package), TEXT("DA_Render_EndfieldPhase2")));
							if (!Preset.IsValid()) Errors.Add(TEXT("Cannot duplicate the dedicated preset"));
							else
							{
								for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
									It->CopyCompleteValue_InContainer(Preset.Get(), Expected.Get());
								if (!bExists) FAssetRegistryModule::AssetCreated(Preset.Get());
								if (!SaveDedicatedAsset(Preset.Get(), Written, Error)) Errors.Add(Error);
							}
						}
					}
				}
			}
		}
	}
	if (Errors.IsEmpty() && GShaderCompilingManager && FApp::CanEverRender() && !GUsingNullRHI)
	{
		for (const TStrongObjectPtr<UMaterial>& Parent : Parents) Parent->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
		FAssetCompilingManager::Get().FinishAllCompilation();
		GShaderCompilingManager->FinishAllCompilation();
		FlushRenderingCommands();
		for (const TStrongObjectPtr<UMaterial>& Parent : Parents)
		{
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> CompileErrors;
			const FMaterialResource* Resource = Parent->GetMaterialResource(GMaxRHIShaderPlatform);
			const FMaterialShaderMap* Map = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
			if (Resource) for (const FString& Failure : Resource->GetCompileErrors()) CompileErrors.Add(MakeShared<FJsonValueString>(Failure));
			if (!Resource || !Resource->IsCompilationFinished() || !Map || !Map->CompiledSuccessfully()
				|| !Map->IsCompilationFinalized() || !Map->IsValidForRendering(false))
				CompileErrors.Add(MakeShared<FJsonValueString>(TEXT("Dedicated material resource is not ready")));
			Item->SetStringField(TEXT("asset"), Parent->GetPathName());
			Item->SetStringField(TEXT("source_signature"), Signature);
			Item->SetNumberField(TEXT("main_inputs"), 44);
			Item->SetBoolField(TEXT("passed"), CompileErrors.IsEmpty());
			Item->SetArrayField(TEXT("errors"), CompileErrors);
			Materials.Add(MakeShared<FJsonValueObject>(Item));
			if (!CompileErrors.IsEmpty()) Errors.Add(TEXT("Dedicated SM6 compile failed: ") + Parent->GetPathName());
		}
	}
	else if (Errors.IsEmpty()) Errors.Add(TEXT("Dedicated validation requires a rendering RHI and shader manager"));
	for (const TStrongObjectPtr<UMaterial>& Parent : Parents)
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset"), Parent->GetPathName());
		Item->SetStringField(TEXT("type"), TEXT("Material"));
		Item->SetBoolField(TEXT("passed"), Errors.IsEmpty() && AllCustomSignaturesMatch(Parent.Get(), Signature)
			&& FPackageName::DoesPackageExist(Parent->GetOutermost()->GetName()));
		Assets.Add(MakeShared<FJsonValueObject>(Item));
	}
	if (Preset.IsValid())
	{
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("asset"), Preset->GetPathName());
		Item->SetStringField(TEXT("type"), TEXT("RenderPreset"));
		Item->SetBoolField(TEXT("passed"), Errors.IsEmpty() && FPackageName::DoesPackageExist(Preset->GetOutermost()->GetName()));
		Assets.Add(MakeShared<FJsonValueObject>(Item));
	}
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), Errors.IsEmpty() && Materials.Num() == 3 && Preset.IsValid());
	Report->SetBoolField(TEXT("generated"), bGenerate);
	Report->SetStringField(TEXT("target"), TEXT("EndfieldPhase2"));
	Report->SetStringField(TEXT("shader_platform"), LexToString(GMaxRHIShaderPlatform));
	Report->SetStringField(TEXT("preset"), Preset.IsValid() ? Preset->GetPathName() : TEXT("missing"));
	Report->SetStringField(TEXT("source_signature"), Signature);
	Report->SetStringField(TEXT("scene_settings_source"), TEXT("Explicit Revision4 profile; stage resource references loaded from read-only Sakura template"));
	Report->SetArrayField(TEXT("written_packages"), Written);
	Report->SetArrayField(TEXT("materials"), Materials);
	Report->SetArrayField(TEXT("assets"), Assets);
	Report->SetNumberField(TEXT("failed"), Errors.Num());
	Report->SetStringField(TEXT("profile_id"), TEXT("Revision4-character-baseline-1"));
	Report->SetBoolField(TEXT("foreground_opened"), false);
	TArray<TSharedPtr<FJsonValue>> JsonErrors;
	for (const FString& Failure : Errors) { JsonErrors.Add(MakeShared<FJsonValueString>(Failure)); UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldPhase2] %s"), *Failure); }
	Report->SetArrayField(TEXT("errors"), JsonErrors);
	FString Json;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	const bool bSaved = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
		&& FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("[MMDEndfieldPhase2] materials=%d writes=%d success=%d"), Materials.Num(), Written.Num(), Errors.IsEmpty() && bSaved);
	return Errors.IsEmpty() && bSaved && Materials.Num() == 3 && Preset.IsValid() ? 0 : 1;
}
