// UE5.8 原生 Substrate Toon BSDF 探针。
//
// 目的：验证"角色改用引擎延迟光照 + Toon Profile"是否可行，以及画面是否真的被引擎灯光照亮，
// 而不是现状的 Unlit + Custom 节点自算光照。
//
// 约束：
//   * 默认全部对象 Transient；显式 -SaveSdfFixture 仅保存 Saved 下的专用派生夹具；
//   * 只读加载既有的终末地预设与角色夹具，不调用任何 Ensure*/Generate 流程；
//   * 与 -run=MMDStagePresets -EndfieldCapture 完全独立。
//
// 用法（Substrate 必须在进程启动时开启，r.Substrate 是 ReadOnly CVar）：
//   UnrealEditor-Cmd.exe <项目>.uproject -run=MMDEndfieldToonProbe
//       -OutDir=<目录> -Tag=<文件名前缀> -Exp=ab,shadow,ramp,lighting,face,hair,override,translucent,runtime
//       -ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True
//       -AllowCommandletRendering -RenderOffscreen -d3d12 -unattended -nopause -nosplash -nosound -stdout
//   可选：-Frames=<每张图累计帧数> -LumenTracking（预览场景开启 Lumen 图元追踪）
//   -NativePreset=<资产路径>：验证已保存的 V2 预设，支持 ab/runtime/shadow/lighting；关闭 Substrate 可验证回退。
//
// 本文件内所有新增 helper 均带 ToonProbe 前缀，避免与同模块 unity build 里的同名 helper 撞名。
#include "MMDEndfieldToonProbeCommandlet.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "AssetCompilingManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ContentStreaming.h"
#include "Curves/CurveLinearColor.h"
#include "Dom/JsonObject.h"
#include "Editor/MMDEndfieldFaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/ToonProfile.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialRenderProxy.h"
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
#include "Rendering/MMDEndfieldFaceData.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "RenderingThread.h"
#include "RenderUtils.h"
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
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr const TCHAR* ToonProbeEndfieldPresetPath = TEXT("/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield.DA_Render_Endfield");
	constexpr const TCHAR* ToonProbeWhiteTexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	constexpr const TCHAR* ToonProbeCubePath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// ------------------------------------------------------------------------------------------
	// 通用小工具
	// ------------------------------------------------------------------------------------------

	FString ToonProbeCVar(const TCHAR* Name)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetString() : FString(TEXT("<missing>"));
	}

	void ToonProbeSetCVarInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[ToonProbe] CVar not found: %s"), Name);
		}
	}

	double ToonProbeWaitForShaders()
	{
		const double Start = FPlatformTime::Seconds();
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
		return FPlatformTime::Seconds() - Start;
	}

	TSharedRef<FJsonObject> ToonProbeJsonObject()
	{
		return MakeShared<FJsonObject>();
	}

	bool ToonProbeHasExp(const TArray<FString>& Exps, const TCHAR* Name)
	{
		return Exps.Contains(FString(Name)) || Exps.Contains(FString(TEXT("all")));
	}

	// ------------------------------------------------------------------------------------------
	// Toon Profile
	// ------------------------------------------------------------------------------------------

	struct FToonProbeBand
	{
		float Start;
		FLinearColor Value;
	};

	// 把若干"分段常数"带写成 ramp 曲线：段间只留 Edge 宽度的线性过渡，等价于硬阈值。
	void ToonProbeSetRamp(FRuntimeCurveLinearColor& Ramp, const TArray<FToonProbeBand>& Bands, float Edge)
	{
		Ramp.ExternalCurve = nullptr;
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			const auto Pick = [Channel](const FLinearColor& Color)
			{
				return Channel == 0 ? Color.R : Channel == 1 ? Color.G : Channel == 2 ? Color.B : 1.0f;
			};
			FRichCurve& Curve = Ramp.ColorCurves[Channel];
			Curve.Reset();
			Curve.AddKey(0.0f, Pick(Bands[0].Value));
			for (int32 Index = 1; Index < Bands.Num(); ++Index)
			{
				Curve.AddKey(Bands[Index].Start - Edge * 0.5f, Pick(Bands[Index - 1].Value));
				Curve.AddKey(Bands[Index].Start + Edge * 0.5f, Pick(Bands[Index].Value));
			}
			Curve.AddKey(1.0f, Pick(Bands.Last().Value));
		}
	}

	enum class EToonProbeProfile : uint8
	{
		Standard, Skin, Hair, Cloth, Metal, Eye, TwoBand, Smooth, EngineDefault, Count
	};

	const TCHAR* ToonProbeProfileName(EToonProbeProfile Kind)
	{
		static const TCHAR* Names[] = { TEXT("Standard"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"),
			TEXT("Eye"), TEXT("TwoBand"), TEXT("Smooth"), TEXT("EngineDefault") };
		return Names[static_cast<int32>(Kind)];
	}

	// 漫反射 ramp 横轴 = NoL * 0.5 + 0.5。0.55 ≈ NoL 0.1（明暗交界），0.35 ≈ NoL -0.3（深暗交界）。
	void ToonProbeConfigureProfile(UToonProfile* Profile, EToonProbeProfile Kind)
	{
		FToonProfileStruct& Settings = Profile->Settings;
		// FToonProfileStruct 的构造函数没有导出（缺 ENGINE_API），插件模块直接 FToonProfileStruct() 会链接失败；
		// UToonProfile 的 CDO 在引擎内构造，Settings 即默认值，直接拷贝。
		Settings = GetDefault<UToonProfile>()->Settings;
		const float Edge = 0.012f;
		const FLinearColor Lit(0.92f, 0.92f, 0.92f);
		switch (Kind)
		{
		case EToonProbeProfile::EngineDefault:
			return;
		case EToonProbeProfile::TwoBand:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.50f, 0.54f, 0.70f) }, { 0.54f, Lit } }, Edge);
			break;
		case EToonProbeProfile::Smooth:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.30f, 0.33f, 0.46f) }, { 0.35f, FLinearColor(0.55f, 0.60f, 0.76f) },
				{ 0.60f, Lit } }, 0.28f);
			break;
		case EToonProbeProfile::Skin:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.50f, 0.40f, 0.48f) }, { 0.35f, FLinearColor(0.80f, 0.64f, 0.68f) },
				{ 0.55f, Lit } }, Edge);
			break;
		case EToonProbeProfile::Hair:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.28f, 0.36f, 0.52f) }, { 0.35f, FLinearColor(0.58f, 0.68f, 0.82f) },
				{ 0.55f, Lit } }, Edge);
			break;
		case EToonProbeProfile::Eye:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.80f, 0.80f, 0.80f) }, { 0.5f, Lit } }, Edge);
			break;
		case EToonProbeProfile::Cloth:
		case EToonProbeProfile::Metal:
		case EToonProbeProfile::Standard:
		default:
			ToonProbeSetRamp(Settings.DiffuseRamp, { { 0.0f, FLinearColor(0.26f, 0.29f, 0.46f) }, { 0.35f, FLinearColor(0.58f, 0.63f, 0.80f) },
				{ 0.55f, Lit } }, Edge);
			break;
		}
		// 高光 ramp 横轴是 D_GGX 的 0..1 饱和值；F0 约 0.04，所以高光带的值要给 HDR 才看得见。
		float SpecularGain = 6.0f;
		float SpecularThreshold = 0.5f;
		if (Kind == EToonProbeProfile::Hair) { SpecularGain = 10.0f; SpecularThreshold = 0.40f; }
		if (Kind == EToonProbeProfile::Metal) { SpecularGain = 16.0f; SpecularThreshold = 0.35f; }
		if (Kind == EToonProbeProfile::Skin || Kind == EToonProbeProfile::Cloth || Kind == EToonProbeProfile::Eye)
		{
			SpecularGain = Kind == EToonProbeProfile::Eye ? 12.0f : 2.0f;
		}
		ToonProbeSetRamp(Settings.SpecularRamp, { { 0.0f, FLinearColor::Black },
			{ SpecularThreshold, FLinearColor(SpecularGain, SpecularGain, SpecularGain) } }, 0.02f);
		Settings.DiffuseIndirectScale = 0.5f;
		Settings.SpecularIndirectScale = 1.0f;
		Settings.bDiffuseRampIncludeShadow = 0;
	}

	UToonProfile* ToonProbeMakeProfile(EToonProbeProfile Kind, TArray<TStrongObjectPtr<UObject>>& KeepAlive)
	{
		UToonProfile* Profile = NewObject<UToonProfile>(GetTransientPackage(),
			FName(*FString::Printf(TEXT("ToonProbe_%s"), ToonProbeProfileName(Kind))), RF_Transient);
		// 参数名里嵌入 Guid；缺省全零 Guid 会让多个 Profile 撞同一个参数名。
		Profile->Guid = FGuid::NewGuid();
		ToonProbeConfigureProfile(Profile, Kind);
		KeepAlive.Emplace(Profile);
		return Profile;
	}

	// ------------------------------------------------------------------------------------------
	// Toon 父材质
	// ------------------------------------------------------------------------------------------

	template<typename T>
	T* ToonProbeAdd(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	UMaterialExpressionScalarParameter* ToonProbeScalar(UMaterial* Material, const TCHAR* Name, float Value)
	{
		UMaterialExpressionScalarParameter* Parameter = ToonProbeAdd<UMaterialExpressionScalarParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->DefaultValue = Value;
		return Parameter;
	}

	UMaterialExpressionVectorParameter* ToonProbeVector(UMaterial* Material, const TCHAR* Name, const FLinearColor& Value)
	{
		UMaterialExpressionVectorParameter* Parameter = ToonProbeAdd<UMaterialExpressionVectorParameter>(Material);
		Parameter->ParameterName = Name;
		Parameter->ExpressionGUID = FGuid::NewGuid();
		Parameter->DefaultValue = Value;
		return Parameter;
	}

	struct FToonProbeMaterialSpec
	{
		FString Name;
		EBlendMode Blend = BLEND_Opaque;
		bool bTwoSided = true;
		bool bCoverageWeight = false;
		UToonProfile* Profile = nullptr;
	};

	// 通用"大一统"Toon 材质：base_color_tex * BaseColor -> Toon BSDF，
	// 球面法线与各向异性为可选入口，默认关闭；不连接阴影偏移纹理。
	// 参数名与现有角色材质约定（base_color_tex/BaseColor/Opacity）一致。
	UMaterial* ToonProbeBuildMaterial(const FToonProbeMaterialSpec& Spec, UTexture* WhiteTexture)
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), *Spec.Name, RF_Transient);
		FArchiveUObject InitialArchive;
		Material->Serialize(InitialArchive);
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = Spec.Blend;
		Material->TwoSided = Spec.bTwoSided;
		Material->SetShadingModel(MSM_DefaultLit);
		Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
		Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
		Material->OpacityMaskClipValue = 0.33f;
		Material->bCastDynamicShadowAsMasked = Spec.Blend != BLEND_Opaque;
		if (Spec.bCoverageWeight)
		{
			Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		}
		// 法线输入走世界空间（球面法线是世界空间量）。
		Material->bTangentSpaceNormal = false;

		UMaterialExpressionTextureSampleParameter2D* BaseTexture = ToonProbeAdd<UMaterialExpressionTextureSampleParameter2D>(Material);
		BaseTexture->ParameterName = TEXT("base_color_tex");
		BaseTexture->ExpressionGUID = FGuid::NewGuid();
		BaseTexture->Texture = Cast<UTexture>(WhiteTexture);
		BaseTexture->SamplerType = SAMPLERTYPE_Color;
		UMaterialExpressionVectorParameter* BaseColor = ToonProbeVector(Material, TEXT("BaseColor"), FLinearColor::White);
		UMaterialExpressionMultiply* Tinted = ToonProbeAdd<UMaterialExpressionMultiply>(Material);
		Tinted->A.Connect(0, BaseTexture);
		Tinted->B.Connect(0, BaseColor);

		UMaterialExpressionScalarParameter* Opacity = ToonProbeScalar(Material, TEXT("Opacity"), 1.0f);
		UMaterialExpressionMultiply* FinalOpacity = ToonProbeAdd<UMaterialExpressionMultiply>(Material);
		FinalOpacity->A.Connect(4, BaseTexture);
		FinalOpacity->B.Connect(0, Opacity);

		UMaterialExpressionSubstrateToonBSDF* Toon = ToonProbeAdd<UMaterialExpressionSubstrateToonBSDF>(Material);
		Toon->ToonProfile = Spec.Profile;
		Toon->BaseColor.Connect(0, Tinted);
		Toon->Roughness.Connect(0, ToonProbeScalar(Material, TEXT("ToonRoughness"), 0.5f));
		Toon->Specular.Connect(0, ToonProbeScalar(Material, TEXT("ToonSpecular"), 0.5f));
		Toon->Metallic.Connect(0, ToonProbeScalar(Material, TEXT("ToonMetallic"), 0.0f));
		Toon->Anisotropy.Connect(0, ToonProbeScalar(Material, TEXT("ToonAnisotropy"), 0.0f));

		// 球面法线混合：N = normalize(lerp(顶点法线, normalize(世界位置 - 头部中心), blend))。
		UMaterialExpressionVectorParameter* Head = ToonProbeVector(Material, UMMDHeadBoneSubsystem::ParamHeadPositionRadius, FLinearColor(0, 0, 0, 0));
		UMaterialExpressionWorldPosition* WorldPosition = ToonProbeAdd<UMaterialExpressionWorldPosition>(Material);
		UMaterialExpressionSubtract* ToHead = ToonProbeAdd<UMaterialExpressionSubtract>(Material);
		ToHead->A.Connect(0, WorldPosition);
		ToHead->B.Connect(0, Head);
		UMaterialExpressionNormalize* SphereNormal = ToonProbeAdd<UMaterialExpressionNormalize>(Material);
		SphereNormal->VectorInput.Connect(0, ToHead);
		UMaterialExpressionVertexNormalWS* MeshNormal = ToonProbeAdd<UMaterialExpressionVertexNormalWS>(Material);
		UMaterialExpressionLinearInterpolate* BlendNormal = ToonProbeAdd<UMaterialExpressionLinearInterpolate>(Material);
		BlendNormal->A.Connect(0, MeshNormal);
		BlendNormal->B.Connect(0, SphereNormal);
		BlendNormal->Alpha.Connect(0, ToonProbeScalar(Material, TEXT("ToonSphereBlend"), 0.0f));
		UMaterialExpressionNormalize* FinalNormal = ToonProbeAdd<UMaterialExpressionNormalize>(Material);
		FinalNormal->VectorInput.Connect(0, BlendNormal);
		Toon->Normal.Connect(0, FinalNormal);

		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		if (Spec.bCoverageWeight)
		{
			// Substrate 里半透明覆盖率不是旧的 Opacity 引脚，而是 Coverage Weight 运算节点。
			UMaterialExpressionSubstrateWeight* Weight = ToonProbeAdd<UMaterialExpressionSubstrateWeight>(Material);
			Weight->A.Connect(0, Toon);
			Weight->Weight.Connect(0, FinalOpacity);
			Data->FrontMaterial.Connect(0, Weight);
		}
		else
		{
			Data->FrontMaterial.Connect(0, Toon);
			if (Spec.Blend == BLEND_Masked)
			{
				Data->OpacityMask.Connect(0, FinalOpacity);
			}
		}
		Material->StateId = FGuid::NewGuid();
		Material->PostEditChange();
		// UE5.8 的 PostEditChange 使用 None 预编译模式，只建立表达式数据；离屏探针必须主动编译绘制 shader。
		Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
		return Material;
	}

	TArray<FString> ToonProbeCompileErrors(UMaterial* Material, bool& bOutShaderMapReady)
	{
		TArray<FString> Errors;
		bOutShaderMapReady = false;
		const FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
		if (!Resource)
		{
			Errors.Add(TEXT("Material resource was unavailable"));
			return Errors;
		}
		Errors.Append(Resource->GetCompileErrors());
		const FMaterialShaderMap* ShaderMap = Resource->GetGameThreadShaderMap();
		// 就绪判定的四个条件逐项落日志，便于区分"仍在编"与"编失败"。
		UE_LOG(LogTemp, Display, TEXT("[ToonProbe] readiness %s: finished=%d map=%d success=%d finalized=%d validForRendering=%d"),
			*Material->GetName(), Resource->IsCompilationFinished() ? 1 : 0, ShaderMap ? 1 : 0,
			ShaderMap && ShaderMap->CompiledSuccessfully() ? 1 : 0, ShaderMap && ShaderMap->IsCompilationFinalized() ? 1 : 0,
			ShaderMap && ShaderMap->IsValidForRendering(false) ? 1 : 0);
		// IsValidForRendering 只检查冻结数据非空，未编译的空 shader map 也可能通过，不能据此接受默认灰材质。
		bOutShaderMapReady = Resource->IsCompilationFinished() && ShaderMap && ShaderMap->CompiledSuccessfully()
			&& ShaderMap->IsCompilationFinalized() && ShaderMap->GetShaderNum() > 0
			&& ShaderMap->IsValidForRendering(false) && Errors.IsEmpty();
		return Errors;
	}

	// ------------------------------------------------------------------------------------------
	// 角色夹具（与 MMDStagePresets 同一选择规则：最新且顶点数 > 5000 的 manifest）
	// ------------------------------------------------------------------------------------------

	struct FToonProbeSample
	{
		FString Mount;
		FString Directory;
		FString MeshPath;
		FString AnimationPath;
		float BeautyTime = 0.6f;
		TArray<float> FaceTimes;
		TStrongObjectPtr<USkeletalMesh> Mesh;
		TStrongObjectPtr<UAnimSequence> Animation;
		~FToonProbeSample()
		{
			if (!Mount.IsEmpty())
			{
				FPackageName::UnRegisterMountPoint(Mount, Directory);
			}
		}
	};

	bool ToonProbeLoadSample(FToonProbeSample& Sample, FString& Error)
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
			}
		}
		if (Sample.Mount.IsEmpty())
		{
			Error = TEXT("No Saved/BackgroundValidation character import was found");
			return false;
		}
		FPackageName::RegisterMountPoint(Sample.Mount, Sample.Directory);
		Sample.Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *Sample.MeshPath));
		if (!Sample.Mesh.IsValid())
		{
			Error = TEXT("Sandbox character mesh could not be loaded");
			return false;
		}
		// 八秒预览动画由现有流程缓存在夹具目录里，只读加载；缺失时退回两秒回归动画。
		const FString CachedPackage = Sample.Mount + TEXT("Animation/MMDStagePreview8s");
		TStrongObjectPtr<UAnimSequence> Cached(LoadObject<UAnimSequence>(nullptr, *(CachedPackage + TEXT(".MMDStagePreview8s"))));
		if (Cached.IsValid())
		{
			Cached->WaitOnExistingCompression();
			if (Cached->IsBoneCompressedDataValid() && Cached->GetPlayLength() >= 6.5f && Cached->GetSkeleton() == Sample.Mesh->GetSkeleton())
			{
				Sample.Animation = Cached;
				Sample.AnimationPath = Cached->GetPathName();
				Sample.BeautyTime = 2.6f;
				Sample.FaceTimes = { 2.6f, 7.0f };
				return true;
			}
		}
		if (!Sample.AnimationPath.IsEmpty())
		{
			Sample.Animation.Reset(LoadObject<UAnimSequence>(nullptr, *Sample.AnimationPath));
		}
		if (!Sample.Animation.IsValid())
		{
			Error = TEXT("Sandbox character animation could not be loaded");
			return false;
		}
		Sample.BeautyTime = 0.6f;
		Sample.FaceTimes = { 0.6f };
		return true;
	}

	// ------------------------------------------------------------------------------------------
	// 截图装置：与 MMDStagePresets::CapturePreset 同一套预览场景 + SceneCapture2D 流程，
	// 但场景、角色、相机在多次截图之间保持不变，只切换材质 / 灯光 / CVar，保证 A/B 同镜头同灯光同姿态。
	// ------------------------------------------------------------------------------------------

	enum class EToonProbeShot : uint8 { Hero, Face, Cloth, Hair };

	const TCHAR* ToonProbeShotName(EToonProbeShot Shot)
	{
		static const TCHAR* Names[] = { TEXT("full"), TEXT("face"), TEXT("cloth"), TEXT("hair") };
		return Names[static_cast<int32>(Shot)];
	}

	struct FToonProbeRig
	{
		TUniquePtr<FPreviewScene> Scene;
		UWorld* World = nullptr;
		AMMDStageActor* Stage = nullptr;
		AActor* CharacterOwner = nullptr;
		AActor* CaptureOwner = nullptr;
		AActor* OccluderOwner = nullptr;
		TStrongObjectPtr<USkeletalMeshComponent> Character;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<USceneCaptureComponent2D> Capture;
		TStrongObjectPtr<UStaticMeshComponent> Occluder;
		UMMDPhysicsPlaybackAnimInstance* Playback = nullptr;
		FPostProcessSettings PostProcess;
		int32 HeadIndex = INDEX_NONE;
		int32 Frames = 4;
		float Intensities[3] = { 0, 0, 0 };

		~FToonProbeRig()
		{
			if (Scene.IsValid() && World)
			{
				if (Capture.IsValid()) Scene->RemoveComponent(Capture.Get());
				if (Character.IsValid()) Scene->RemoveComponent(Character.Get());
				if (Occluder.IsValid()) Scene->RemoveComponent(Occluder.Get());
				if (CaptureOwner) World->DestroyActor(CaptureOwner);
				if (CharacterOwner) World->DestroyActor(CharacterOwner);
				if (OccluderOwner) World->DestroyActor(OccluderOwner);
				if (Stage) World->DestroyActor(Stage);
			}
		}
	};

	void ToonProbeWriteLights(USceneCaptureComponent2D* Capture)
	{
		if (UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get())
		{
			// 常规 ViewExtension 排除了 SceneCapture，截图显式提交同一世界的灯光（Unlit 对照组需要）。
			const FVector CameraPosition = Capture->GetComponentLocation();
			Lights->CollectLightsForFrame(Capture->GetWorld(), &CameraPosition);
			ENQUEUE_RENDER_COMMAND(ToonProbeCaptureLights)(
				[Lights](FRHICommandListImmediate& RHICmdList)
				{
					FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("ToonProbeCaptureLights"));
					Lights->WriteLightData_RenderThread(GraphBuilder);
					GraphBuilder.Execute();
				});
			FlushRenderingCommands();
		}
	}

	void ToonProbeApplyBasePostProcess(FToonProbeRig& Rig, const UMMDStageRenderPreset* Preset)
	{
		FPostProcessSettings& PP = Rig.PostProcess;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = Preset->ExposureBias;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = Preset->BloomIntensity;
		PP.bOverride_BloomThreshold = true;
		PP.BloomThreshold = 0.6f;
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
		PP.VignetteIntensity = 0.20f;
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.0f;
		PP.bOverride_MotionBlurAmount = true;
		PP.MotionBlurAmount = 0.0f;
		PP.bOverride_DepthOfFieldSensorWidth = true;
		PP.DepthOfFieldSensorWidth = 36.0f;
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.bOverride_DepthOfFieldFstop = true;
	}

	void ToonProbeSetShot(FToonProbeRig& Rig, EToonProbeShot Shot)
	{
		FVector Location(65, -440, 145);
		FVector Target(0, -5, 75);
		float Fov = 50.0f;
		float FStop = 4.0f;
		if (Shot == EToonProbeShot::Face || Shot == EToonProbeShot::Hair)
		{
			const FVector Head = Rig.HeadIndex != INDEX_NONE ? Rig.Character->GetBoneTransform(Rig.HeadIndex).GetLocation() : FVector(0, -55, 130);
			if (Shot == EToonProbeShot::Face)
			{
				Location = Head + FVector(38.0f, -118.0f, 17.0f);
				Target = Head + FVector(0.0f, -3.0f, 8.0f);
				Fov = 35.0f;
				FStop = 2.8f;
			}
			else
			{
				// 从斜上方看头顶，天使环高光带应出现在头顶发片上。
				Location = Head + FVector(40.0f, -95.0f, 52.0f);
				Target = Head + FVector(0.0f, -2.0f, 10.0f);
				Fov = 32.0f;
				FStop = 4.0f;
			}
		}
		else if (Shot == EToonProbeShot::Cloth)
		{
			Location = FVector(70, -205, 138);
			Target = FVector(0, -55, 125);
			Fov = 35.0f;
			FStop = 5.6f;
		}
		Rig.PostProcess.DepthOfFieldFocalDistance = FVector::Distance(Location, Target);
		Rig.PostProcess.DepthOfFieldFstop = FStop;
		Rig.Capture->PostProcessSettings = Rig.PostProcess;
		Rig.Capture->FOVAngle = Fov;
		Rig.Capture->SetWorldLocationAndRotation(Location, (Target - Location).Rotation());
		Rig.Capture->bCameraCutThisFrame = true;
	}

	void ToonProbePose(FToonProbeRig& Rig, float AnimationTime)
	{
		if (Rig.Playback)
		{
			Rig.Playback->ResetPhysics();
			Rig.Playback->SetPlaying(true);
		}
		// 顺序推进固定步长，保证 Bullet 摆动确定，跳帧不会把刚体遗留在旧姿态。
		const int32 Steps = FMath::Max(1, FMath::RoundToInt(AnimationTime * 60.0f));
		for (int32 Step = 1; Step <= Steps; ++Step)
		{
			++GFrameCounter;
			Rig.Character->SetPosition(AnimationTime * (Step - 1) / Steps, false);
			Rig.Character->TickAnimation(AnimationTime / Steps, false);
			Rig.Character->RefreshBoneTransforms();
			Rig.Character->HandleExistingParallelEvaluationTask(true, true);
		}
		Rig.Character->UpdateComponentToWorld();
		Rig.Character->MarkRenderDynamicDataDirty();
		UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
		Rig.World->SendAllEndOfFrameUpdates();
	}

	struct FToonProbeImageStats
	{
		int32 Width = 0;
		int32 Height = 0;
		double MeanLuma = 0.0;
		double CenterMeanLuma = 0.0;
		int32 MinLuma = 255;
		int32 MaxLuma = 0;
		double Seconds = 0.0;
	};

	// 多帧累计后读回；2x 超采样后线性降采样，与现有截图流程一致。
	bool ToonProbeShoot(FToonProbeRig& Rig, const FString& Filename, FToonProbeImageStats& Stats, FString& Error, int32 Frames = -1)
	{
		const int32 FrameCount = Frames > 0 ? Frames : Rig.Frames;
		const double Start = FPlatformTime::Seconds();
		const bool bCut = Rig.Capture->bCameraCutThisFrame;
		for (int32 Index = 0; Index < FrameCount; ++Index)
		{
			Rig.Capture->bCameraCutThisFrame = bCut && Index == 0;
			ToonProbeWriteLights(Rig.Capture.Get());
			Rig.Capture->CaptureScene();
			FlushRenderingCommands();
		}
		Rig.Capture->bCameraCutThisFrame = false;
		UTextureRenderTarget2D* Target = Rig.Target.Get();
		FTextureRenderTargetResource* Resource = Target ? Target->GameThread_GetRenderTargetResource() : nullptr;
		TArray<FColor> Pixels;
		if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Target->SizeX * Target->SizeY)
		{
			Error = TEXT("Native GPU readback failed");
			return false;
		}
		const int32 Width = Target->SizeX / 2;
		const int32 Height = Target->SizeY / 2;
		TArray<FColor> Down;
		Down.SetNumUninitialized(Width * Height);
		double Sum = 0.0;
		double CenterSum = 0.0;
		int64 CenterCount = 0;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Source = 2 * Y * Target->SizeX + 2 * X;
				const FLinearColor Average = (FLinearColor(Pixels[Source]) + FLinearColor(Pixels[Source + 1])
					+ FLinearColor(Pixels[Source + Target->SizeX]) + FLinearColor(Pixels[Source + Target->SizeX + 1])) * 0.25f;
				FColor Out = Average.ToFColorSRGB();
				Out.A = 255;
				Down[Y * Width + X] = Out;
				const int32 Luma = (int32(Out.R) + Out.G + Out.B) / 3;
				Stats.MinLuma = FMath::Min(Stats.MinLuma, Luma);
				Stats.MaxLuma = FMath::Max(Stats.MaxLuma, Luma);
				Sum += Luma;
				if (X > Width / 3 && X < Width * 2 / 3 && Y > Height / 5 && Y < Height * 4 / 5)
				{
					CenterSum += Luma;
					++CenterCount;
				}
			}
		}
		Stats.Width = Width;
		Stats.Height = Height;
		Stats.MeanLuma = Sum / FMath::Max(1, Width * Height);
		Stats.CenterMeanLuma = CenterSum / FMath::Max<int64>(1, CenterCount);
		Stats.Seconds = FPlatformTime::Seconds() - Start;
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Down.GetData(), Down.Num()), Png);
		if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *Filename))
		{
			Error = TEXT("PNG could not be saved");
			return false;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------
	// 场景构建（与 CapturePreset 同序）
	// ------------------------------------------------------------------------------------------

	bool ToonProbeBuildRig(FToonProbeRig& Rig, UMMDStageRenderPreset* Preset, FToonProbeSample& Sample, bool bLumenTracking, FString& Error)
	{
		FPreviewScene::ConstructionValues Values;
		Values.SetCreateDefaultLighting(false).SetEditor(true).SetCreatePhysicsScene(false).SetTransactional(false);
		Values.AllowLumenPrimitiveTrackingInPreviewWorld(bLumenTracking);
		Rig.Scene = MakeUnique<FPreviewScene>(Values);
		Rig.World = Rig.Scene->GetWorld();
		if (!Rig.World || !Rig.World->Scene || !Rig.World->Scene->GetSkeletalMeshUpdater())
		{
			Error = TEXT("A rendering scene with skeletal mesh support is unavailable");
			return false;
		}
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Spawn.ObjectFlags = RF_Transient;
		Rig.Stage = Rig.World->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.CharacterOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.CaptureOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.OccluderOwner = Rig.World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Spawn);
		Rig.Character.Reset(Rig.CharacterOwner ? NewObject<USkeletalMeshComponent>(Rig.CharacterOwner, TEXT("ToonProbeCharacter"), RF_Transient) : nullptr);
		Rig.Capture.Reset(Rig.CaptureOwner ? NewObject<USceneCaptureComponent2D>(Rig.CaptureOwner, TEXT("ToonProbeCapture"), RF_Transient) : nullptr);
		Rig.Occluder.Reset(Rig.OccluderOwner ? NewObject<UStaticMeshComponent>(Rig.OccluderOwner, TEXT("ToonProbeOccluder"), RF_Transient) : nullptr);
		Rig.Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
		if (!Rig.Stage || !Rig.Character.IsValid() || !Rig.Capture.IsValid() || !Rig.Occluder.IsValid() || !Rig.Target.IsValid())
		{
			Error = TEXT("Preview world or component creation failed");
			return false;
		}
		Rig.CharacterOwner->SetRootComponent(Rig.Character.Get());
		Rig.CharacterOwner->AddInstanceComponent(Rig.Character.Get());
		Rig.CaptureOwner->SetRootComponent(Rig.Capture.Get());
		Rig.CaptureOwner->AddInstanceComponent(Rig.Capture.Get());
		Rig.OccluderOwner->SetRootComponent(Rig.Occluder.Get());
		Rig.OccluderOwner->AddInstanceComponent(Rig.Occluder.Get());

		Rig.Stage->Theme = Preset->Theme;
		Rig.Stage->ApplyStageRenderPreset(Preset);
		Rig.Stage->bUseManualTime = true;
		Rig.Stage->RebuildStage();
		Rig.Character->SetSkeletalMesh(Sample.Mesh.Get());
		Rig.Character->SetMobility(EComponentMobility::Movable);
		Rig.Character->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rig.Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Rig.Character->SetCastShadow(true);
		Rig.Character->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Rig.Scene->AddComponent(Rig.Character.Get(), FTransform(FRotator(0, 180, 0), FVector(0, -55, 2)));
		Rig.Character->SetAnimation(Sample.Animation.Get());
		Rig.Character->SetUpdateAnimationInEditor(true);

		// 遮挡物：不进主 pass，只投影。位置在运行时按主光方向放到角色与主光之间。
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, ToonProbeCubePath);
		if (Cube)
		{
			Rig.Occluder->SetStaticMesh(Cube);
			Rig.Occluder->SetMobility(EComponentMobility::Movable);
			Rig.Occluder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Rig.Occluder->SetCastShadow(true);
			Rig.Occluder->SetRenderInMainPass(false);
		}
		Rig.Scene->AddComponent(Rig.Occluder.Get(), FTransform(FRotator::ZeroRotator, FVector(0, 0, -5000), FVector(0.01f)));

		for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") })
		{
			Rig.HeadIndex = Rig.Character->GetBoneIndex(FName(Name));
			if (Rig.HeadIndex != INDEX_NONE) break;
		}

		// 渲染目标：FinalColorLDR 已由 tonemapper 编码；UNORM 配 2.2，避免 Gamma 1 压暗或 sRGB RTV 重复编码。
		Rig.Target->RenderTargetFormat = RTF_RGBA8;
		Rig.Target->bForceLinearGamma = true;
		Rig.Target->TargetGamma = 2.2f;
		Rig.Target->ClearColor = FLinearColor(0.005f, 0.009f, 0.022f, 1.0f);
		Rig.Target->InitAutoFormat(2560, 1440);
		Rig.Target->UpdateResourceImmediate(true);
		Rig.Capture->TextureTarget = Rig.Target.Get();
		Rig.Capture->CaptureSource = SCS_FinalColorLDR;
		Rig.Capture->bCaptureEveryFrame = false;
		Rig.Capture->bCaptureOnMovement = false;
		Rig.Capture->bAlwaysPersistRenderingState = true;
		Rig.Capture->PostProcessBlendWeight = 1.0f;
		ToonProbeApplyBasePostProcess(Rig, Preset);
		Rig.Scene->AddComponent(Rig.Capture.Get(), Rig.Capture->GetComponentTransform());
		Rig.Stage->EvaluateStageAtTime(0.0f);
		ToonProbeSetShot(Rig, EToonProbeShot::Hero);
		return true;
	}

	// 材质换完后统一收尾：等 shader、串流贴图、天空卷积、重新绑定 VMD（usage 编译可能重新注册组件）。
	double ToonProbeFinishMaterialSwap(FToonProbeRig& Rig, FToonProbeSample& Sample)
	{
		Rig.World->SendAllEndOfFrameUpdates();
		const double WaitSeconds = ToonProbeWaitForShaders();
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			if (UMaterialInterface* Material = Rig.Character->GetMaterial(Slot))
			{
				Material->SetForceMipLevelsToBeResident(false, true, 30.0f, 0, true);
			}
		}
		IStreamingManager::Get().StreamAllResources(30.0f);
		if (Rig.Stage->GardenSkyLight)
		{
			Rig.Stage->GardenSkyLight->RecaptureSky();
			USkyLightComponent::UpdateSkyCaptureContents(Rig.World);
			FlushRenderingCommands();
		}
		Rig.Character->MarkRenderStateDirty();
		Rig.World->SendAllEndOfFrameUpdates();
		FlushRenderingCommands();
		Rig.Playback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(Rig.Character.Get(), Sample.Animation.Get(), false, true);
		Rig.Character->SetUpdateAnimationInEditor(true);
		return WaitSeconds;
	}

	// ------------------------------------------------------------------------------------------
	// 槽位级 Toon 参数（按 MaterialRole 区分）
	// ------------------------------------------------------------------------------------------

	struct FToonProbeSlotOptions
	{
		float SphereBlendSkin = 0.0f;
		float HairAnisotropy = 0.0f;
		float HairRoughness = 0.35f;
		bool bPerRoleProfiles = false;
		TMap<EMMDMaterialRole, UToonProfile*> RoleProfiles;
		// 0 = 不处理；1 = 只写字段（不刷新代理）；2 = 写字段 + ClearParameterValues 恢复；3 = 写字段 + PostEditChange
		int32 OverrideMethod = 0;
	};

	// 返回成功写入 ToonProfileOverride 的 MID 数。
	int32 ToonProbeConfigureSlots(FToonProbeRig& Rig, const FToonProbeSlotOptions& Options)
	{
		int32 OverrideCount = 0;
		USkeletalMeshComponent* Character = Rig.Character.Get();
		for (int32 Slot = 0; Slot < Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Character->GetMaterial(Slot));
			if (!MID)
			{
				continue;
			}
			const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Character, Slot);
			float Roughness = 0.5f;
			float Specular = 0.5f;
			float Anisotropy = 0.0f;
			float Sphere = 0.0f;
			switch (Role)
			{
			case EMMDMaterialRole::Skin: Roughness = 0.55f; Specular = 0.35f; Sphere = Options.SphereBlendSkin; break;
			case EMMDMaterialRole::Hair: Roughness = Options.HairRoughness; Specular = 0.5f; Anisotropy = Options.HairAnisotropy; break;
			case EMMDMaterialRole::Cloth: Roughness = 0.6f; Specular = 0.3f; break;
			case EMMDMaterialRole::Metal: Roughness = 0.25f; Specular = 0.8f; break;
			case EMMDMaterialRole::Eye: Roughness = 0.25f; Specular = 0.5f; break;
			default: break;
			}
			if (Slot < 6 || Role == EMMDMaterialRole::Skin)
			{
				UTexture* BaseTex = nullptr;
				const bool bHas = MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), BaseTex);
				FLinearColor BaseCol = FLinearColor::White;
				MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), BaseCol);
				UE_LOG(LogTemp, Display, TEXT("[ToonProbe] slot %d role=%d parent=%s base_tex=%s(%d) base_color=%s"),
					Slot, static_cast<int32>(Role), MID->Parent ? *MID->Parent->GetName() : TEXT("none"),
					BaseTex ? *BaseTex->GetName() : TEXT("none"), bHas ? 1 : 0, *BaseCol.ToString());
			}
			MID->SetScalarParameterValue(TEXT("ToonRoughness"), Roughness);
			MID->SetScalarParameterValue(TEXT("ToonSpecular"), Specular);
			MID->SetScalarParameterValue(TEXT("ToonAnisotropy"), Anisotropy);
			MID->SetScalarParameterValue(TEXT("ToonSphereBlend"), Sphere);

			if (Options.OverrideMethod != 0)
			{
				UToonProfile* const* Found = Options.RoleProfiles.Find(Role);
				UToonProfile* Profile = Found ? *Found : nullptr;
				if (Profile)
				{
					MID->ToonProfileOverride = Profile;
					MID->bOverrideToonProfile = true;
					++OverrideCount;
					if (Options.OverrideMethod == 2)
					{
						// ClearParameterValues 公开且运行时可用，内部以 InitResources 收尾，会把覆盖推送到渲染代理；
						// 代价是清空参数，所以先快照再恢复。
						const TArray<FScalarParameterValue> Scalars = MID->ScalarParameterValues;
						const TArray<FVectorParameterValue> Vectors = MID->VectorParameterValues;
						const TArray<FTextureParameterValue> Textures = MID->TextureParameterValues;
						MID->ClearParameterValues();
						for (const FScalarParameterValue& Value : Scalars) MID->SetScalarParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
						for (const FVectorParameterValue& Value : Vectors) MID->SetVectorParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
						for (const FTextureParameterValue& Value : Textures) MID->SetTextureParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
					}
					else if (Options.OverrideMethod == 3)
					{
						MID->PostEditChange();
					}
				}
			}
		}
		return OverrideCount;
	}

	void ToonProbeSetShowFlags(FToonProbeRig& Rig, const TArray<FString>& DisabledFlags)
	{
		TArray<FEngineShowFlagsSetting> Settings;
		for (const FString& Name : DisabledFlags)
		{
			FEngineShowFlagsSetting& Setting = Settings.AddDefaulted_GetRef();
			Setting.ShowFlagName = Name;
			Setting.Enabled = false;
		}
		Rig.Capture->SetShowFlagSettings(Settings);
	}

	void ToonProbeSetLights(FToonProbeRig& Rig, bool bKey, bool bFill, bool bRim)
	{
		ULightComponent* Lights[3] = { Rig.Stage->KeyLight.Get(), Rig.Stage->FillLight.Get(), Rig.Stage->RimLight.Get() };
		const bool Enabled[3] = { bKey, bFill, bRim };
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (!Lights[Index]) continue;
			if (Rig.Intensities[Index] <= 0.0f && Lights[Index]->Intensity > 0.0f)
			{
				Rig.Intensities[Index] = Lights[Index]->Intensity;
			}
			Lights[Index]->SetIntensity(Enabled[Index] ? Rig.Intensities[Index] : 0.0f);
		}
		Rig.World->SendAllEndOfFrameUpdates();
	}

	// 把一个细长竖条放到"角色躯干 - 主光方向"上，主光投下一道竖条阴影。
	void ToonProbePlaceOccluder(FToonProbeRig& Rig, bool bEnable)
	{
		if (!bEnable)
		{
			Rig.Occluder->SetWorldLocationAndRotation(FVector(0, 0, -5000), FRotator::ZeroRotator);
			Rig.World->SendAllEndOfFrameUpdates();
			return;
		}
		const FVector LightTravel = Rig.Stage->KeyLight ? Rig.Stage->KeyLight->GetComponentRotation().Vector() : FVector(0.567f, 0.630f, -0.530f);
		const FVector Target(0, -55, 105);
		const FVector Location = Target - LightTravel * 120.0f;
		Rig.Occluder->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
		Rig.Occluder->SetWorldScale3D(FVector(0.16f, 0.16f, 4.0f));
		Rig.World->SendAllEndOfFrameUpdates();
	}

	// ------------------------------------------------------------------------------------------
	// 实验记录
	// ------------------------------------------------------------------------------------------

	struct FToonProbeLog
	{
		FString OutDir;
		FString Tag;
		TArray<TSharedPtr<FJsonValue>> Images;
		TArray<TSharedPtr<FJsonValue>> Notes;
		TArray<TSharedPtr<FJsonValue>> SlotChecks;
		int32 Failures = 0;

		FString File(const FString& Name) const
		{
			return FPaths::Combine(OutDir, FString::Printf(TEXT("%s_%s.png"), *Tag, *Name));
		}

		void Note(const FString& Key, const FString& Value)
		{
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetStringField(Key, Value);
			Notes.Add(MakeShared<FJsonValueObject>(Item));
			UE_LOG(LogTemp, Display, TEXT("[ToonProbe] %s = %s"), *Key, *Value);
		}
	};

	void ToonProbeValidateSlots(FToonProbeRig& Rig, FToonProbeLog& Log, const FString& Label)
	{
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Material = Rig.Character->GetMaterial(Slot);
			const FMaterialRenderProxy* Proxy = Material ? Material->GetRenderProxy() : nullptr;
			bool bDefaultMaterial = true;
			bool bFallback = true;
			if (Proxy)
			{
				ENQUEUE_RENDER_COMMAND(ToonProbeCheckFallback)(
					[Proxy, &bDefaultMaterial, &bFallback](FRHICommandListImmediate& RHICmdList)
					{
						// 引擎仅在实际回退时写此输出；无回退时保持传入值。
						const FMaterialRenderProxy* ActualProxy = Proxy;
						const FMaterial& ActualMaterial = Proxy->GetMaterialWithFallback(GMaxRHIFeatureLevel, ActualProxy);
						bDefaultMaterial = ActualMaterial.IsDefaultMaterial();
						bFallback = ActualProxy != Proxy;
					});
				FlushRenderingCommands();
			}
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetStringField(TEXT("label"), Label);
			Item->SetNumberField(TEXT("slot"), Slot);
			Item->SetStringField(TEXT("slot_name"), Rig.Character->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialSlotName.ToString());
			Item->SetBoolField(TEXT("rendered_default_material"), bDefaultMaterial);
			Item->SetBoolField(TEXT("rendered_fallback"), bFallback);
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material))
			{
				Item->SetStringField(TEXT("parent"), MID->Parent ? MID->Parent->GetPathName() : TEXT("none"));
				Item->SetStringField(TEXT("profile_override"), MID->ToonProfileOverride ? MID->ToonProfileOverride->GetPathName() : TEXT("none"));
				UTexture* Texture = nullptr;
				MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), Texture);
				Item->SetStringField(TEXT("base_color_texture"), Texture ? Texture->GetPathName() : TEXT("none"));
				float SdfEnabled = 0.0f;
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSdfEnabled")), SdfEnabled);
				Item->SetNumberField(TEXT("face_sdf_enabled"), SdfEnabled);
				Texture = nullptr;
				MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSdfTexture")), Texture);
				Item->SetStringField(TEXT("face_sdf_texture"), Texture ? Texture->GetPathName() : TEXT("none"));
			}
			if (const UMMDStageMaterialState* State = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass())))
			{
				if (State->RoleReasons.IsValidIndex(Slot)) Item->SetStringField(TEXT("role_reason"), State->RoleReasons[Slot]);
			}
			Log.SlotChecks.Add(MakeShared<FJsonValueObject>(Item));
			Log.Failures += bDefaultMaterial || bFallback ? 1 : 0;
		}
	}

	void ToonProbeCapture(FToonProbeRig& Rig, FToonProbeLog& Log, const FString& Name, int32 Frames = -1)
	{
		FToonProbeImageStats Stats;
		FString Error;
		const FString Filename = Log.File(Name);
		const bool bOk = ToonProbeShoot(Rig, Filename, Stats, Error, Frames);
		TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
		Item->SetStringField(TEXT("name"), Name);
		Item->SetStringField(TEXT("file"), Filename);
		Item->SetBoolField(TEXT("ok"), bOk);
		Item->SetNumberField(TEXT("mean_luma"), Stats.MeanLuma);
		Item->SetNumberField(TEXT("center_mean_luma"), Stats.CenterMeanLuma);
		Item->SetNumberField(TEXT("min_luma"), Stats.MinLuma);
		Item->SetNumberField(TEXT("max_luma"), Stats.MaxLuma);
		Item->SetNumberField(TEXT("seconds"), Stats.Seconds);
		if (!Error.IsEmpty()) Item->SetStringField(TEXT("error"), Error);
		Log.Images.Add(MakeShared<FJsonValueObject>(Item));
		Log.Failures += bOk ? 0 : 1;
		UE_LOG(LogTemp, Display, TEXT("[ToonProbe] shot %s ok=%d mean=%.2f center=%.2f %.2fs"), *Name, bOk ? 1 : 0, Stats.MeanLuma, Stats.CenterMeanLuma, Stats.Seconds);
	}

	// 一次性拍某个机位：切姿态、切机位、截图。
	void ToonProbeCaptureShot(FToonProbeRig& Rig, FToonProbeSample& Sample, FToonProbeLog& Log, const FString& Prefix, EToonProbeShot Shot, int32 Frames = -1)
	{
		float AnimationTime = Sample.BeautyTime;
		if (Shot == EToonProbeShot::Face || Shot == EToonProbeShot::Hair)
		{
			AnimationTime = Sample.FaceTimes.Num() > 0 ? Sample.FaceTimes[0] : Sample.BeautyTime;
		}
		ToonProbePose(Rig, AnimationTime);
		ToonProbeSetShot(Rig, Shot);
		ToonProbeCapture(Rig, Log, FString::Printf(TEXT("%s_%s"), *Prefix, ToonProbeShotName(Shot)), Frames);
	}

	bool ToonProbeApplyPreset(FToonProbeRig& Rig, UMMDStageRenderPreset* Preset)
	{
		return UMMDStageLibrary::ApplyCharacterRenderPreset(Rig.Character.Get(), Preset, true);
	}

	void ToonProbeRefreshProfile(UToonProfile* Profile)
	{
		// 编辑器路径：PostEditChange -> 渲染线程 UpdateProfile，图集下一帧重建。
		Profile->PostEditChange();
		FlushRenderingCommands();
	}

	FString ToonProbeJsonToString(const TSharedRef<FJsonObject>& Object)
	{
		FString Json;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json));
		return Json;
	}
}

