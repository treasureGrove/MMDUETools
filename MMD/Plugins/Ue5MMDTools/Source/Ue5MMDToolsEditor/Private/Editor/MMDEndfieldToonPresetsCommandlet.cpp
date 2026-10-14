// 原生 Toon 资产与旧 Endfield 资产独立；只有 -Generate 且三套材质真实编译成功后才写盘。
// 启动参数需开启 r.Substrate=True；ToonUnifiedDiffuse 是可选实验开关，默认 0。
// 用法：-run=MMDEndfieldToonPresets [-Generate] -Report=<JSON路径> -AllowCommandletRendering -d3d12
#include "MMDEndfieldToonPresetsCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/ToonProfile.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "Materials/MaterialExpressionMMDEndfieldNativePayload.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
#include "RHI.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "ShaderCore.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Internationalization/Regex.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* EndfieldToonAllowedPackages[] = {
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Opaque"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Masked"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Translucent"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Generic"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Skin"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Hair"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Cloth"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Metal"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/TP_EndfieldV2_Eye"),
		TEXT("/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2"),
		TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/Profiles/T_EndfieldV2_OffsetLinear") };
	constexpr const TCHAR* EndfieldToonTemplatePath = TEXT("/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield.DA_Render_Endfield");
	constexpr const TCHAR* EndfieldToonGraphVersion = TEXT("EndfieldV2NativeToonGraph=4");
	constexpr int32 EndfieldToonMaterialCount = 3;
	constexpr int32 EndfieldToonProfileCount = 6;
	constexpr int32 EndfieldToonGradientWidth = 256;
	constexpr int32 EndfieldToonGradientHeight = 4;

	FString EndfieldToonObjectPath(const FString& Package)
	{
		return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
	}

	FString EndfieldToonHash(const FString& Text)
	{
		FTCHARToUTF8 Utf8(*Text);
		return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}

	bool EndfieldToonAppendShaderSource(const FString& VirtualPath, FString& Bundle, TSet<FString>& Visited)
	{
		if (Visited.Contains(VirtualPath)) return true;
		Visited.Add(VirtualPath);
		const FString Filename = GetShaderSourceFilePath(VirtualPath);
		FString Source;
		if (Filename.IsEmpty() || !FFileHelper::LoadFileToString(Source, *Filename)) return false;
		Bundle += TEXT("\ninclude=") + VirtualPath + TEXT(":") + EndfieldToonHash(Source);
		FRegexMatcher Includes(FRegexPattern(TEXT("#[ \\t]*include[ \\t]*[\"<]([^\">]+)[\">]")), Source);
		while (Includes.FindNext())
		{
			FString IncludedPath = Includes.GetCaptureGroup(1);
			if (!IncludedPath.StartsWith(TEXT("/")))
				IncludedPath = FPaths::Combine(FPaths::GetPath(VirtualPath), IncludedPath);
			FPaths::CollapseRelativeDirectories(IncludedPath);
			if (!EndfieldToonAppendShaderSource(IncludedPath, Bundle, Visited)) return false;
		}
		return true;
	}

	FString EndfieldToonCVar(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetString() : TEXT("<missing>");
	}

	bool EndfieldToonCurveNumberMatches(double Actual, double Expected, const FString& Field, TArray<FString>& Diagnostics)
	{
		if (Actual == Expected) return true;
		Diagnostics.Add(FString::Printf(TEXT("%s: actual=%.9g expected=%.9g"), *Field, Actual, Expected));
		return false;
	}

	bool EndfieldToonRichCurveMatches(const FRichCurve& Actual, const FRichCurve& Expected, const FString& Field,
		TArray<FString>& Diagnostics)
	{
		// FIndexedCurve 的 KeyHandlesToIndices 是 transient；反射 Identical 会误比进程内句柄。
		// FRichCurveKey::operator== 还会略过线性段切线与切线权重，因此逐项精确比序列化数据。
		if (!EndfieldToonCurveNumberMatches(Actual.DefaultValue, Expected.DefaultValue, Field + TEXT(".DefaultValue"), Diagnostics)
			|| !EndfieldToonCurveNumberMatches(static_cast<int32>(Actual.PreInfinityExtrap), static_cast<int32>(Expected.PreInfinityExtrap),
				Field + TEXT(".PreInfinityExtrap"), Diagnostics)
			|| !EndfieldToonCurveNumberMatches(static_cast<int32>(Actual.PostInfinityExtrap), static_cast<int32>(Expected.PostInfinityExtrap),
				Field + TEXT(".PostInfinityExtrap"), Diagnostics)) return false;
		const TArray<FRichCurveKey>& ActualKeys = Actual.GetConstRefOfKeys();
		const TArray<FRichCurveKey>& ExpectedKeys = Expected.GetConstRefOfKeys();
		if (!EndfieldToonCurveNumberMatches(ActualKeys.Num(), ExpectedKeys.Num(), Field + TEXT(".KeyCount"), Diagnostics)) return false;
		for (int32 Index = 0; Index < ActualKeys.Num(); ++Index)
		{
			const FRichCurveKey& ActualKey = ActualKeys[Index];
			const FRichCurveKey& ExpectedKey = ExpectedKeys[Index];
			const FString Key = Field + FString::Printf(TEXT(".Keys[%d]"), Index);
			if (!EndfieldToonCurveNumberMatches(ActualKey.Time, ExpectedKey.Time, Key + TEXT(".Time"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(ActualKey.Value, ExpectedKey.Value, Key + TEXT(".Value"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(static_cast<int32>(ActualKey.InterpMode), static_cast<int32>(ExpectedKey.InterpMode),
					Key + TEXT(".InterpMode"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(static_cast<int32>(ActualKey.TangentMode), static_cast<int32>(ExpectedKey.TangentMode),
					Key + TEXT(".TangentMode"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(static_cast<int32>(ActualKey.TangentWeightMode), static_cast<int32>(ExpectedKey.TangentWeightMode),
					Key + TEXT(".TangentWeightMode"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(ActualKey.ArriveTangent, ExpectedKey.ArriveTangent, Key + TEXT(".ArriveTangent"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(ActualKey.ArriveTangentWeight, ExpectedKey.ArriveTangentWeight,
					Key + TEXT(".ArriveTangentWeight"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(ActualKey.LeaveTangent, ExpectedKey.LeaveTangent, Key + TEXT(".LeaveTangent"), Diagnostics)
				|| !EndfieldToonCurveNumberMatches(ActualKey.LeaveTangentWeight, ExpectedKey.LeaveTangentWeight,
					Key + TEXT(".LeaveTangentWeight"), Diagnostics)) return false;
		}
		return true;
	}

	bool EndfieldToonSettingsMatch(const FToonProfileStruct& Actual, const FToonProfileStruct& Expected,
		TArray<FString>& Diagnostics)
	{
		bool bMatches = true;
		for (TFieldIterator<FProperty> It(FToonProfileStruct::StaticStruct()); It; ++It)
		{
			const FString Field = It->GetName();
			const FStructProperty* Struct = CastField<FStructProperty>(*It);
			if (Struct && Struct->Struct == FRuntimeCurveLinearColor::StaticStruct())
			{
				const FRuntimeCurveLinearColor& ActualRamp = *Struct->ContainerPtrToValuePtr<FRuntimeCurveLinearColor>(&Actual);
				const FRuntimeCurveLinearColor& ExpectedRamp = *Struct->ContainerPtrToValuePtr<FRuntimeCurveLinearColor>(&Expected);
				if (ActualRamp.ExternalCurve != ExpectedRamp.ExternalCurve)
				{
					Diagnostics.Add(Field + TEXT(".ExternalCurve: actual=") + GetPathNameSafe(ActualRamp.ExternalCurve.Get())
						+ TEXT(" expected=") + GetPathNameSafe(ExpectedRamp.ExternalCurve.Get()));
					bMatches = false;
				}
				const TCHAR* Channels[] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
				for (int32 Channel = 0; Channel < 4; ++Channel)
					bMatches &= EndfieldToonRichCurveMatches(ActualRamp.ColorCurves[Channel], ExpectedRamp.ColorCurves[Channel],
						Field + TEXT(".") + Channels[Channel], Diagnostics);
			}
			else if (Struct && Struct->Struct == FRuntimeFloatCurve::StaticStruct())
			{
				const FRuntimeFloatCurve& ActualRamp = *Struct->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&Actual);
				const FRuntimeFloatCurve& ExpectedRamp = *Struct->ContainerPtrToValuePtr<FRuntimeFloatCurve>(&Expected);
				if (ActualRamp.ExternalCurve != ExpectedRamp.ExternalCurve)
				{
					Diagnostics.Add(Field + TEXT(".ExternalCurve: actual=") + GetPathNameSafe(ActualRamp.ExternalCurve.Get())
						+ TEXT(" expected=") + GetPathNameSafe(ExpectedRamp.ExternalCurve.Get()));
					bMatches = false;
				}
				bMatches &= EndfieldToonRichCurveMatches(ActualRamp.EditorCurveData, ExpectedRamp.EditorCurveData, Field, Diagnostics);
			}
			else if (!It->Identical_InContainer(&Actual, &Expected))
			{
				FString ActualText;
				FString ExpectedText;
				It->ExportText_InContainer(0, ActualText, &Actual, nullptr, nullptr, PPF_None);
				It->ExportText_InContainer(0, ExpectedText, &Expected, nullptr, nullptr, PPF_None);
				Diagnostics.Add(Field + TEXT(": actual=") + ActualText.Left(160) + TEXT(" expected=") + ExpectedText.Left(160));
				bMatches = false;
			}
		}
		return bMatches;
	}

	struct FEndfieldToonBand
	{
		float Start;
		FLinearColor Color;
	};

	void EndfieldToonSetRamp(FRuntimeCurveLinearColor& Ramp, const TArray<FEndfieldToonBand>& Bands, float Edge)
	{
		Ramp.ExternalCurve = nullptr;
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			FRichCurve& Curve = Ramp.ColorCurves[Channel];
			Curve.Reset();
			const auto Pick = [Channel](const FLinearColor& Color)
			{
				return Channel == 0 ? Color.R : Channel == 1 ? Color.G : Channel == 2 ? Color.B : 1.0f;
			};
			const auto Add = [&Curve](float Time, float Value)
			{
				// 硬阈值过渡必须线性，避免默认 cubic 曲线越过设定色值。
				Curve.SetKeyInterpMode(Curve.AddKey(Time, Value), RCIM_Linear);
			};
			Add(0.0f, Pick(Bands[0].Color));
			for (int32 Index = 1; Index < Bands.Num(); ++Index)
			{
				Add(Bands[Index].Start - Edge * 0.5f, Pick(Bands[Index - 1].Color));
				Add(Bands[Index].Start + Edge * 0.5f, Pick(Bands[Index].Color));
			}
			Add(1.0f, Pick(Bands.Last().Color));
		}
	}

	TArray<FLinearColor> EndfieldToonGradientPixels()
	{
		TArray<FLinearColor> Pixels;
		Pixels.SetNumUninitialized(EndfieldToonGradientWidth * EndfieldToonGradientHeight);
		for (int32 Y = 0; Y < EndfieldToonGradientHeight; ++Y)
			for (int32 X = 0; X < EndfieldToonGradientWidth; ++X)
			{
				const float Value = (X + 0.5f) / EndfieldToonGradientWidth;
				Pixels[Y * EndfieldToonGradientWidth + X] = FLinearColor(Value, Value, Value, 1.0f);
			}
		return Pixels;
	}

	void EndfieldToonConfigureGradient(UTexture2D* Texture)
	{
		const TArray<FLinearColor> Pixels = EndfieldToonGradientPixels();
		Texture->Source.Init(EndfieldToonGradientWidth, EndfieldToonGradientHeight, 1, 1, TSF_RGBA32F,
			reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_HDR_F32;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->Filter = TF_Bilinear;
		Texture->VirtualTextureStreaming = false;
		Texture->NeverStream = true;
		Texture->LODGroup = TEXTUREGROUP_Pixels2D;
		Texture->PostEditChange();
	}

	bool EndfieldToonGradientMatches(UTexture2D* Texture, FString& SourceHash)
	{
		if (!Texture || Texture->Source.GetSizeX() != EndfieldToonGradientWidth
			|| Texture->Source.GetSizeY() != EndfieldToonGradientHeight || Texture->Source.GetNumMips() != 1
			|| Texture->Source.GetFormat() != TSF_RGBA32F || Texture->SRGB || Texture->CompressionSettings != TC_HDR_F32
			|| Texture->MipGenSettings != TMGS_NoMipmaps || Texture->AddressX != TA_Clamp || Texture->AddressY != TA_Clamp
			|| Texture->Filter != TF_Bilinear || Texture->VirtualTextureStreaming || !Texture->NeverStream
			|| Texture->LODGroup != TEXTUREGROUP_Pixels2D) return false;
		TArray64<uint8> Actual;
		if (!Texture->Source.GetMipData(Actual, 0)) return false;
		const TArray<FLinearColor> Expected = EndfieldToonGradientPixels();
		const int64 ExpectedBytes = Expected.Num() * sizeof(FLinearColor);
		if (Actual.Num() != ExpectedBytes) return false;
		SourceHash = FMD5::HashBytes(Actual.GetData(), Actual.Num());
		return FMemory::Memcmp(Actual.GetData(), Expected.GetData(), ExpectedBytes) == 0;
	}

	void EndfieldToonConfigureProfile(UToonProfile* Profile, EMMDMaterialRole Role, UTexture2D* Gradient)
	{
		// 构造函数未导出；拷贝引擎 CDO，避免在插件模块构造 FToonProfileStruct。
		Profile->Settings = GetDefault<UToonProfile>()->Settings;
		FToonProfileStruct& Settings = Profile->Settings;
		const FLinearColor Lit(0.92f, 0.92f, 0.92f);
		if (Role == EMMDMaterialRole::Skin)
		{
			EndfieldToonSetRamp(Settings.DiffuseRamp, {
				{ 0.0f, FLinearColor(0.50f, 0.40f, 0.48f) },
				{ 0.35f, FLinearColor(0.80f, 0.64f, 0.68f) }, { 0.55f, Lit } }, 0.012f);
		}
		else if (Role == EMMDMaterialRole::Hair)
		{
			EndfieldToonSetRamp(Settings.DiffuseRamp, {
				{ 0.0f, FLinearColor(0.28f, 0.36f, 0.52f) },
				{ 0.35f, FLinearColor(0.58f, 0.68f, 0.82f) }, { 0.55f, Lit } }, 0.012f);
		}
		else if (Role == EMMDMaterialRole::Eye)
		{
			EndfieldToonSetRamp(Settings.DiffuseRamp, {
				{ 0.0f, FLinearColor(0.80f, 0.80f, 0.80f) }, { 0.5f, Lit } }, 0.012f);
		}
		else
		{
			EndfieldToonSetRamp(Settings.DiffuseRamp, {
				{ 0.0f, FLinearColor(0.26f, 0.29f, 0.46f) },
				{ 0.35f, FLinearColor(0.58f, 0.63f, 0.80f) }, { 0.55f, Lit } }, 0.012f);
		}
		float SpecularGain = 6.0f;
		float SpecularThreshold = 0.5f;
		if (Role == EMMDMaterialRole::Hair) { SpecularGain = 10.0f; SpecularThreshold = 0.4f; }
		if (Role == EMMDMaterialRole::Metal) { SpecularGain = 16.0f; SpecularThreshold = 0.35f; }
		if (Role == EMMDMaterialRole::Skin || Role == EMMDMaterialRole::Cloth) SpecularGain = 2.0f;
		if (Role == EMMDMaterialRole::Eye) SpecularGain = 12.0f;
		EndfieldToonSetRamp(Settings.SpecularRamp, {
			{ 0.0f, FLinearColor::Black },
			{ SpecularThreshold, FLinearColor(SpecularGain, SpecularGain, SpecularGain) } }, 0.02f);
		Settings.DiffuseRampOffsetTexture = Role == EMMDMaterialRole::Skin ? Gradient : nullptr;
		Settings.DiffuseRampOffsetStrength = Role == EMMDMaterialRole::Skin ? 1.0f : 0.0f;
		Settings.DiffuseRampOffsetSize = 1.0f;
		Settings.SpecularRampOffsetTexture = nullptr;
		Settings.SpecularRampOffsetStrength = 0.0f;
		Settings.ShadowHatchingPatternTexture = nullptr;
		Settings.ShadowHatchingPatternStrength = 0.0f;
		Settings.DiffuseIndirectScale = 0.5f;
		Settings.SpecularIndirectScale = 1.0f;
		Settings.bDiffuseRampIncludeShadow = 1;
	}

	template<typename T>
	T* EndfieldToonAdd(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	UMaterialExpressionScalarParameter* EndfieldToonScalar(UMaterial* Material, const TCHAR* Name, float Value,
		float Min = 0.0f, float Max = 1.0f)
	{
		UMaterialExpressionScalarParameter* Parameter = EndfieldToonAdd<UMaterialExpressionScalarParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->DefaultValue = Value;
		Parameter->SliderMin = Min;
		Parameter->SliderMax = Max;
		Parameter->Group = TEXT("Endfield V2 Toon");
		return Parameter;
	}

	UMaterialExpressionVectorParameter* EndfieldToonVector(UMaterial* Material, const TCHAR* Name, const FLinearColor& Value)
	{
		UMaterialExpressionVectorParameter* Parameter = EndfieldToonAdd<UMaterialExpressionVectorParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->DefaultValue = Value;
		Parameter->Group = TEXT("Endfield V2 Toon");
		return Parameter;
	}

	void EndfieldToonCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* Source,
		int32 OutputIndex = 0)
	{
		FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
		Input.InputName = Name;
		Input.Input.Connect(OutputIndex, Source);
	}

	bool EndfieldToonIsRootInput(const FStructProperty* Property)
	{
		const FString Name = Property->Struct->GetName();
		return Name.EndsWith(TEXT("MaterialInput")) || Name == TEXT("MaterialAttributesInput");
	}

	void EndfieldToonResetRootInputs(UMaterial* Material)
	{
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		TStrongObjectPtr<UMaterialEditorOnlyData> Defaults(NewObject<UMaterialEditorOnlyData>(GetTransientPackage()));
		for (TFieldIterator<FStructProperty> It(UMaterialEditorOnlyData::StaticClass()); It; ++It)
		{
			if (EndfieldToonIsRootInput(*It)) It->CopyCompleteValue_InContainer(Data, Defaults.Get());
		}
	}

	void EndfieldToonConfigureMaterial(UMaterial* Material, EBlendMode Blend, UTexture2D* White, UTexture2D* Black, UToonProfile* Profile)
	{
		Material->GetExpressionCollection().Empty();
		EndfieldToonResetRootInputs(Material);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = Blend;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->TwoSided = true;
		Material->bTangentSpaceNormal = false;
		Material->bUseMaterialAttributes = false;
		Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
		Material->OpacityMaskClipValue = 0.1f;
		Material->bCastDynamicShadowAsMasked = Blend == BLEND_TranslucentGreyTransmittance;
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;

		UMaterialExpressionTextureSampleParameter2D* Texture = EndfieldToonAdd<UMaterialExpressionTextureSampleParameter2D>(Material);
		Texture->ParameterName = TEXT("base_color_tex");
		Texture->ExpressionGUID = FGuid::NewGuid();
		Texture->Group = TEXT("Imported material");
		Texture->Texture = White;
		Texture->SamplerType = SAMPLERTYPE_Color;
		UMaterialExpressionMultiply* Base = EndfieldToonAdd<UMaterialExpressionMultiply>(Material);
		Base->A.Connect(0, Texture);
		Base->B.Connect(0, EndfieldToonVector(Material, TEXT("BaseColor"), FLinearColor::White));
		UMaterialExpressionMultiply* Opacity = EndfieldToonAdd<UMaterialExpressionMultiply>(Material);
		Opacity->A.Connect(4, Texture);
		Opacity->B.Connect(0, EndfieldToonScalar(Material, TEXT("Opacity"), 1.0f));

		UMaterialExpressionSubstrateToonBSDF* Toon = EndfieldToonAdd<UMaterialExpressionSubstrateToonBSDF>(Material);
		Toon->ToonProfile = Profile;
		Toon->BaseColor.Connect(0, Base);
		Toon->Roughness.Connect(0, EndfieldToonScalar(Material, TEXT("ToonRoughness"), 0.5f, 0.04f));
		Toon->Specular.Connect(0, EndfieldToonScalar(Material, TEXT("ToonSpecular"), 0.5f));
		Toon->Metallic.Connect(0, EndfieldToonScalar(Material, TEXT("ToonMetallic"), 0.0f));
		Toon->Anisotropy.Connect(0, EndfieldToonScalar(Material, TEXT("ToonAnisotropy"), 0.0f, -1.0f));

		UMaterialExpressionWorldPosition* Position = EndfieldToonAdd<UMaterialExpressionWorldPosition>(Material);
		UMaterialExpressionVertexNormalWS* MeshNormal = EndfieldToonAdd<UMaterialExpressionVertexNormalWS>(Material);
		UMaterialExpressionVectorParameter* Head = EndfieldToonVector(Material,
			UMMDHeadBoneSubsystem::ParamHeadPositionRadius, FLinearColor(0, 0, 0, 0));
		UMaterialExpressionCustom* Normal = EndfieldToonAdd<UMaterialExpressionCustom>(Material);
		Normal->Description = TEXT("独立高光法线、头空间漫反射方向与兼容 SDF");
		Normal->Desc = EndfieldToonGraphVersion;
		Normal->OutputType = CMOT_Float3;
		Normal->Code = TEXT(
			"#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNative.ush\"\n"
			"TMMDEndfieldNative geometry;\n"
			"return geometry.PrepareSurface(world_pos, mesh_normal, head_position_radius, head_forward, head_right, "
			"sphere_blend, surface_uv, sdf_tex, sdf_texSampler, sdf_enabled, ramp_boundary, shadow_suppress, "
			"direction_tex, direction_texSampler, direction_enabled, direction_version, payload_enabled, specular_sphere_blend, "
			"pattern_uv, face_mask, shadow_coverage, face_direction, head_quaternion);\n");
		Normal->Inputs.Reset();
		EndfieldToonCustomInput(Normal, TEXT("world_pos"), Position);
		EndfieldToonCustomInput(Normal, TEXT("mesh_normal"), MeshNormal);
		EndfieldToonCustomInput(Normal, TEXT("head_position_radius"), Head, 5);
		EndfieldToonCustomInput(Normal, TEXT("sphere_blend"), EndfieldToonScalar(Material, TEXT("ToonSphereBlend"), 0.0f));
		EndfieldToonCustomInput(Normal, TEXT("head_forward"), EndfieldToonVector(Material,
			UMMDHeadBoneSubsystem::ParamHeadForward, FLinearColor(0, 0, 0, 0)));
		EndfieldToonCustomInput(Normal, TEXT("head_right"), EndfieldToonVector(Material,
			UMMDHeadBoneSubsystem::ParamHeadRight, FLinearColor(0, 0, 0, 0)));
		UMaterialExpressionTextureCoordinate* UV = EndfieldToonAdd<UMaterialExpressionTextureCoordinate>(Material);
		UV->CoordinateIndex = 0;
		EndfieldToonCustomInput(Normal, TEXT("surface_uv"), UV);
		UMaterialExpressionTextureObjectParameter* Sdf = EndfieldToonAdd<UMaterialExpressionTextureObjectParameter>(Material);
		Sdf->ParameterName = TEXT("ToonFaceSdfTexture");
		Sdf->ExpressionGUID = FGuid::NewGuid();
		Sdf->Group = TEXT("Endfield V2 Toon");
		Sdf->Texture = Black;
		Sdf->SamplerType = SAMPLERTYPE_LinearColor;
		EndfieldToonCustomInput(Normal, TEXT("sdf_tex"), Sdf);
		EndfieldToonCustomInput(Normal, TEXT("sdf_enabled"), EndfieldToonScalar(Material, TEXT("ToonFaceSdfEnabled"), 0.0f));
		EndfieldToonCustomInput(Normal, TEXT("ramp_boundary"), EndfieldToonScalar(Material, TEXT("ToonFaceRampBoundary"), 0.55f));
		EndfieldToonCustomInput(Normal, TEXT("shadow_suppress"), EndfieldToonScalar(Material, TEXT("ToonFaceShadowSuppress"), 0.0f));
		UMaterialExpressionTextureObjectParameter* Direction = EndfieldToonAdd<UMaterialExpressionTextureObjectParameter>(Material);
		Direction->ParameterName = TEXT("ToonFaceDirectionTexture");
		Direction->ExpressionGUID = FGuid::NewGuid();
		Direction->Group = TEXT("Endfield V2 Toon");
		Direction->Texture = Black;
		Direction->SamplerType = SAMPLERTYPE_LinearColor;
		EndfieldToonCustomInput(Normal, TEXT("direction_tex"), Direction);
		EndfieldToonCustomInput(Normal, TEXT("direction_enabled"), EndfieldToonScalar(Material, TEXT("ToonFaceDirectionEnabled"), 0.0f));
		EndfieldToonCustomInput(Normal, TEXT("direction_version"), EndfieldToonScalar(Material, TEXT("ToonFaceDirectionVersion"), 0.0f));
		UMaterialExpressionScalarParameter* PayloadEnabled = EndfieldToonScalar(Material, TEXT("ToonNativePayloadEnabled"), 0.0f);
		EndfieldToonCustomInput(Normal, TEXT("payload_enabled"), PayloadEnabled);
		EndfieldToonCustomInput(Normal, TEXT("specular_sphere_blend"), EndfieldToonScalar(Material, TEXT("ToonSpecularSphereBlend"), 0.0f));
		FCustomOutput& Pattern = Normal->AdditionalOutputs.AddDefaulted_GetRef();
		Pattern.OutputName = TEXT("pattern_uv");
		Pattern.OutputType = CMOT_Float2;
		FCustomOutput& FaceMask = Normal->AdditionalOutputs.AddDefaulted_GetRef();
		FaceMask.OutputName = TEXT("face_mask");
		FaceMask.OutputType = CMOT_Float1;
		FCustomOutput& ShadowCoverage = Normal->AdditionalOutputs.AddDefaulted_GetRef();
		ShadowCoverage.OutputName = TEXT("shadow_coverage");
		ShadowCoverage.OutputType = CMOT_Float1;
		FCustomOutput& FaceDirection = Normal->AdditionalOutputs.AddDefaulted_GetRef();
		FaceDirection.OutputName = TEXT("face_direction");
		FaceDirection.OutputType = CMOT_Float4;
		FCustomOutput& HeadQuaternion = Normal->AdditionalOutputs.AddDefaulted_GetRef();
		HeadQuaternion.OutputName = TEXT("head_quaternion");
		HeadQuaternion.OutputType = CMOT_Float4;
		Normal->RebuildOutputs();
		Toon->Normal.Connect(0, Normal);
		Toon->PatternUVs.Connect(1, Normal);
		UMaterialExpressionMMDEndfieldNativePayload* Payload = EndfieldToonAdd<UMaterialExpressionMMDEndfieldNativePayload>(Material);
		Payload->FaceDirection.Connect(4, Normal);
		Payload->HeadQuaternion.Connect(5, Normal);
		Payload->Enabled.Connect(0, PayloadEnabled);

		// 本机 UE5.8 Toon 编译器不消费 Tangent，保留引擎默认 basis；不伪接切线参数。
		// Skin 的共享线性 offset 将 .ush 输出的偏移交给原生 Toon；其他 Profile 偏移关闭。

		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		if (Blend == BLEND_TranslucentGreyTransmittance)
		{
			UMaterialExpressionSubstrateWeight* Weight = EndfieldToonAdd<UMaterialExpressionSubstrateWeight>(Material);
			Weight->A.Connect(0, Toon);
			Weight->Weight.Connect(0, Opacity);
			Data->FrontMaterial.Connect(0, Weight);
		}
		else
		{
			Data->FrontMaterial.Connect(0, Toon);
			if (Blend == BLEND_Masked)
			{
				UMaterialExpressionMultiply* ShadowOpacity = EndfieldToonAdd<UMaterialExpressionMultiply>(Material);
				ShadowOpacity->A.Connect(0, Opacity);
				ShadowOpacity->B.Connect(3, Normal);
				UMaterialExpressionShadowReplace* ShadowSwitch = EndfieldToonAdd<UMaterialExpressionShadowReplace>(Material);
				ShadowSwitch->Default.Connect(0, Opacity);
				ShadowSwitch->Shadow.Connect(0, ShadowOpacity);
				Data->OpacityMask.Connect(0, ShadowSwitch);
			}
		}
	}

	// 指纹包含所有表达式、参数默认值、纹理/Profile 引用与连线；不依赖对象随机名称或 Guid。
	FString EndfieldToonGraphSignature(UMaterial* Material)
	{
		const auto& Expressions = Material->GetExpressionCollection().Expressions;
		const auto InputKey = [&Expressions](const FExpressionInput& Input)
		{
			int32 Index = Input.Expression ? Expressions.IndexOfByKey(Input.Expression) : INDEX_NONE;
			if (Input.Expression && Index == INDEX_NONE) Index = -2;
			return FString::Printf(TEXT("%d:%d:%d:%d:%d:%d:%d"), Index, Input.OutputIndex,
				Input.Mask, Input.MaskR, Input.MaskG, Input.MaskB, Input.MaskA);
		};
		FString Bundle = EndfieldToonGraphVersion;
		TSet<FString> ShaderSources;
		for (UMaterialExpression* Expression : Expressions)
		{
			if (!Expression) return TEXT("invalid_null_expression");
			Bundle += TEXT("\n") + Expression->GetClass()->GetPathName();
			for (FExpressionInputIterator It(Expression); It; ++It)
			{
				Bundle += FString::Printf(TEXT("\ninput_%d="), It.Index) + InputKey(*It.Input);
			}
			if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
				Bundle += FString::Printf(TEXT("\n%s=%.9g:cpd=%d:%d"), *Scalar->ParameterName.ToString(), Scalar->DefaultValue,
					Scalar->bUseCustomPrimitiveData ? 1 : 0, Scalar->PrimitiveDataIndex);
			if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
			{
				Bundle += FString::Printf(TEXT("\n%s=%.9g,%.9g,%.9g,%.9g"), *Vector->ParameterName.ToString(),
					Vector->DefaultValue.R, Vector->DefaultValue.G, Vector->DefaultValue.B, Vector->DefaultValue.A);
				Bundle += FString::Printf(TEXT(":cpd=%d:%d"), Vector->bUseCustomPrimitiveData ? 1 : 0, Vector->PrimitiveDataIndex);
			}
			if (const UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression))
				Bundle += FString::Printf(TEXT("\n%s=%s:%d:uv=%d:mip=%d:sampler=%d:bias=%d"), *Texture->ParameterName.ToString(),
					Texture->Texture ? *Texture->Texture->GetPathName() : TEXT("null"), static_cast<int32>(Texture->SamplerType),
					Texture->ConstCoordinate, static_cast<int32>(Texture->MipValueMode), static_cast<int32>(Texture->SamplerSource),
					Texture->AutomaticViewMipBias ? 1 : 0);
			if (const UMaterialExpressionTextureObjectParameter* Texture = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
				Bundle += FString::Printf(TEXT("\n%s=%s:sampler=%d:source=%d"), *Texture->ParameterName.ToString(),
					Texture->Texture ? *Texture->Texture->GetPathName() : TEXT("null"), static_cast<int32>(Texture->SamplerType),
					static_cast<int32>(Texture->SamplerSource));
			if (const UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(Expression))
				Bundle += FString::Printf(TEXT("\nuv=%d:%.9g:%.9g:%d:%d"), UV->CoordinateIndex, UV->UTiling, UV->VTiling,
					UV->UnMirrorU ? 1 : 0, UV->UnMirrorV ? 1 : 0);
			if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
			{
				Bundle += TEXT("\n") + Custom->Desc + TEXT("\n") + Custom->Code;
				Bundle += FString::Printf(TEXT("\noutput=%d"), static_cast<int32>(Custom->OutputType));
				Bundle += FString::Printf(TEXT("\nclip=%d"), static_cast<int32>(Custom->ContainsClipInstruction));
				for (const FCustomInput& Input : Custom->Inputs) Bundle += TEXT("\n") + Input.InputName.ToString();
				for (const FCustomOutput& Output : Custom->AdditionalOutputs)
					Bundle += FString::Printf(TEXT("\nadditional_output=%s:%d"), *Output.OutputName.ToString(),
						static_cast<int32>(Output.OutputType));
				// include 的源码参与指纹，避免仅核对节点而遗漏共享着色器变化。
				for (const FString& VirtualPath : Custom->IncludeFilePaths)
				{
					Bundle += TEXT("\ninclude_file_path=") + VirtualPath;
					if (!EndfieldToonAppendShaderSource(VirtualPath, Bundle, ShaderSources)) return FString();
				}
				FRegexMatcher Includes(FRegexPattern(TEXT("#[ \\t]*include[ \\t]*[\"<]([^\">]+)[\">]")), Custom->Code);
				while (Includes.FindNext())
					if (!EndfieldToonAppendShaderSource(Includes.GetCaptureGroup(1), Bundle, ShaderSources)) return FString();
				if (!Custom->AdditionalDefines.IsEmpty()) return FString();
			}
			if (const UMaterialExpressionSubstrateToonBSDF* Toon = Cast<UMaterialExpressionSubstrateToonBSDF>(Expression))
				Bundle += TEXT("\nprofile=") + (Toon->ToonProfile ? Toon->ToonProfile->GetPathName() : TEXT("null"));
			if (const UMaterialExpressionWorldPosition* Position = Cast<UMaterialExpressionWorldPosition>(Expression))
				Bundle += FString::Printf(TEXT("\nworld_position_mode=%d"), static_cast<int32>(Position->WorldPositionShaderOffset));
		}
		const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		for (TFieldIterator<FStructProperty> It(UMaterialEditorOnlyData::StaticClass()); It; ++It)
		{
			if (!EndfieldToonIsRootInput(*It)) continue;
			for (int32 Index = 0; Index < It->ArrayDim; ++Index)
			{
				const void* Value = It->ContainerPtrToValuePtr<void>(Data, Index);
				const FExpressionInput& Input = *static_cast<const FExpressionInput*>(Value);
				Bundle += TEXT("\nroot_") + It->GetName() + FString::FromInt(Index) + TEXT("=") + InputKey(Input);
				for (TFieldIterator<FProperty> Field(It->Struct); Field; ++Field)
				{
					if (Field->GetName() == TEXT("UseConstant") || Field->GetName().StartsWith(TEXT("Constant")))
					{
						FString Constant;
						Field->ExportText_InContainer(0, Constant, Value, nullptr, nullptr, PPF_None);
						Bundle += TEXT(":") + Field->GetName() + TEXT("=") + Constant;
					}
				}
			}
		}
		return EndfieldToonHash(Bundle);
	}

	bool EndfieldToonMaterialMatches(UMaterial* Actual, UMaterial* Expected)
	{
		const FString ActualSignature = EndfieldToonGraphSignature(Actual);
		const FString ExpectedSignature = EndfieldToonGraphSignature(Expected);
		return Actual->MaterialDomain == Expected->MaterialDomain && Actual->BlendMode == Expected->BlendMode
			&& Actual->GetShadingModels() == Expected->GetShadingModels() && Actual->TwoSided
			&& !Actual->bTangentSpaceNormal && !Actual->bUseMaterialAttributes && Actual->GetUsageByFlag(MATUSAGE_SkeletalMesh)
			&& Actual->GetUsageByFlag(MATUSAGE_MorphTargets)
			&& FMath::IsNearlyEqual(Actual->OpacityMaskClipValue, Expected->OpacityMaskClipValue)
			&& Actual->bCastDynamicShadowAsMasked == Expected->bCastDynamicShadowAsMasked
			&& Actual->TranslucencyLightingMode == Expected->TranslucencyLightingMode
			&& !ActualSignature.IsEmpty() && ActualSignature == ExpectedSignature;
	}

	void EndfieldToonCopyPreset(UMMDStageRenderPreset* Target, const UMMDStageRenderPreset* Source)
	{
		for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
			It->CopyCompleteValue_InContainer(Target, Source);
	}

	bool EndfieldToonPresetMatches(const UMMDStageRenderPreset* Actual, const UMMDStageRenderPreset* Expected)
	{
		for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
			if (!It->Identical_InContainer(Actual, Expected)) return false;
		return true;
	}

	void EndfieldToonConfigurePreset(UMMDStageRenderPreset* Preset, const TArray<TStrongObjectPtr<UMaterial>>& Materials,
		const TArray<TStrongObjectPtr<UToonProfile>>& Profiles)
	{
		Preset->DisplayName = FText::FromString(TEXT("终末地 V2 原生 Toon（实验）"));
		Preset->CharacterToonMaterialOpaque = Materials[0].Get();
		Preset->CharacterToonMaterialMasked = Materials[1].Get();
		Preset->CharacterToonMaterialTranslucent = Materials[2].Get();
		Preset->CharacterToonRoles.Reset();
		Preset->bCharacterContactShadow = false;
		const float Roughness[] = { 0.696f, 0.742f, 0.35f, 0.696f, 0.25f, 0.25f };
		const float Specular[] = { 0.454f, 0.454f, 0.5f, 0.454f, 0.8f, 0.5f };
		for (int32 Index = 0; Index < EndfieldToonProfileCount; ++Index)
		{
			const EMMDMaterialRole Role = static_cast<EMMDMaterialRole>(Index);
			FMMDStageToonRoleSettings Settings;
			Settings.ToonProfile = Profiles[Index].Get();
			Settings.Roughness = Roughness[Index];
			Settings.Specular = Specular[Index];
			Settings.Metallic = Role == EMMDMaterialRole::Metal ? 0.8f : 0.0f;
			Settings.Anisotropy = Role == EMMDMaterialRole::Hair ? -0.8f : 0.0f;
			Settings.SphereBlend = Role == EMMDMaterialRole::Skin ? 0.75f : 0.0f;
			Settings.FaceShadowSuppress = 0.0f;
			Preset->CharacterToonRoles.Add(Role, Settings);
		}
	}

	bool EndfieldToonSaveAsset(UObject* Asset, FString& Error)
	{
		const FString Package = Asset->GetOutermost()->GetName();
		bool bAllowed = false;
		for (const TCHAR* Allowed : EndfieldToonAllowedPackages) bAllowed |= Package == Allowed;
		if (!bAllowed || Asset->GetName() != FPackageName::GetLongPackageAssetName(Package))
		{
			Error = TEXT("拒绝写入白名单之外或对象名称不匹配的包：") + Package;
			return false;
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		Asset->SetFlags(RF_Public | RF_Standalone);
		Asset->ClearFlags(RF_Transient);
		Asset->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
		{
			Error = TEXT("保存失败：") + Package;
			return false;
		}
		return true;
	}

	template<typename T>
	T* EndfieldToonLoadOrCreate(const TCHAR* Package, bool bGenerate, FString& Error)
	{
		if (FPackageName::DoesPackageExist(Package))
		{
			T* Existing = LoadObject<T>(nullptr, *EndfieldToonObjectPath(Package));
			if (!Existing) Error = TEXT("已有包无法按期望类型加载：") + FString(Package);
			return Existing;
		}
		if (!bGenerate)
		{
			Error = TEXT("缺少资产，需 -Generate：") + FString(Package);
			return nullptr;
		}
		T* Asset = NewObject<T>(CreatePackage(Package), *FPackageName::GetLongPackageAssetName(Package), RF_Public | RF_Standalone);
		if (!Asset)
		{
			Error = TEXT("无法创建资产：") + FString(Package);
			return nullptr;
		}
		if constexpr (std::is_same_v<T, UMaterial>)
		{
			FArchiveUObject InitialArchive;
			Asset->Serialize(InitialArchive);
		}
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}
}

UMMDEndfieldToonPresetsCommandlet::UMMDEndfieldToonPresetsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UMMDEndfieldToonPresetsCommandlet::Main(const FString& Params)
{
	const bool bGenerate = FParse::Param(*Params, TEXT("Generate"));
	FString ReportPath;
	if (!FParse::Value(*Params, TEXT("Report="), ReportPath))
		ReportPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/EndfieldV2/presets_report.json"));
	ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
	TArray<FString> Errors;
	TArray<TSharedPtr<FJsonValue>> Written;
	TArray<TSharedPtr<FJsonValue>> AssetReports;
	TArray<TSharedPtr<FJsonValue>> MaterialReports;
	TArray<TStrongObjectPtr<UToonProfile>> Profiles;
	TArray<TStrongObjectPtr<UMaterial>> Materials;
	TStrongObjectPtr<UMMDStageRenderPreset> Preset;
	TStrongObjectPtr<UTexture2D> Gradient;
	FString GradientSourceHash;
	bool bGradientMatches = false;
	const bool bToonEnabled = Substrate::IsToonProfileEnabled();
	const bool bCanCompile = bToonEnabled && GShaderCompilingManager
		&& FApp::CanEverRender() && !GUsingNullRHI;
	if (!bToonEnabled) Errors.Add(TEXT("原生 Toon 校验需要启动时开启 r.Substrate=True；本次不编译也不写入资产"));
	if (!GShaderCompilingManager || !FApp::CanEverRender() || GUsingNullRHI)
		Errors.Add(TEXT("原生 Toon 校验需要 shader 编译管理器与可渲染 RHI（-AllowCommandletRendering，不能 -nullrhi）"));

	TStrongObjectPtr<UMMDStageRenderPreset> Template(LoadObject<UMMDStageRenderPreset>(nullptr, EndfieldToonTemplatePath));
	TStrongObjectPtr<UTexture2D> White(LoadObject<UTexture2D>(nullptr,
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
	TStrongObjectPtr<UTexture2D> Black(LoadObject<UTexture2D>(nullptr,
		TEXT("/Engine/EngineResources/Black.Black")));
	if (!Template.IsValid()) Errors.Add(TEXT("缺少只读旧 Endfield 模板，无法继承舞台参数与非 Substrate 父材质回退"));
	else if (!Template->CharacterMaterialOpaque || !Template->CharacterMaterialMasked || !Template->CharacterMaterialTranslucent)
		Errors.Add(TEXT("旧 Endfield 模板没有完整的三套父材质回退，V2 不能生成不完整预设"));
	if (!White.IsValid()) Errors.Add(TEXT("缺少引擎白贴图，无法提供 base_color_tex 默认值"));
	if (!Black.IsValid()) Errors.Add(TEXT("缺少引擎黑贴图，无法提供自动 SDF 禁用时的默认值"));
	// -Generate 的前提不满足时，不创建或修改资产；只读模式仍检查磁盘包与结构。
	const bool bInspect = !bGenerate || Errors.IsEmpty();
	FString Error;
	TSet<FGuid> ProfileGuids;
	if (bInspect)
	{
		Gradient.Reset(EndfieldToonLoadOrCreate<UTexture2D>(EndfieldToonAllowedPackages[10], bGenerate, Error));
		if (!Gradient.IsValid()) Errors.Add(Error);
		else
		{
			if (bGenerate) EndfieldToonConfigureGradient(Gradient.Get());
			bGradientMatches = EndfieldToonGradientMatches(Gradient.Get(), GradientSourceHash);
			if (!bGradientMatches) Errors.Add(TEXT("共享 offset 线性贴图源数据或采样设置过期：") + Gradient->GetPathName());
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Gradient->GetPathName());
			Item->SetStringField(TEXT("type"), TEXT("Texture2D"));
			Item->SetNumberField(TEXT("source_width"), Gradient->Source.GetSizeX());
			Item->SetNumberField(TEXT("source_height"), Gradient->Source.GetSizeY());
			Item->SetStringField(TEXT("source_hash"), GradientSourceHash);
			Item->SetBoolField(TEXT("structure_passed"), bGradientMatches);
			AssetReports.Add(MakeShared<FJsonValueObject>(Item));
		}
		for (int32 Index = 0; Index < EndfieldToonProfileCount; ++Index)
		{
			UToonProfile* Profile = EndfieldToonLoadOrCreate<UToonProfile>(EndfieldToonAllowedPackages[Index + 3], bGenerate, Error);
			if (!Profile) { Errors.Add(Error); continue; }
			Profiles.Emplace(Profile);
			TStrongObjectPtr<UToonProfile> Expected(NewObject<UToonProfile>(GetTransientPackage()));
			EndfieldToonConfigureProfile(Expected.Get(), static_cast<EMMDMaterialRole>(Index), Gradient.Get());
			TArray<FString> SettingMismatches;
			bool bSettingsMatch = EndfieldToonSettingsMatch(Profile->Settings, Expected->Settings, SettingMismatches);
			if (bGenerate)
			{
				Profile->Settings = Expected->Settings;
				if (!Profile->Guid.IsValid() || ProfileGuids.Contains(Profile->Guid)) Profile->Guid = FGuid::NewGuid();
				SettingMismatches.Reset();
				bSettingsMatch = true;
			}
			else if (!bSettingsMatch) Errors.Add(TEXT("Profile 设置过期或共享 offset 依赖不匹配：") + Profile->GetPathName());
			const bool bGuidValid = Profile->Guid.IsValid() && !ProfileGuids.Contains(Profile->Guid);
			if (!bGuidValid)
				Errors.Add(TEXT("Profile Guid 为空或重复：") + Profile->GetPathName());
			ProfileGuids.Add(Profile->Guid);
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Profile->GetPathName());
			Item->SetStringField(TEXT("type"), TEXT("ToonProfile"));
			Item->SetStringField(TEXT("guid"), Profile->Guid.ToString());
			Item->SetBoolField(TEXT("structure_passed"), (bGenerate || bSettingsMatch) && bGuidValid);
			TArray<TSharedPtr<FJsonValue>> JsonMismatches;
			for (const FString& Mismatch : SettingMismatches)
			{
				JsonMismatches.Add(MakeShared<FJsonValueString>(Mismatch));
				UE_LOG(LogTemp, Warning, TEXT("[MMDEndfieldToonPresets] %s: %s"), *Profile->GetName(), *Mismatch);
			}
			Item->SetArrayField(TEXT("settings_mismatches"), JsonMismatches);
			AssetReports.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	const EBlendMode Blends[] = { BLEND_Opaque, BLEND_Masked, BLEND_TranslucentGreyTransmittance };
	if (bInspect && Profiles.Num() == EndfieldToonProfileCount && White.IsValid() && Black.IsValid())
	{
		for (int32 Index = 0; Index < EndfieldToonMaterialCount; ++Index)
		{
			UMaterial* Material = EndfieldToonLoadOrCreate<UMaterial>(EndfieldToonAllowedPackages[Index], bGenerate, Error);
			if (!Material) { Errors.Add(Error); continue; }
			Materials.Emplace(Material);
			TStrongObjectPtr<UMaterial> Expected(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
			FArchiveUObject InitialArchive;
			Expected->Serialize(InitialArchive);
			EndfieldToonConfigureMaterial(Expected.Get(), Blends[Index], White.Get(), Black.Get(), Profiles[0].Get());
			const bool bMatches = EndfieldToonMaterialMatches(Material, Expected.Get());
			if (!bMatches && bGenerate)
			{
				EndfieldToonConfigureMaterial(Material, Blends[Index], White.Get(), Black.Get(), Profiles[0].Get());
				Material->StateId = FGuid::NewGuid();
				Material->PostEditChange();
			}
			else if (!bMatches) Errors.Add(TEXT("原生 Toon 材质图或属性过期：") + Material->GetPathName());
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Material->GetPathName());
			Item->SetStringField(TEXT("type"), TEXT("Material"));
			Item->SetStringField(TEXT("graph_signature"), EndfieldToonGraphSignature(Material));
			Item->SetStringField(TEXT("expected_graph_signature"), EndfieldToonGraphSignature(Expected.Get()));
			Item->SetBoolField(TEXT("structure_passed"), EndfieldToonMaterialMatches(Material, Expected.Get()));
			AssetReports.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	if (bInspect && Template.IsValid() && Materials.Num() == EndfieldToonMaterialCount && Profiles.Num() == EndfieldToonProfileCount)
	{
		Preset.Reset(EndfieldToonLoadOrCreate<UMMDStageRenderPreset>(EndfieldToonAllowedPackages[9], bGenerate, Error));
		if (!Preset.IsValid()) Errors.Add(Error);
		else
		{
			TStrongObjectPtr<UMMDStageRenderPreset> Expected(NewObject<UMMDStageRenderPreset>(GetTransientPackage()));
			EndfieldToonCopyPreset(Expected.Get(), Template.Get());
			EndfieldToonConfigurePreset(Expected.Get(), Materials, Profiles);
			if (bGenerate) EndfieldToonCopyPreset(Preset.Get(), Expected.Get());
			const bool bMatches = EndfieldToonPresetMatches(Preset.Get(), Expected.Get());
			if (!bMatches) Errors.Add(TEXT("V2 预设设置过期或没有保留旧父材质回退：") + Preset->GetPathName());
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Preset->GetPathName());
			Item->SetStringField(TEXT("type"), TEXT("RenderPreset"));
			Item->SetBoolField(TEXT("structure_passed"), bMatches);
			AssetReports.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	if (Errors.IsEmpty() && bCanCompile && Materials.Num() == EndfieldToonMaterialCount)
	{
		// UE5.8 PostEditChange 的 None 模式只建图；必须主动编译真实绘制 shader。
		for (const TStrongObjectPtr<UMaterial>& Material : Materials)
			Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
		FAssetCompilingManager::Get().FinishAllCompilation();
		GShaderCompilingManager->FinishAllCompilation();
		FlushRenderingCommands();
		for (const TStrongObjectPtr<UMaterial>& Material : Materials)
		{
			const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
			const FMaterialShaderMap* Map = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
			TArray<TSharedPtr<FJsonValue>> CompileErrors;
			if (Resource)
				for (const FString& Failure : Resource->GetCompileErrors()) CompileErrors.Add(MakeShared<FJsonValueString>(Failure));
			const bool bFinished = Resource && Resource->IsCompilationFinished();
			const bool bSucceeded = Map && Map->CompiledSuccessfully();
			const bool bFinalized = Map && Map->IsCompilationFinalized();
			const int32 ShaderCount = Map ? Map->GetShaderNum() : 0;
			const bool bRenderingValid = Map && Map->IsValidForRendering(false);
			const bool bReady = bFinished && bSucceeded && bFinalized && ShaderCount > 0 && bRenderingValid && CompileErrors.IsEmpty();
			if (!bReady)
			{
				CompileErrors.Add(MakeShared<FJsonValueString>(TEXT("真实绘制 shader 未就绪：要求 finished/success/finalized、shader_count>0 与 rendering_valid")));
				Errors.Add(TEXT("原生 Toon 编译失败，不保存任何资产：") + Material->GetPathName());
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("asset"), Material->GetPathName());
			Item->SetBoolField(TEXT("finished"), bFinished);
			Item->SetBoolField(TEXT("success"), bSucceeded);
			Item->SetBoolField(TEXT("finalized"), bFinalized);
			Item->SetNumberField(TEXT("shader_count"), ShaderCount);
			Item->SetBoolField(TEXT("rendering_valid"), bRenderingValid);
			Item->SetBoolField(TEXT("passed"), bReady);
			Item->SetArrayField(TEXT("errors"), CompileErrors);
			MaterialReports.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	if (bGenerate && Errors.IsEmpty() && MaterialReports.Num() == EndfieldToonMaterialCount && Preset.IsValid())
	{
		TArray<UObject*> Assets;
		Assets.Add(Gradient.Get());
		for (const TStrongObjectPtr<UToonProfile>& Profile : Profiles) Assets.Add(Profile.Get());
		for (const TStrongObjectPtr<UMaterial>& Material : Materials) Assets.Add(Material.Get());
		Assets.Add(Preset.Get());
		for (UObject* Asset : Assets)
		{
			if (!EndfieldToonSaveAsset(Asset, Error)) { Errors.Add(Error); break; }
			Written.Add(MakeShared<FJsonValueString>(Asset->GetOutermost()->GetName()));
		}
	}

	const bool bSuccess = Errors.IsEmpty() && bGradientMatches && Profiles.Num() == EndfieldToonProfileCount
		&& Materials.Num() == EndfieldToonMaterialCount && Preset.IsValid()
		&& MaterialReports.Num() == EndfieldToonMaterialCount && (!bGenerate || Written.Num() == UE_ARRAY_COUNT(EndfieldToonAllowedPackages));
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), bSuccess);
	Report->SetBoolField(TEXT("generate_requested"), bGenerate);
	Report->SetBoolField(TEXT("read_only"), !bGenerate);
	Report->SetBoolField(TEXT("compile_attempted"), !MaterialReports.IsEmpty());
	Report->SetBoolField(TEXT("toon_profile_enabled"), bToonEnabled);
	Report->SetStringField(TEXT("target"), TEXT("EndfieldV2NativeToon"));
	Report->SetStringField(TEXT("graph_version"), EndfieldToonGraphVersion);
	Report->SetNumberField(TEXT("expected_package_count"), UE_ARRAY_COUNT(EndfieldToonAllowedPackages));
	Report->SetStringField(TEXT("offset_gradient_source_hash"), GradientSourceHash);
	FString NativeSource;
	const FString NativeSourcePath = GetShaderSourceFilePath(TEXT("/Plugin/Ue5MMDTools/Core/TMMDEndfieldNative.ush"));
	Report->SetStringField(TEXT("native_ush_source_hash"), FFileHelper::LoadFileToString(NativeSource, *NativeSourcePath)
		? EndfieldToonHash(NativeSource) : TEXT("missing"));
	Report->SetStringField(TEXT("shader_platform"), LexToString(GMaxRHIShaderPlatform));
	Report->SetStringField(TEXT("template"), EndfieldToonTemplatePath);
	Report->SetStringField(TEXT("preset"), Preset.IsValid() ? Preset->GetPathName() : TEXT("missing"));
	Report->SetStringField(TEXT("r.Substrate"), EndfieldToonCVar(TEXT("r.Substrate")));
	Report->SetStringField(TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"), EndfieldToonCVar(TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse")));
	Report->SetStringField(TEXT("unified_diffuse_notice"), TEXT("ToonUnifiedDiffuse 是可选实验开关，引擎默认 0；设为 1 的路径尚未通过视觉验收，生成与编译校验不要求启用。"));
	Report->SetStringField(TEXT("candidate_notice"), TEXT("后端 Mode2 的 Skin/Hair/Cloth 使用客户端 RD 数据语义。Skin roughness=.742/specular=.454、Cloth roughness=.696 来自代表样本；Metal/Eye 和无贴图头发方向仍是通用近似，不代表原作完整复刻。"));
	Report->SetStringField(TEXT("face_sdf_bridge"), TEXT("Mode2 + adaptive GBuffer 的自有 Skin 通过专用 CustomOutput 传递头空间方向和旋转，原生 Normal 独立保留高光/GI。自动 V4 是完整几何方向场（非遮挡距离场），四tap先解离散镜像再插值，未提供时用球面/几何法线。Mode0/1、fmt0和透明保持旧 threshold bridge/原生 Toon 回退。"));
	Report->SetStringField(TEXT("face_shadow_candidate"), TEXT("默认保留脸面普通投影（FaceShadowSuppress=0），关闭角色接触阴影。P15 对照确认鼻灰点来自接触阴影，单独关闭已改善；普通投影和外部遮挡仍工作。可选 masked ShadowPassSwitch 的正常分支保持原 alpha，投影分支乘 1-SDF有效脸面mask*suppress；启用时也影响脸面对身体和场景的投射。无有效 SDF/head frame 时覆盖率=1；opaque/translucent 不使用此开关。"));
	Report->SetStringField(TEXT("anisotropy_limit"), TEXT("本机 UE5.8 原生 Toon 不消费 Tangent 输入，各向异性方向只用引擎默认 basis，尚不能定制天使环。"));
	Report->SetArrayField(TEXT("assets"), AssetReports);
	Report->SetArrayField(TEXT("materials"), MaterialReports);
	Report->SetArrayField(TEXT("written_packages"), Written);
	TArray<TSharedPtr<FJsonValue>> JsonErrors;
	for (const FString& Failure : Errors)
	{
		JsonErrors.Add(MakeShared<FJsonValueString>(Failure));
		UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldToonPresets] %s"), *Failure);
	}
	Report->SetNumberField(TEXT("failed"), Errors.Num());
	Report->SetArrayField(TEXT("errors"), JsonErrors);
	FString Json;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	const bool bSavedReport = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
		&& FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	if (!bSavedReport) UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldToonPresets] 无法保存报告：%s"), *ReportPath);
	UE_LOG(LogTemp, Display, TEXT("[MMDEndfieldToonPresets] profiles=%d materials=%d writes=%d success=%d report=%s"),
		Profiles.Num(), Materials.Num(), Written.Num(), bSuccess && bSavedReport, *ReportPath);
	return bSuccess && bSavedReport ? 0 : 1;
}