UMMDEndfieldToonProbeCommandlet::UMMDEndfieldToonProbeCommandlet()
{
	IsClient = true;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMMDEndfieldToonProbeCommandlet::Main(const FString& Params)
{
	FToonProbeLog Log;
	Log.OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Phase2Reference/Endfield2/toon-probe"));
	FParse::Value(*Params, TEXT("OutDir="), Log.OutDir);
	Log.Tag = TEXT("probe");
	FParse::Value(*Params, TEXT("Tag="), Log.Tag);
	FString ExpText = TEXT("ab");
	// FParse 默认在逗号处结束，必须关闭，才能实际执行全部请求的实验。
	FParse::Value(*Params, TEXT("Exp="), ExpText, false);
	TArray<FString> Exps;
	ExpText.ParseIntoArray(Exps, TEXT(","), true);
	const TArray<FString> AllowedExps = { TEXT("ab"), TEXT("shadow"), TEXT("ramp"), TEXT("lighting"), TEXT("face"),
		TEXT("hair"), TEXT("override"), TEXT("translucent"), TEXT("runtime"), TEXT("sdf"), TEXT("backend"), TEXT("all") };
	for (const FString& Exp : Exps)
	{
		if (!AllowedExps.Contains(Exp))
		{
			UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Unknown experiment: %s"), *Exp);
			return 1;
		}
	}
	int32 Frames = 4;
	FParse::Value(*Params, TEXT("Frames="), Frames);
	const bool bLumenTracking = FParse::Param(*Params, TEXT("LumenTracking"));
	ToonProbeSetCVarInt(TEXT("r.DepthOfFieldQuality"), 0);
	ToonProbeSetCVarInt(TEXT("r.MotionBlurQuality"), 0);
	IFileManager::Get().MakeDirectory(*Log.OutDir, true);

	TSharedRef<FJsonObject> Report = ToonProbeJsonObject();
	TArray<FString> Errors;

	// ---- 环境 ----
	const bool bRender = FApp::CanEverRender() && !GUsingNullRHI;
	TSharedRef<FJsonObject> Environment = ToonProbeJsonObject();
	Environment->SetBoolField(TEXT("can_render"), bRender);
	Environment->SetBoolField(TEXT("substrate_enabled"), Substrate::IsSubstrateEnabled());
	Environment->SetBoolField(TEXT("substrate_blendable_gbuffer"), Substrate::IsSubstrateBlendableGBufferEnabled(GMaxRHIShaderPlatform));
	Environment->SetBoolField(TEXT("toon_profile_enabled"), Substrate::IsToonProfileEnabled());
	Environment->SetStringField(TEXT("shader_platform"), LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
	Environment->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : FString(TEXT("none")));
	for (const TCHAR* Name : { TEXT("r.Substrate"), TEXT("r.Substrate.ProjectGBufferFormat"), TEXT("r.Substrate.ClosuresPerPixel"),
		TEXT("r.Substrate.BytesPerPixel"), TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse"),
		TEXT("r.Substrate.Experimental.ToonReflectionQuantizationEnabled"), TEXT("r.Substrate.ToonProfile.Resolution"),
		TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Shadow.Virtual.Enable"),
		TEXT("r.RayTracing"), TEXT("r.Lumen.HardwareRayTracing"), TEXT("r.GenerateMeshDistanceFields"),
		TEXT("r.AntiAliasingMethod"), TEXT("r.DefaultFeature.AutoExposure") })
	{
		Environment->SetStringField(Name, ToonProbeCVar(Name));
	}
	Report->SetObjectField(TEXT("environment"), Environment);
	UE_LOG(LogTemp, Display, TEXT("[ToonProbe] Substrate=%s Blendable=%s UnifiedDiffuse=%s"),
		*ToonProbeCVar(TEXT("r.Substrate")), Substrate::IsSubstrateBlendableGBufferEnabled(GMaxRHIShaderPlatform) ? TEXT("1") : TEXT("0"),
		*ToonProbeCVar(TEXT("r.Substrate.Experimental.ToonUnifiedDiffuse")));
	if (!bRender)
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Rendering is required: use -AllowCommandletRendering -RenderOffscreen"));
		return 1;
	}
	const bool bSubstrate = Substrate::IsSubstrateEnabled();
	FString NativePresetPath;
	FParse::Value(*Params, TEXT("NativePreset="), NativePresetPath);
	TStrongObjectPtr<UMMDStageRenderPreset> NativePreset;
	if (!NativePresetPath.IsEmpty())
	{
		NativePreset.Reset(LoadObject<UMMDStageRenderPreset>(nullptr, *NativePresetPath));
		if (!NativePreset.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Native preset could not be loaded: %s"), *NativePresetPath);
			return 1;
		}
		Report->SetStringField(TEXT("native_preset"), NativePresetPath);
		for (const FString& Exp : Exps)
		{
			if (Exp != TEXT("ab") && Exp != TEXT("runtime") && Exp != TEXT("shadow") && Exp != TEXT("lighting") && Exp != TEXT("sdf") && Exp != TEXT("backend"))
			{
				UE_LOG(LogTemp, Error, TEXT("[ToonProbe] NativePreset only supports ab/runtime/shadow/lighting/sdf/backend, received %s"), *Exp);
				return 1;
			}
		}
	}
	if ((Exps.Contains(TEXT("runtime")) || Exps.Contains(TEXT("sdf"))) && !NativePreset.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Runtime checks require -NativePreset"));
		return 1;
	}
	if (!bSubstrate && !NativePreset.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Native Toon requires Substrate; use -NativePreset for fallback verification"));
		return 1;
	}

	// ---- 预设（只读）与 LightDataRT 收尾保护（与 MMDStagePresets 相同做法）----
	UMMDStageRenderPreset* EndfieldPreset = LoadObject<UMMDStageRenderPreset>(nullptr, ToonProbeEndfieldPresetPath);
	if (!EndfieldPreset || !EndfieldPreset->CharacterMaterialOpaque || !EndfieldPreset->CharacterMaterialMasked || !EndfieldPreset->CharacterMaterialTranslucent)
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] DA_Render_Endfield or its parents could not be loaded"));
		return 1;
	}
	UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
	TStrongObjectPtr<UTextureRenderTarget2D> OriginalLightTarget(Lights ? Lights->GetLightDataRenderTarget() : nullptr);
	TStrongObjectPtr<UTextureRenderTarget2D> SharedLightTarget(LoadObject<UTextureRenderTarget2D>(
		nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT")));
	const int32 OriginalWidth = SharedLightTarget.IsValid() ? SharedLightTarget->SizeX : 0;
	const int32 OriginalHeight = SharedLightTarget.IsValid() ? SharedLightTarget->SizeY : 0;
	const ETextureRenderTargetFormat OriginalFormat = SharedLightTarget.IsValid() ? SharedLightTarget->RenderTargetFormat.GetValue() : RTF_RGBA32f;
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
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] LightData subsystem is unavailable or a preview light override is active"));
		return 1;
	}

	FToonProbeSample Sample;
	FString Error;
	if (!ToonProbeLoadSample(Sample, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] %s"), *Error);
		return 1;
	}
	Report->SetStringField(TEXT("character"), Sample.MeshPath);
	Report->SetStringField(TEXT("animation"), Sample.AnimationPath);
	const FString SdfMount = TEXT("/MMDSdfValidation/");
	const FString SdfDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EndfieldValidation/FaceSdfFixture/"));
	FPackageName::RegisterMountPoint(SdfMount, SdfDirectory);
	ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(SdfMount, SdfDirectory); };
	const bool bSaveSdfFixture = FParse::Param(*Params, TEXT("SaveSdfFixture"));
	const bool bLoadSdfFixture = FParse::Param(*Params, TEXT("LoadSdfFixture"));
	if (bLoadSdfFixture && (bSaveSdfFixture || FParse::Param(*Params, TEXT("BakeSdf")))) return 1;
	if (bLoadSdfFixture)
	{
		Sample.Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/MMDSdfValidation/SK_FaceFixture.SK_FaceFixture")));
		if (!Sample.Mesh.IsValid()) return 1;
		Report->SetBoolField(TEXT("sdf_fixture_read_from_disk"), true);
	}
	if (FParse::Param(*Params, TEXT("BakeSdf")) || bSaveSdfFixture)
	{
		if (bSaveSdfFixture)
		{
			if (FPackageName::DoesPackageExist(TEXT("/MMDSdfValidation/SK_FaceFixture")))
			{
				Sample.Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/MMDSdfValidation/SK_FaceFixture.SK_FaceFixture")));
				if (!Sample.Mesh.IsValid()) return 1;
				Sample.Mesh->GetOutermost()->FullyLoad();
			}
			else
			{
				UPackage* Package = CreatePackage(TEXT("/MMDSdfValidation/SK_FaceFixture"));
				Sample.Mesh.Reset(DuplicateObject<USkeletalMesh>(Sample.Mesh.Get(), Package, TEXT("SK_FaceFixture")));
			}
			Sample.Mesh->SetFlags(RF_Public | RF_Standalone);
		}
		MMDEndfieldFaceBinding::FResult Bake;
		const bool bBaked = MMDEndfieldFaceBinding::BakeAndBind(Sample.Mesh.Get(), TEXT("/MMDSdfValidation/Textures"), bSaveSdfFixture, Bake);
		Report->SetBoolField(TEXT("sdf_bake_success"), bBaked);
		Report->SetNumberField(TEXT("sdf_bound_slots"), Bake.BoundSlots.Num());
		Report->SetNumberField(TEXT("sdf_face_texels"), Bake.FaceTexelCount);
		TArray<TSharedPtr<FJsonValue>> BakeStats;
		for (const MMDEndfieldFaceBinding::FSlotStats& Stats : Bake.SlotStats)
		{
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Stats.Slot);
			Item->SetNumberField(TEXT("face_texels"), Stats.FaceTexels);
			Item->SetNumberField(TEXT("mirrored_texels"), Stats.MirroredTexels);
			Item->SetNumberField(TEXT("conflicting_texels"), Stats.ConflictingTexels);
			Item->SetNumberField(TEXT("triangles"), Stats.Triangles);
			Item->SetNumberField(TEXT("front_dark_texels"), Stats.FrontDarkTexels);
			Item->SetNumberField(TEXT("noncontinuous_texels"), Stats.NonContinuousTexels);
			BakeStats.Add(MakeShared<FJsonValueObject>(Item));
		}
		Report->SetArrayField(TEXT("sdf_bake_slot_stats"), BakeStats);
		for (const FString& Note : Bake.Notes) Log.Note(TEXT("sdf_bake_note"), Note);
		for (const FString& Failure : Bake.Errors) Log.Note(TEXT("sdf_bake_error"), Failure);
		if (!bBaked || Bake.BoundSlots.IsEmpty()) return 1;
		if (bSaveSdfFixture)
		{
			FSavePackageArgs Save;
			Save.TopLevelFlags = RF_Public | RF_Standalone;
			const FString File = SdfDirectory / TEXT("SK_FaceFixture.uasset");
			if (!UPackage::SavePackage(Sample.Mesh->GetOutermost(), Sample.Mesh.Get(), *File, Save)) return 1;
			Report->SetStringField(TEXT("saved_sdf_fixture"), File);
		}
	}

	Lights->SetLightDataRenderTarget(SharedLightTarget.Get());
	FVector4f EmptyShadowData[18];
	for (FVector4f& Vector : EmptyShadowData) Vector = FVector4f(0, 0, 0, 0);
	Lights->SetShadowCameraData(EmptyShadowData);

	// ---- 原生 Toon 材质与 Profile ----
	TArray<TStrongObjectPtr<UObject>> KeepAlive;
	UTexture* White = LoadObject<UTexture>(nullptr, ToonProbeWhiteTexturePath);
	TMap<EToonProbeProfile, UToonProfile*> Profiles;
	for (int32 Index = 0; Index < static_cast<int32>(EToonProbeProfile::Count); ++Index)
	{
		const EToonProbeProfile Kind = static_cast<EToonProbeProfile>(Index);
		Profiles.Add(Kind, ToonProbeMakeProfile(Kind, KeepAlive));
	}

	TArray<TSharedPtr<FJsonValue>> MaterialReports;
	TMap<FString, UMaterial*> ToonMaterials;
	const double MaterialStart = FPlatformTime::Seconds();
	if (bSubstrate)
	{
		const struct { const TCHAR* Name; EBlendMode Blend; bool bWeight; } Variants[] =
		{
			{ TEXT("ToonProbe_Opaque"), BLEND_Opaque, false },
			{ TEXT("ToonProbe_Masked"), BLEND_Masked, false },
			{ TEXT("ToonProbe_TranslucentGrey"), BLEND_TranslucentGreyTransmittance, true },
		};
		for (const auto& Variant : Variants)
		{
			if (!NativePreset.IsValid() && FString(Variant.Name).Contains(TEXT("Translucent")) && !ToonProbeHasExp(Exps, TEXT("translucent")))
			{
				continue;
			}
			FToonProbeMaterialSpec Spec;
			Spec.Name = Variant.Name;
			Spec.Blend = Variant.Blend;
			Spec.bCoverageWeight = Variant.bWeight;
			Spec.Profile = Profiles[EToonProbeProfile::Standard];
			UMaterial* Material = NativePreset.IsValid()
				? Cast<UMaterial>(Spec.Blend == BLEND_Opaque ? NativePreset->CharacterToonMaterialOpaque.Get()
					: Spec.Blend == BLEND_Masked ? NativePreset->CharacterToonMaterialMasked.Get()
					: NativePreset->CharacterToonMaterialTranslucent.Get())
				: ToonProbeBuildMaterial(Spec, White);
			if (Material)
			{
				Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
				ToonMaterials.Add(Variant.Name, Material);
			}
		}
		const double BuildSeconds = FPlatformTime::Seconds() - MaterialStart;
		const double CompileSeconds = ToonProbeWaitForShaders();
		for (const TPair<FString, UMaterial*>& Pair : ToonMaterials)
		{
			bool bReady = false;
			const TArray<FString> CompileErrors = ToonProbeCompileErrors(Pair.Value, bReady);
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetStringField(TEXT("material"), Pair.Key);
			Item->SetStringField(TEXT("blend_mode"), UEnum::GetValueAsString(Pair.Value->BlendMode.GetValue()));
			Item->SetBoolField(TEXT("shader_map_ready"), bReady);
			const FMaterialResource* Resource = Pair.Value->GetMaterialResource(GMaxRHIShaderPlatform);
			const FMaterialShaderMap* ShaderMap = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
			Item->SetBoolField(TEXT("compiled_successfully"), ShaderMap && ShaderMap->CompiledSuccessfully());
			Item->SetBoolField(TEXT("compilation_finalized"), ShaderMap && ShaderMap->IsCompilationFinalized());
			Item->SetNumberField(TEXT("shader_count"), ShaderMap ? ShaderMap->GetShaderNum() : 0);
			Item->SetNumberField(TEXT("toon_profile_count"), Pair.Value->NumToonProfile_Internal());
			TArray<TSharedPtr<FJsonValue>> JsonErrors;
			for (const FString& CompileError : CompileErrors) JsonErrors.Add(MakeShared<FJsonValueString>(CompileError));
			Item->SetArrayField(TEXT("errors"), JsonErrors);
			MaterialReports.Add(MakeShared<FJsonValueObject>(Item));
			UE_LOG(LogTemp, Display, TEXT("[ToonProbe] material %s ready=%d errors=%d toon_profiles=%d"),
				*Pair.Key, bReady ? 1 : 0, CompileErrors.Num(), static_cast<int32>(Pair.Value->NumToonProfile_Internal()));
			for (const FString& CompileError : CompileErrors)
			{
				UE_LOG(LogTemp, Warning, TEXT("[ToonProbe]   %s: %s"), *Pair.Key, *CompileError);
			}
		}
		Report->SetNumberField(TEXT("toon_material_build_seconds"), BuildSeconds);
		Report->SetNumberField(TEXT("toon_material_compile_wait_seconds"), CompileSeconds);
		UE_LOG(LogTemp, Display, TEXT("[ToonProbe] toon materials built in %.2fs, compile wait %.2fs"), BuildSeconds, CompileSeconds);
	}
	Report->SetArrayField(TEXT("toon_materials"), MaterialReports);

	auto ReadyMaterial = [&](const TCHAR* Name) -> UMaterial*
	{
		UMaterial** Found = ToonMaterials.Find(FString(Name));
		bool bReady = false;
		if (Found && *Found && ToonProbeCompileErrors(*Found, bReady).IsEmpty() && bReady)
		{
			return *Found;
		}
		return nullptr;
	};
	UMaterial* ToonOpaque = ReadyMaterial(TEXT("ToonProbe_Opaque"));
	UMaterial* ToonMasked = ReadyMaterial(TEXT("ToonProbe_Masked"));
	UMaterial* ToonTranslucent = ReadyMaterial(TEXT("ToonProbe_TranslucentGrey"));
	Report->SetBoolField(TEXT("toon_opaque_ready"), ToonOpaque != nullptr);
	Report->SetBoolField(TEXT("toon_masked_ready"), ToonMasked != nullptr);
	Report->SetBoolField(TEXT("toon_translucent_ready"), ToonTranslucent != nullptr);
	if (bSubstrate && (!ToonOpaque || !ToonMasked))
	{
		++Log.Failures;
		Log.Note(TEXT("required_material_failure"), TEXT("Opaque or masked Toon shaders did not compile"));
	}

	// 两套预设：现状 Unlit（Endfield 原样）与 Toon（只替换三个父材质，其余灯光/后处理/角色分类完全相同）。
	TStrongObjectPtr<UMMDStageRenderPreset> ToonPreset(DuplicateObject<UMMDStageRenderPreset>(NativePreset.IsValid() ? NativePreset.Get() : EndfieldPreset, GetTransientPackage()));
	ToonPreset->SetFlags(RF_Transient);
	ToonPreset->ClearFlags(RF_Public | RF_Standalone);
	if (!NativePreset.IsValid())
	{
		ToonPreset->CharacterMaterialOpaque = ToonOpaque ? ToonOpaque : EndfieldPreset->CharacterMaterialOpaque.Get();
		ToonPreset->CharacterMaterialMasked = ToonMasked ? ToonMasked : EndfieldPreset->CharacterMaterialMasked.Get();
		ToonPreset->CharacterMaterialTranslucent = ToonTranslucent ? ToonTranslucent : EndfieldPreset->CharacterMaterialTranslucent.Get();
	}
	// Toon BSDF 不支持半透明时保留现有 Unlit 半透明父材质（退路）。
	Report->SetBoolField(TEXT("toon_preset_uses_toon_translucent"), ToonTranslucent != nullptr);
	TStrongObjectPtr<UMMDStageRenderPreset> HybridPreset(DuplicateObject<UMMDStageRenderPreset>(ToonPreset.Get(), GetTransientPackage()));
	HybridPreset->SetFlags(RF_Transient);
	HybridPreset->ClearFlags(RF_Public | RF_Standalone);
	HybridPreset->CharacterMaterialTranslucent = EndfieldPreset->CharacterMaterialTranslucent.Get();

	// ---- 场景 ----
	FToonProbeRig Rig;
	Rig.Frames = Frames;
	if (!ToonProbeBuildRig(Rig, EndfieldPreset, Sample, bLumenTracking, Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] %s"), *Error);
		return 1;
	}
	ToonProbeApplyPreset(Rig, EndfieldPreset);
	ToonProbeSetLights(Rig, true, true, true);
	{
		const double Seconds = ToonProbeFinishMaterialSwap(Rig, Sample);
		Report->SetNumberField(TEXT("unlit_compile_wait_seconds"), Seconds);
		UE_LOG(LogTemp, Display, TEXT("[ToonProbe] unlit scene compile wait %.2fs"), Seconds);
	}
	ToonProbeValidateSlots(Rig, Log, TEXT("unlit_baseline"));
	// 预热：首帧的 TAA/Lumen/天空历史不稳定，丢弃数帧。
	{
		ToonProbePose(Rig, Sample.BeautyTime);
		ToonProbeSetShot(Rig, EToonProbeShot::Hero);
		FToonProbeImageStats Warm;
		FString WarmError;
		ToonProbeShoot(Rig, Log.File(TEXT("_warmup")), Warm, WarmError, 3);
		IFileManager::Get().Delete(*Log.File(TEXT("_warmup")));
	}

	const EToonProbeShot AllShots[] = { EToonProbeShot::Hero, EToonProbeShot::Face, EToonProbeShot::Cloth, EToonProbeShot::Hair };

	// =========================================================================================
	// ab：现状 Unlit 与 Toon 同镜头同灯光同姿态
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("ab")))
	{
		for (EToonProbeShot Shot : AllShots)
		{
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("unlit"), Shot);
		}
		if (NativePreset.IsValid() || (bSubstrate && ToonOpaque))
		{
			ToonProbeApplyPreset(Rig, ToonPreset.Get());
			FToonProbeSlotOptions Options;
			if (!NativePreset.IsValid()) ToonProbeConfigureSlots(Rig, Options);
			const double Seconds = ToonProbeFinishMaterialSwap(Rig, Sample);
			Log.Note(TEXT("toon_apply_compile_wait_seconds"), FString::SanitizeFloat(Seconds));
			ToonProbeValidateSlots(Rig, Log, bSubstrate ? TEXT("toon_native") : TEXT("native_preset_unlit_fallback"));
			for (EToonProbeShot Shot : AllShots)
			{
				ToonProbeCaptureShot(Rig, Sample, Log, TEXT("toon"), Shot);
			}
			// 混合：半透明槽位仍走现有 Unlit（Toon 半透明失败时与上面相同）。
			if (ToonTranslucent && !NativePreset.IsValid())
			{
				ToonProbeApplyPreset(Rig, HybridPreset.Get());
				ToonProbeConfigureSlots(Rig, Options);
				ToonProbeFinishMaterialSwap(Rig, Sample);
				for (EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Face })
				{
					ToonProbeCaptureShot(Rig, Sample, Log, TEXT("toonhybrid"), Shot);
				}
			}
		}
	}

	// 其余实验都以 Toon 预设为基线。
	auto UseToonBaseline = [&]()
	{
		ToonProbeApplyPreset(Rig, ToonPreset.Get());
		FToonProbeSlotOptions Options;
		if (!NativePreset.IsValid()) ToonProbeConfigureSlots(Rig, Options);
		ToonProbeFinishMaterialSwap(Rig, Sample);
	};
	const bool bCanToon = bSubstrate && ToonOpaque && ToonMasked;
	if (ToonProbeHasExp(Exps, TEXT("sdf")) && NativePreset.IsValid() && (bCanToon || !bSubstrate))
	{
		UseToonBaseline();
		const TCHAR* SdfEnabledName = bSubstrate ? TEXT("ToonFaceSdfEnabled") : TEXT("UseSdfMap");
		const TCHAR* SdfTextureName = bSubstrate ? TEXT("ToonFaceSdfTexture") : TEXT("SDFMap");
		auto Check = [&](const TCHAR* Name, bool bOk)
		{
			Log.Note(Name, bOk ? TEXT("passed") : TEXT("failed"));
			Log.Failures += bOk ? 0 : 1;
		};
		UMMDEndfieldFaceData* Data = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		Check(TEXT("sdf_mesh_binding_present"), Data && !Data->Slots.IsEmpty());
		if (Data && !Data->Slots.IsEmpty())
		{
			const int32 FaceSlot = Data->Slots[0].MaterialIndex;
			auto IsBound = [&]()
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(FaceSlot));
				float Enabled = 0.0f;
				UTexture* Texture = nullptr;
				if (MID)
				{
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(SdfEnabledName), Enabled);
					MID->GetTextureParameterValue(FHashedMaterialParameterInfo(SdfTextureName), Texture);
				}
				return MID && Enabled == 1.0f && Texture == Data->Slots[0].Texture;
			};
			Check(TEXT("sdf_runtime_texture_bound"), IsBound());
			Check(TEXT("sdf_role_override_generic"), UMMDStageLibrary::SetCharacterMaterialRoleOverride(Rig.Character.Get(), FaceSlot, EMMDMaterialRole::Generic));
			float Enabled = 1.0f;
			Rig.Character->GetMaterial(FaceSlot)->GetScalarParameterValue(FHashedMaterialParameterInfo(SdfEnabledName), Enabled);
			Check(TEXT("sdf_role_override_disables_face"), Enabled == 0.0f);
			UMMDStageLibrary::ClearCharacterMaterialRoleOverride(Rig.Character.Get(), FaceSlot);
			Check(TEXT("sdf_role_clear_rebinds_face"), IsBound());
			ToonProbeSetLights(Rig, true, false, false);
			ToonProbePose(Rig, Sample.BeautyTime);
			FLinearColor HeadForward, HeadRight;
			Rig.Character->GetMaterial(FaceSlot)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), HeadForward);
			Rig.Character->GetMaterial(FaceSlot)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), HeadRight);
			const FVector F = FVector(HeadForward.R, HeadForward.G, HeadForward.B).GetSafeNormal();
			const FVector R = FVector(HeadRight.R, HeadRight.G, HeadRight.B).GetSafeNormal();
			const FVector U = FVector::CrossProduct(R, F).GetSafeNormal();
			Check(TEXT("sdf_head_basis_valid"), !F.IsNearlyZero() && !R.IsNearlyZero() && !U.IsNearlyZero());
			const FRotator OriginalLight = Rig.Stage->KeyLight->GetComponentRotation();
			for (const float Angle : { -75.0f, 0.0f, 75.0f, 140.0f })
			{
				const float Theta = FMath::DegreesToRadians(Angle);
				const FVector L = (F * FMath::Cos(Theta) + R * FMath::Sin(Theta)) * FMath::Cos(0.35f) + U * FMath::Sin(0.35f);
				Rig.Stage->KeyLight->SetWorldRotation((-L).Rotation());
				for (const bool bEnabled : { true, false })
				{
					for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
						if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex)))
							MID->SetScalarParameterValue(SdfEnabledName, bEnabled ? 1.0f : 0.0f);
					ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("sdf_%s_angle_%d"), bEnabled ? TEXT("on") : TEXT("off"), FMath::RoundToInt(Angle)), EToonProbeShot::Face);
				}
			}
			Rig.Stage->KeyLight->SetWorldRotation(OriginalLight);
			ToonProbeSetLights(Rig, true, true, true);
			UseToonBaseline();
			ToonProbeValidateSlots(Rig, Log, TEXT("sdf_reapply"));
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("sdf_stage"), EToonProbeShot::Hero);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("sdf_stage"), EToonProbeShot::Face);
			// 移除派生数据后重新应用，验证材质在没有 SDF 时仍能正确着色。
			TStrongObjectPtr<UMMDEndfieldFaceData> KeepData(Data);
			Sample.Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
			UseToonBaseline();
			Rig.Character->GetMaterial(FaceSlot)->GetScalarParameterValue(FHashedMaterialParameterInfo(SdfEnabledName), Enabled);
			Check(TEXT("sdf_missing_data_fallback"), Enabled == 0.0f);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("sdf_missing_data"), EToonProbeShot::Face);
			Sample.Mesh->AddAssetUserData(Data);
			UseToonBaseline();
			Check(TEXT("sdf_data_restore_rebinds"), IsBound());
			if (bSubstrate)
			{
				TStrongObjectPtr<UMMDStageRenderPreset> Diagnostic(DuplicateObject<UMMDStageRenderPreset>(ToonPreset.Get(), GetTransientPackage()));
				UMaterialInterface* Sources[] = { ToonPreset->CharacterToonMaterialOpaque.Get(), ToonPreset->CharacterToonMaterialMasked.Get(), ToonPreset->CharacterToonMaterialTranslucent.Get() };
				UMaterial* Parents[3] = {};
				for (int32 Index = 0; Index < 3; ++Index)
				{
					Parents[Index] = DuplicateObject<UMaterial>(Cast<UMaterial>(Sources[Index]), GetTransientPackage());
					KeepAlive.Emplace(Parents[Index]);
					UMaterialExpressionCustom* Geometry = nullptr;
					UMaterialExpressionSubstrateToonBSDF* Toon = nullptr;
					for (UMaterialExpression* Expression : Parents[Index]->GetExpressions())
					{
						if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression)) Geometry = Custom;
						if (UMaterialExpressionSubstrateToonBSDF* BSDF = Cast<UMaterialExpressionSubstrateToonBSDF>(Expression)) Toon = BSDF;
					}
					if (Geometry && Toon) Toon->BaseColor.Connect(2, Geometry);
					else ++Log.Failures;
					Parents[Index]->PostEditChange();
					Parents[Index]->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
				}
				Diagnostic->CharacterToonMaterialOpaque = Parents[0];
				Diagnostic->CharacterToonMaterialMasked = Parents[1];
				Diagnostic->CharacterToonMaterialTranslucent = Parents[2];
				for (auto& Role : Diagnostic->CharacterToonRoles) Role.Value.Specular = 0.0f;
				ToonProbeApplyPreset(Rig, Diagnostic.Get());
				ToonProbeFinishMaterialSwap(Rig, Sample);
				ToonProbeValidateSlots(Rig, Log, TEXT("sdf_gpu_mask"));
				ToonProbeCaptureShot(Rig, Sample, Log, TEXT("sdf_gpu_mask_on"), EToonProbeShot::Face);
				for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
					if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex)))
						MID->SetScalarParameterValue(SdfEnabledName, 0.0f);
				ToonProbeCaptureShot(Rig, Sample, Log, TEXT("sdf_gpu_mask_off"), EToonProbeShot::Face);
				UseToonBaseline();
			}
		}
	}
	if (ToonProbeHasExp(Exps, TEXT("runtime")) && NativePreset.IsValid())
	{
		UseToonBaseline();
		auto Check = [&](const TCHAR* Name, bool bOk)
		{
			Log.Note(Name, bOk ? TEXT("passed") : TEXT("failed"));
			Log.Failures += bOk ? 0 : 1;
		};
		const int32 TestSlot = 0;
		const EMMDMaterialRole OriginalRole = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), TestSlot);
		UTexture* OriginalTexture = nullptr;
		Rig.Character->GetMaterial(TestSlot)->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), OriginalTexture);
		UMaterialInterface* OtherSlot = Rig.Character->GetMaterial(1);
		auto CheckRole = [&](EMMDMaterialRole Role)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(TestSlot));
			const FMMDStageToonRoleSettings* Settings = NativePreset->CharacterToonRoles.Find(Role);
			if (!Settings) Settings = NativePreset->CharacterToonRoles.Find(EMMDMaterialRole::Generic);
			UTexture* Texture = nullptr;
			if (MID) MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), Texture);
			Check(TEXT("runtime_source_texture_preserved"), Texture == OriginalTexture);
			Check(TEXT("runtime_other_slot_preserved"), Rig.Character->GetMaterial(1) == OtherSlot);
			if (bSubstrate && Settings)
			{
				float Roughness = -1.0f;
				if (MID) MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonRoughness")), Roughness);
				Check(TEXT("runtime_profile_matches_role"), MID && MID->bOverrideToonProfile && MID->ToonProfileOverride == Settings->ToonProfile);
				Check(TEXT("runtime_roughness_matches_role"), FMath::IsNearlyEqual(Roughness, Settings->Roughness));
			}
		};
		Check(TEXT("runtime_set_role"), UMMDStageLibrary::SetCharacterMaterialRoleOverride(Rig.Character.Get(), TestSlot, EMMDMaterialRole::Hair));
		CheckRole(EMMDMaterialRole::Hair);
		Check(TEXT("runtime_set_generic"), UMMDStageLibrary::SetCharacterMaterialRoleOverride(Rig.Character.Get(), TestSlot, EMMDMaterialRole::Generic));
		CheckRole(EMMDMaterialRole::Generic);
		Check(TEXT("runtime_clear_role"), UMMDStageLibrary::ClearCharacterMaterialRoleOverride(Rig.Character.Get(), TestSlot));
		CheckRole(OriginalRole);
		const UMMDStageMaterialState* State = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
		const TArray<TObjectPtr<UMaterialInterface>> SourceOverrides = State ? State->SourceOverrides : TArray<TObjectPtr<UMaterialInterface>>();
		const bool SourceContactShadow = State && State->bHasContactShadowSnapshot ? State->bHadContactShadow : Rig.Character->bCastContactShadow;
		Check(TEXT("runtime_restore_materials"), UMMDStageLibrary::RestoreCharacterMaterials(Rig.Character.Get()));
		Check(TEXT("runtime_restored_initial_overrides"), Rig.Character->OverrideMaterials == SourceOverrides);
		Check(TEXT("runtime_restored_contact_shadow"), Rig.Character->bCastContactShadow == SourceContactShadow);
		UseToonBaseline();
		ToonProbeValidateSlots(Rig, Log, TEXT("runtime_reapply"));
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("runtime_reapply"), EToonProbeShot::Face);
	}

	// =========================================================================================
	// shadow：主光竖条阴影、自阴影、DynamicShadows 开关
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("shadow")) && bCanToon)
	{
		ToonProbePlaceOccluder(Rig, true);
		for (EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Cloth })
		{
			ToonProbeApplyPreset(Rig, EndfieldPreset);
			ToonProbeFinishMaterialSwap(Rig, Sample);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("shadow_unlit"), Shot);
		}
		UseToonBaseline();
		for (EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Cloth })
		{
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("shadow_toon"), Shot);
		}
		ToonProbeSetShowFlags(Rig, { TEXT("DynamicShadows") });
		for (EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Cloth })
		{
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("shadow_toon_noshadows"), Shot);
		}
		ToonProbeSetShowFlags(Rig, {});
		// 自阴影：角色不投影（头发 -> 脸、下巴 -> 脖子的遮挡应消失）。
		ToonProbePlaceOccluder(Rig, false);
		if (NativePreset.IsValid())
		{
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("selfshadow_face_suppressed"), EToonProbeShot::Face);
			TStrongObjectPtr<UMMDStageRenderPreset> FullShadow(DuplicateObject<UMMDStageRenderPreset>(ToonPreset.Get(), GetTransientPackage()));
			FullShadow->CharacterToonRoles.FindOrAdd(EMMDMaterialRole::Skin).FaceShadowSuppress = 0.0f;
			FullShadow->bCharacterContactShadow = false;
			ToonProbeApplyPreset(Rig, FullShadow.Get());
			ToonProbeFinishMaterialSwap(Rig, Sample);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("selfshadow_full_nocontact"), EToonProbeShot::Face);
			Rig.Character->SetCastContactShadow(true);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("selfshadow_full_contact"), EToonProbeShot::Face);
			Rig.Character->SetCastContactShadow(false);
			const FMMDStageToonRoleSettings* Skin = FullShadow->CharacterToonRoles.Find(EMMDMaterialRole::Skin);
			if (Skin && Skin->ToonProfile)
			{
				for (const float Sigma : { 3.0f, 6.0f, 10.0f, 20.0f })
				{
					UToonProfile* Profile = DuplicateObject<UToonProfile>(Skin->ToonProfile.Get(), GetTransientPackage());
					KeepAlive.Emplace(Profile);
					Profile->Guid = FGuid::NewGuid();
					Profile->Settings.ShadowExtinctionCoefficient = Sigma;
					ToonProbeRefreshProfile(Profile);
					FullShadow->CharacterToonRoles[EMMDMaterialRole::Skin].ToonProfile = Profile;
					ToonProbeApplyPreset(Rig, FullShadow.Get());
					ToonProbeFinishMaterialSwap(Rig, Sample);
					ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("selfshadow_sigma_%d"), FMath::RoundToInt(Sigma)), EToonProbeShot::Face);
				}
			}
			UseToonBaseline();
		}
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("selfshadow_on"), EToonProbeShot::Face);
		Rig.Character->SetCastShadow(false);
		Rig.World->SendAllEndOfFrameUpdates();
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("selfshadow_off"), EToonProbeShot::Face);
		Rig.Character->SetCastShadow(true);
		Rig.World->SendAllEndOfFrameUpdates();
	}

	// =========================================================================================
	// ramp：改 DiffuseRamp、bDiffuseRampIncludeShadow、DiffuseIndirectScale、Ramp 分辨率
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("ramp")) && bCanToon)
	{
		UseToonBaseline();
		UToonProfile* Standard = Profiles[EToonProbeProfile::Standard];
		const FToonProfileStruct StandardSettings = Standard->Settings;
		struct FRampVariant { const TCHAR* Name; EToonProbeProfile Kind; };
		const FRampVariant RampVariants[] =
		{
			{ TEXT("standard3"), EToonProbeProfile::Standard }, { TEXT("twoband"), EToonProbeProfile::TwoBand },
			{ TEXT("enginedefault"), EToonProbeProfile::EngineDefault }, { TEXT("smooth"), EToonProbeProfile::Smooth }
		};
		for (const FRampVariant& Variant : RampVariants)
		{
			// 原地改同一个 Profile 对象的 ramp，验证编辑器实时更新路径（图集重建）。
			ToonProbeConfigureProfile(Standard, Variant.Kind);
			ToonProbeRefreshProfile(Standard);
			for (EToonProbeShot Shot : { EToonProbeShot::Face, EToonProbeShot::Cloth })
			{
				ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("ramp_%s"), Variant.Name), Shot);
			}
		}
		Standard->Settings = StandardSettings;
		ToonProbeRefreshProfile(Standard);

		// ramp 分辨率：默认 64 与 256 对阶梯边缘硬度的影响。
		ToonProbeSetCVarInt(TEXT("r.Substrate.ToonProfile.Resolution"), 256);
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("ramp_res256"), EToonProbeShot::Cloth);
		ToonProbeSetCVarInt(TEXT("r.Substrate.ToonProfile.Resolution"), 64);

		// bDiffuseRampIncludeShadow：竖条阴影下阴影是"乘在光上"还是"进 ramp"。
		ToonProbePlaceOccluder(Rig, true);
		for (int32 Include = 0; Include < 2; ++Include)
		{
			Standard->Settings.bDiffuseRampIncludeShadow = Include;
			ToonProbeRefreshProfile(Standard);
			for (EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Cloth })
			{
				ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("includeshadow%d"), Include), Shot);
			}
		}
		Standard->Settings.bDiffuseRampIncludeShadow = 0;
		ToonProbeRefreshProfile(Standard);
		ToonProbePlaceOccluder(Rig, false);

		// DiffuseIndirectScale：关主光，只剩补光 / 背光 / 天空 / GI，间接光比例才可见。
		ToonProbeSetLights(Rig, false, false, false);
		for (float Scale : { 0.0f, 0.5f, 1.0f })
		{
			Standard->Settings.DiffuseIndirectScale = Scale;
			ToonProbeRefreshProfile(Standard);
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("indirect%03d_nolights"), FMath::RoundToInt(Scale * 100.0f)), EToonProbeShot::Face);
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("indirect%03d_nolights"), FMath::RoundToInt(Scale * 100.0f)), EToonProbeShot::Hero);
		}
		Standard->Settings = StandardSettings;
		ToonProbeRefreshProfile(Standard);
		ToonProbeSetLights(Rig, true, true, true);
	}

	// =========================================================================================
	// 独立灯型与遮挡对照，不把 shader 编译成功当成阴影消费证明。
	if (ToonProbeHasExp(Exps, TEXT("backend")) && bCanToon)
	{
		UseToonBaseline();
		ToonProbeValidateSlots(Rig, Log, TEXT("backend_baseline"));
		Report->SetBoolField(TEXT("backend_profile_marker_allowed"), MMDEndfieldNativeBackend::GetStatus().bProfileMarkerAllowed);
		TArray<float> AccentIntensities;
		TInlineComponentArray<UPointLightComponent*> AccentLights;
		Rig.Stage->GetComponents(AccentLights);
		for (UPointLightComponent* Accent : AccentLights)
		{
			AccentIntensities.Add(Accent ? Accent->Intensity : 0.0f);
			if (Accent) Accent->SetIntensity(0.0f);
		}
		const FVector Target(0, -55, 105);
		const FVector LocalLightPosition(80, -180, 190);
		const TCHAR* LightNames[] = { TEXT("directional"), TEXT("point"), TEXT("spot"), TEXT("rect") };
		for (int32 LightIndex = 0; LightIndex < 4; ++LightIndex)
		{
			ToonProbeSetLights(Rig, LightIndex == 0, false, false);
			TStrongObjectPtr<ULightComponent> LocalLight;
			if (LightIndex == 1) LocalLight.Reset(NewObject<UPointLightComponent>(Rig.Stage, NAME_None, RF_Transient));
			if (LightIndex == 2) LocalLight.Reset(NewObject<USpotLightComponent>(Rig.Stage, NAME_None, RF_Transient));
			if (LightIndex == 3) LocalLight.Reset(NewObject<URectLightComponent>(Rig.Stage, NAME_None, RF_Transient));
			if (LocalLight.IsValid())
			{
				LocalLight->SetMobility(EComponentMobility::Movable);
				LocalLight->SetCastShadows(true);
				LocalLight->SetLightColor(FLinearColor::White);
				if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(LocalLight.Get()))
				{
					Local->SetIntensityUnits(ELightUnits::Candelas);
					Local->SetAttenuationRadius(500.0f);
				}
				LocalLight->SetIntensity(LightIndex == 3 ? 18.0f : 12.0f);
				if (USpotLightComponent* Spot = Cast<USpotLightComponent>(LocalLight.Get()))
				{
					Spot->SetInnerConeAngle(20.0f);
					Spot->SetOuterConeAngle(45.0f);
				}
				if (URectLightComponent* Rect = Cast<URectLightComponent>(LocalLight.Get()))
				{
					Rect->SetSourceWidth(40.0f);
					Rect->SetSourceHeight(40.0f);
				}
				Rig.Scene->AddComponent(LocalLight.Get(), FTransform((Target - LocalLightPosition).Rotation(), LocalLightPosition));
			}
			ToonProbeSetShowFlags(Rig, { TEXT("GlobalIllumination"), TEXT("SkyLighting"), TEXT("ReflectionEnvironment"),
				TEXT("LumenReflections"), TEXT("ScreenSpaceReflections"), TEXT("AmbientOcclusion") });
			ToonProbePlaceOccluder(Rig, false);
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("backend_%s_clear"), LightNames[LightIndex]), EToonProbeShot::Hero, FMath::Max(Frames, 12));
			ToonProbePlaceOccluder(Rig, true);
			if (LightIndex > 0)
			{
				Rig.Occluder->SetWorldLocation(FMath::Lerp(Target, LocalLightPosition, 0.5f));
				Rig.Occluder->SetWorldScale3D(FVector(0.45f, 0.45f, 3.0f));
			}
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("backend_%s_blocked"), LightNames[LightIndex]), EToonProbeShot::Hero, FMath::Max(Frames, 12));
			ToonProbePlaceOccluder(Rig, false);
			if (LocalLight.IsValid()) Rig.Scene->RemoveComponent(LocalLight.Get());
		}
		ToonProbeSetShowFlags(Rig, {});
		ToonProbeSetLights(Rig, true, true, true);
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("backend_all_lights"), EToonProbeShot::Hero, FMath::Max(Frames, 12));
		ToonProbeSetLights(Rig, false, false, false);
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("backend_indirect"), EToonProbeShot::Hero, FMath::Max(Frames, 24));
		ToonProbeCaptureShot(Rig, Sample, Log, TEXT("backend_indirect"), EToonProbeShot::Face, FMath::Max(Frames, 24));
		ToonProbeSetLights(Rig, true, true, true);
		for (int32 Index = 0; Index < AccentLights.Num(); ++Index)
		{
			if (AccentLights[Index]) AccentLights[Index]->SetIntensity(AccentIntensities[Index]);
		}
	}

	// lighting：Lumen GI / 反射 / AO / 天空 / 阴影 到底有没有作用到角色（含 SceneCapture 光追开关）
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("lighting")) && bCanToon)
	{
		struct FLightingVariant { const TCHAR* Name; TArray<FString> Disabled; bool bRayTracing; };
		const TArray<FLightingVariant> LightingVariants =
		{
			{ TEXT("rt0_all"), {}, false },
			{ TEXT("rt1_all"), {}, true },
			{ TEXT("rt1_nogi"), { TEXT("GlobalIllumination") }, true },
			{ TEXT("rt1_nolumengi"), { TEXT("LumenGlobalIllumination") }, true },
			{ TEXT("rt1_noreflect"), { TEXT("ReflectionEnvironment"), TEXT("LumenReflections"), TEXT("ScreenSpaceReflections") }, true },
			{ TEXT("rt1_noao"), { TEXT("AmbientOcclusion"), TEXT("LumenShortRangeAmbientOcclusion") }, true },
			{ TEXT("rt1_nosky"), { TEXT("SkyLighting") }, true },
		};
		for (int32 Setup = 0; Setup < 2; ++Setup)
		{
			// Setup 0：默认三盏灯；Setup 1：关掉三盏直射灯，只剩天空 + GI，间接光贡献最容易看出。
			ToonProbeSetLights(Rig, Setup == 0, Setup == 0, Setup == 0);
			for (int32 Mode = 0; Mode < 2; ++Mode)
			{
				if (Mode == 0) { ToonProbeApplyPreset(Rig, EndfieldPreset); ToonProbeFinishMaterialSwap(Rig, Sample); }
				else UseToonBaseline();
				for (const FLightingVariant& Variant : LightingVariants)
				{
					// Unlit 对照只拍两张，足以证明 Unlit 不响应任何引擎光照开关。
					if (Mode == 0 && !(FString(Variant.Name) == TEXT("rt1_all") || FString(Variant.Name) == TEXT("rt1_nogi"))) continue;
					Rig.Capture->bUseRayTracingIfEnabled = Variant.bRayTracing;
					ToonProbeSetShowFlags(Rig, Variant.Disabled);
					Rig.Capture->bCameraCutThisFrame = true;
					const FString Prefix = FString::Printf(TEXT("light%d_%s_%s"), Setup, Mode == 0 ? TEXT("unlit") : TEXT("toon"), Variant.Name);
					ToonProbeCaptureShot(Rig, Sample, Log, Prefix, EToonProbeShot::Hero, FMath::Max(Frames, 10));
					ToonProbeCaptureShot(Rig, Sample, Log, Prefix, EToonProbeShot::Face, FMath::Max(Frames, 10));
				}
				ToonProbeSetShowFlags(Rig, {});
				Rig.Capture->bUseRayTracingIfEnabled = false;
			}
		}
		ToonProbeSetLights(Rig, true, true, true);
	}

	// =========================================================================================
	// face：仅用几何头部中心与法线改善明暗，不依赖 UV 或阴影阈值贴图。
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("face")) && bCanToon)
	{
		ToonProbeApplyPreset(Rig, ToonPreset.Get());
		ToonProbeFinishMaterialSwap(Rig, Sample);
		for (float Blend : { 0.0f, 0.6f, 1.0f })
		{
			FToonProbeSlotOptions Options;
			Options.SphereBlendSkin = Blend;
			ToonProbeConfigureSlots(Rig, Options);
			for (EToonProbeShot Shot : { EToonProbeShot::Face, EToonProbeShot::Hero })
			{
				ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("sphere%03d"), FMath::RoundToInt(Blend * 100.0f)), Shot);
			}
		}
	}

	// =========================================================================================
	// hair：各向异性高光带（符号 / 粗糙度扫描）
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("hair")) && bCanToon)
	{
		ToonProbeApplyPreset(Rig, ToonPreset.Get());
		ToonProbeFinishMaterialSwap(Rig, Sample);
		FToonProbeSlotOptions Options;
		Options.bPerRoleProfiles = true;
		Options.RoleProfiles.Add(EMMDMaterialRole::Hair, Profiles[EToonProbeProfile::Hair]);
		Options.OverrideMethod = 2;
		struct FHairVariant { const TCHAR* Name; float Anisotropy; float Roughness; };
		const FHairVariant HairVariants[] =
		{
			{ TEXT("iso_r35"), 0.0f, 0.35f }, { TEXT("aniso_p80_r35"), 0.8f, 0.35f }, { TEXT("aniso_n80_r35"), -0.8f, 0.35f },
			{ TEXT("aniso_p95_r20"), 0.95f, 0.20f }, { TEXT("aniso_n95_r20"), -0.95f, 0.20f }
		};
		for (const FHairVariant& Variant : HairVariants)
		{
			Options.HairAnisotropy = Variant.Anisotropy;
			Options.HairRoughness = Variant.Roughness;
			ToonProbeConfigureSlots(Rig, Options);
			for (EToonProbeShot Shot : { EToonProbeShot::Hair, EToonProbeShot::Face, EToonProbeShot::Hero })
			{
				ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("hair_%s"), Variant.Name), Shot);
			}
		}
	}

	// =========================================================================================
	// override：MID.ToonProfileOverride 能否让"一个父材质 + 每槽不同 ramp"成立
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("override")) && bCanToon)
	{
		for (int32 Method = 0; Method <= 3; ++Method)
		{
			ToonProbeApplyPreset(Rig, ToonPreset.Get());
			ToonProbeFinishMaterialSwap(Rig, Sample);
			FToonProbeSlotOptions Options;
			Options.bPerRoleProfiles = true;
			Options.OverrideMethod = Method;
			// 故意用差异很大的 ramp，便于一眼分辨覆盖是否生效：皮肤二段、头发平滑、布料引擎默认五段。
			Options.RoleProfiles.Add(EMMDMaterialRole::Skin, Profiles[EToonProbeProfile::TwoBand]);
			Options.RoleProfiles.Add(EMMDMaterialRole::Hair, Profiles[EToonProbeProfile::Smooth]);
			Options.RoleProfiles.Add(EMMDMaterialRole::Cloth, Profiles[EToonProbeProfile::EngineDefault]);
			const int32 Count = ToonProbeConfigureSlots(Rig, Options);
			Log.Note(FString::Printf(TEXT("override_method%d_mid_count"), Method), FString::FromInt(Count));
			ToonProbeFinishMaterialSwap(Rig, Sample);
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("override_m%d"), Method), EToonProbeShot::Hero);
			ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("override_m%d"), Method), EToonProbeShot::Face);
		}
	}

	// =========================================================================================
	// translucent：Toon BSDF 的半透明混合是否可用，并和 Unlit 半透明父材质并排
	// =========================================================================================
	if (ToonProbeHasExp(Exps, TEXT("translucent")) && bSubstrate)
	{
		Log.Note(TEXT("toon_translucent_grey_ready"), ReadyMaterial(TEXT("ToonProbe_TranslucentGrey")) ? TEXT("true") : TEXT("false"));
		if (bCanToon)
		{
			ToonProbeApplyPreset(Rig, ToonPreset.Get());
			FToonProbeSlotOptions Options;
			ToonProbeConfigureSlots(Rig, Options);
			ToonProbeFinishMaterialSwap(Rig, Sample);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("translucent_toonpreset"), EToonProbeShot::Face);
			ToonProbeApplyPreset(Rig, HybridPreset.Get());
			ToonProbeConfigureSlots(Rig, Options);
			ToonProbeFinishMaterialSwap(Rig, Sample);
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("translucent_hybrid"), EToonProbeShot::Face);
		}
	}

	// ---- 报告 ----
	Report->SetStringField(TEXT("tag"), Log.Tag);
	Report->SetStringField(TEXT("experiments"), ExpText);
	Report->SetNumberField(TEXT("frames_per_shot"), Frames);
	Report->SetBoolField(TEXT("lumen_tracking_in_preview_world"), bLumenTracking);
	Report->SetArrayField(TEXT("images"), Log.Images);
	Report->SetArrayField(TEXT("notes"), Log.Notes);
	Report->SetArrayField(TEXT("slot_checks"), Log.SlotChecks);
	if (Log.Images.IsEmpty()) ++Log.Failures;
	Report->SetNumberField(TEXT("failures"), Log.Failures);
	const FString ReportPath = FPaths::Combine(Log.OutDir, FString::Printf(TEXT("%s_report.json"), *Log.Tag));
	FFileHelper::SaveStringToFile(ToonProbeJsonToString(Report), *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogTemp, Display, TEXT("[ToonProbe] done: %d images, %d failures. %s"), Log.Images.Num(), Log.Failures, *ReportPath);
	return Log.Failures == 0 ? 0 : 1;
}
