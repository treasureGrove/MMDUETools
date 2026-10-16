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
//   -SampleManifest=<Saved 导入清单>：严格使用指定样本，失败不回退；不能与旧固定 SDF fixture 混用。
//   -PoseTime=<秒>：所有机位使用同一动作时间；-1 清除播放实例并强制参考姿势。
//   -FitSampleCamera：仅 full 机位按当前模型包围球取安全距离，默认机位保持原基准。
//   -Exp=facefix：显式 YYB16th SampleManifest + -PoseTime=-1；12 张隔离 + 4 张掠射 + 1 张鼻壳隔离 + 2 张阈值响应 + 2 张载荷诊断。
//   -Exp=skin：同一 Face 父材质的表面处理关闭/三档候选/关闭复拍，以及 GI、高光、主光和方向场隔离。
//   -Exp=skinresponse：布局41的逐灯颜色查表/视角响应，16张开关/复拍对照+2张独立柔化宽度对照。
//   -Exp=skinredness：布局42仅切换尾槽216.R，18张核心/复拍+8张原生阴影与遮挡对照。
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
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Math/RotationMatrix.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/ToonProfile.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Import/MMDModelDataAsset.h"
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
#include "Rendering/MMDOutlineState.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "Rendering/MMDEndfieldHeadFrame.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "Rendering/SkeletalMeshRenderData.h"
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
#include "Serialization/ObjectWriter.h"
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
#include "UObject/UObjectIterator.h"

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
	// 显式 manifest 优先且失败不回退；未指定时保留最新且顶点数 > 5000 的旧规则。
	// ------------------------------------------------------------------------------------------

	struct FToonProbeSample
	{
		FString Mount;
		FString Directory;
		FString MeshPath;
		FString AnimationPath;
		FString ManifestPath;
		FString SourcePmx;
		bool bExplicitManifest = false;
		bool bReferencePose = false;
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

	bool ToonProbeSampleHasNoLinkedAncestor(const FString& Path)
	{
		FString Current = Path;
		while (!Current.IsEmpty())
		{
			if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

	bool ToonProbeLoadSample(FToonProbeSample& Sample, const FString& RequestedManifest, bool bExplicitManifest, FString& Error)
	{
		Sample.bExplicitManifest = bExplicitManifest;
		const FString ImportRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets"));
		if (!ToonProbeSampleHasNoLinkedAncestor(ImportRoot))
		{
			Error = TEXT("样本 Saved 导入目录的祖先包含链接或 junction，拒绝重定向挂载");
			return false;
		}
		if (bExplicitManifest)
		{
			if (RequestedManifest.IsEmpty()) { Error = TEXT("SampleManifest 不能为空，不会回退到最新样本"); return false; }
			Sample.ManifestPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RequestedManifest);
			FPaths::NormalizeFilename(Sample.ManifestPath);
			if (!FPaths::CollapseRelativeDirectories(Sample.ManifestPath)
				|| !FPaths::IsUnderDirectory(Sample.ManifestPath, ImportRoot)
				|| !ToonProbeSampleHasNoLinkedAncestor(Sample.ManifestPath))
			{
				Error = TEXT("SampleManifest 必须位于本工程 Saved/BackgroundValidation/ImportedAssets，不会回退到最新样本");
				return false;
			}
		}
		else
		{
			TArray<FString> Manifests;
			IFileManager::Get().FindFilesRecursive(Manifests, *ImportRoot, TEXT("manifest.json"), true, false);
			FDateTime LatestTime = FDateTime::MinValue();
			for (const FString& ManifestPath : Manifests)
			{
				FString Json;
				TSharedPtr<FJsonObject> Manifest;
				if (!FFileHelper::LoadFileToString(Json, *ManifestPath)
					|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest.IsValid()) continue;
				double Vertices = 0.0;
				const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*ManifestPath);
				if (Manifest->TryGetNumberField(TEXT("vertices"), Vertices) && Vertices > 5000.0 && Timestamp > LatestTime)
				{
					LatestTime = Timestamp;
					Sample.ManifestPath = FPaths::ConvertRelativePathToFull(ManifestPath);
				}
			}
		}
		if (Sample.ManifestPath.IsEmpty())
		{
			Error = TEXT("No Saved/BackgroundValidation character import was found");
			return false;
		}
		FString Json;
		TSharedPtr<FJsonObject> Manifest;
		if (!ToonProbeSampleHasNoLinkedAncestor(Sample.ManifestPath)
			|| !IFileManager::Get().FileExists(*Sample.ManifestPath) || !FFileHelper::LoadFileToString(Json, *Sample.ManifestPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Manifest) || !Manifest.IsValid())
		{
			Error = TEXT("所选 SampleManifest 不存在或 JSON 无效：") + Sample.ManifestPath;
			return false;
		}
		Manifest->TryGetStringField(TEXT("source_pmx"), Sample.SourcePmx);
		double Vertices = 0.0;
		if (!Manifest->TryGetNumberField(TEXT("vertices"), Vertices) || !FMath::IsFinite(Vertices) || Vertices <= 0.0)
		{
			Error = TEXT("所选 SampleManifest 没有有效顶点数，不会回退：") + Sample.ManifestPath;
			return false;
		}
		Sample.Directory = FPaths::GetPath(Sample.ManifestPath) + TEXT("/");
		const FString Name = FPaths::GetCleanFilename(FPaths::GetPath(Sample.ManifestPath));
		Sample.Mount = FString::Printf(TEXT("/Game/MMDModels/%s/"), *Name);
		Sample.MeshPath = Sample.Mount + TEXT("Model/") + Name + TEXT(".") + Name;
		Manifest->TryGetStringField(TEXT("mesh"), Sample.MeshPath);
		Manifest->TryGetStringField(TEXT("animation"), Sample.AnimationPath);
		if (!Sample.MeshPath.StartsWith(Sample.Mount)
			|| (!Sample.AnimationPath.IsEmpty() && !Sample.AnimationPath.StartsWith(Sample.Mount)))
		{
			Error = TEXT("所选 manifest 的模型或动画引用超出自身 Saved 沙盒，不会回退");
			Sample.Mount.Reset();
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
		TStrongObjectPtr<UAnimSequence> Cached;
		if (!bExplicitManifest && FPackageName::DoesPackageExist(CachedPackage))
			Cached.Reset(LoadObject<UAnimSequence>(nullptr, *(CachedPackage + TEXT(".MMDStagePreview8s"))));
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
		if (!Sample.Animation.IsValid() || Sample.Animation->GetSkeleton() != Sample.Mesh->GetSkeleton())
		{
			Error = TEXT("所选样本的动画无法加载或 Skeleton 不匹配，不会回退到其它样本");
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
		bool bReferencePose = false;
		FPostProcessSettings PostProcess;
		bool bFitSampleCamera = false;
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
		if (Rig.bFitSampleCamera && Shot == EToonProbeShot::Hero)
		{
			Rig.Character->UpdateBounds();
			const FBoxSphereBounds Bounds = Rig.Character->Bounds;
			const float Aspect = Rig.Target.IsValid() && Rig.Target->SizeY > 0
				? float(Rig.Target->SizeX) / Rig.Target->SizeY : 16.0f / 9.0f;
			const float HalfHorizontal = FMath::DegreesToRadians(Fov * 0.5f);
			const float HalfVertical = FMath::Atan(FMath::Tan(HalfHorizontal) / FMath::Max(Aspect, 0.1f));
			const double Distance = FMath::Max(double(Bounds.SphereRadius), 1.0)
				/ FMath::Sin(FMath::Min(HalfHorizontal, HalfVertical)) * 1.10;
			const FVector ViewOffset = (Location - Target).GetSafeNormal();
			Target = Bounds.Origin;
			Location = Target + ViewOffset * Distance;
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
		if (Rig.bReferencePose)
		{
			Rig.Character->SetForceRefPose(true);
			Rig.Character->TickAnimation(0.0f, false);
			Rig.Character->RefreshBoneTransforms();
			Rig.Character->HandleExistingParallelEvaluationTask(true, true);
			Rig.Character->UpdateComponentToWorld();
			Rig.Character->MarkRenderDynamicDataDirty();
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			Rig.World->SendAllEndOfFrameUpdates();
			return;
		}
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
	bool ToonProbeShoot(FToonProbeRig& Rig, const FString& Filename, FToonProbeImageStats& Stats, FString& Error,
		int32 Frames = -1, TArray<FColor>* OutPixels = nullptr)
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
		if (OutPixels) *OutPixels = MoveTemp(Down);
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
		Rig.bReferencePose = Sample.bReferencePose;
		Rig.Character->SetForceRefPose(Sample.bReferencePose);
		Rig.Character->SetAnimation(Sample.bReferencePose ? nullptr : Sample.Animation.Get());
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
		if (Sample.bReferencePose)
		{
			// 不能仅把时间设为零：旧播放实例仍可能持有上一段物理和 Morph 姿态。
			Rig.Playback = nullptr;
			Rig.Character->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			Rig.Character->SetAnimation(nullptr);
			Rig.Character->SetForceRefPose(true);
		}
		else Rig.Playback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(Rig.Character.Get(), Sample.Animation.Get(), false, true);
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

	void ToonProbeWriteFaceBindingFields(const TSharedRef<FJsonObject>& Item, const FMMDGeneratedFaceSdfSlot* Binding)
	{
		Item->SetBoolField(TEXT("face_data_binding_valid"), Binding != nullptr);
		Item->SetBoolField(TEXT("forward_overlay"), Binding && Binding->bForwardOverlay);
		Item->SetNumberField(TEXT("host_material_index"), Binding ? Binding->HostMaterialIndex : INDEX_NONE);
		Item->SetStringField(TEXT("host_slot_name"), Binding ? Binding->HostSlotName.ToString() : TEXT("none"));
		Item->SetStringField(TEXT("expected_face_payload_transport"), !Binding ? TEXT("none") : Binding->bForwardOverlay ? TEXT("forward_inline") : TEXT("opaque_masked_packed"));
		Item->SetNumberField(TEXT("overlay_validated_lods"), Binding ? Binding->OverlayValidatedLODs : 0);
		Item->SetNumberField(TEXT("overlay_tested_vertices"), Binding ? Binding->OverlayTestedVertices : 0);
		Item->SetNumberField(TEXT("overlay_valid_vertex_ratio"), Binding ? Binding->OverlayValidVertexRatio : 0.0f);
		Item->SetNumberField(TEXT("overlay_valid_area_ratio"), Binding ? Binding->OverlayValidAreaRatio : 0.0f);
		Item->SetNumberField(TEXT("overlay_minimum_head_weight"), Binding ? Binding->OverlayMinimumHeadWeight : 0.0f);
		Item->SetNumberField(TEXT("overlay_max_matched_depth_error"), Binding ? Binding->OverlayMaxMatchedDepthError : 0.0f);
	}

	FString ToonProbeNativeSurfaceParentPath(EBlendMode Blend, bool bFace)
	{
		const TCHAR* Suffix = Blend == BLEND_Opaque ? TEXT("Opaque") : Blend == BLEND_Masked ? TEXT("Masked")
			: Blend == BLEND_Translucent ? TEXT("Translucent") : nullptr;
		if (!Suffix) return FString();
		const FString Name = FString::Printf(TEXT("M_MMD_EndfieldV2_%s%s"), bFace ? TEXT("Face_") : TEXT(""), Suffix);
		return FString::Printf(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/%s.%s"), *Name, *Name);
	}

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
			auto RecordMaterialInputs = [&](const FString& Prefix, UMaterialInterface* Interface)
			{
				float Opacity = 0.0f;
				FLinearColor Base = FLinearColor::Transparent;
				const bool bOpacity = Interface && Interface->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity);
				const bool bBase = Interface && Interface->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Base);
				Item->SetStringField(Prefix + TEXT("material"), Interface ? Interface->GetPathName() : TEXT("none"));
				Item->SetStringField(Prefix + TEXT("blend_mode"), Interface ? UEnum::GetValueAsString(Interface->GetBlendMode()) : TEXT("none"));
				Item->SetBoolField(Prefix + TEXT("opacity_parameter_found"), bOpacity);
				Item->SetBoolField(Prefix + TEXT("base_color_parameter_found"), bBase);
				if (bOpacity) Item->SetNumberField(Prefix + TEXT("opacity"), Opacity);
				if (bBase)
				{
					Item->SetStringField(Prefix + TEXT("base_color_rgba"), Base.ToString());
					Item->SetNumberField(Prefix + TEXT("base_color_alpha"), Base.A);
				}
			};
			RecordMaterialInputs(TEXT("actual_"), Material);
			UMaterialInterface* Source = Rig.Character->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialInterface;
			if (const UMMDStageMaterialState* SourceState = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass())))
				if (SourceState->SourceMaterials.IsValidIndex(Slot)) Source = SourceState->SourceMaterials[Slot];
			RecordMaterialInputs(TEXT("source_"), Source);
			const UMMDEndfieldFaceData* FaceData = Rig.Character->GetSkeletalMeshAsset()->GetAssetUserData<UMMDEndfieldFaceData>();
			const FMMDGeneratedFaceSdfSlot* DirectionBinding = FaceData
				? FaceData->FindDirectionSlot(Slot, Rig.Character->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialSlotName) : nullptr;
			ToonProbeWriteFaceBindingFields(Item, DirectionBinding);
			bool bValidateForwardOverlayRole = false;
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
				float DirectionEnabled = 0.0f, PayloadEnabled = 0.0f, DirectionVersion = 0.0f;
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceDirectionEnabled")), DirectionEnabled);
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), PayloadEnabled);
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceDirectionVersion")), DirectionVersion);
				Item->SetNumberField(TEXT("face_direction_enabled"), DirectionEnabled);
				Item->SetNumberField(TEXT("native_payload_enabled"), PayloadEnabled);
				Item->SetNumberField(TEXT("face_direction_version"), DirectionVersion);
				Texture = nullptr;
				MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceDirectionTexture")), Texture);
				Item->SetStringField(TEXT("face_direction_texture"), Texture ? Texture->GetPathName() : TEXT("none"));
				const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot);
				const EBlendMode Blend = MID->GetBlendMode();
				const FString ActualParent = MID->Parent ? MID->Parent->GetPathName() : FString();
				const bool bNativeNamespace = ActualParent.StartsWith(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/"));
				const FString OrdinaryParent = ToonProbeNativeSurfaceParentPath(Blend, false);
				const FString FaceParent = ToonProbeNativeSurfaceParentPath(Blend, true);
				const bool bNativeParent = !OrdinaryParent.IsEmpty() && (ActualParent == OrdinaryParent || ActualParent == FaceParent);
				const UMMDStageMaterialState* RoleState = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
				if (bNativeNamespace)
				{
					// 与 Stage 路由一致：有效绑定的 Skin 使用 Face 父；Mode0/1 不额外禁止不透明 Face 路由。
					const UMMDStageRenderPreset* Applied = RoleState ? RoleState->AppliedPreset.Get() : nullptr;
					const UMaterialInterface* ConfiguredFace = !Applied ? nullptr : Blend == BLEND_Opaque ? Applied->CharacterToonFaceMaterialOpaque.Get()
						: Blend == BLEND_Masked ? Applied->CharacterToonFaceMaterialMasked.Get() : Applied->CharacterToonFaceMaterialTranslucent.Get();
					const bool bFaceConfigured = IsValid(ConfiguredFace) && ConfiguredFace->GetBlendMode() == Blend;
					const FString ExpectedParent = DirectionBinding && Role == EMMDMaterialRole::Skin && bFaceConfigured ? FaceParent : OrdinaryParent;
					const bool bParentMatches = bNativeParent && ActualParent == ExpectedParent;
					bool bParentReady = false;
					const TArray<FString> ParentErrors = ToonProbeCompileErrors(Cast<UMaterial>(MID->Parent.Get()), bParentReady);
					Item->SetStringField(TEXT("expected_parent"), ExpectedParent);
					Item->SetBoolField(TEXT("parent_matches_expected"), bParentMatches);
					Item->SetBoolField(TEXT("exact_native_parent_recognized"), bNativeParent);
					Item->SetBoolField(TEXT("actual_parent_shader_map_ready"), bParentReady && ParentErrors.IsEmpty());
					if (!bParentMatches || !bParentReady || !ParentErrors.IsEmpty()) ++Log.Failures;
				}
				const bool bForwardOverlay = DirectionBinding && DirectionBinding->bForwardOverlay && Blend == BLEND_Translucent;
				const MMDEndfieldNativeBackend::FStatus& Backend = MMDEndfieldNativeBackend::GetStatus();
				bValidateForwardOverlayRole = bNativeParent && Substrate::IsToonProfileEnabled() && Backend.bProfileMarkerAllowed
					&& Backend.Mode == 2 && bForwardOverlay && (!RoleState || !RoleState->RoleOverrides.Contains(Slot));
				const bool bExpectedPayload = bNativeParent && Substrate::IsSubstrateEnabled() && Role == EMMDMaterialRole::Skin
					&& Backend.bProfileMarkerAllowed && Backend.Mode == 2 && ToonProbeCVar(TEXT("r.Substrate.ProjectGBufferFormat")) == TEXT("1")
					&& (Blend == BLEND_Opaque || Blend == BLEND_Masked || bForwardOverlay);
				Item->SetBoolField(TEXT("native_payload_expected"), bExpectedPayload);
				Item->SetBoolField(TEXT("native_payload_matches_expected"), (PayloadEnabled > 0.0f) == bExpectedPayload);
				if ((PayloadEnabled > 0.0f) != bExpectedPayload) ++Log.Failures;
			}
			if (const UMMDStageMaterialState* State = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass())))
			{
				if (State->RoleReasons.IsValidIndex(Slot)) Item->SetStringField(TEXT("role_reason"), State->RoleReasons[Slot]);
				if (bValidateForwardOverlayRole)
				{
					const bool bRoleMatches = State->ResolvedRoles.IsValidIndex(Slot) && State->ResolvedRoles[Slot] == EMMDMaterialRole::Skin
						&& State->RoleReasons.IsValidIndex(Slot) && State->RoleReasons[Slot].StartsWith(TEXT("skin:attached_overlay:"));
					Item->SetBoolField(TEXT("forward_overlay_role_matches"), bRoleMatches);
					if (!bRoleMatches) ++Log.Failures;
				}
			}
			Log.SlotChecks.Add(MakeShared<FJsonValueObject>(Item));
			Log.Failures += bDefaultMaterial || bFallback ? 1 : 0;
		}
	}

	void ToonProbeCapture(FToonProbeRig& Rig, FToonProbeLog& Log, const FString& Name,
		int32 Frames = -1, TArray<FColor>* OutPixels = nullptr)
	{
		FToonProbeImageStats Stats;
		FString Error;
		const FString Filename = Log.File(Name);
		const bool bOk = ToonProbeShoot(Rig, Filename, Stats, Error, Frames, OutPixels);
		TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
		Item->SetStringField(TEXT("name"), Name);
		Item->SetStringField(TEXT("file"), Filename);
		Item->SetBoolField(TEXT("ok"), bOk);
		Item->SetNumberField(TEXT("mean_luma"), Stats.MeanLuma);
		Item->SetNumberField(TEXT("center_mean_luma"), Stats.CenterMeanLuma);
		Item->SetNumberField(TEXT("min_luma"), Stats.MinLuma);
		Item->SetNumberField(TEXT("max_luma"), Stats.MaxLuma);
		Item->SetNumberField(TEXT("seconds"), Stats.Seconds);
		Item->SetNumberField(TEXT("width"), Stats.Width);
		Item->SetNumberField(TEXT("height"), Stats.Height);
		Item->SetStringField(TEXT("camera_location"), Rig.Capture->GetComponentLocation().ToString());
		Item->SetStringField(TEXT("camera_rotation"), Rig.Capture->GetComponentRotation().ToString());
		Item->SetNumberField(TEXT("camera_fov"), Rig.Capture->FOVAngle);
		if (!Error.IsEmpty()) Item->SetStringField(TEXT("error"), Error);
		Log.Images.Add(MakeShared<FJsonValueObject>(Item));
		Log.Failures += bOk ? 0 : 1;
		UE_LOG(LogTemp, Display, TEXT("[ToonProbe] shot %s ok=%d mean=%.2f center=%.2f %.2fs"), *Name, bOk ? 1 : 0, Stats.MeanLuma, Stats.CenterMeanLuma, Stats.Seconds);
	}

	// 一次性拍某个机位：切姿态、切机位、截图。
	void ToonProbeCaptureShot(FToonProbeRig& Rig, FToonProbeSample& Sample, FToonProbeLog& Log,
		const FString& Prefix, EToonProbeShot Shot, int32 Frames = -1, TArray<FColor>* OutPixels = nullptr)
	{
		float AnimationTime = Sample.BeautyTime;
		if (Shot == EToonProbeShot::Face || Shot == EToonProbeShot::Hair)
		{
			AnimationTime = Sample.FaceTimes.Num() > 0 ? Sample.FaceTimes[0] : Sample.BeautyTime;
		}
		ToonProbePose(Rig, AnimationTime);
		ToonProbeSetShot(Rig, Shot);
		ToonProbeCapture(Rig, Log, FString::Printf(TEXT("%s_%s"), *Prefix, ToonProbeShotName(Shot)), Frames, OutPixels);
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
		TEXT("hair"), TEXT("override"), TEXT("translucent"), TEXT("runtime"), TEXT("sdf"), TEXT("backend"), TEXT("fidelity"), TEXT("outline"), TEXT("volume"), TEXT("facefix"), TEXT("skin"), TEXT("skinresponse"), TEXT("skinredness"), TEXT("all") };
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
			if (Exp != TEXT("ab") && Exp != TEXT("runtime") && Exp != TEXT("shadow") && Exp != TEXT("lighting") && Exp != TEXT("sdf") && Exp != TEXT("backend") && Exp != TEXT("fidelity") && Exp != TEXT("outline") && Exp != TEXT("volume") && Exp != TEXT("facefix") && Exp != TEXT("skin") && Exp != TEXT("skinresponse") && Exp != TEXT("skinredness"))
			{
				UE_LOG(LogTemp, Error, TEXT("[ToonProbe] NativePreset only supports ab/runtime/shadow/lighting/sdf/backend/fidelity/outline/volume/facefix/skin/skinresponse/skinredness, received %s"), *Exp);
				return 1;
			}
		}
	}
	if ((Exps.Contains(TEXT("runtime")) || Exps.Contains(TEXT("sdf")) || Exps.Contains(TEXT("outline")) || Exps.Contains(TEXT("volume")) || Exps.Contains(TEXT("facefix")) || Exps.Contains(TEXT("skin")) || Exps.Contains(TEXT("skinresponse")) || Exps.Contains(TEXT("skinredness"))) && !NativePreset.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] Runtime/SDF/outline/volume/facefix/skin/skinresponse/skinredness checks require -NativePreset"));
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
	FString RequestedSampleManifest;
	const bool bExplicitSample = FParse::Value(*Params, TEXT("SampleManifest="), RequestedSampleManifest)
		|| Params.Contains(TEXT("-SampleManifest="), ESearchCase::IgnoreCase);
	if (!ToonProbeLoadSample(Sample, RequestedSampleManifest, bExplicitSample, Error))
	{
		Report->SetStringField(TEXT("sample_manifest"), Sample.ManifestPath);
		Report->SetStringField(TEXT("source_pmx"), Sample.SourcePmx);
		Report->SetBoolField(TEXT("sample_manifest_explicit"), bExplicitSample);
		Report->SetBoolField(TEXT("sample_selection_success"), false);
		Report->SetStringField(TEXT("error"), Error);
		Report->SetNumberField(TEXT("failures"), 1);
		const FString FailureReport = Log.OutDir / (Log.Tag + TEXT("_report.json"));
		FFileHelper::SaveStringToFile(ToonProbeJsonToString(Report), *FailureReport, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] %s"), *Error);
		return 1;
	}
	Report->SetStringField(TEXT("sample_manifest"), Sample.ManifestPath);
	Report->SetStringField(TEXT("source_pmx"), Sample.SourcePmx);
	Report->SetBoolField(TEXT("sample_manifest_explicit"), bExplicitSample);
	Report->SetBoolField(TEXT("sample_selection_success"), true);
	FString PoseTimeText;
	if (FParse::Value(*Params, TEXT("PoseTime="), PoseTimeText))
	{
		float PoseTime = 0.0f;
		if (!LexTryParseString(PoseTime, *PoseTimeText) || !FMath::IsFinite(PoseTime) || (PoseTime < 0.0f && PoseTime != -1.0f))
		{
			UE_LOG(LogTemp, Error, TEXT("[ToonProbe] PoseTime 必须为非负秒数或 -1（参考姿势）"));
			return 1;
		}
		Sample.bReferencePose = PoseTime == -1.0f;
		Sample.BeautyTime = Sample.bReferencePose ? 0.0f : FMath::Clamp(PoseTime, 0.0f, static_cast<float>(Sample.Animation->GetPlayLength()));
		Sample.FaceTimes = { Sample.BeautyTime };
		Report->SetNumberField(TEXT("pose_time_requested"), PoseTime);
	}
	Report->SetBoolField(TEXT("reference_pose"), Sample.bReferencePose);
	Report->SetNumberField(TEXT("pose_time_seconds"), Sample.BeautyTime);
	Report->SetStringField(TEXT("character"), Sample.MeshPath);
	Report->SetStringField(TEXT("animation"), Sample.AnimationPath);
	if (Exps.Contains(TEXT("facefix")) && (!bExplicitSample || !Sample.bReferencePose || !Sample.MeshPath.Contains(TEXT("YYB16th"))))
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] facefix requires an explicit YYB16th SampleManifest and -PoseTime=-1"));
		return 1;
	}
	const FString SdfMount = TEXT("/MMDSdfValidation/");
	const FString SdfDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("EndfieldValidation/FaceSdfFixture/"));
	FPackageName::RegisterMountPoint(SdfMount, SdfDirectory);
	ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(SdfMount, SdfDirectory); };
	const bool bSaveSdfFixture = FParse::Param(*Params, TEXT("SaveSdfFixture"));
	const bool bLoadSdfFixture = FParse::Param(*Params, TEXT("LoadSdfFixture"));
	if (bExplicitSample && (bLoadSdfFixture || bSaveSdfFixture))
	{
		UE_LOG(LogTemp, Error, TEXT("[ToonProbe] 显式 SampleManifest 不能使用旧固定 SDF fixture；请用 -BakeSdf 在所选模型上临时烘焙"));
		return 1;
	}
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
		Report->SetNumberField(TEXT("sdf_data_version"), Bake.DataVersion);
		Report->SetNumberField(TEXT("sdf_frame_version"), Bake.FrameVersion);
		Report->SetNumberField(TEXT("sdf_uv_channel"), Bake.UVChannel);
		Report->SetBoolField(TEXT("sdf_head_frame_valid"), Bake.bHeadFrameValid);
		Report->SetStringField(TEXT("sdf_head_bone"), Bake.HeadBoneName.ToString());
		Report->SetStringField(TEXT("sdf_head_center_reference_component"), Bake.HeadCenter.ToString());
		Report->SetStringField(TEXT("sdf_head_center_head_local"), Bake.HeadCenterHeadLocal.ToString());
		Report->SetStringField(TEXT("sdf_head_forward"), Bake.HeadForward.ToString());
		Report->SetStringField(TEXT("sdf_head_right"), Bake.HeadRight.ToString());
		Report->SetStringField(TEXT("sdf_head_up"), Bake.HeadUp.ToString());
		Report->SetNumberField(TEXT("sdf_head_radius"), Bake.HeadRadius);
		Report->SetNumberField(TEXT("sdf_bone_radius"), Bake.BoneRadius);
		Report->SetStringField(TEXT("sdf_head_reference_rotation"), Bake.HeadReferenceRotation.ToString());
		Report->SetStringField(TEXT("sdf_head_skeleton_signature"), Bake.HeadSkeletonSignature);
		Report->SetStringField(TEXT("sdf_stats_scope"), TEXT("V5 projected_occluded_texels measures front-layer occlusion; clipped/depth_clamped report projection limits. Legacy front_dark/noncontinuous values are not V5 occlusion validation."));
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
			Item->SetNumberField(TEXT("projected_occluded_texels"), Stats.ProjectedOccludedTexels);
			Item->SetNumberField(TEXT("clipped_triangles"), Stats.ClippedTriangles);
			Item->SetNumberField(TEXT("depth_clamped_texels"), Stats.DepthClampedTexels);
			Item->SetBoolField(TEXT("skipped_translucent"), Stats.bSkippedTranslucent);
			BakeStats.Add(MakeShared<FJsonValueObject>(Item));
		}
		Report->SetArrayField(TEXT("sdf_bake_slot_stats"), BakeStats);
		TArray<TSharedPtr<FJsonValue>> OverlayStats, ForwardOverlaySlots;
		for (const int32 Slot : Bake.ForwardOverlaySlots) ForwardOverlaySlots.Add(MakeShared<FJsonValueNumber>(Slot));
		for (const MMDEndfieldFaceBinding::FOverlayStats& Stats : Bake.OverlayStats)
		{
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Stats.Slot);
			Item->SetNumberField(TEXT("host_material_index"), Stats.HostMaterialIndex);
			Item->SetStringField(TEXT("host_slot_name"), Stats.HostSlotName.ToString());
			Item->SetStringField(TEXT("source_role_reason"), Stats.SourceRoleReason);
			Item->SetBoolField(TEXT("associated"), Stats.bAssociated);
			Item->SetNumberField(TEXT("validated_lods"), Stats.ValidatedLODs);
			Item->SetNumberField(TEXT("tested_vertices"), Stats.TestedVertices);
			Item->SetNumberField(TEXT("matched_vertices"), Stats.MatchedVertices);
			Item->SetNumberField(TEXT("tested_triangles"), Stats.TestedTriangles);
			Item->SetNumberField(TEXT("valid_vertex_ratio"), Stats.ValidVertexRatio);
			Item->SetNumberField(TEXT("valid_area_ratio"), Stats.ValidAreaRatio);
			Item->SetNumberField(TEXT("minimum_head_weight"), Stats.MinimumHeadWeight);
			Item->SetNumberField(TEXT("max_matched_depth_error"), Stats.MaxMatchedDepthError);
			Item->SetStringField(TEXT("reason"), Stats.Reason);
			OverlayStats.Add(MakeShared<FJsonValueObject>(Item));
		}
		Report->SetArrayField(TEXT("sdf_forward_overlay_slots"), ForwardOverlaySlots);
		Report->SetArrayField(TEXT("sdf_overlay_association_stats"), OverlayStats);
		for (const FString& Note : Bake.Notes) Log.Note(TEXT("sdf_bake_note"), Note);
		for (const FString& Failure : Bake.Errors) Log.Note(TEXT("sdf_bake_error"), Failure);
		if (!bBaked || Bake.BoundSlots.IsEmpty()) return 1;
		TArray<TSharedPtr<FJsonValue>> TextureExports;
		if (const UMMDEndfieldFaceData* BakedData = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>())
		{
			for (const FMMDGeneratedFaceSdfSlot& Slot : BakedData->Slots)
			{
				UTexture2D* Texture = Slot.DirectionTexture;
				if (!Texture || !Bake.BoundSlots.Contains(Slot.MaterialIndex)) continue;
				TArray64<uint8> SourceBytes, Png;
				const FString Filename = Log.OutDir / FString::Printf(TEXT("%s_face_texture_slot%d_v%d.png"), *Log.Tag, Slot.MaterialIndex, Slot.DirectionVersion);
				const int32 Width = Texture->Source.GetSizeX(), Height = Texture->Source.GetSizeY();
				bool bExported = Texture->Source.GetFormat() == TSF_BGRA8 && Texture->Source.GetMipData(SourceBytes, 0)
					&& SourceBytes.Num() == int64(Width) * Height * sizeof(FColor);
				if (bExported)
				{
					// 按源 BGRA 字节直接编码，PNG 的 RGBA 对应阈值/深度/覆盖率，不做显示色彩调整。
					FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(reinterpret_cast<const FColor*>(SourceBytes.GetData()), int64(Width) * Height), Png);
					bExported = FFileHelper::SaveArrayToFile(Png, *Filename);
				}
				TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
				Item->SetNumberField(TEXT("slot"), Slot.MaterialIndex);
				ToonProbeWriteFaceBindingFields(Item, &Slot);
				Item->SetStringField(TEXT("texture"), Texture->GetPathName());
				Item->SetStringField(TEXT("file"), Filename);
				Item->SetNumberField(TEXT("width"), Width);
				Item->SetNumberField(TEXT("height"), Height);
				Item->SetBoolField(TEXT("exported"), bExported);
				Item->SetBoolField(TEXT("source_srgb"), Texture->SRGB);
				TextureExports.Add(MakeShared<FJsonValueObject>(Item));
				if (!bExported) ++Log.Failures;
			}
		}
		Report->SetArrayField(TEXT("sdf_bound_texture_source_exports"), TextureExports);
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
		if (NativePreset.IsValid() && (ToonProbeHasExp(Exps, TEXT("skin")) || ToonProbeHasExp(Exps, TEXT("skinresponse")) || ToonProbeHasExp(Exps, TEXT("skinredness"))))
		{
			for (const EBlendMode Blend : { BLEND_Opaque, BLEND_Masked, BLEND_Translucent })
			{
				const FString Path = ToonProbeNativeSurfaceParentPath(Blend, true);
				UMaterial* Face = LoadObject<UMaterial>(nullptr, *Path);
				if (Face)
				{
					Face->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
					ToonMaterials.Add(FString::Printf(TEXT("ToonProbe_Face_%s"), Blend == BLEND_Opaque ? TEXT("Opaque") : Blend == BLEND_Masked ? TEXT("Masked") : TEXT("Translucent")), Face);
				}
				else ++Log.Failures;
			}
			UMaterial* Outline = LoadObject<UMaterial>(nullptr, TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Outline.M_MMD_EndfieldV2_Outline"));
			if (Outline)
			{
				Outline->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
				ToonMaterials.Add(TEXT("ToonProbe_Outline"), Outline);
			}
			else ++Log.Failures;
		}
		const double BuildSeconds = FPlatformTime::Seconds() - MaterialStart;
		const double CompileSeconds = ToonProbeWaitForShaders();
		for (const TPair<FString, UMaterial*>& Pair : ToonMaterials)
		{
			bool bReady = false;
			const TArray<FString> CompileErrors = ToonProbeCompileErrors(Pair.Value, bReady);
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetStringField(TEXT("material"), Pair.Key);
			Item->SetStringField(TEXT("material_path"), Pair.Value->GetPathName());
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
	Rig.bFitSampleCamera = FParse::Param(*Params, TEXT("FitSampleCamera"));
	Report->SetBoolField(TEXT("fit_sample_camera"), Rig.bFitSampleCamera);
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
	for (const bool bSkinRednessExperiment : { false, true })
	if (ToonProbeHasExp(Exps, bSkinRednessExperiment ? TEXT("skinredness") : TEXT("skinresponse")) && NativePreset.IsValid())
	{
		const FTransform EntryCamera = Rig.Capture->GetComponentTransform();
		const float EntryFOV = Rig.Capture->FOVAngle;
		const FPostProcessSettings EntryPostProcess = Rig.PostProcess, EntryCapturePostProcess = Rig.Capture->PostProcessSettings;
		const TArray<FEngineShowFlagsSetting> EntryFlags = Rig.Capture->GetShowFlagSettings();
		UseToonBaseline();
		ToonProbePose(Rig, Sample.BeautyTime);
		ToonProbeSetShot(Rig, EToonProbeShot::Face);
		UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
		TSharedRef<FJsonObject> Response = ToonProbeJsonObject();
		Report->SetObjectField(bSkinRednessExperiment ? TEXT("skinredness") : TEXT("skinresponse"), Response);
		auto Check = [&](const TCHAR* Name, bool bPassed)
		{
			Response->SetBoolField(Name, bPassed);
			if (!bPassed) ++Log.Failures;
			return bPassed;
		};
		Response->SetStringField(TEXT("scope"), bSkinRednessExperiment
			? TEXT("Layout42: only SpecularRamp RGB slot216.R strength changes between zero and preset. Master, LUT/view response, slot216.GB/217.RGB, RD alpha, SDF and native specular stay fixed. Both albedo preprocessing switches and environment lighting showflags are disabled. Normal AB retains preset GI. Shadow-only cube placement is CPU evidence; actual occlusion must be established from captured shadow-on/off images. No spatial diffusion or thickness transmission.")
			: TEXT("Layout41/42 plugin-owned procedural 6x6x6 color LUT and view response. Same-run Master0/1, with both albedo preprocessing switches disabled. Not a game LUT, screen diffusion or thickness transmission. The last two softness captures form a separate experiment."));
		Response->SetBoolField(TEXT("body_skin_tested"), false);
		Response->SetBoolField(TEXT("all_gpu_verified"), false);
		Response->SetBoolField(TEXT("pixel_difference_verified_by_this_report"), false);
		Response->SetNumberField(TEXT("planned_core_images"), bSkinRednessExperiment ? 18 : 16);
		Response->SetNumberField(TEXT("planned_softness_images"), bSkinRednessExperiment ? 0 : 2);
		Response->SetNumberField(TEXT("planned_shadow_images"), bSkinRednessExperiment ? 8 : 0);
		Response->SetNumberField(TEXT("planned_images"), bSkinRednessExperiment ? 26 : 18);
		if (bSkinRednessExperiment)
		{
			Response->SetNumberField(TEXT("redness_parameters_slot"), 216);
			Response->SetNumberField(TEXT("redness_tint_slot"), 217);
			Response->SetNumberField(TEXT("preset_redness_strength_expected"), 0.18f);
		}
		const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
		UMMDEndfieldFaceData* FaceData = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		bool bReady = Check(TEXT("native_layout_path_ready"), bCanToon && Backend.bProfileMarkerAllowed && Backend.Mode == 2
			&& ToonProbeCVar(TEXT("r.Substrate.ProjectGBufferFormat")) == TEXT("1")
			&& FCString::Atoi(*ToonProbeCVar(TEXT("r.Substrate.ToonProfile.Resolution"))) >= 256);
		bReady &= Check(TEXT("explicit_yyb_reference_pose"), bExplicitSample && Sample.bReferencePose && Sample.MeshPath.Contains(TEXT("YYB16th")));
		bReady &= Check(TEXT("current_face_data_ready"), FaceData && FaceData->Version == UMMDEndfieldFaceData::CurrentVersion
			&& FaceData->FrameVersion == MMDEndfieldHeadFrame::CurrentFrameVersion && Rig.HeadIndex != INDEX_NONE && Rig.Stage->KeyLight);
		bool bParentsReady = true;
		for (const auto& Pair : { TPair<const TCHAR*, EBlendMode>(TEXT("Opaque"), BLEND_Opaque), { TEXT("Masked"), BLEND_Masked }, { TEXT("Translucent"), BLEND_Translucent } })
		{
			const FString OrdinaryName = Pair.Value == BLEND_Translucent ? TEXT("ToonProbe_TranslucentGrey") : FString(TEXT("ToonProbe_")) + Pair.Key;
			UMaterial* Ordinary = ReadyMaterial(*OrdinaryName);
			UMaterial* Face = ReadyMaterial(*(FString(TEXT("ToonProbe_Face_")) + Pair.Key));
			bParentsReady &= Ordinary && Face && Ordinary->GetPathName() == ToonProbeNativeSurfaceParentPath(Pair.Value, false)
				&& Face->GetPathName() == ToonProbeNativeSurfaceParentPath(Pair.Value, true);
		}
		UMaterial* Outline = ReadyMaterial(TEXT("ToonProbe_Outline"));
		bParentsReady &= Outline && Outline->GetPathName() == TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Outline.M_MMD_EndfieldV2_Outline");
		bReady &= Check(TEXT("seven_exact_parent_shaders_ready"), bParentsReady);
		struct FToonProbeResponseSlot
		{
			int32 Slot = INDEX_NONE;
			UMaterialInstanceDynamic* Material = nullptr;
			UMaterialInterface* Parent = nullptr;
			UToonProfile* Profile = nullptr;
			bool bOverride = false, bHasOpacity = false, bHasBase = false;
			bool bSkin = false, bFace = false;
			EMMDMaterialRole Role = EMMDMaterialRole::Generic;
			float Opacity = 0.0f;
			FLinearColor Base = FLinearColor::Transparent;
			TArray<FScalarParameterValue> Scalars;
			TArray<FVectorParameterValue> Vectors;
			TArray<FTextureParameterValue> Textures;
		};
		struct FToonProbeResponseProfile
		{
			UToonProfile* Original = nullptr;
			UToonProfile* Copy = nullptr;
			float Metadata[9] = {};
			float Redness[6] = {};
			TArray<uint8> OriginalBytes;
		};
		constexpr int32 Centers[9] = { 9, 10, 11, 21, 22, 23, 29, 30, 31 };
		TArray<TSharedPtr<FJsonValue>> MetadataCenters;
		for (const int32 Center : Centers) MetadataCenters.Add(MakeShared<FJsonValueNumber>(Center));
		Response->SetArrayField(TEXT("metadata_centers_over64"), MetadataCenters);
		auto AlphaCurve = [](UToonProfile* Profile) -> FRichCurve&
		{
			return Profile->Settings.DiffuseRamp.ExternalCurve ? Profile->Settings.DiffuseRamp.ExternalCurve->FloatCurves[3]
				: Profile->Settings.DiffuseRamp.ColorCurves[3];
		};
		auto SpecularCurve = [](UToonProfile* Profile, int32 Channel) -> FRichCurve&
		{
			return Profile->Settings.SpecularRamp.ExternalCurve ? Profile->Settings.SpecularRamp.ExternalCurve->FloatCurves[Channel]
				: Profile->Settings.SpecularRamp.ColorCurves[Channel];
		};
		TArray<FToonProbeResponseSlot> Slots;
		TArray<FToonProbeResponseProfile> ProfileCopies;
		TMap<UToonProfile*, int32> ProfileIndices;
		TArray<TSharedPtr<FJsonValue>> SlotReports;
		int32 SkinCount = 0, FaceCount = 0, InlineCount = 0;
		bool bMetadataValid = true, bSourceAlphaMatches = true;
		const UMMDStageMaterialState* MaterialState = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
			if (!MID) { bReady = false; ++Log.Failures; continue; }
			FToonProbeResponseSlot& Saved = Slots.AddDefaulted_GetRef();
			Saved.Slot = Slot; Saved.Material = MID; Saved.Parent = MID->Parent.Get();
			Saved.Profile = MID->ToonProfileOverride; Saved.bOverride = MID->bOverrideToonProfile;
			Saved.Scalars = MID->ScalarParameterValues; Saved.Vectors = MID->VectorParameterValues; Saved.Textures = MID->TextureParameterValues;
			Saved.Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot);
			Saved.bSkin = Saved.Role == EMMDMaterialRole::Skin;
			Saved.bHasOpacity = MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Saved.Opacity);
			Saved.bHasBase = MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Saved.Base);
			const FName SlotName = Sample.Mesh->GetMaterials()[Slot].MaterialSlotName;
			const FMMDGeneratedFaceSdfSlot* Binding = FaceData ? FaceData->FindDirectionSlot(Slot, SlotName) : nullptr;
			Saved.bFace = Saved.bSkin && Binding;
			if (Saved.bFace) { ++FaceCount; if (Binding->bForwardOverlay) ++InlineCount; }
			KeepAlive.Emplace(MID);
			if (Saved.bSkin)
			{
				++SkinCount;
				UToonProfile* Original = Saved.Profile;
				if (!Original || !Original->HasAnyFlags(RF_Transient)) { bMetadataValid = false; continue; }
				if (!ProfileIndices.Contains(Original))
				{
					const int32 Index = ProfileCopies.Num();
					FToonProbeResponseProfile& Profile = ProfileCopies.AddDefaulted_GetRef();
					Profile.Original = Original;
					FObjectWriter Writer(Original, Profile.OriginalBytes);
					FRichCurve& Alpha = AlphaCurve(Original);
					for (int32 Field = 0; Field < 9; ++Field) Profile.Metadata[Field] = Alpha.Eval(float(Centers[Field]) / 64.0f);
					bMetadataValid &= (bSkinRednessExperiment ? FMath::IsNearlyEqual(Profile.Metadata[0], 42.0f)
						: FMath::IsNearlyEqual(Profile.Metadata[0], 41.0f) || FMath::IsNearlyEqual(Profile.Metadata[0], 42.0f)) && FMath::IsNearlyEqual(Alpha.Eval(0.0f), -33.0f)
						&& FMath::IsNearlyEqual(Alpha.Eval(1.0f), -65.0f) && FMath::IsNearlyEqual(Alpha.Eval(0.25f), 2.0f);
					for (const float Value : Profile.Metadata) bMetadataValid &= FMath::IsFinite(Value);
					for (int32 Field = 0; Field < 6; ++Field)
						Profile.Redness[Field] = SpecularCurve(Original, Field % 3).Eval((216.5f + Field / 3) / 256.0f);
					if (bSkinRednessExperiment)
					{
						bMetadataValid &= FMath::IsNearlyEqual(Profile.Metadata[1], 1.0f) && FMath::IsNearlyEqual(Profile.Redness[0], 0.18f)
							&& FMath::IsNearlyEqual(Profile.Redness[1], 0.22f) && FMath::IsNearlyEqual(Profile.Redness[2], 0.035f)
							&& FMath::IsNearlyEqual(Profile.Redness[3], 1.0f) && FMath::IsNearlyEqual(Profile.Redness[4], 0.4f) && FMath::IsNearlyEqual(Profile.Redness[5], 0.3f);
					}
					Profile.Copy = DuplicateObject<UToonProfile>(Original, GetTransientPackage());
					Profile.Copy->Guid = FGuid::NewGuid();
					Profile.Copy->SetFlags(RF_Transient); Profile.Copy->ClearFlags(RF_Public | RF_Standalone);
					if (Original->Settings.DiffuseRamp.ExternalCurve)
					{
						UCurveLinearColor* Copy = DuplicateObject<UCurveLinearColor>(Original->Settings.DiffuseRamp.ExternalCurve, GetTransientPackage());
						Profile.Copy->Settings.DiffuseRamp.ExternalCurve = Copy;
						KeepAlive.Emplace(Copy);
					}
					if (Original->Settings.SpecularRamp.ExternalCurve)
					{
						UCurveLinearColor* Copy = DuplicateObject<UCurveLinearColor>(Original->Settings.SpecularRamp.ExternalCurve, GetTransientPackage());
						Profile.Copy->Settings.SpecularRamp.ExternalCurve = Copy; KeepAlive.Emplace(Copy);
					}
					KeepAlive.Emplace(Original); KeepAlive.Emplace(Profile.Copy);
					ProfileIndices.Add(Original, Index);
				}
			}
			UMaterialInterface* Source = MaterialState && MaterialState->SourceMaterials.IsValidIndex(Slot) ? MaterialState->SourceMaterials[Slot].Get()
				: Sample.Mesh->GetMaterials()[Slot].MaterialInterface.Get();
			float SourceOpacity = 0.0f;
			const bool bSourceOpacity = Source && Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), SourceOpacity);
			if (bSourceOpacity) bSourceAlphaMatches &= Saved.bHasOpacity && FMath::IsNearlyEqual(SourceOpacity, Saved.Opacity);
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Slot); Item->SetStringField(TEXT("slot_name"), SlotName.ToString());
			Item->SetStringField(TEXT("role"), UEnum::GetValueAsString(Saved.Role));
			Item->SetBoolField(TEXT("skin_response_target"), Saved.bSkin); Item->SetBoolField(TEXT("bound_face_target"), Saved.bFace);
			Item->SetStringField(TEXT("parent"), Saved.Parent ? Saved.Parent->GetPathName() : TEXT("none"));
			Item->SetStringField(TEXT("original_profile"), Saved.Profile ? Saved.Profile->GetPathName() : TEXT("none"));
			Item->SetBoolField(TEXT("source_opacity_parameter_found"), bSourceOpacity);
			if (bSourceOpacity) Item->SetNumberField(TEXT("source_opacity"), SourceOpacity);
			if (Saved.bHasOpacity) Item->SetNumberField(TEXT("actual_opacity"), Saved.Opacity);
			if (Saved.bHasBase) Item->SetStringField(TEXT("actual_base_color_rgba"), Saved.Base.ToString());
			ToonProbeWriteFaceBindingFields(Item, Binding); SlotReports.Add(MakeShared<FJsonValueObject>(Item));
		}
		Response->SetArrayField(TEXT("source_slots"), SlotReports);
		Response->SetNumberField(TEXT("skin_slot_count"), SkinCount); Response->SetNumberField(TEXT("bound_face_slot_count"), FaceCount);
		Response->SetNumberField(TEXT("packed_face_slot_count"), FaceCount - InlineCount); Response->SetNumberField(TEXT("inline_face_slot_count"), InlineCount);
		Response->SetNumberField(TEXT("unbound_skin_fallback_slot_count"), SkinCount - FaceCount);
		bReady &= Check(TEXT("seven_skin_five_face_targets"), SkinCount == 7 && FaceCount == 5 && InlineCount == 2);
		bReady &= Check(bSkinRednessExperiment ? TEXT("skin_layout42_metadata_preserved") : TEXT("skin_layout41_metadata_preserved"), bMetadataValid && !ProfileCopies.IsEmpty());
		bReady &= Check(TEXT("source_opacity_preserved"), bSourceAlphaMatches);
		ToonProbeValidateSlots(Rig, Log, bSkinRednessExperiment ? TEXT("skinredness_initial") : TEXT("skinresponse_initial"));
		if (bReady)
		{
			struct FToonProbeResponseLight { ULightComponent* Light; float Intensity; };
			TArray<FToonProbeResponseLight> SceneLights;
			for (TObjectIterator<ULightComponent> It; It; ++It)
				if (It->GetWorld() == Rig.World) SceneLights.Add({ *It, It->Intensity });
			UDirectionalLightComponent* Key = Rig.Stage->KeyLight.Get();
			const float KeyIntensity = Key->Intensity;
			Response->SetNumberField(TEXT("scene_light_component_count"), SceneLights.Num());
			const bool bKeyTracked = SceneLights.ContainsByPredicate([Key](const FToonProbeResponseLight& Entry) { return Entry.Light == Key; });
			Check(TEXT("key_light_in_scene_snapshot"), bKeyTracked && KeyIntensity > 0.0f);
			const FRotator KeyRotation = Key->GetComponentRotation();
			const bool bKeyShadow = Key->CastShadows, bKeyDynamicShadow = Key->CastDynamicShadows;
			const ECastRayTracedShadow::Type RayShadow = Key->CastRaytracedShadow;
			const bool bDistanceFieldShadow = Key->bUseRayTracedDistanceFieldShadows;
			auto RecordKeyShadowState = [&]()
			{
				TSharedRef<FJsonObject> State = ToonProbeJsonObject();
				State->SetBoolField(TEXT("cast_shadows"), Key->CastShadows);
				State->SetBoolField(TEXT("cast_dynamic_shadows"), Key->CastDynamicShadows);
				State->SetNumberField(TEXT("ray_traced_shadows"), static_cast<int32>(Key->CastRaytracedShadow.GetValue()));
				State->SetBoolField(TEXT("distance_field_shadows"), Key->bUseRayTracedDistanceFieldShadows);
				State->SetStringField(TEXT("rotation"), Key->GetComponentRotation().ToString());
				return State;
			};
			Response->SetObjectField(TEXT("native_key_shadow_state_before"), RecordKeyShadowState());
			const bool bContact = Rig.Character->bCastContactShadow;
			const FTransform OccluderTransform = Rig.Occluder->GetComponentTransform();
			const bool bOccluderDepth = Rig.Occluder->bRenderInDepthPass, bOccluderMain = Rig.Occluder->bRenderInMainPass;
			const bool bOccluderVisible = Rig.Occluder->IsVisible(), bOccluderShadow = Rig.Occluder->CastShadow;
			TArray<UMaterialInterface*> OccluderMaterials;
			for (int32 Slot = 0; Slot < Rig.Occluder->GetNumMaterials(); ++Slot) OccluderMaterials.Add(Rig.Occluder->GetMaterial(Slot));
			bReady &= Check(TEXT("shadow_only_occluder_available"), !bSkinRednessExperiment || (Rig.Occluder->GetStaticMesh()
				&& Rig.Occluder->CastShadow && !Rig.Occluder->bRenderInMainPass && Rig.Occluder->IsVisible()));
			bool bRestored = false;
			auto AssignProfile = [](UMaterialInstanceDynamic* MID, UToonProfile* Profile, bool bOverride)
			{
				const TArray<FScalarParameterValue> Scalars = MID->ScalarParameterValues;
				const TArray<FVectorParameterValue> Vectors = MID->VectorParameterValues;
				const TArray<FTextureParameterValue> Textures = MID->TextureParameterValues;
				MID->ToonProfileOverride = Profile; MID->bOverrideToonProfile = bOverride; MID->ClearParameterValues();
				for (const FScalarParameterValue& Value : Scalars) MID->SetScalarParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				for (const FVectorParameterValue& Value : Vectors) MID->SetVectorParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				for (const FTextureParameterValue& Value : Textures) MID->SetTextureParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
			};
			auto Restore = [&]()
			{
				if (bRestored) return; bRestored = true;
				for (const FToonProbeResponseSlot& Saved : Slots)
				{
					UMaterialInstanceDynamic* MID = Saved.Material;
					MID->ToonProfileOverride = Saved.Profile; MID->bOverrideToonProfile = Saved.bOverride; MID->ClearParameterValues();
					for (const FScalarParameterValue& Value : Saved.Scalars) MID->SetScalarParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
					for (const FVectorParameterValue& Value : Saved.Vectors) MID->SetVectorParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
					for (const FTextureParameterValue& Value : Saved.Textures) MID->SetTextureParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				}
				for (const auto& Light : SceneLights) Light.Light->SetIntensity(Light.Intensity);
				Key->SetIntensity(KeyIntensity);
				Key->SetWorldRotation(KeyRotation); Key->SetCastShadows(bKeyShadow); Key->CastDynamicShadows = bKeyDynamicShadow;
				Key->SetCastRaytracedShadows(RayShadow);
				// 精确还原快照；运行时 setter 会按项目距离场支持裁剪原始属性位。
				Key->bUseRayTracedDistanceFieldShadows = bDistanceFieldShadow;
				Key->MarkRenderStateDirty(); Rig.Character->SetCastContactShadow(bContact);
				Rig.Occluder->SetWorldTransform(OccluderTransform);
				Rig.Occluder->SetRenderInDepthPass(bOccluderDepth);
				Rig.Capture->SetShowFlagSettings(EntryFlags); Rig.PostProcess = EntryPostProcess;
				Rig.Capture->PostProcessSettings = EntryCapturePostProcess; Rig.Capture->SetWorldTransform(EntryCamera); Rig.Capture->FOVAngle = EntryFOV;
				Rig.World->SendAllEndOfFrameUpdates(); FlushRenderingCommands();
			};
			ON_SCOPE_EXIT { Restore(); };
			for (const FToonProbeResponseSlot& Saved : Slots)
			{
				if (Saved.bSkin) AssignProfile(Saved.Material, ProfileCopies[ProfileIndices[Saved.Profile]].Copy, true);
				Saved.Material->SetScalarParameterValue(TEXT("ToonSkinEnabled"), 0.0f);
				Saved.Material->SetScalarParameterValue(TEXT("ToonFaceSkinEnabled"), 0.0f);
			}
			for (const auto& Light : SceneLights) if (Light.Light != Key) Light.Light->SetIntensity(0.0f);
			Key->SetCastShadows(true); Key->CastDynamicShadows = true;
			Key->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled); Key->SetUseRayTracedDistanceFieldShadows(false); Key->MarkRenderStateDirty();
			Rig.Character->SetCastContactShadow(false);
			if (bSkinRednessExperiment) Rig.Occluder->SetRenderInDepthPass(false);
			if (!bSkinRednessExperiment) ToonProbeSetShowFlags(Rig, { TEXT("GlobalIllumination"), TEXT("SkyLighting"), TEXT("ReflectionEnvironment"),
				TEXT("LumenReflections"), TEXT("ScreenSpaceReflections"), TEXT("AmbientOcclusion"), TEXT("LumenShortRangeAmbientOcclusion"), TEXT("ContactShadows") });
			FLinearColor F, R;
			Rig.Character->GetMaterial(FaceData->Slots[0].MaterialIndex)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), F);
			Rig.Character->GetMaterial(FaceData->Slots[0].MaterialIndex)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), R);
			const FVector HeadForward = FVector(F.R, F.G, F.B).GetSafeNormal(), HeadRight = FVector(R.R, R.G, R.B).GetSafeNormal();
			const FVector HeadUp = FVector::CrossProduct(HeadRight, HeadForward).GetSafeNormal();
			const FVector Head = Rig.Character->GetBoneTransform(Rig.HeadIndex).GetLocation();
			const FVector CameraTarget = Head + FVector(0.0f, -3.0f, 8.0f), BaseCamera = Rig.Capture->GetComponentLocation();
			Response->SetStringField(TEXT("head_forward"), HeadForward.ToString()); Response->SetStringField(TEXT("head_right"), HeadRight.ToString());
			Response->SetStringField(TEXT("head_up"), HeadUp.ToString()); Response->SetNumberField(TEXT("base_key_intensity"), KeyIntensity);
			Response->SetStringField(TEXT("camera_orbit_contract"), TEXT("Camera alone rotates about head-up; model and world light stay fixed for view-left/right. ROI positions must be reviewed separately for these views."));
			TArray<TSharedPtr<FJsonValue>> Captures;
			const int32 FirstImage = Log.Images.Num();
			bool bAllControls = bReady && bKeyTracked && KeyIntensity > 0.0f;
			auto SetPlateau = [](FRichCurve& Alpha, int32 Center, float Value)
			{
				for (const float Delta : { -1.0f / 256.0f, 1.0f / 256.0f })
					Alpha.SetKeyInterpMode(Alpha.UpdateOrAddKey(float(Center) / 64.0f + Delta, Value), RCIM_Linear);
			};
			auto Capture = [&](const TCHAR* Name, const TCHAR* Group, bool bOn, float LightYaw, float CameraYaw, float Multiplier, bool bSoftness, float Softness, bool bNativeShadows = true, bool bOccluded = false)
			{
				bool bControls = bReady && bKeyTracked && KeyIntensity > 0.0f;
				TArray<TSharedPtr<FJsonValue>> Metadata;
				for (FToonProbeResponseProfile& Profile : ProfileCopies)
				{
					FRichCurve& Alpha = AlphaCurve(Profile.Copy);
					if (bSkinRednessExperiment)
					{
						FRichCurve& Strength = SpecularCurve(Profile.Copy, 0);
						Strength.SetKeyInterpMode(Strength.UpdateOrAddKey(216.0f / 256.0f, bOn ? Profile.Redness[0] : 0.0f), RCIM_Constant);
					}
					else SetPlateau(Alpha, 10, bOn ? 1.0f : 0.0f);
					if (bSoftness) SetPlateau(Alpha, 31, Softness);
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetStringField(TEXT("profile"), Profile.Copy->GetPathName());
					TArray<TSharedPtr<FJsonValue>> Values;
					for (int32 Field = 0; Field < 9; ++Field)
					{
						const float Value = Alpha.Eval(float(Centers[Field]) / 64.0f);
						const float Expected = !bSkinRednessExperiment && Field == 1 ? (bOn ? 1.0f : 0.0f) : Field == 8 && bSoftness ? Softness : Profile.Metadata[Field];
						bControls &= FMath::IsNearlyEqual(Value, Expected); Values.Add(MakeShared<FJsonValueNumber>(Value));
					}
					Item->SetArrayField(TEXT("layout41_values"), Values); Metadata.Add(MakeShared<FJsonValueObject>(Item));
					if (bSkinRednessExperiment)
					{
						TArray<TSharedPtr<FJsonValue>> RednessValues;
						for (int32 Field = 0; Field < 6; ++Field)
						{
							const float Value = SpecularCurve(Profile.Copy, Field % 3).Eval((216.5f + Field / 3) / 256.0f);
							bControls &= FMath::IsNearlyEqual(Value, Field == 0 ? (bOn ? Profile.Redness[0] : 0.0f) : Profile.Redness[Field]);
							RednessValues.Add(MakeShared<FJsonValueNumber>(Value));
						}
						Item->SetArrayField(TEXT("redness_tail216_217_rgb"), RednessValues);
					}
					ToonProbeRefreshProfile(Profile.Copy);
				}
				const float Yaw = FMath::DegreesToRadians(LightYaw), Pitch = FMath::DegreesToRadians(25.0f);
				const FVector L = ((HeadForward * FMath::Cos(Yaw) + HeadRight * FMath::Sin(Yaw)) * FMath::Cos(Pitch) + HeadUp * FMath::Sin(Pitch)).GetSafeNormal();
				Key->SetWorldRotation((-L).Rotation()); Key->SetIntensity(KeyIntensity * Multiplier);
				bool bBlockerRayIntersects = false;
				if (bSkinRednessExperiment)
				{
					TArray<FEngineShowFlagsSetting> Settings;
					for (const TCHAR* Flag : { TEXT("GlobalIllumination"), TEXT("SkyLighting"), TEXT("ReflectionEnvironment"), TEXT("LumenReflections"), TEXT("ScreenSpaceReflections"), TEXT("AmbientOcclusion"), TEXT("LumenShortRangeAmbientOcclusion"), TEXT("Specular") })
					{
						FEngineShowFlagsSetting& Setting = Settings.AddDefaulted_GetRef(); Setting.ShowFlagName = Flag; Setting.Enabled = FString(Flag) == TEXT("Specular");
					}
					FEngineShowFlagsSetting& ShadowSetting = Settings.AddDefaulted_GetRef(); ShadowSetting.ShowFlagName = TEXT("DynamicShadows"); ShadowSetting.Enabled = bNativeShadows;
					FEngineShowFlagsSetting& ContactSetting = Settings.AddDefaulted_GetRef(); ContactSetting.ShowFlagName = TEXT("ContactShadows"); ContactSetting.Enabled = false;
					Rig.Capture->SetShowFlagSettings(Settings);
					Rig.Occluder->SetWorldTransform(bOccluded ? FTransform(FRotationMatrix::MakeFromX(L).Rotator(), Head + L * 120.0f, FVector(0.2f, 1.2f, 1.2f)) : OccluderTransform);
					if (bOccluded && Rig.Occluder->GetStaticMesh())
					{
						const FVector Start = Rig.Occluder->GetComponentTransform().InverseTransformPosition(Head);
						const FVector End = Rig.Occluder->GetComponentTransform().InverseTransformPosition(Head + L * 240.0f);
						bBlockerRayIntersects = FMath::LineBoxIntersection(Rig.Occluder->GetStaticMesh()->GetBoundingBox(), Start, End, End - Start);
					}
					bControls &= (!bOccluded || bBlockerRayIntersects) && !Rig.Occluder->bRenderInMainPass && !Rig.Occluder->bRenderInDepthPass && Rig.Occluder->CastShadow;
				}
				const FVector Location = Head + (BaseCamera - Head).RotateAngleAxis(CameraYaw, HeadUp);
				Rig.Capture->SetWorldLocationAndRotation(Location, (CameraTarget - Location).Rotation());
				bControls &= !HeadUp.IsNearlyZero() && !HeadForward.IsNearlyZero() && !HeadRight.IsNearlyZero();
				bool bAlpha = true, bBindings = true;
				for (const FToonProbeResponseSlot& Saved : Slots)
				{
					float Body = 0.0f, Face = 0.0f, Opacity = 0.0f;
					FLinearColor Base;
					Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonSkinEnabled")), Body);
					Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), Face);
					bControls &= FMath::IsNearlyZero(Body) && FMath::IsNearlyZero(Face) && Saved.Material->Parent == Saved.Parent
						&& UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Saved.Slot) == Saved.Role;
					bControls &= Saved.Material->ToonProfileOverride == (Saved.bSkin ? ProfileCopies[ProfileIndices[Saved.Profile]].Copy : Saved.Profile);
					bAlpha &= !Saved.bHasOpacity || (Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity) && FMath::IsNearlyEqual(Opacity, Saved.Opacity));
					bAlpha &= !Saved.bHasBase || (Saved.Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Base) && Base.Equals(Saved.Base));
					if (Saved.bFace)
					{
						float Enabled = 0.0f, Version = 0.0f, Payload = 0.0f;
						UTexture* Texture = nullptr;
						Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled);
						Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version);
						Saved.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Payload);
						Saved.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture);
						bBindings &= Enabled == 1.0f && Payload == 1.0f && Version == UMMDEndfieldFaceData::CurrentVersion
							&& Texture == FaceData->FindDirectionTexture(Saved.Slot, Sample.Mesh->GetMaterials()[Saved.Slot].MaterialSlotName);
					}
				}
				for (const auto& Light : SceneLights) if (Light.Light != Key) bControls &= FMath::IsNearlyZero(Light.Light->Intensity);
				bControls &= FMath::IsNearlyEqual(Key->Intensity, KeyIntensity * Multiplier) && Rig.Capture->ShowFlags.DynamicShadows == bNativeShadows
					&& Key->CastShadows && Key->CastDynamicShadows && Rig.Character->CastShadow
					&& !Rig.Capture->ShowFlags.ContactShadows && !Rig.Capture->ShowFlags.GlobalIllumination && !Rig.Capture->ShowFlags.SkyLighting
					&& !Rig.Capture->ShowFlags.ReflectionEnvironment && !Rig.Capture->ShowFlags.LumenReflections && !Rig.Capture->ShowFlags.ScreenSpaceReflections
					&& Rig.Capture->ShowFlags.Specular;
				TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
				Item->SetStringField(TEXT("image"), Name); Item->SetStringField(TEXT("group"), Group);
				Item->SetBoolField(TEXT("master_enabled"), bSkinRednessExperiment ? ProfileCopies[0].Metadata[1] > 0.5f : bOn); Item->SetBoolField(TEXT("softness_override"), bSoftness);
				if (bSkinRednessExperiment)
				{
					Item->SetBoolField(TEXT("redness_enabled"), bOn); Item->SetNumberField(TEXT("redness_strength"), bOn ? ProfileCopies[0].Redness[0] : 0.0f);
					Item->SetBoolField(TEXT("native_shadows_enabled"), bNativeShadows); Item->SetBoolField(TEXT("occluder_placed"), bOccluded);
					Item->SetBoolField(TEXT("cpu_light_ray_intersects_occluder"), bBlockerRayIntersects);
					Item->SetBoolField(TEXT("occlusion_verified_by_cpu"), false); Item->SetStringField(TEXT("occluder_transform"), Rig.Occluder->GetComponentTransform().ToString());
					Item->SetBoolField(TEXT("showflag_global_illumination"), Rig.Capture->ShowFlags.GlobalIllumination);
					Item->SetBoolField(TEXT("showflag_skylighting"), Rig.Capture->ShowFlags.SkyLighting);
					Item->SetBoolField(TEXT("showflag_reflection_environment"), Rig.Capture->ShowFlags.ReflectionEnvironment);
					Item->SetBoolField(TEXT("showflag_lumen_reflections"), Rig.Capture->ShowFlags.LumenReflections);
					Item->SetBoolField(TEXT("showflag_screen_space_reflections"), Rig.Capture->ShowFlags.ScreenSpaceReflections);
					Item->SetBoolField(TEXT("showflag_specular"), Rig.Capture->ShowFlags.Specular);
					Item->SetBoolField(TEXT("showflag_ambient_occlusion"), Rig.Capture->ShowFlags.AmbientOcclusion);
					Item->SetBoolField(TEXT("contact_shadows_enabled"), Rig.Capture->ShowFlags.ContactShadows);
					Item->SetBoolField(TEXT("occluder_main_pass_enabled"), Rig.Occluder->bRenderInMainPass);
					Item->SetBoolField(TEXT("occluder_depth_pass_enabled"), Rig.Occluder->bRenderInDepthPass);
				}
				if (bSoftness) Item->SetNumberField(TEXT("angular_softness"), Softness);
				Item->SetNumberField(TEXT("head_relative_light_yaw"), LightYaw); Item->SetNumberField(TEXT("head_relative_light_elevation"), 25.0f);
				Item->SetNumberField(TEXT("camera_orbit_yaw"), CameraYaw); Item->SetNumberField(TEXT("main_light_multiplier"), Multiplier);
				Item->SetStringField(TEXT("mesh_transform"), Rig.Character->GetComponentTransform().ToString());
				Item->SetNumberField(TEXT("key_intensity"), Key->Intensity); Item->SetStringField(TEXT("key_color"), Key->GetLightColor().ToString());
				Item->SetStringField(TEXT("surface_to_light_world"), L.ToString()); Item->SetStringField(TEXT("key_rotation"), Key->GetComponentRotation().ToString());
				Item->SetArrayField(TEXT("profiles"), Metadata);
				Item->SetBoolField(TEXT("cpu_controls_verified"), bControls); Item->SetBoolField(TEXT("cpu_source_alpha_unchanged"), bAlpha);
				Item->SetBoolField(TEXT("cpu_five_face_bindings_preserved"), bBindings);
				const FPostProcessSettings& PP = Rig.Capture->PostProcessSettings;
				Item->SetNumberField(TEXT("auto_exposure_method"), int32(PP.AutoExposureMethod)); Item->SetNumberField(TEXT("exposure_bias"), PP.AutoExposureBias);
				Item->SetBoolField(TEXT("physical_camera_exposure"), PP.AutoExposureApplyPhysicalCameraExposure);
				Item->SetNumberField(TEXT("white_temperature"), PP.WhiteTemp); Item->SetStringField(TEXT("color_gain"), PP.ColorGain.ToString());
				Item->SetStringField(TEXT("color_gamma"), PP.ColorGamma.ToString()); Item->SetStringField(TEXT("color_contrast"), PP.ColorContrast.ToString());
				Item->SetStringField(TEXT("color_saturation"), PP.ColorSaturation.ToString()); Item->SetNumberField(TEXT("film_toe"), PP.FilmToe); Item->SetNumberField(TEXT("film_slope"), PP.FilmSlope);
				bAllControls &= bControls && bAlpha && bBindings;
				if (bControls && bAlpha && bBindings) { Rig.Capture->bCameraCutThisFrame = true; ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 24)); }
				else { ++Log.Failures; Item->SetBoolField(TEXT("capture_skipped"), true); }
				Captures.Add(MakeShared<FJsonValueObject>(Item));
			};
			const TCHAR* Prefix = bSkinRednessExperiment ? TEXT("skinredness") : TEXT("skinresponse");
			TArray<TPair<const TCHAR*, float>> LightCases = { { TEXT("side"), -65.0f }, { TEXT("back"), 120.0f } };
			if (bSkinRednessExperiment) LightCases.Insert(TPair<const TCHAR*, float>(TEXT("front"), 0.0f), 0);
			for (const auto& Light : LightCases)
			{
				for (const bool bOn : { false, true }) Capture(*FString::Printf(TEXT("%s_%s_%s_face"), Prefix, Light.Key, bOn ? TEXT("on") : TEXT("off")), TEXT("core"), bOn, Light.Value, 0.0f, 1.0f, false, 0.0f);
				if (bSkinRednessExperiment || FString(Light.Key) == TEXT("back"))
					if (FString(Light.Key) != TEXT("front")) Capture(*FString::Printf(TEXT("%s_%s_off_repeat_face"), Prefix, Light.Key), TEXT("core"), false, Light.Value, 0.0f, 1.0f, false, 0.0f);
			}
			for (const auto& View : { TPair<const TCHAR*, float>(TEXT("left"), -25.0f), { TEXT("right"), 25.0f } })
				for (const bool bOn : { false, true }) Capture(*FString::Printf(TEXT("%s_view_%s_%s_face"), Prefix, View.Key, bOn ? TEXT("on") : TEXT("off")), TEXT("core"), bOn, -65.0f, View.Value, 1.0f, false, 0.0f);
			for (const auto& Intensity : { TPair<const TCHAR*, float>(TEXT("zero"), 0.0f), { TEXT("half"), 0.5f }, { TEXT("double"), 2.0f } })
				for (const bool bOn : { false, true }) Capture(*FString::Printf(TEXT("%s_main_%s_%s_face"), Prefix, Intensity.Key, bOn ? TEXT("on") : TEXT("off")), TEXT("core"), bOn, -65.0f, 0.0f, Intensity.Value, false, 0.0f);
			if (!bSkinRednessExperiment) Capture(TEXT("skinresponse_side_off_repeat_face"), TEXT("core"), false, -65.0f, 0.0f, 1.0f, false, 0.0f);
			bool bOriginalSoftnessMatches = true;
			const float OriginalSoftness = ProfileCopies[0].Metadata[8];
			for (const FToonProbeResponseProfile& Profile : ProfileCopies) bOriginalSoftnessMatches &= FMath::IsNearlyEqual(Profile.Metadata[8], OriginalSoftness);
			Check(TEXT("shared_original_angular_softness"), bOriginalSoftnessMatches);
			if (bOriginalSoftnessMatches && !bSkinRednessExperiment)
			{
				Capture(TEXT("skinresponse_softness_original_face"), TEXT("angular_softness_only"), true, -65.0f, 0.0f, 1.0f, true, OriginalSoftness);
				Capture(TEXT("skinresponse_softness_025_face"), TEXT("angular_softness_only"), true, -65.0f, 0.0f, 1.0f, true, 0.025f);
			}
			if (bSkinRednessExperiment)
			{
				for (const auto& Light : { TPair<const TCHAR*, float>(TEXT("side"), -65.0f), { TEXT("back"), 120.0f } })
					for (const bool bOn : { false, true }) Capture(*FString::Printf(TEXT("skinredness_%s_shadowoff_%s_face"), Light.Key, bOn ? TEXT("on") : TEXT("off")), TEXT("shadow_toggle"), bOn, Light.Value, 0.0f, 1.0f, false, 0.0f, false);
				for (const bool bShadow : { true, false })
					for (const bool bOn : { false, true }) Capture(*FString::Printf(TEXT("skinredness_deep_shadow%s_%s_face"), bShadow ? TEXT("on") : TEXT("off"), bOn ? TEXT("on") : TEXT("off")), TEXT("deep_occlusion"), bOn, -65.0f, 0.0f, 1.0f, false, 0.0f, bShadow, true);
			}
			Response->SetArrayField(TEXT("captures"), Captures); Response->SetNumberField(TEXT("captured_images"), Log.Images.Num() - FirstImage);
			Check(TEXT("all_capture_cpu_controls_verified"), bAllControls);
			Check(bSkinRednessExperiment ? TEXT("all_twenty_six_images_captured") : TEXT("all_eighteen_images_captured"), Log.Images.Num() - FirstImage == (bSkinRednessExperiment ? 26 : 18));
			Restore();
			bool bRestoredValues = true, bOriginalProfilesUnchanged = true, bLightsRestored = true;
			for (const FToonProbeResponseSlot& Saved : Slots)
			{
				UMaterialInstanceDynamic* MID = Saved.Material;
				bRestoredValues &= MID->Parent == Saved.Parent && MID->ToonProfileOverride == Saved.Profile && MID->bOverrideToonProfile == Saved.bOverride
					&& MID->ScalarParameterValues.Num() == Saved.Scalars.Num() && MID->VectorParameterValues.Num() == Saved.Vectors.Num()
					&& MID->TextureParameterValues.Num() == Saved.Textures.Num() && UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Saved.Slot) == Saved.Role;
				for (const FScalarParameterValue& Value : Saved.Scalars) { float Actual = 0.0f; bRestoredValues &= MID->GetScalarParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && FMath::IsNearlyEqual(Actual, Value.ParameterValue); }
				for (const FVectorParameterValue& Value : Saved.Vectors) { FLinearColor Actual; bRestoredValues &= MID->GetVectorParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && Actual.Equals(Value.ParameterValue); }
				for (const FTextureParameterValue& Value : Saved.Textures) { UTexture* Actual = nullptr; bRestoredValues &= MID->GetTextureParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && Actual == Value.ParameterValue; }
			}
			for (FToonProbeResponseProfile& Profile : ProfileCopies) { TArray<uint8> Bytes; FObjectWriter Writer(Profile.Original, Bytes); bOriginalProfilesUnchanged &= Bytes == Profile.OriginalBytes; }
			for (const auto& Light : SceneLights) bLightsRestored &= FMath::IsNearlyEqual(Light.Light->Intensity, Light.Intensity);
			Check(TEXT("all_material_parameters_profiles_and_roles_restored"), bRestoredValues);
			Check(TEXT("original_profile_bytes_unchanged"), bOriginalProfilesUnchanged);
			Check(TEXT("all_scene_light_intensities_restored"), bLightsRestored);
			bool bOccluderRestored = Rig.Occluder->GetComponentTransform().Equals(OccluderTransform) && Rig.Occluder->bRenderInDepthPass == bOccluderDepth
				&& Rig.Occluder->bRenderInMainPass == bOccluderMain && Rig.Occluder->IsVisible() == bOccluderVisible && Rig.Occluder->CastShadow == bOccluderShadow
				&& Rig.Occluder->GetNumMaterials() == OccluderMaterials.Num();
			for (int32 Slot = 0; Slot < OccluderMaterials.Num(); ++Slot) bOccluderRestored &= Rig.Occluder->GetMaterial(Slot) == OccluderMaterials[Slot];
			Check(TEXT("occluder_transform_material_visibility_shadow_restored"), bOccluderRestored);
			Response->SetObjectField(TEXT("native_key_shadow_state_after"), RecordKeyShadowState());
			Check(TEXT("native_key_shadow_flags_restored"), Key->CastShadows == bKeyShadow && Key->CastDynamicShadows == bKeyDynamicShadow
				&& Key->CastRaytracedShadow == RayShadow && Key->bUseRayTracedDistanceFieldShadows == bDistanceFieldShadow && Key->GetComponentRotation().Equals(KeyRotation, 0.001f));
			const TArray<FEngineShowFlagsSetting> RestoredFlags = Rig.Capture->GetShowFlagSettings();
			bool bFlagsRestored = RestoredFlags.Num() == EntryFlags.Num();
			for (int32 Index = 0; Index < FMath::Min(RestoredFlags.Num(), EntryFlags.Num()); ++Index)
				bFlagsRestored &= RestoredFlags[Index].ShowFlagName == EntryFlags[Index].ShowFlagName && RestoredFlags[Index].Enabled == EntryFlags[Index].Enabled;
			Check(TEXT("showflag_settings_restored"), bFlagsRestored);
			Check(TEXT("postprocess_settings_restored"), FPostProcessSettings::StaticStruct()->CompareScriptStruct(&Rig.PostProcess, &EntryPostProcess, 0)
				&& FPostProcessSettings::StaticStruct()->CompareScriptStruct(&Rig.Capture->PostProcessSettings, &EntryCapturePostProcess, 0));
			Check(TEXT("camera_and_contact_restored"), Rig.Capture->GetComponentTransform().Equals(EntryCamera) && FMath::IsNearlyEqual(Rig.Capture->FOVAngle, EntryFOV) && Rig.Character->bCastContactShadow == bContact);
			ToonProbeValidateSlots(Rig, Log, bSkinRednessExperiment ? TEXT("skinredness_restored") : TEXT("skinresponse_restored"));
		}
	}
	if (ToonProbeHasExp(Exps, TEXT("skin")) && NativePreset.IsValid())
	{
		const FTransform SkinEntryCamera = Rig.Capture->GetComponentTransform();
		const float SkinEntryFOV = Rig.Capture->FOVAngle;
		const FPostProcessSettings SkinEntryPostProcess = Rig.PostProcess;
		const FPostProcessSettings SkinEntryCapturePostProcess = Rig.Capture->PostProcessSettings;
		UseToonBaseline();
		ToonProbePose(Rig, Sample.FaceTimes.Num() > 0 ? Sample.FaceTimes[0] : Sample.BeautyTime);
		ToonProbeSetShot(Rig, EToonProbeShot::Face);
		UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
		TSharedRef<FJsonObject> Skin = ToonProbeJsonObject();
		Report->SetObjectField(TEXT("skin"), Skin);
		auto SkinCheck = [&](const TCHAR* Name, bool bPassed)
		{
			Skin->SetBoolField(Name, bPassed);
			if (!bPassed) ++Log.Failures;
			return bPassed;
		};
		Skin->SetStringField(TEXT("scope"), TEXT("Same-run linear skin-surface preprocessing candidates on the same Face parents; source alpha, native BSDF/Profile protocol, global exposure and non-Skin roles are preserved. PNG response and aesthetic acceptance require external comparison."));
		Skin->SetBoolField(TEXT("body_skin_tested"), false);
		Skin->SetStringField(TEXT("body_skin_limit"), TEXT("No role overrides are applied. The YYB body01 source slot is Generic; these face captures do not establish Body Skin quality."));
		Skin->SetBoolField(TEXT("pixel_difference_verified_by_this_report"), false);
		Skin->SetBoolField(TEXT("all_gpu_verified"), false);
		Skin->SetStringField(TEXT("before_reference"), TEXT("skin_surface_off_face within this run; historical final11 is a separately dated reference, not an assumed pixel-equivalent baseline"));
		const auto& Backend = MMDEndfieldNativeBackend::GetStatus();
		bool bSkinReady = SkinCheck(TEXT("native_mode2_ready"), bCanToon && Backend.bProfileMarkerAllowed && Backend.Mode == 2
			&& ToonProbeCVar(TEXT("r.Substrate.ProjectGBufferFormat")) == TEXT("1"));
		bSkinReady &= SkinCheck(TEXT("explicit_reference_pose_sample"), bExplicitSample && Sample.bReferencePose);
		bSkinReady &= SkinCheck(TEXT("key_light_present"), Rig.Stage->KeyLight != nullptr);
		bool bSevenParentsReady = true;
		for (const auto& Pair : { TPair<FString, FString>(TEXT("ToonProbe_Opaque"), ToonProbeNativeSurfaceParentPath(BLEND_Opaque, false)),
			TPair<FString, FString>(TEXT("ToonProbe_Masked"), ToonProbeNativeSurfaceParentPath(BLEND_Masked, false)),
			TPair<FString, FString>(TEXT("ToonProbe_TranslucentGrey"), ToonProbeNativeSurfaceParentPath(BLEND_Translucent, false)),
			TPair<FString, FString>(TEXT("ToonProbe_Face_Opaque"), ToonProbeNativeSurfaceParentPath(BLEND_Opaque, true)),
			TPair<FString, FString>(TEXT("ToonProbe_Face_Masked"), ToonProbeNativeSurfaceParentPath(BLEND_Masked, true)),
			TPair<FString, FString>(TEXT("ToonProbe_Face_Translucent"), ToonProbeNativeSurfaceParentPath(BLEND_Translucent, true)),
			TPair<FString, FString>(TEXT("ToonProbe_Outline"), TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Outline.M_MMD_EndfieldV2_Outline")) })
		{
			UMaterial* Material = ReadyMaterial(*Pair.Key);
			bSevenParentsReady &= Material && Material->GetPathName() == Pair.Value;
		}
		bSkinReady &= SkinCheck(TEXT("seven_published_parent_shaders_ready"), bSevenParentsReady);
		const UMMDEndfieldFaceData* Data = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		bSkinReady &= SkinCheck(TEXT("current_face_frame_ready"), Data && Data->Version == UMMDEndfieldFaceData::CurrentVersion
			&& Data->FrameVersion == MMDEndfieldHeadFrame::CurrentFrameVersion);
		struct FToonProbeSkinSnapshot
		{
			int32 Slot = INDEX_NONE;
			UMaterialInstanceDynamic* Material = nullptr;
			UMaterialInterface* Parent = nullptr;
			UToonProfile* Profile = nullptr;
			bool bProfileOverride = false;
			TArray<FScalarParameterValue> Scalars;
			TArray<FVectorParameterValue> Vectors;
			TArray<FTextureParameterValue> Textures;
			EMMDMaterialRole Role = EMMDMaterialRole::Generic;
			bool bFace = false;
			bool bHasOpacity = false;
			float Opacity = 0.0f;
			bool bHasBase = false;
			FLinearColor Base = FLinearColor::Transparent;
			float DirectionEnabled = 0.0f;
			float DirectionVersion = 0.0f;
			UTexture* DirectionTexture = nullptr;
		};
		TArray<FToonProbeSkinSnapshot> Snapshots;
		int32 FaceCount = 0, InlineCount = 0;
		bool bSourceAlphaPreserved = true, bFaceParentsMatch = true, bNonSkinSurfacesDisabled = true;
		TArray<TSharedPtr<FJsonValue>> SourceSlots;
		const UMMDStageMaterialState* State = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
			if (!MID) { bSkinReady = false; ++Log.Failures; continue; }
			FToonProbeSkinSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
			Snapshot.Slot = Slot;
			Snapshot.Material = MID;
			Snapshot.Parent = MID->Parent.Get();
			Snapshot.Profile = MID->ToonProfileOverride;
			Snapshot.bProfileOverride = MID->bOverrideToonProfile;
			Snapshot.Scalars = MID->ScalarParameterValues;
			Snapshot.Vectors = MID->VectorParameterValues;
			Snapshot.Textures = MID->TextureParameterValues;
			Snapshot.Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot);
			const FName SlotName = Sample.Mesh->GetMaterials()[Slot].MaterialSlotName;
			const FMMDGeneratedFaceSdfSlot* Binding = Data ? Data->FindDirectionSlot(Slot, SlotName) : nullptr;
			Snapshot.bFace = Binding && Snapshot.Role == EMMDMaterialRole::Skin;
			Snapshot.bHasOpacity = MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Snapshot.Opacity);
			Snapshot.bHasBase = MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Snapshot.Base);
			MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Snapshot.DirectionEnabled);
			MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Snapshot.DirectionVersion);
			MID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Snapshot.DirectionTexture);
			KeepAlive.Emplace(MID);
			if (Snapshot.bFace)
			{
				++FaceCount;
				if (Binding->bForwardOverlay) ++InlineCount;
				bFaceParentsMatch &= Snapshot.Parent && Snapshot.Parent->GetPathName() == ToonProbeNativeSurfaceParentPath(MID->GetBlendMode(), true);
				bFaceParentsMatch &= Snapshot.DirectionEnabled > 0.0f && Snapshot.DirectionVersion == UMMDEndfieldFaceData::CurrentVersion
					&& Snapshot.DirectionTexture == Binding->DirectionTexture;
			}
			float BodyEnabled = 0.0f, FaceEnabled = 0.0f;
			MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonSkinEnabled")), BodyEnabled);
			MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), FaceEnabled);
			if (Snapshot.Role != EMMDMaterialRole::Skin) bNonSkinSurfacesDisabled &= FMath::IsNearlyZero(BodyEnabled) && FMath::IsNearlyZero(FaceEnabled);
			UMaterialInterface* Source = State && State->SourceMaterials.IsValidIndex(Slot) ? State->SourceMaterials[Slot].Get()
				: Sample.Mesh->GetMaterials()[Slot].MaterialInterface.Get();
			float SourceOpacity = 0.0f;
			const bool bSourceOpacity = Source && Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), SourceOpacity);
			if (bSourceOpacity) bSourceAlphaPreserved &= Snapshot.bHasOpacity && FMath::IsNearlyEqual(Snapshot.Opacity, SourceOpacity);
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Slot);
			Item->SetStringField(TEXT("slot_name"), SlotName.ToString());
			Item->SetStringField(TEXT("role"), UEnum::GetValueAsString(Snapshot.Role));
			Item->SetStringField(TEXT("role_reason"), State && State->RoleReasons.IsValidIndex(Slot) ? State->RoleReasons[Slot] : TEXT("unavailable"));
			Item->SetStringField(TEXT("parent"), Snapshot.Parent ? Snapshot.Parent->GetPathName() : TEXT("none"));
			Item->SetStringField(TEXT("source_material"), Source ? Source->GetPathName() : TEXT("none"));
			Item->SetStringField(TEXT("actual_blend_mode"), UEnum::GetValueAsString(MID->GetBlendMode()));
			Item->SetBoolField(TEXT("face_surface_target"), Snapshot.bFace);
			Item->SetBoolField(TEXT("source_opacity_parameter_found"), bSourceOpacity);
			if (bSourceOpacity) Item->SetNumberField(TEXT("source_opacity"), SourceOpacity);
			if (Snapshot.bHasOpacity) Item->SetNumberField(TEXT("actual_opacity"), Snapshot.Opacity);
			if (Snapshot.bHasBase) Item->SetStringField(TEXT("actual_base_color_rgba"), Snapshot.Base.ToString());
			Item->SetNumberField(TEXT("initial_body_surface_enabled"), BodyEnabled);
			Item->SetNumberField(TEXT("initial_face_surface_enabled"), FaceEnabled);
			ToonProbeWriteFaceBindingFields(Item, Binding);
			SourceSlots.Add(MakeShared<FJsonValueObject>(Item));
		}
		Skin->SetArrayField(TEXT("source_slots"), SourceSlots);
		Skin->SetNumberField(TEXT("face_target_count"), FaceCount);
		Skin->SetNumberField(TEXT("forward_inline_target_count"), InlineCount);
		Skin->SetNumberField(TEXT("opaque_masked_target_count"), FaceCount - InlineCount);
		bSkinReady &= SkinCheck(TEXT("face_parent_and_direction_binding_match"), FaceCount > 0 && bFaceParentsMatch);
		if (Sample.MeshPath.Contains(TEXT("YYB16th")))
			bSkinReady &= SkinCheck(TEXT("yyb_three_packed_two_inline_targets"), FaceCount == 5 && InlineCount == 2);
		bSkinReady &= SkinCheck(TEXT("source_opacity_preserved"), bSourceAlphaPreserved);
		bSkinReady &= SkinCheck(TEXT("non_skin_source_surface_disabled"), bNonSkinSurfacesDisabled);
		ToonProbeValidateSlots(Rig, Log, TEXT("skin_initial_route"));
		if (bSkinReady)
		{
			const TArray<FEngineShowFlagsSetting> OriginalFlags = Rig.Capture->GetShowFlagSettings();
			const float OriginalIntensity = Rig.Stage->KeyLight->Intensity;
			const bool bOriginalContact = Rig.Character->bCastContactShadow;
			bool bRestored = false;
			auto Restore = [&]()
			{
				if (bRestored) return;
				bRestored = true;
				for (const FToonProbeSkinSnapshot& Snapshot : Snapshots)
				{
					UMaterialInstanceDynamic* MID = Snapshot.Material;
					MID->ToonProfileOverride = Snapshot.Profile;
					MID->bOverrideToonProfile = Snapshot.bProfileOverride;
					MID->ClearParameterValues();
					for (const FScalarParameterValue& Value : Snapshot.Scalars) MID->SetScalarParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
					for (const FVectorParameterValue& Value : Snapshot.Vectors) MID->SetVectorParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
					for (const FTextureParameterValue& Value : Snapshot.Textures) MID->SetTextureParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				}
				Rig.Stage->KeyLight->SetIntensity(OriginalIntensity);
				Rig.Character->SetCastContactShadow(bOriginalContact);
				Rig.Capture->SetShowFlagSettings(OriginalFlags);
				Rig.PostProcess = SkinEntryPostProcess;
				Rig.Capture->PostProcessSettings = SkinEntryCapturePostProcess;
				Rig.Capture->FOVAngle = SkinEntryFOV;
				Rig.Capture->SetWorldTransform(SkinEntryCamera);
				Rig.World->SendAllEndOfFrameUpdates();
				FlushRenderingCommands();
			};
			ON_SCOPE_EXIT { Restore(); };
			struct FToonProbeSkinCandidate { const TCHAR* Name; float Scale; FLinearColor Tint; float Strength; float Saturation; };
			const FToonProbeSkinCandidate Gentle = { TEXT("gentle"), 0.90f, FLinearColor(1.0f, 0.92f, 0.88f), 0.5f, 1.05f };
			const FToonProbeSkinCandidate Medium = { TEXT("medium"), 0.85f, FLinearColor(1.0f, 0.84f, 0.76f), 0.7f, 1.10f };
			const FToonProbeSkinCandidate Warm = { TEXT("warm"), 0.82f, FLinearColor(1.0f, 0.78f, 0.68f), 0.8f, 1.12f };
			TArray<TSharedPtr<FJsonValue>> Captures;
			const int32 FirstSkinImage = Log.Images.Num();
			bool bAllCaptureControls = true, bSurfaceOffSdfPreserved = true;
			auto CaptureSkin = [&](const FString& Name, const FToonProbeSkinCandidate& Candidate, bool bEnabled,
				bool bGI, bool bSpecular, float MainMultiplier, bool bDirection)
			{
				TArray<FString> Disabled = { TEXT("ContactShadows") };
				if (!bGI) Disabled.Append({ TEXT("GlobalIllumination"), TEXT("SkyLighting"), TEXT("ReflectionEnvironment"), TEXT("LumenReflections"), TEXT("ScreenSpaceReflections") });
				if (!bSpecular) Disabled.Add(TEXT("Specular"));
				ToonProbeSetShowFlags(Rig, Disabled);
				Rig.Character->SetCastContactShadow(false);
				Rig.Stage->KeyLight->SetIntensity(OriginalIntensity * MainMultiplier);
				bool bAlphaUnchanged = true, bControlsMatch = true, bSdfMatches = true;
				bControlsMatch &= FMath::IsNearlyEqual(Rig.Stage->KeyLight->Intensity, OriginalIntensity * MainMultiplier);
				bControlsMatch &= !Rig.Capture->ShowFlags.ContactShadows && Rig.Capture->ShowFlags.Specular == bSpecular;
				bControlsMatch &= Rig.Capture->ShowFlags.GlobalIllumination == bGI && Rig.Capture->ShowFlags.SkyLighting == bGI
					&& Rig.Capture->ShowFlags.ReflectionEnvironment == bGI && Rig.Capture->ShowFlags.LumenReflections == bGI
					&& Rig.Capture->ShowFlags.ScreenSpaceReflections == bGI;
				TArray<TSharedPtr<FJsonValue>> Parameters;
				for (const FToonProbeSkinSnapshot& Snapshot : Snapshots)
				{
					UMaterialInstanceDynamic* MID = Snapshot.Material;
					bControlsMatch &= MID->Parent == Snapshot.Parent && MID->ToonProfileOverride == Snapshot.Profile
						&& UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Snapshot.Slot) == Snapshot.Role;
					MID->SetScalarParameterValue(TEXT("ToonSkinEnabled"), 0.0f);
					MID->SetScalarParameterValue(TEXT("ToonFaceSkinEnabled"), Snapshot.bFace && bEnabled ? 1.0f : 0.0f);
					if (Snapshot.bFace)
					{
						MID->SetVectorParameterValue(TEXT("ToonFaceSkinTint"), Candidate.Tint);
						MID->SetScalarParameterValue(TEXT("ToonFaceSkinTintStrength"), Candidate.Strength);
						MID->SetScalarParameterValue(TEXT("ToonFaceSkinAlbedoScale"), Candidate.Scale);
						MID->SetScalarParameterValue(TEXT("ToonFaceSkinSaturation"), Candidate.Saturation);
						MID->SetScalarParameterValue(TEXT("ToonFaceSkinRoughness"), NativePreset->CharacterFaceSkinSurface.Roughness);
						MID->SetScalarParameterValue(TEXT("ToonFaceSkinSpecular"), NativePreset->CharacterFaceSkinSurface.Specular);
						MID->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, bDirection ? Snapshot.DirectionEnabled : 0.0f);
					}
					float Opacity = 0.0f, BodyEnabled = 0.0f, FaceEnabled = 0.0f;
					FLinearColor Base;
					bAlphaUnchanged &= !Snapshot.bHasOpacity || (MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity) && FMath::IsNearlyEqual(Opacity, Snapshot.Opacity));
					bAlphaUnchanged &= !Snapshot.bHasBase || (MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Base) && Base.Equals(Snapshot.Base));
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonSkinEnabled")), BodyEnabled);
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), FaceEnabled);
					bControlsMatch &= FMath::IsNearlyZero(BodyEnabled) && FMath::IsNearlyEqual(FaceEnabled, Snapshot.bFace && bEnabled ? 1.0f : 0.0f);
					if (!Snapshot.bFace) continue;
					float DirectionEnabled = 0.0f, Version = 0.0f, Payload = 0.0f;
					UTexture* Texture = nullptr;
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), DirectionEnabled);
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version);
					MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Payload);
					MID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture);
					bSdfMatches &= FMath::IsNearlyEqual(DirectionEnabled, bDirection ? Snapshot.DirectionEnabled : 0.0f)
						&& Version == UMMDEndfieldFaceData::CurrentVersion && Payload > 0.0f && Texture == Snapshot.DirectionTexture;
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetNumberField(TEXT("slot"), Snapshot.Slot);
					Item->SetNumberField(TEXT("direction_enabled"), DirectionEnabled);
					Item->SetNumberField(TEXT("direction_version"), Version);
					Item->SetNumberField(TEXT("native_payload_enabled"), Payload);
					Item->SetNumberField(TEXT("body_surface_enabled"), BodyEnabled);
					Item->SetNumberField(TEXT("face_surface_enabled"), FaceEnabled);
					for (const TCHAR* Suffix : { TEXT("TintStrength"), TEXT("AlbedoScale"), TEXT("Saturation"), TEXT("Roughness"), TEXT("Specular") })
					{
						float Value = 0.0f;
						const FString Parameter = FString(TEXT("ToonFaceSkin")) + Suffix;
						const bool bFound = MID->GetScalarParameterValue(FHashedMaterialParameterInfo(FName(*Parameter)), Value);
						const FString SuffixName(Suffix);
						const float Expected = SuffixName == TEXT("TintStrength") ? Candidate.Strength : SuffixName == TEXT("AlbedoScale") ? Candidate.Scale
							: SuffixName == TEXT("Saturation") ? Candidate.Saturation : SuffixName == TEXT("Roughness")
							? NativePreset->CharacterFaceSkinSurface.Roughness : NativePreset->CharacterFaceSkinSurface.Specular;
						bControlsMatch &= bFound && FMath::IsNearlyEqual(Value, Expected);
						Item->SetNumberField(Suffix, Value);
					}
					FLinearColor Tint;
					bControlsMatch &= MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinTint")), Tint) && Tint.Equals(Candidate.Tint);
					Item->SetStringField(TEXT("Tint"), Tint.ToString());
					Parameters.Add(MakeShared<FJsonValueObject>(Item));
				}
				TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
				Item->SetStringField(TEXT("image"), Name);
				Item->SetStringField(TEXT("candidate"), Candidate.Name);
				Item->SetBoolField(TEXT("surface_enabled"), bEnabled);
				Item->SetBoolField(TEXT("direction_enabled"), bDirection);
				Item->SetBoolField(TEXT("native_environment_enabled"), bGI);
				Item->SetBoolField(TEXT("specular_enabled"), bSpecular);
				Item->SetNumberField(TEXT("main_light_multiplier"), MainMultiplier);
				Item->SetNumberField(TEXT("key_intensity"), Rig.Stage->KeyLight->Intensity);
				Item->SetStringField(TEXT("key_color"), Rig.Stage->KeyLight->GetLightColor().ToString());
				Item->SetStringField(TEXT("key_rotation"), Rig.Stage->KeyLight->GetComponentRotation().ToString());
				Item->SetNumberField(TEXT("fill_intensity"), Rig.Stage->FillLight ? Rig.Stage->FillLight->Intensity : 0.0f);
				Item->SetNumberField(TEXT("rim_intensity"), Rig.Stage->RimLight ? Rig.Stage->RimLight->Intensity : 0.0f);
				Item->SetBoolField(TEXT("cpu_source_alpha_unchanged"), bAlphaUnchanged);
				Item->SetBoolField(TEXT("cpu_surface_controls_match"), bControlsMatch);
				Item->SetBoolField(TEXT("cpu_sdf_binding_preserved"), bSdfMatches);
				Item->SetBoolField(TEXT("contact_shadows_enabled"), false);
				Item->SetBoolField(TEXT("showflag_global_illumination"), Rig.Capture->ShowFlags.GlobalIllumination);
				Item->SetBoolField(TEXT("showflag_skylighting"), Rig.Capture->ShowFlags.SkyLighting);
				Item->SetBoolField(TEXT("showflag_reflection_environment"), Rig.Capture->ShowFlags.ReflectionEnvironment);
				Item->SetBoolField(TEXT("showflag_lumen_reflections"), Rig.Capture->ShowFlags.LumenReflections);
				Item->SetBoolField(TEXT("showflag_screen_space_reflections"), Rig.Capture->ShowFlags.ScreenSpaceReflections);
				Item->SetBoolField(TEXT("showflag_specular"), Rig.Capture->ShowFlags.Specular);
				Item->SetArrayField(TEXT("target_parameters"), Parameters);
				const FPostProcessSettings& PP = Rig.Capture->PostProcessSettings;
				TSharedRef<FJsonObject> Exposure = ToonProbeJsonObject();
				Exposure->SetNumberField(TEXT("auto_exposure_method"), int32(PP.AutoExposureMethod));
				Exposure->SetNumberField(TEXT("exposure_bias"), PP.AutoExposureBias);
				Exposure->SetBoolField(TEXT("physical_camera_exposure"), PP.AutoExposureApplyPhysicalCameraExposure);
				Exposure->SetNumberField(TEXT("white_temperature"), PP.WhiteTemp);
				Exposure->SetNumberField(TEXT("film_toe"), PP.FilmToe);
				Exposure->SetNumberField(TEXT("film_slope"), PP.FilmSlope);
				Exposure->SetStringField(TEXT("gain"), PP.ColorGain.ToString());
				Exposure->SetStringField(TEXT("gamma"), PP.ColorGamma.ToString());
				Exposure->SetStringField(TEXT("contrast"), PP.ColorContrast.ToString());
				Exposure->SetStringField(TEXT("saturation"), PP.ColorSaturation.ToString());
				Item->SetObjectField(TEXT("post_process_cpu_settings"), Exposure);
				bAllCaptureControls &= bAlphaUnchanged && bControlsMatch && bSdfMatches;
				if (!bEnabled && bDirection) bSurfaceOffSdfPreserved &= bSdfMatches;
				if (!bAlphaUnchanged || !bControlsMatch || !bSdfMatches) ++Log.Failures;
				if (bAlphaUnchanged && bControlsMatch && bSdfMatches)
				{
					// 与上一张使用同一相机和 parent，切换不重建场景；输出尺寸由实际 PNG 统计给出。
					Rig.Capture->bCameraCutThisFrame = true;
					ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 24));
				}
				else Item->SetBoolField(TEXT("capture_skipped_due_to_cpu_control_mismatch"), true);
				Captures.Add(MakeShared<FJsonValueObject>(Item));
			};
			CaptureSkin(TEXT("skin_surface_off_face"), Medium, false, true, true, 1.0f, true);
			CaptureSkin(TEXT("skin_gentle_face"), Gentle, true, true, true, 1.0f, true);
			CaptureSkin(TEXT("skin_medium_face"), Medium, true, true, true, 1.0f, true);
			CaptureSkin(TEXT("skin_warm_face"), Warm, true, true, true, 1.0f, true);
			for (const auto& Variant : { TPair<const TCHAR*, int32>(TEXT("gi_off"), 0), TPair<const TCHAR*, int32>(TEXT("spec_off"), 1),
				TPair<const TCHAR*, int32>(TEXT("main_half"), 2), TPair<const TCHAR*, int32>(TEXT("main_double"), 3), TPair<const TCHAR*, int32>(TEXT("direction_off"), 4) })
			{
				for (const bool bEnabled : { false, true }) CaptureSkin(FString::Printf(TEXT("skin_%s_%s_face"), Variant.Key, bEnabled ? TEXT("medium") : TEXT("surface_off")),
					Medium, bEnabled, Variant.Value != 0, Variant.Value != 1, Variant.Value == 2 ? 0.5f : Variant.Value == 3 ? 2.0f : 1.0f, Variant.Value != 4);
			}
			CaptureSkin(TEXT("skin_surface_off_repeat_face"), Medium, false, true, true, 1.0f, true);
			Skin->SetArrayField(TEXT("captures"), Captures);
			Skin->SetNumberField(TEXT("planned_images"), 15);
			Skin->SetNumberField(TEXT("captured_images"), Log.Images.Num() - FirstSkinImage);
			Skin->SetNumberField(TEXT("frames_per_shot"), FMath::Max(Frames, 24));
			SkinCheck(TEXT("surface_off_preserves_current_sdf_controls"), bSurfaceOffSdfPreserved);
			SkinCheck(TEXT("all_capture_cpu_controls_verified"), bAllCaptureControls);
			SkinCheck(TEXT("all_planned_images_captured"), Log.Images.Num() - FirstSkinImage == 15);
			Restore();
			bool bParametersRestored = true;
			for (const FToonProbeSkinSnapshot& Snapshot : Snapshots)
			{
				UMaterialInstanceDynamic* MID = Snapshot.Material;
				bParametersRestored &= MID->Parent == Snapshot.Parent && MID->ToonProfileOverride == Snapshot.Profile && MID->bOverrideToonProfile == Snapshot.bProfileOverride;
				bParametersRestored &= MID->ScalarParameterValues.Num() == Snapshot.Scalars.Num() && MID->VectorParameterValues.Num() == Snapshot.Vectors.Num()
					&& MID->TextureParameterValues.Num() == Snapshot.Textures.Num()
					&& UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Snapshot.Slot) == Snapshot.Role;
				for (const FScalarParameterValue& Value : Snapshot.Scalars)
				{
					float Actual = 0.0f;
					bParametersRestored &= MID->GetScalarParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && FMath::IsNearlyEqual(Actual, Value.ParameterValue);
				}
				for (const FVectorParameterValue& Value : Snapshot.Vectors)
				{
					FLinearColor Actual;
					bParametersRestored &= MID->GetVectorParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && Actual.Equals(Value.ParameterValue);
				}
				for (const FTextureParameterValue& Value : Snapshot.Textures)
				{
					UTexture* Actual = nullptr;
					bParametersRestored &= MID->GetTextureParameterValue(FHashedMaterialParameterInfo(Value.ParameterInfo.Name), Actual) && Actual == Value.ParameterValue;
				}
			}
			SkinCheck(TEXT("all_mid_parameters_profile_and_parent_restored"), bParametersRestored);
			SkinCheck(TEXT("key_intensity_and_contact_setting_restored"), FMath::IsNearlyEqual(Rig.Stage->KeyLight->Intensity, OriginalIntensity)
				&& Rig.Character->bCastContactShadow == bOriginalContact);
			ToonProbeValidateSlots(Rig, Log, TEXT("skin_restored"));
		}
	}
	// facefix：一次只改方向场或原生投影，避免把可见发片移除误当成头发投影隔离。
	if (ToonProbeHasExp(Exps, TEXT("facefix")) && NativePreset.IsValid())
	{
		UseToonBaseline();
		ToonProbeValidateSlots(Rig, Log, TEXT("facefix_native_baseline"));
		TSharedRef<FJsonObject> Facefix = ToonProbeJsonObject();
		Report->SetObjectField(TEXT("facefix"), Facefix);
		Facefix->SetStringField(TEXT("scope"), TEXT("YYB16th reference pose; fixed face camera/exposure; key direct light only; raster VSM/ShadowDepth; Hair material-slot shadow isolation, not a Face-only receiver mask."));
		Facefix->SetBoolField(TEXT("image_difference_verified"), false);
		Facefix->SetBoolField(TEXT("isolation_controls_applied"), false);
		Facefix->SetNumberField(TEXT("captured_images"), 0);
		Facefix->SetNumberField(TEXT("planned_images"), 21);
		Facefix->SetNumberField(TEXT("frames_per_shot"), FMath::Max(Frames, 8));
		auto FacefixCheck = [&](const TCHAR* Name, bool bOk)
		{
			Facefix->SetBoolField(Name, bOk);
			if (!bOk) { ++Log.Failures; Log.Note(Name, TEXT("failed")); }
			return bOk;
		};
		const MMDEndfieldNativeBackend::FStatus& Backend = MMDEndfieldNativeBackend::GetStatus();
		UMMDEndfieldFaceData* Data = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		FSkeletalMeshRenderData* MeshData = Sample.Mesh->GetResourceForRendering();
		bool bFacefixReady = FacefixCheck(TEXT("native_direction_path_ready"), bCanToon && Backend.Mode == 2
			&& Backend.bProfileMarkerAllowed && ToonProbeCVar(TEXT("r.Substrate.ProjectGBufferFormat")) == TEXT("1")
			&& FCString::Atoi(*ToonProbeCVar(TEXT("r.Substrate.ToonProfile.Resolution"))) >= 256);
		bFacefixReady &= FacefixCheck(TEXT("current_mesh_data_and_frame_present"), Data && Data->Version == UMMDEndfieldFaceData::CurrentVersion
			&& Data->FrameVersion == MMDEndfieldHeadFrame::CurrentFrameVersion && !Data->Slots.IsEmpty()
			&& Data->HeadRadius > 0.0f && !Data->HeadSkeletonSignature.IsEmpty());
		if (Data)
		{
			Facefix->SetNumberField(TEXT("data_version"), Data->Version);
			Facefix->SetNumberField(TEXT("frame_version"), Data->FrameVersion);
			Facefix->SetStringField(TEXT("head_bone"), Data->HeadBoneName.ToString());
			Facefix->SetStringField(TEXT("head_skeleton_signature"), Data->HeadSkeletonSignature);
		}
		bFacefixReady &= FacefixCheck(TEXT("raster_mesh_and_key_present"), MeshData && !MeshData->LODRenderData.IsEmpty() && Rig.Stage->KeyLight);

		struct FToonProbeFacefixDirection
		{
			UMaterialInstanceDynamic* Material = nullptr;
			float Enabled = 0.0f;
			int32 Slot = INDEX_NONE;
		};
		TArray<FToonProbeFacefixDirection> Directions;
		TArray<TSharedPtr<FJsonValue>> DirectionSlots;
		int32 ForwardDirectionSlots = 0, PackedDirectionSlots = 0;
		FVector HeadForward = FVector::ZeroVector, HeadRight = FVector::ZeroVector;
		if (Data)
		{
			for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex));
				float Enabled = 0.0f, Payload = 0.0f, Version = 0.0f, Suppress = 0.0f;
				UTexture* Texture = nullptr;
				if (!MID) continue;
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled);
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Payload);
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version);
				MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceShadowSuppress")), Suppress);
				MID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture);
				const bool bBound = Enabled > 0.0f && Payload > 0.0f && Version == UMMDEndfieldFaceData::CurrentVersion && Slot.UVChannel == -1
					&& Slot.DirectionVersion == UMMDEndfieldFaceData::CurrentVersion
					&& Texture && Texture == Data->FindDirectionTexture(Slot.MaterialIndex, Slot.MaterialSlotName);
				TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
				Item->SetNumberField(TEXT("slot"), Slot.MaterialIndex);
				ToonProbeWriteFaceBindingFields(Item, &Slot);
				Item->SetNumberField(TEXT("enabled"), Enabled);
				Item->SetNumberField(TEXT("payload_enabled"), Payload);
				Item->SetNumberField(TEXT("version"), Version);
				Item->SetNumberField(TEXT("uv_channel"), Slot.UVChannel);
				Item->SetNumberField(TEXT("face_shadow_suppress"), Suppress);
				Item->SetBoolField(TEXT("bound_and_toggled"), bBound);
				Item->SetStringField(TEXT("texture"), Texture ? Texture->GetPathName() : TEXT("none"));
				DirectionSlots.Add(MakeShared<FJsonValueObject>(Item));
				if (!bBound) continue;
				if (Slot.bForwardOverlay) ++ForwardDirectionSlots; else ++PackedDirectionSlots;
				Directions.Add({ MID, Enabled, Slot.MaterialIndex });
			}
		}
		Facefix->SetArrayField(TEXT("direction_slots"), DirectionSlots);
		Facefix->SetNumberField(TEXT("direction_binding_count"), Directions.Num());
		Facefix->SetNumberField(TEXT("forward_inline_direction_binding_count"), ForwardDirectionSlots);
		Facefix->SetNumberField(TEXT("opaque_masked_packed_direction_binding_count"), PackedDirectionSlots);
		Facefix->SetStringField(TEXT("payload_transport_scope"), TEXT("Opaque/masked adaptive deferred format1 stores packed V5; validated translucent attached overlays pass V5 inline through desktop per-pixel forward. Green/red diagnostics must be interpreted per route and alpha compositing; forward is not a GBuffer roundtrip."));
		ToonProbePose(Rig, Sample.BeautyTime);
		// 真实组件 API 的缓存回归；全部改动只在内存中发生，退出此作用域后恢复原状态。
		if (Data && !Directions.IsEmpty() && Data->Version == UMMDEndfieldFaceData::CurrentVersion
			&& Data->FrameVersion == MMDEndfieldHeadFrame::CurrentFrameVersion)
		{
			TStrongObjectPtr<UMMDEndfieldFaceData> KeepFaceData(Data);
			const int32 OriginalDataVersion = Data->Version;
			const bool bMeshWasDirty = Sample.Mesh->GetOutermost()->IsDirty();
			struct FToonProbeFacefixBindingSnapshot
			{
				UMaterialInstanceDynamic* Material = nullptr;
				float Enabled = 0.0f, Version = 0.0f;
				UTexture* Texture = nullptr;
			};
			TArray<FToonProbeFacefixBindingSnapshot> BindingSnapshots;
			for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
				if (!MID) continue;
				FToonProbeFacefixBindingSnapshot Snapshot;
				Snapshot.Material = MID;
				if (!MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Snapshot.Enabled)
					|| !MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Snapshot.Version)
					|| !MID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Snapshot.Texture)) continue;
				BindingSnapshots.Add(Snapshot);
				if (Snapshot.Texture) KeepAlive.Emplace(Snapshot.Texture);
			}
			ON_SCOPE_EXIT
			{
				Data->Version = OriginalDataVersion;
				if (Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>() != Data)
				{
					Sample.Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
					Sample.Mesh->AddAssetUserData(Data);
				}
				UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				for (const FToonProbeFacefixBindingSnapshot& Snapshot : BindingSnapshots)
				{
					Snapshot.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Snapshot.Enabled);
					Snapshot.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionVersionParameter, Snapshot.Version);
					Snapshot.Material->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, Snapshot.Texture);
				}
				Sample.Mesh->GetOutermost()->SetDirtyFlag(bMeshWasDirty);
			};
			auto BindingMatches = [&](bool bExpectedEnabled)
			{
				bool bMatches = true;
				for (const FToonProbeFacefixDirection& Direction : Directions)
				{
					float Enabled = -1.0f, Version = 0.0f;
					UTexture* Texture = nullptr;
					bMatches &= Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled)
						&& FMath::IsNearlyEqual(Enabled, bExpectedEnabled ? 1.0f : 0.0f);
					bMatches &= Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version)
						&& Version == (bExpectedEnabled ? float(UMMDEndfieldFaceData::CurrentVersion) : 0.0f);
					if (bExpectedEnabled)
					{
						const FName SlotName = Sample.Mesh->GetMaterials()[Direction.Slot].MaterialSlotName;
						UTexture2D* ExpectedTexture = Data->FindDirectionTexture(Direction.Slot, SlotName);
						bMatches &= Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version)
							&& Version == float(UMMDEndfieldFaceData::CurrentVersion)
							&& Direction.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture)
							&& ExpectedTexture && Texture == ExpectedTexture;
					}
				}
				return bMatches;
			};
			const bool bPersistentCache = Rig.World->GetSubsystem<UMMDHeadBoneSubsystem>() != nullptr;
			bFacefixReady &= FacefixCheck(TEXT("persistent_subsystem_present"), bPersistentCache);
			Sample.Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_remove_data_disables_direction"), BindingMatches(false));
			Sample.Mesh->AddAssetUserData(Data);
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_reattach_same_data_restores_binding"), BindingMatches(true));
			Data->Version = 4;
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_same_data_legacy_version_disables_direction"), BindingMatches(false));
			Data->Version = OriginalDataVersion;
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_restore_current_version_restores_binding"), BindingMatches(true));
			{
				const int32 FaceSlot = Directions[0].Slot;
				TStrongObjectPtr<UMaterialInstanceDynamic> OriginalMID(Directions[0].Material);
				TStrongObjectPtr<UMaterialInstanceDynamic> ReplacementMID(UMaterialInstanceDynamic::Create(OriginalMID->Parent.Get(), GetTransientPackage()));
				UTexture* Black = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
				const bool bCreated = ReplacementMID.IsValid() && Black;
				bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_new_mid_created"), bCreated);
				if (bCreated)
				{
					ON_SCOPE_EXIT
					{
						Rig.Character->SetMaterial(FaceSlot, OriginalMID.Get());
						UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					};
					ReplacementMID->ToonProfileOverride = OriginalMID->ToonProfileOverride;
					ReplacementMID->bOverrideToonProfile = OriginalMID->bOverrideToonProfile;
					ReplacementMID->CopyParameterOverrides(OriginalMID.Get());
					ReplacementMID->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), 1.0f);
					ReplacementMID->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, Black);
					ReplacementMID->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, 0.0f);
					ReplacementMID->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionVersionParameter, float(UMMDEndfieldFaceData::CurrentVersion));
					Rig.Character->SetMaterial(FaceSlot, ReplacementMID.Get());
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					float NewEnabled = -1.0f, NewVersion = 0.0f;
					UTexture* NewTexture = nullptr;
					UTexture2D* ExpectedTexture = Data->FindDirectionTexture(FaceSlot, Sample.Mesh->GetMaterials()[FaceSlot].MaterialSlotName);
					const bool bRebound = Rig.Character->GetMaterial(FaceSlot) == ReplacementMID.Get()
						&& ReplacementMID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), NewEnabled)
						&& NewEnabled == 1.0f
						&& ReplacementMID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), NewVersion)
						&& NewVersion == float(UMMDEndfieldFaceData::CurrentVersion)
						&& ReplacementMID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), NewTexture)
						&& ExpectedTexture && NewTexture == ExpectedTexture;
					bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_new_mid_same_frame_rebinds_direction"), bRebound);
				}
			}
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_original_mid_restored_binding"), Rig.Character->GetMaterial(Directions[0].Slot) == Directions[0].Material && BindingMatches(true));
			const int32 OverlayIndex = Data->Slots.IndexOfByPredicate([&](const FMMDGeneratedFaceSdfSlot& Slot)
			{
				return Slot.bForwardOverlay && Data->FindDirectionSlot(Slot.MaterialIndex, Slot.MaterialSlotName)
					&& Directions.ContainsByPredicate([&](const FToonProbeFacefixDirection& Direction) { return Direction.Slot == Slot.MaterialIndex; });
			});
			bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_binding_case_present"), OverlayIndex != INDEX_NONE);
			if (OverlayIndex != INDEX_NONE)
			{
				const FName OriginalHostName = Data->Slots[OverlayIndex].HostSlotName;
				TStrongObjectPtr<UTexture2D> OriginalTexture(Data->Slots[OverlayIndex].DirectionTexture.Get());
				const int32 OverlaySlot = Data->Slots[OverlayIndex].MaterialIndex, HostSlot = Data->Slots[OverlayIndex].HostMaterialIndex;
				const uint32 OriginalSignature = Data->ComputeBindingSignature();
				Facefix->SetNumberField(TEXT("cpu_overlay_regression_slot"), OverlaySlot);
				Facefix->SetNumberField(TEXT("cpu_overlay_regression_host_slot"), HostSlot);
				Facefix->SetNumberField(TEXT("cpu_overlay_binding_signature_original"), OriginalSignature);
				ON_SCOPE_EXIT
				{
					Data->Slots[OverlayIndex].HostSlotName = OriginalHostName;
					Data->Slots[OverlayIndex].DirectionTexture = OriginalTexture.Get();
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				};
				auto OverlayEnabledMatches = [&](bool bEnabled)
				{
					UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(OverlaySlot));
					float Enabled = -1.0f;
					return MID && MID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled)
						&& Enabled == (bEnabled ? 1.0f : 0.0f);
				};
				auto HostBindingStillValid = [&]()
				{
					UMaterialInstanceDynamic* HostMID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(HostSlot));
					float Enabled = -1.0f;
					UTexture* Texture = nullptr;
					return HostMID && Data->FindDirectionTexture(HostSlot, OriginalHostName) == OriginalTexture.Get()
						&& HostMID->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled) && Enabled == 1.0f
						&& HostMID->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture) && Texture == OriginalTexture.Get();
				};
				Data->Slots[OverlayIndex].HostSlotName = FName(*(OriginalHostName.ToString() + TEXT("_ToonProbeMismatch")));
				const uint32 WrongHostSignature = Data->ComputeBindingSignature();
				UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				Facefix->SetNumberField(TEXT("cpu_overlay_binding_signature_wrong_host"), WrongHostSignature);
				bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_inplace_host_name_mismatch_disables"), WrongHostSignature != OriginalSignature
					&& !Data->FindDirectionSlot(OverlaySlot, Sample.Mesh->GetMaterials()[OverlaySlot].MaterialSlotName)
					&& OverlayEnabledMatches(false) && HostBindingStillValid());
				Data->Slots[OverlayIndex].HostSlotName = OriginalHostName;
				UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_host_name_restore_rebinds"), Data->ComputeBindingSignature() == OriginalSignature && BindingMatches(true));
				UTexture2D* WrongTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
				const bool bWrongTextureValid = WrongTexture && WrongTexture != OriginalTexture.Get();
				bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_mismatch_texture_available"), bWrongTextureValid);
				if (bWrongTextureValid)
				{
					Data->Slots[OverlayIndex].DirectionTexture = WrongTexture;
					const uint32 WrongTextureSignature = Data->ComputeBindingSignature();
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					Facefix->SetNumberField(TEXT("cpu_overlay_binding_signature_wrong_texture"), WrongTextureSignature);
					bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_inplace_texture_mismatch_disables"), WrongTextureSignature != OriginalSignature
						&& !Data->FindDirectionSlot(OverlaySlot, Sample.Mesh->GetMaterials()[OverlaySlot].MaterialSlotName)
						&& OverlayEnabledMatches(false) && HostBindingStillValid());
					Data->Slots[OverlayIndex].DirectionTexture = OriginalTexture.Get();
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					bFacefixReady &= FacefixCheck(TEXT("cpu_overlay_texture_restore_rebinds"), Data->ComputeBindingSignature() == OriginalSignature && BindingMatches(true));
				}
			}
			Directions[0].Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, 0.0f);
			UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
			float ManualEnabled = -1.0f;
			bFacefixReady &= FacefixCheck(TEXT("cpu_lifecycle_unchanged_cache_preserves_manual_off"), bPersistentCache
				&& Directions[0].Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), ManualEnabled)
				&& ManualEnabled == 0.0f);
			Directions[0].Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Directions[0].Enabled);
			const int32 HeadBone = Rig.Character->GetBoneIndex(Data->HeadBoneName);
			FLinearColor PositionRadius = FLinearColor::Transparent;
			const FVector ExpectedCenter = HeadBone != INDEX_NONE
				? Rig.Character->GetBoneTransform(HeadBone).TransformPosition(Data->HeadCenterHeadLocal) : FVector::ZeroVector;
			const bool bCenterRead = Directions[0].Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadPositionRadius), PositionRadius);
			const FVector ActualCenter(PositionRadius.R, PositionRadius.G, PositionRadius.B);
			const double CenterError = FVector::Distance(ExpectedCenter, ActualCenter);
			Facefix->SetStringField(TEXT("cpu_head_center_expected_world"), ExpectedCenter.ToString());
			Facefix->SetStringField(TEXT("cpu_head_center_mid_world"), ActualCenter.ToString());
			Facefix->SetNumberField(TEXT("cpu_head_center_error_cm"), CenterError);
			bFacefixReady &= FacefixCheck(TEXT("cpu_head_center_matches_saved_local_transform"), HeadBone != INDEX_NONE && bCenterRead && CenterError <= 0.001);
			// 肤色独立于方向开关：payload 关闭时，真实失效仍须清 Version，手动 off 则保留有效 Version。
			{
				struct FToonProbeFaceOnlyControl
				{
					UMaterialInstanceDynamic* Material = nullptr;
					float Skin = 0.0f, Payload = 0.0f, Direction = 0.0f, Version = 0.0f;
					UTexture* Texture = nullptr;
				};
				TArray<FToonProbeFaceOnlyControl> Controls;
				for (const FToonProbeFacefixDirection& Direction : Directions)
				{
					FToonProbeFaceOnlyControl Control;
					Control.Material = Direction.Material;
					if (Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), Control.Skin)
						&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Control.Payload)
						&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Control.Direction)
						&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Control.Version)
						&& Control.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Control.Texture)) Controls.Add(Control);
				}
				const bool bFaceOnlyReady = Controls.Num() == Directions.Num() && OverlayIndex != INDEX_NONE;
				bFacefixReady &= FacefixCheck(TEXT("cpu_face_skin_only_fixture_ready"), bFaceOnlyReady);
				if (bFaceOnlyReady)
				{
					const FName HostName = Data->Slots[OverlayIndex].HostSlotName;
					TStrongObjectPtr<UTexture2D> HostTexture(Data->Slots[OverlayIndex].DirectionTexture.Get());
					bool bRestored = false;
					auto RestoreFaceOnly = [&]()
					{
						if (bRestored) return;
						bRestored = true;
						Data->Slots[OverlayIndex].HostSlotName = HostName;
						Data->Slots[OverlayIndex].DirectionTexture = HostTexture.Get();
						if (Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>() != Data)
						{
							Sample.Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
							Sample.Mesh->AddAssetUserData(Data);
						}
						UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
						for (const FToonProbeFaceOnlyControl& Control : Controls)
						{
							Control.Material->SetScalarParameterValue(TEXT("ToonFaceSkinEnabled"), Control.Skin);
							Control.Material->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), Control.Payload);
							Control.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Control.Direction);
							Control.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionVersionParameter, Control.Version);
							Control.Material->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, Control.Texture);
						}
					};
					ON_SCOPE_EXIT { RestoreFaceOnly(); };
					for (const FToonProbeFaceOnlyControl& Control : Controls)
					{
						Control.Material->SetScalarParameterValue(TEXT("ToonFaceSkinEnabled"), 1.0f);
						Control.Material->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), 0.0f);
						Control.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, 0.0f);
					}
					TArray<TSharedPtr<FJsonValue>> Observations;
					auto CheckFaceOnly = [&](const TCHAR* Name)
					{
						bool bMatches = true;
						const bool bAttached = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>() == Data;
						for (const FToonProbeFacefixDirection& Direction : Directions)
						{
							const FMMDGeneratedFaceSdfSlot* Binding = bAttached
								? Data->FindDirectionSlot(Direction.Slot, Sample.Mesh->GetMaterials()[Direction.Slot].MaterialSlotName) : nullptr;
							float Version = -1.0f, Enabled = -1.0f, Skin = -1.0f, Payload = -1.0f;
							UTexture* Texture = nullptr;
							const bool bRead = Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version)
								&& Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled)
								&& Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), Skin)
								&& Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Payload)
								&& Direction.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture);
							const float ExpectedVersion = Binding ? float(UMMDEndfieldFaceData::CurrentVersion) : 0.0f;
							const bool bSlotMatches = bRead && Version == ExpectedVersion && Enabled == 0.0f && Skin == 1.0f && Payload == 0.0f
								&& (!Binding || Texture == Binding->DirectionTexture);
							bMatches &= bSlotMatches;
							TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
							Item->SetStringField(TEXT("case"), Name);
							Item->SetNumberField(TEXT("slot"), Direction.Slot);
							Item->SetBoolField(TEXT("data_attached"), bAttached);
							Item->SetBoolField(TEXT("binding_valid"), Binding != nullptr);
							Item->SetNumberField(TEXT("expected_version"), ExpectedVersion);
							Item->SetNumberField(TEXT("actual_version"), Version);
							Item->SetNumberField(TEXT("direction_enabled"), Enabled);
							Item->SetNumberField(TEXT("face_skin_enabled"), Skin);
							Item->SetNumberField(TEXT("native_payload_enabled"), Payload);
							Item->SetBoolField(TEXT("matches"), bSlotMatches);
							Observations.Add(MakeShared<FJsonValueObject>(Item));
						}
						bFacefixReady &= FacefixCheck(Name, bMatches);
					};
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					CheckFaceOnly(TEXT("cpu_face_skin_only_manual_off_preserves_current_version"));
					Sample.Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					CheckFaceOnly(TEXT("cpu_face_skin_only_remove_data_clears_version"));
					Sample.Mesh->AddAssetUserData(Data);
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					CheckFaceOnly(TEXT("cpu_face_skin_only_reattach_restores_current_version"));
					Data->Slots[OverlayIndex].HostSlotName = FName(*(HostName.ToString() + TEXT("_FaceSkinOnlyMismatch")));
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					CheckFaceOnly(TEXT("cpu_face_skin_only_host_name_mismatch_clears_version"));
					Data->Slots[OverlayIndex].HostSlotName = HostName;
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					CheckFaceOnly(TEXT("cpu_face_skin_only_host_name_restore_restores_current_version"));
					UTexture2D* WrongTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
					const bool bWrongTexture = WrongTexture && WrongTexture != HostTexture.Get();
					bFacefixReady &= FacefixCheck(TEXT("cpu_face_skin_only_mismatch_texture_available"), bWrongTexture);
					if (bWrongTexture)
					{
						Data->Slots[OverlayIndex].DirectionTexture = WrongTexture;
						UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
						CheckFaceOnly(TEXT("cpu_face_skin_only_host_texture_mismatch_clears_version"));
						Data->Slots[OverlayIndex].DirectionTexture = HostTexture.Get();
						UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
						CheckFaceOnly(TEXT("cpu_face_skin_only_host_texture_restore_restores_current_version"));
					}
					Facefix->SetArrayField(TEXT("cpu_face_skin_only_binding_states"), Observations);
					RestoreFaceOnly();
					bool bControlsRestored = true;
					for (const FToonProbeFaceOnlyControl& Control : Controls)
					{
						float Skin = -1.0f, Payload = -1.0f, Direction = -1.0f, Version = -1.0f;
						UTexture* Texture = nullptr;
						bControlsRestored &= Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonFaceSkinEnabled")), Skin) && Skin == Control.Skin
							&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), Payload) && Payload == Control.Payload
							&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Direction) && Direction == Control.Direction
							&& Control.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), Version) && Version == Control.Version
							&& Control.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Texture) && Texture == Control.Texture;
					}
					bFacefixReady &= FacefixCheck(TEXT("cpu_face_skin_only_original_controls_restored"), bControlsRestored);
				}
			}
			Facefix->SetStringField(TEXT("cpu_lifecycle_scope"), TEXT("Current V5 bindings: remove/reattach/version, new MID, overlay host-name/texture mismatch, stable-cache manual off and head center. With FaceSkin1/Payload0/Direction0, valid bindings retain Version5; true invalidation clears Version0; controls restore before capture. These are CPU binding checks, not GPU coverage readback or arbitrary slot/deformation tests."));
		}
		if (!Directions.IsEmpty())
		{
			FLinearColor Forward = FLinearColor::Black, Right = FLinearColor::Black;
			Directions[0].Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), Forward);
			Directions[0].Material->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), Right);
			HeadForward = FVector(Forward.R, Forward.G, Forward.B).GetSafeNormal();
			HeadRight = FVector(Right.R, Right.G, Right.B).GetSafeNormal();
		}
		const FVector HeadUp = FVector::CrossProduct(HeadRight, HeadForward).GetSafeNormal();
		bFacefixReady &= FacefixCheck(TEXT("bound_direction_and_head_frame_present"), !Directions.IsEmpty()
			&& !HeadForward.IsNearlyZero() && !HeadRight.IsNearlyZero() && !HeadUp.IsNearlyZero()
			&& FMath::Abs(FVector::DotProduct(HeadForward, HeadRight)) < 0.05f);
		Facefix->SetStringField(TEXT("head_forward"), HeadForward.ToString());
		Facefix->SetStringField(TEXT("head_right"), HeadRight.ToString());
		Facefix->SetStringField(TEXT("head_up"), HeadUp.ToString());

		struct FToonProbeFacefixSection
		{
			int32 LOD = 0, Section = 0, Material = 0, MappedMaterial = 0;
			bool bShown = true, bHair = false;
		};
		TArray<FToonProbeFacefixSection> Sections;
		TArray<TSharedPtr<FJsonValue>> SectionReports, HairSlots, OverlaySlots;
		TSet<int32> HairMaterialIndices;
		const UMMDStageMaterialState* MaterialState = Cast<UMMDStageMaterialState>(Rig.Character->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			const bool bHair = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot) == EMMDMaterialRole::Hair;
			const FString Reason = MaterialState && MaterialState->RoleReasons.IsValidIndex(Slot) ? MaterialState->RoleReasons[Slot] : FString();
			if (!bHair && !Reason.Contains(TEXT("shadow_overlay"))) continue;
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Slot);
			Item->SetStringField(TEXT("name"), Sample.Mesh->GetMaterials()[Slot].MaterialSlotName.ToString());
			Item->SetStringField(TEXT("role_reason"), Reason);
			if (bHair) { HairMaterialIndices.Add(Slot); HairSlots.Add(MakeShared<FJsonValueObject>(Item)); }
			else OverlaySlots.Add(MakeShared<FJsonValueObject>(Item));
		}
		int32 HairCasterSectionsLOD0 = 0;
		bool bMaterialMappingValid = true;
		if (MeshData)
		{
			for (int32 LOD = 0; LOD < MeshData->LODRenderData.Num(); ++LOD)
			{
				const FSkeletalMeshLODInfo* LODInfo = Sample.Mesh->GetLODInfo(LOD);
				const FSkeletalMeshLODRenderData& RenderLOD = MeshData->LODRenderData[LOD];
				for (int32 SectionIndex = 0; SectionIndex < RenderLOD.RenderSections.Num(); ++SectionIndex)
				{
					const FSkelMeshRenderSection& Section = RenderLOD.RenderSections[SectionIndex];
					int32 Mapped = Section.MaterialIndex;
					if (LODInfo && LODInfo->LODMaterialMap.IsValidIndex(SectionIndex) && LODInfo->LODMaterialMap[SectionIndex] != INDEX_NONE)
						Mapped = LODInfo->LODMaterialMap[SectionIndex];
					const bool bValidSlot = Sample.Mesh->GetMaterials().IsValidIndex(Mapped);
					bMaterialMappingValid &= bValidSlot;
					const bool bHair = HairMaterialIndices.Contains(Mapped);
					const bool bShown = bValidSlot && Rig.Character->IsMaterialSectionShown(Mapped, LOD);
					Sections.Add({ LOD, SectionIndex, Section.MaterialIndex, Mapped, bShown, bHair });
					if (LOD == 0 && bHair && bShown && Section.bCastShadow && Section.IsValid()) ++HairCasterSectionsLOD0;
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetNumberField(TEXT("lod"), LOD);
					Item->SetNumberField(TEXT("section"), SectionIndex);
					Item->SetNumberField(TEXT("material"), Section.MaterialIndex);
					Item->SetNumberField(TEXT("mapped_material"), Mapped);
					Item->SetNumberField(TEXT("triangles"), Section.NumTriangles);
					Item->SetBoolField(TEXT("source_shown"), bShown);
					Item->SetBoolField(TEXT("source_cast_shadow"), Section.bCastShadow);
					Item->SetBoolField(TEXT("source_disabled"), Section.bDisabled);
					Item->SetBoolField(TEXT("hair"), bHair);
					SectionReports.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
		}
		Facefix->SetArrayField(TEXT("hair_slots"), HairSlots);
		Facefix->SetArrayField(TEXT("visible_shadow_overlay_slots_unchanged"), OverlaySlots);
		Facefix->SetArrayField(TEXT("proxy_sections"), SectionReports);
		Facefix->SetNumberField(TEXT("hair_casting_sections_lod0"), HairCasterSectionsLOD0);
		bFacefixReady &= FacefixCheck(TEXT("hair_slot_isolation_available"), bMaterialMappingValid && HairCasterSectionsLOD0 > 0);

		if (bFacefixReady)
		{
			const TArray<FEngineShowFlagsSetting> OriginalFlags = Rig.Capture->GetShowFlagSettings();
			const FRotator OriginalKeyRotation = Rig.Stage->KeyLight->GetComponentRotation();
			const bool bOriginalKeyShadow = Rig.Stage->KeyLight->CastShadows;
			const bool bOriginalKeyDynamicShadow = Rig.Stage->KeyLight->CastDynamicShadows;
			const ECastRayTracedShadow::Type OriginalRayShadow = Rig.Stage->KeyLight->CastRaytracedShadow;
			const bool bOriginalDistanceFieldShadow = Rig.Stage->KeyLight->bUseRayTracedDistanceFieldShadows;
			const bool bOriginalCastShadow = Rig.Character->CastShadow;
			const bool bOriginalContactShadow = Rig.Character->bCastContactShadow;
			const bool bOriginalCapsuleDirect = Rig.Character->bCastCapsuleDirectShadow;
			const bool bOriginalCapsuleIndirect = Rig.Character->bCastCapsuleIndirectShadow;
			const bool bOriginalDisableNanite = Rig.Character->IsForceDisableNanite();
			const int32 OriginalLOD = Rig.Character->GetForcedLOD();
			const FTransform OriginalOccluderTransform = Rig.Occluder->GetComponentTransform();
			ULightComponent* StageLights[] = { Rig.Stage->KeyLight.Get(), Rig.Stage->FillLight.Get(), Rig.Stage->RimLight.Get() };
			float OriginalLightIntensities[3] = {};
			for (int32 Index = 0; Index < 3; ++Index) OriginalLightIntensities[Index] = StageLights[Index] ? StageLights[Index]->Intensity : 0.0f;
			TInlineComponentArray<UPointLightComponent*> AccentLights;
			Rig.Stage->GetComponents(AccentLights);
			TArray<float> AccentIntensities;
			for (UPointLightComponent* Accent : AccentLights)
			{
				AccentIntensities.Add(Accent ? Accent->Intensity : 0.0f);
				if (Accent) Accent->SetIntensity(0.0f);
			}
			TStrongObjectPtr<USkeletalMeshComponent> ShadowProxy(NewObject<USkeletalMeshComponent>(Rig.CharacterOwner, NAME_None, RF_Transient));
			ON_SCOPE_EXIT
			{
				Rig.Scene->RemoveComponent(ShadowProxy.Get());
				ShadowProxy->DestroyComponent();
				for (const FToonProbeFacefixDirection& Direction : Directions)
					Direction.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Direction.Enabled);
				Rig.Character->SetCastShadow(bOriginalCastShadow);
				Rig.Character->SetCastContactShadow(bOriginalContactShadow);
				Rig.Character->SetCastCapsuleDirectShadow(bOriginalCapsuleDirect);
				Rig.Character->SetCastCapsuleIndirectShadow(bOriginalCapsuleIndirect);
				Rig.Character->SetForceDisableNanite(bOriginalDisableNanite);
				Rig.Character->SetForcedLOD(OriginalLOD);
				Rig.Character->UpdateLODStatus();
				Rig.Stage->KeyLight->SetWorldRotation(OriginalKeyRotation);
				Rig.Stage->KeyLight->SetCastShadows(bOriginalKeyShadow);
				Rig.Stage->KeyLight->CastDynamicShadows = bOriginalKeyDynamicShadow;
				Rig.Stage->KeyLight->SetCastRaytracedShadows(OriginalRayShadow);
				// 保留原始属性位，不让支持性检查改变探针进入前的状态。
				Rig.Stage->KeyLight->bUseRayTracedDistanceFieldShadows = bOriginalDistanceFieldShadow;
				Rig.Stage->KeyLight->MarkRenderStateDirty();
				for (int32 Index = 0; Index < 3; ++Index) if (StageLights[Index]) StageLights[Index]->SetIntensity(OriginalLightIntensities[Index]);
				for (int32 Index = 0; Index < AccentLights.Num(); ++Index) if (AccentLights[Index]) AccentLights[Index]->SetIntensity(AccentIntensities[Index]);
				Rig.Capture->SetShowFlagSettings(OriginalFlags);
				Rig.Occluder->SetWorldTransform(OriginalOccluderTransform);
				Rig.World->SendAllEndOfFrameUpdates();
				FlushRenderingCommands();
			};
			Rig.CharacterOwner->AddInstanceComponent(ShadowProxy.Get());
			ShadowProxy->SetSkeletalMesh(Sample.Mesh.Get());
			ShadowProxy->SetMobility(EComponentMobility::Movable);
			ShadowProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ShadowProxy->SetLeaderPoseComponent(Rig.Character.Get(), true);
			ShadowProxy->SetRenderInMainPass(false);
			ShadowProxy->SetRenderInDepthPass(false);
			ShadowProxy->SetCastShadow(true);
			ShadowProxy->bCastDynamicShadow = Rig.Character->bCastDynamicShadow;
			ShadowProxy->bCastStaticShadow = Rig.Character->bCastStaticShadow;
			ShadowProxy->SetCastContactShadow(false);
			ShadowProxy->SetCastCapsuleDirectShadow(false);
			ShadowProxy->SetCastCapsuleIndirectShadow(false);
			ShadowProxy->SetVisibleInRayTracing(false);
			ShadowProxy->SetForceDisableNanite(true);
			ShadowProxy->SetForcedLOD(1);
			ShadowProxy->SetLightingChannels(Rig.Character->LightingChannels.bChannel0, Rig.Character->LightingChannels.bChannel1, Rig.Character->LightingChannels.bChannel2);
			ShadowProxy->SetOverlayMaterial(Rig.Character->GetOverlayMaterial());
			for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			{
				ShadowProxy->SetMaterial(Slot, Rig.Character->GetMaterial(Slot));
				ShadowProxy->SetOverlayMaterial(Rig.Character->GetOverlayMaterial(true, Slot), true, Slot);
			}
			Rig.Scene->AddComponent(ShadowProxy.Get(), Rig.Character->GetComponentTransform());
			Rig.Character->SetCastShadow(false);
			Rig.Character->SetCastContactShadow(false);
			Rig.Character->SetCastCapsuleDirectShadow(false);
			Rig.Character->SetCastCapsuleIndirectShadow(false);
			Rig.Character->SetForceDisableNanite(true);
			Rig.Character->SetForcedLOD(1);
			Rig.Stage->KeyLight->SetCastShadows(true);
			Rig.Stage->KeyLight->CastDynamicShadows = true;
			Rig.Stage->KeyLight->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
			Rig.Stage->KeyLight->SetUseRayTracedDistanceFieldShadows(false);
			Rig.Stage->KeyLight->MarkRenderStateDirty();
			ToonProbeSetLights(Rig, true, false, false);
			ToonProbePlaceOccluder(Rig, false);
			// 截图 helper 不执行 world tick，必须显式应用 LOD，再更新 leader/follower 骨骼。
			Rig.Character->UpdateLODStatus();
			ShadowProxy->UpdateLODStatus();
			ToonProbePose(Rig, Sample.BeautyTime);
			ShadowProxy->UpdateFollowerComponent();
			const bool bLODVerified = FacefixCheck(TEXT("source_and_proxy_lod0_active"), Rig.Character->GetPredictedLODLevel() == 0 && ShadowProxy->GetPredictedLODLevel() == 0);
			ToonProbeSetShot(Rig, EToonProbeShot::Face);
			Facefix->SetStringField(TEXT("camera_location"), Rig.Capture->GetComponentLocation().ToString());
			Facefix->SetStringField(TEXT("camera_rotation"), Rig.Capture->GetComponentRotation().ToString());
			Facefix->SetNumberField(TEXT("camera_fov"), Rig.Capture->FOVAngle);
			Facefix->SetNumberField(TEXT("key_intensity"), Rig.Stage->KeyLight->Intensity);
			Facefix->SetStringField(TEXT("key_color"), Rig.Stage->KeyLight->GetLightColor().ToString());
			Facefix->SetNumberField(TEXT("accent_lights_disabled"), AccentLights.Num());
			Facefix->SetBoolField(TEXT("contact_shadows_enabled"), false);
			Facefix->SetBoolField(TEXT("proxy_main_pass_enabled"), false);
			Facefix->SetBoolField(TEXT("proxy_depth_pass_enabled"), false);
			Facefix->SetBoolField(TEXT("source_cast_shadow_enabled"), false);
			Facefix->SetBoolField(TEXT("ray_traced_and_capsule_shadows_enabled"), false);
			Facefix->SetNumberField(TEXT("forced_lod_index"), 0);
			Facefix->SetNumberField(TEXT("source_predicted_lod_index"), Rig.Character->GetPredictedLODLevel());
			Facefix->SetNumberField(TEXT("proxy_predicted_lod_index"), ShadowProxy->GetPredictedLODLevel());
			Facefix->SetStringField(TEXT("native_shadow_path"), ToonProbeCVar(TEXT("r.Shadow.Virtual.Enable")) == TEXT("1") ? TEXT("raster VSM") : TEXT("raster ShadowDepth/CSM"));
			Facefix->SetStringField(TEXT("hair_mask_scope"), TEXT("Resolved Hair slots only; shared material slots cannot be split. All other proxy slots retain source visibility/material/section cast flags. Generic hairshadow overlays remain visible and keep their original casting state. LeaderPose is used for this fixed reference pose; arbitrary cloth/deformer synchronization is not verified."));

			const TArray<FString> DisabledDirectFlags = { TEXT("GlobalIllumination"), TEXT("SkyLighting"), TEXT("ReflectionEnvironment"),
				TEXT("LumenReflections"), TEXT("ScreenSpaceReflections"), TEXT("AmbientOcclusion"), TEXT("LumenShortRangeAmbientOcclusion"), TEXT("ContactShadows") };
			TArray<TSharedPtr<FJsonValue>> DisabledFlagsReport;
			for (const FString& Flag : DisabledDirectFlags) DisabledFlagsReport.Add(MakeShared<FJsonValueString>(Flag));
			Facefix->SetArrayField(TEXT("disabled_show_flags_all_variants"), DisabledFlagsReport);
			struct FToonProbeFacefixAngle { const TCHAR* Name; float Yaw, Elevation; };
			const FToonProbeFacefixAngle Angles[] = { { TEXT("left"), -65.0f, 25.0f }, { TEXT("right"), 65.0f, 25.0f }, { TEXT("top"), 0.0f, 65.0f } };
			struct FToonProbeFacefixVariant { const TCHAR* Name; bool bDirection, bShadows, bHair; };
			const FToonProbeFacefixVariant Variants[] = {
				{ TEXT("direction_on_shadow_on_hair_on"), true, true, true },
				{ TEXT("direction_off_shadow_on_hair_on"), false, true, true },
				{ TEXT("direction_on_shadow_off_hair_on"), true, false, true },
				{ TEXT("direction_on_shadow_on_hair_off"), true, true, false } };
			TArray<TSharedPtr<FJsonValue>> Captures;
			bool bAllControlsVerified = bLODVerified;
			for (const FToonProbeFacefixAngle& Angle : Angles)
			{
				if (!bLODVerified) break;
				const float Yaw = FMath::DegreesToRadians(Angle.Yaw), Pitch = FMath::DegreesToRadians(Angle.Elevation);
				const FVector L = ((HeadForward * FMath::Cos(Yaw) + HeadRight * FMath::Sin(Yaw)) * FMath::Cos(Pitch) + HeadUp * FMath::Sin(Pitch)).GetSafeNormal();
				Rig.Stage->KeyLight->SetWorldRotation((-L).Rotation());
				for (const FToonProbeFacefixVariant& Variant : Variants)
				{
					for (const FToonProbeFacefixDirection& Direction : Directions)
						Direction.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Variant.bDirection ? Direction.Enabled : 0.0f);
					for (const FToonProbeFacefixSection& Section : Sections)
						ShadowProxy->ShowMaterialSection(Section.Material, Section.Section, Section.bShown && (Variant.bHair || !Section.bHair), Section.LOD);
					bool bControlsVerified = true;
					for (const FToonProbeFacefixSection& Section : Sections)
						bControlsVerified &= ShadowProxy->IsMaterialSectionShown(Section.MappedMaterial, Section.LOD) == (Section.bShown && (Variant.bHair || !Section.bHair));
					for (const FToonProbeFacefixDirection& Direction : Directions)
					{
						float Enabled = -1.0f;
						bControlsVerified &= Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Enabled)
							&& FMath::IsNearlyEqual(Enabled, Variant.bDirection ? Direction.Enabled : 0.0f);
					}
					bAllControlsVerified &= bControlsVerified;
					if (!bControlsVerified) { ++Log.Failures; Log.Note(TEXT("facefix_cpu_control_readback"), TEXT("Direction scalar or mapped proxy visibility mismatch; capture skipped")); continue; }
					TArray<FString> Disabled = DisabledDirectFlags;
					if (!Variant.bShadows) Disabled.Add(TEXT("DynamicShadows"));
					ToonProbeSetShowFlags(Rig, Disabled);
					const bool bShadowFlagsVerified = Rig.Capture->ShowFlags.DynamicShadows == Variant.bShadows && !Rig.Capture->ShowFlags.ContactShadows;
					bAllControlsVerified &= bShadowFlagsVerified;
					if (!bShadowFlagsVerified) { ++Log.Failures; Log.Note(TEXT("facefix_cpu_shadow_flags"), TEXT("Capture shadow/contact flags mismatch; capture skipped")); continue; }
					Rig.Capture->bCameraCutThisFrame = true;
					Rig.World->SendAllEndOfFrameUpdates();
					FlushRenderingCommands();
					const FString Name = FString::Printf(TEXT("facefix_%s_%s_face"), Angle.Name, Variant.Name);
					ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 8));
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetStringField(TEXT("image"), Name);
					Item->SetStringField(TEXT("light_side"), Angle.Name);
					Item->SetNumberField(TEXT("head_relative_yaw_degrees"), Angle.Yaw);
					Item->SetNumberField(TEXT("head_relative_elevation_degrees"), Angle.Elevation);
					Item->SetStringField(TEXT("surface_to_light_world"), L.ToString());
					Item->SetStringField(TEXT("light_travel_world"), (-L).ToString());
					Item->SetBoolField(TEXT("direction_enabled"), Variant.bDirection);
					Item->SetBoolField(TEXT("native_dynamic_shadows_enabled"), Variant.bShadows);
					Item->SetBoolField(TEXT("hair_proxy_sections_enabled"), Variant.bHair);
					Item->SetBoolField(TEXT("cpu_direction_proxy_visibility_and_shadow_flags_verified"), bControlsVerified && bShadowFlagsVerified);
					Captures.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			// 近90度只拍当前方向场与完整原生投影，避免扩成无意义的全组合。
			if (bLODVerified)
			{
				const TArray<FEngineShowFlagsSetting> GrazingOriginalFlags = Rig.Capture->GetShowFlagSettings();
				const FRotator GrazingOriginalLight = Rig.Stage->KeyLight->GetComponentRotation();
				ON_SCOPE_EXIT
				{
					Rig.Capture->SetShowFlagSettings(GrazingOriginalFlags);
					Rig.Stage->KeyLight->SetWorldRotation(GrazingOriginalLight);
				};
				ToonProbeSetShowFlags(Rig, DisabledDirectFlags);
				for (const FToonProbeFacefixDirection& Direction : Directions)
					Direction.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Direction.Enabled);
				for (const FToonProbeFacefixSection& Section : Sections)
					ShadowProxy->ShowMaterialSection(Section.Material, Section.Section, Section.bShown, Section.LOD);
				const FToonProbeFacefixAngle GrazingAngles[] = { { TEXT("left85"), -85.0f, 25.0f }, { TEXT("left90"), -90.0f, 25.0f },
					{ TEXT("right85"), 85.0f, 25.0f }, { TEXT("right90"), 90.0f, 25.0f } };
				for (const FToonProbeFacefixAngle& Angle : GrazingAngles)
				{
					const float Yaw = FMath::DegreesToRadians(Angle.Yaw), Elevation = FMath::DegreesToRadians(Angle.Elevation);
					const FVector L = ((HeadForward * FMath::Cos(Yaw) + HeadRight * FMath::Sin(Yaw)) * FMath::Cos(Elevation) + HeadUp * FMath::Sin(Elevation)).GetSafeNormal();
					Rig.Stage->KeyLight->SetWorldRotation((-L).Rotation());
					Rig.Capture->bCameraCutThisFrame = true;
					Rig.World->SendAllEndOfFrameUpdates();
					FlushRenderingCommands();
					const FString Name = FString::Printf(TEXT("facefix_grazing_%s_face"), Angle.Name);
					ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 8));
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetStringField(TEXT("image"), Name);
					Item->SetBoolField(TEXT("normal_profile"), true);
					Item->SetBoolField(TEXT("direction_enabled"), true);
					Item->SetBoolField(TEXT("native_dynamic_shadows_enabled"), true);
					Item->SetBoolField(TEXT("hair_proxy_sections_enabled"), true);
					Item->SetNumberField(TEXT("head_relative_yaw_degrees"), Angle.Yaw);
					Item->SetNumberField(TEXT("head_relative_elevation_degrees"), Angle.Elevation);
					Item->SetStringField(TEXT("surface_to_light_world"), L.ToString());
					Captures.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			// 固定YYB测试槽，仅改变可见鼻壳Opacity；生产着色不使用这些槽号。
			if (bLODVerified)
			{
				const TArray<FEngineShowFlagsSetting> ShellOriginalFlags = Rig.Capture->GetShowFlagSettings();
				const FRotator ShellOriginalLight = Rig.Stage->KeyLight->GetComponentRotation();
				TArray<TPair<UMaterialInstanceDynamic*, float>> ShellOpacities;
				TArray<TSharedPtr<FJsonValue>> ShellSlots;
				ON_SCOPE_EXIT
				{
					for (const TPair<UMaterialInstanceDynamic*, float>& Pair : ShellOpacities) Pair.Key->SetScalarParameterValue(TEXT("Opacity"), Pair.Value);
					Rig.Capture->SetShowFlagSettings(ShellOriginalFlags);
					Rig.Stage->KeyLight->SetWorldRotation(ShellOriginalLight);
				};
				for (const int32 Slot : { 11, 12 })
				{
					UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
					float Opacity = 0.0f;
					if (!MID || !MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity)) continue;
					ShellOpacities.Add({ MID, Opacity });
					MID->SetScalarParameterValue(TEXT("Opacity"), 0.0f);
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetNumberField(TEXT("slot"), Slot);
					Item->SetNumberField(TEXT("original_opacity"), Opacity);
					Item->SetStringField(TEXT("material"), MID->GetPathName());
					ShellSlots.Add(MakeShared<FJsonValueObject>(Item));
				}
				bool bShellControlsVerified = ShellOpacities.Num() == 2;
				for (const TPair<UMaterialInstanceDynamic*, float>& Pair : ShellOpacities)
				{
					float Opacity = -1.0f;
					bShellControlsVerified &= Pair.Key->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity) && Opacity == 0.0f;
				}
				Facefix->SetArrayField(TEXT("nose_shell_opacity_slots"), ShellSlots);
				bAllControlsVerified &= FacefixCheck(TEXT("nose_shell_opacity_zero_applied"), bShellControlsVerified);
				if (bShellControlsVerified)
				{
					const float Elevation = FMath::DegreesToRadians(65.0f);
					Rig.Stage->KeyLight->SetWorldRotation((-(HeadForward * FMath::Cos(Elevation) + HeadUp * FMath::Sin(Elevation))).Rotation());
					TArray<FString> Disabled = DisabledDirectFlags;
					Disabled.Add(TEXT("DynamicShadows"));
					ToonProbeSetShowFlags(Rig, Disabled);
					for (const FToonProbeFacefixDirection& Direction : Directions)
						Direction.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Direction.Enabled);
					Rig.Capture->bCameraCutThisFrame = true;
					Rig.World->SendAllEndOfFrameUpdates();
					FlushRenderingCommands();
					const FString Name = TEXT("facefix_generic_nose_shell_opacity_off_face");
					ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 8));
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetStringField(TEXT("image"), Name);
					Item->SetStringField(TEXT("opacity_zero_slots"), TEXT("11,12"));
					Item->SetBoolField(TEXT("normal_profile"), true);
					Item->SetBoolField(TEXT("direction_enabled"), true);
					Item->SetBoolField(TEXT("native_dynamic_shadows_enabled"), false);
					Item->SetNumberField(TEXT("head_relative_yaw_degrees"), 0.0f);
					Item->SetNumberField(TEXT("head_relative_elevation_degrees"), 65.0f);
					Item->SetStringField(TEXT("baseline_image"), TEXT("facefix_top_direction_on_shadow_off_hair_on_face"));
					Item->SetStringField(TEXT("contract"), TEXT("YYB16th test-only: only current MID Opacity for Generic slots11/12 set0; slots8/9 and shadow proxy unchanged; DynamicShadows off; RAII restores opacity."));
					Captures.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			// 正常 Profile 下只替换角阈值；保留源 B/A，排除阴影投射及诊断颜色对结论的干扰。
			if (bLODVerified)
			{
				struct FToonProbeFacefixThreshold
				{
					UMaterialInstanceDynamic* Material = nullptr;
					UTexture* Original = nullptr;
					UTexture2D* Dark = nullptr;
					UTexture2D* Lit = nullptr;
					float Enabled = 0.0f;
				};
				TArray<FToonProbeFacefixThreshold> Thresholds;
				const TArray<FEngineShowFlagsSetting> ThresholdOriginalFlags = Rig.Capture->GetShowFlagSettings();
				const FRotator ThresholdOriginalKeyRotation = Rig.Stage->KeyLight->GetComponentRotation();
				ON_SCOPE_EXIT
				{
					for (const FToonProbeFacefixThreshold& Threshold : Thresholds)
					{
						Threshold.Material->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, Threshold.Original);
						Threshold.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Threshold.Enabled);
					}
					Rig.Capture->SetShowFlagSettings(ThresholdOriginalFlags);
					Rig.Stage->KeyLight->SetWorldRotation(ThresholdOriginalKeyRotation);
				};
				bool bThresholdTexturesReady = true;
				TArray<TSharedPtr<FJsonValue>> ThresholdTextureReports;
				for (const FToonProbeFacefixDirection& Direction : Directions)
				{
					FToonProbeFacefixThreshold Threshold;
					Threshold.Material = Direction.Material;
					Direction.Material->GetTextureParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), Threshold.Original);
					Direction.Material->GetScalarParameterValue(FHashedMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), Threshold.Enabled);
					Thresholds.Add(Threshold);
					FToonProbeFacefixThreshold& Stored = Thresholds.Last();
					UTexture2D* Source = Cast<UTexture2D>(Stored.Original);
					TArray64<uint8> SourceBytes;
					const int32 Width = Source ? Source->Source.GetSizeX() : 0, Height = Source ? Source->Source.GetSizeY() : 0;
					bool bReady = Source && Source->Source.GetFormat() == TSF_BGRA8 && Source->Source.GetMipData(SourceBytes, 0)
						&& SourceBytes.Num() == int64(Width) * Height * sizeof(FColor);
					if (bReady)
					{
						for (const bool bLit : { false, true })
						{
							TArray64<uint8> Bytes = SourceBytes;
							for (int64 Pixel = 0; Pixel < int64(Width) * Height; ++Pixel)
							{
								Bytes[Pixel * 4 + 1] = bLit ? 255 : 0;
								Bytes[Pixel * 4 + 2] = bLit ? 255 : 0;
							}
							UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8, NAME_None, TConstArrayView64<uint8>(Bytes.GetData(), Bytes.Num()));
							bReady &= Texture != nullptr;
							if (!Texture) continue;
							KeepAlive.Emplace(Texture);
							Texture->SRGB = false;
							Texture->MipGenSettings = TMGS_NoMipmaps;
							Texture->NeverStream = true;
							Texture->Filter = TF_Bilinear;
							Texture->AddressX = TA_Clamp;
							Texture->AddressY = TA_Clamp;
							Texture->UpdateResource();
							if (bLit) Stored.Lit = Texture; else Stored.Dark = Texture;
						}
					}
					bThresholdTexturesReady &= bReady && Stored.Dark && Stored.Lit;
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetNumberField(TEXT("slot"), Direction.Slot);
					Item->SetStringField(TEXT("source_texture"), Source ? Source->GetPathName() : TEXT("none"));
					Item->SetNumberField(TEXT("width"), Width);
					Item->SetNumberField(TEXT("height"), Height);
					Item->SetBoolField(TEXT("copied_source_bgra_preserving_b_a"), bReady);
					ThresholdTextureReports.Add(MakeShared<FJsonValueObject>(Item));
				}
				Facefix->SetArrayField(TEXT("threshold_override_source_textures"), ThresholdTextureReports);
				bThresholdTexturesReady &= Thresholds.Num() == Directions.Num() && !Thresholds.IsEmpty();
				bAllControlsVerified &= FacefixCheck(TEXT("threshold_override_textures_ready"), bThresholdTexturesReady);
				if (bThresholdTexturesReady)
				{
					const float Elevation = FMath::DegreesToRadians(25.0f);
					Rig.Stage->KeyLight->SetWorldRotation((-(HeadForward * FMath::Cos(Elevation) + HeadUp * FMath::Sin(Elevation))).Rotation());
					TArray<FString> Disabled = DisabledDirectFlags;
					Disabled.Add(TEXT("DynamicShadows"));
					ToonProbeSetShowFlags(Rig, Disabled);
					for (const bool bLit : { false, true })
					{
						for (const FToonProbeFacefixThreshold& Threshold : Thresholds)
						{
							Threshold.Material->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, bLit ? Threshold.Lit : Threshold.Dark);
							Threshold.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, 1.0f);
						}
						Rig.Capture->bCameraCutThisFrame = true;
						Rig.World->SendAllEndOfFrameUpdates();
						FlushRenderingCommands();
						const FString Name = bLit ? TEXT("facefix_threshold_always_lit_face") : TEXT("facefix_threshold_always_dark_face");
						ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 8));
						TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
						Item->SetStringField(TEXT("image"), Name);
						Item->SetNumberField(TEXT("threshold_r_g_bytes"), bLit ? 255 : 0);
						Item->SetBoolField(TEXT("normal_profile"), true);
						Item->SetBoolField(TEXT("direction_enabled"), true);
						Item->SetBoolField(TEXT("native_dynamic_shadows_enabled"), false);
						Item->SetNumberField(TEXT("head_relative_yaw_degrees"), 0.0f);
						Item->SetNumberField(TEXT("head_relative_elevation_degrees"), 25.0f);
						Item->SetStringField(TEXT("contract"), TEXT("Transient linear one-mip BGRA copies; R/G=0 or255 only, source B/A unchanged. These captures test SDF threshold response with the normal Profile; CPU setup does not assert image quality."));
						Captures.Add(MakeShared<FJsonValueObject>(Item));
					}
				}
			}
			// 仅复制已经标记的 Skin Profile；18 由逐灯 BSDF 检查 valid && has_face_sdf。
			TArray<TPair<UMaterialInstanceDynamic*, UToonProfile*>> OriginalProfiles;
			for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			{
				if (UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot) != EMMDMaterialRole::Skin) continue;
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
				UToonProfile* Original = MID ? MID->ToonProfileOverride.Get() : nullptr;
				if (!Original || !Original->HasAnyFlags(RF_Transient)) continue;
				const FRichCurve& SourceAlpha = Original->Settings.DiffuseRamp.ExternalCurve
					? Original->Settings.DiffuseRamp.ExternalCurve->FloatCurves[3] : Original->Settings.DiffuseRamp.ColorCurves[3];
				if (!FMath::IsNearlyEqual(SourceAlpha.Eval(0.0f), -33.0f) || !FMath::IsNearlyEqual(SourceAlpha.Eval(1.0f), -65.0f)) continue;
				OriginalProfiles.Add({ MID, Original });
				KeepAlive.Emplace(Original);
				UToonProfile* Diagnostic = DuplicateObject<UToonProfile>(Original, GetTransientPackage());
				KeepAlive.Emplace(Diagnostic);
				Diagnostic->Guid = FGuid::NewGuid();
				FRichCurve* Alpha = &Diagnostic->Settings.DiffuseRamp.ColorCurves[3];
				if (UCurveLinearColor* External = Diagnostic->Settings.DiffuseRamp.ExternalCurve)
				{
					UCurveLinearColor* Copy = DuplicateObject<UCurveLinearColor>(External, GetTransientPackage());
					Diagnostic->Settings.DiffuseRamp.ExternalCurve = Copy;
					Alpha = &Copy->FloatCurves[3];
				}
				for (const float Time : { 0.6875f, 0.8125f }) Alpha->SetKeyInterpMode(Alpha->AddKey(Time, 18.0f), RCIM_Linear);
				MID->ToonProfileOverride = Diagnostic;
				MID->bOverrideToonProfile = true;
				const TArray<FScalarParameterValue> Scalars = MID->ScalarParameterValues;
				const TArray<FVectorParameterValue> Vectors = MID->VectorParameterValues;
				const TArray<FTextureParameterValue> Textures = MID->TextureParameterValues;
				MID->ClearParameterValues();
				for (const FScalarParameterValue& Value : Scalars) MID->SetScalarParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				for (const FVectorParameterValue& Value : Vectors) MID->SetVectorParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				for (const FTextureParameterValue& Value : Textures) MID->SetTextureParameterValue(Value.ParameterInfo.Name, Value.ParameterValue);
				ToonProbeRefreshProfile(Diagnostic);
			}
			Facefix->SetNumberField(TEXT("payload_sdf_diagnostic_profiles"), OriginalProfiles.Num());
			bAllControlsVerified &= FacefixCheck(TEXT("payload_sdf_diagnostic_skin_profiles_present"), !OriginalProfiles.IsEmpty());
			if (bLODVerified && !OriginalProfiles.IsEmpty())
			{
				const float Elevation = FMath::DegreesToRadians(25.0f);
				Rig.Stage->KeyLight->SetWorldRotation((-(HeadForward * FMath::Cos(Elevation) + HeadUp * FMath::Sin(Elevation))).Rotation());
				ToonProbeSetShowFlags(Rig, DisabledDirectFlags);
				for (const FToonProbeFacefixSection& Section : Sections)
					ShadowProxy->ShowMaterialSection(Section.Material, Section.Section, Section.bShown, Section.LOD);
				for (const bool bEnabled : { true, false })
				{
					for (const FToonProbeFacefixDirection& Direction : Directions)
						Direction.Material->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, bEnabled ? Direction.Enabled : 0.0f);
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
					Rig.Capture->bCameraCutThisFrame = true;
					Rig.World->SendAllEndOfFrameUpdates();
					FlushRenderingCommands();
					const FString Name = bEnabled ? TEXT("facefix_payload_sdf_on_face") : TEXT("facefix_payload_sdf_off_face");
					ToonProbeCapture(Rig, Log, Name, FMath::Max(Frames, 8));
					TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
					Item->SetStringField(TEXT("image"), Name);
					Item->SetNumberField(TEXT("bsdf_diagnostic"), 18);
					Item->SetBoolField(TEXT("direction_enabled"), bEnabled);
					Item->SetNumberField(TEXT("head_relative_yaw_degrees"), 0.0f);
					Item->SetNumberField(TEXT("head_relative_elevation_degrees"), 25.0f);
					Item->SetStringField(TEXT("contract"), TEXT("Marked Skin only: green=valid && has_face_sdf, red=otherwise. Opaque/masked reads packed deferred data; attached translucent overlays read forward inline data. Continuous alpha may mix layers; CPU binding alone is not GPU proof."));
					Captures.Add(MakeShared<FJsonValueObject>(Item));
				}
			}
			for (const TPair<UMaterialInstanceDynamic*, UToonProfile*>& Pair : OriginalProfiles) Pair.Key->ToonProfileOverride = Pair.Value;
			Facefix->SetArrayField(TEXT("captures"), Captures);
			Facefix->SetNumberField(TEXT("captured_images"), Captures.Num());
			Facefix->SetBoolField(TEXT("isolation_controls_applied"), bAllControlsVerified);
		}
		UseToonBaseline();
	}
	// volume：固定材质、机位与曝光，分别检查主光、完整灯组、GI 和高光。
	if (ToonProbeHasExp(Exps, TEXT("volume")) && NativePreset.IsValid() && bCanToon)
	{
		UseToonBaseline();
		ToonProbeValidateSlots(Rig, Log, TEXT("volume_baseline"));
		TInlineComponentArray<UPointLightComponent*> AccentLights;
		Rig.Stage->GetComponents(AccentLights);
		TArray<float> AccentIntensities;
		for (UPointLightComponent* Accent : AccentLights)
		{
			AccentIntensities.Add(Accent ? Accent->Intensity : 0.0f);
			if (Accent) Accent->SetIntensity(0.0f);
		}
		const TArray<FString> IndirectFlags = { TEXT("GlobalIllumination"), TEXT("SkyLighting"),
			TEXT("ReflectionEnvironment"), TEXT("LumenReflections"), TEXT("ScreenSpaceReflections") };
		for (int32 Variant = 0; Variant < 5; ++Variant)
		{
			const bool bKeyOnly = Variant == 0 || Variant == 1;
			const bool bIndirectOnly = Variant == 4;
			ToonProbeSetLights(Rig, !bIndirectOnly, !bKeyOnly && !bIndirectOnly, !bKeyOnly && !bIndirectOnly);
			TArray<FString> Disabled = Variant == 0 || Variant == 2 ? IndirectFlags : TArray<FString>();
			if (Variant == 3) Disabled.Add(TEXT("Specular"));
			ToonProbeSetShowFlags(Rig, Disabled);
			const TCHAR* Names[] = { TEXT("volume_key_direct"), TEXT("volume_key_gi"), TEXT("volume_three_direct"),
				TEXT("volume_three_nospec"), TEXT("volume_indirect") };
			for (EToonProbeShot Shot : { EToonProbeShot::Face, EToonProbeShot::Cloth, EToonProbeShot::Hair })
				ToonProbeCaptureShot(Rig, Sample, Log, Names[Variant], Shot, FMath::Max(Frames, 24));
		}
		ToonProbeSetShowFlags(Rig, {});
		ToonProbeSetLights(Rig, true, true, true);
		for (int32 Index = 0; Index < AccentLights.Num(); ++Index)
			if (AccentLights[Index]) AccentLights[Index]->SetIntensity(AccentIntensities[Index]);
		Log.Note(TEXT("volume_scope"), TEXT("Fixed camera/exposure/materials; accent points disabled during isolation; GI show flags isolate contributions, not a cooked-runtime or AO payload proof."));
	}
	if (ToonProbeHasExp(Exps, TEXT("sdf")) && NativePreset.IsValid() && (bCanToon || !bSubstrate))
	{
		UseToonBaseline();
		const bool bDirection = bSubstrate && MMDEndfieldNativeBackend::GetStatus().Mode == 2
			&& MMDEndfieldNativeBackend::GetStatus().bProfileMarkerAllowed && ToonProbeCVar(TEXT("r.Substrate.ProjectGBufferFormat")) == TEXT("1");
		const TCHAR* SdfEnabledName = bDirection ? TEXT("ToonFaceDirectionEnabled") : bSubstrate ? TEXT("ToonFaceSdfEnabled") : TEXT("UseSdfMap");
		const TCHAR* SdfTextureName = bDirection ? TEXT("ToonFaceDirectionTexture") : bSubstrate ? TEXT("ToonFaceSdfTexture") : TEXT("SDFMap");
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
				return MID && Enabled == 1.0f && Texture == (bDirection ? Data->Slots[0].DirectionTexture.Get() : Data->Slots[0].Texture.Get());
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
				if (bDirection)
				{
					// 临时诊断父材质不在正式资产路径，显式绑定，不能误用生产路径门控结果。
					for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
						if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex)))
						{
							MID->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), 1.0f);
							MID->SetScalarParameterValue(TEXT("ToonFaceDirectionVersion"), float(UMMDEndfieldFaceData::CurrentVersion));
							MID->SetScalarParameterValue(SdfEnabledName, 1.0f);
							MID->SetTextureParameterValue(SdfTextureName, Slot.DirectionTexture);
						}
					UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				}
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
	if (ToonProbeHasExp(Exps, TEXT("fidelity")) && NativePreset.IsValid() && bCanToon)
	{
		UseToonBaseline();
		ToonProbeSetLights(Rig, true, false, false);
		ToonProbePose(Rig, Sample.FaceTimes.Num() > 0 ? Sample.FaceTimes[0] : Sample.BeautyTime);
		UMMDEndfieldFaceData* Data = Sample.Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		if (!Data || Data->Version != UMMDEndfieldFaceData::CurrentVersion || Data->FrameVersion != MMDEndfieldHeadFrame::CurrentFrameVersion
			|| Data->Slots.IsEmpty() || Data->Slots.ContainsByPredicate([](const FMMDGeneratedFaceSdfSlot& Slot) { return Slot.UVChannel != -1 || Slot.DirectionVersion != UMMDEndfieldFaceData::CurrentVersion; }))
		{
			++Log.Failures;
			Log.Note(TEXT("fidelity_direction_fixture"), TEXT("需要当前 V5 头投影夹具、FrameVersion1 与过程化坐标 UVChannel=-1"));
		}
		else
		{
			const FRotator LightRotation = Rig.Stage->KeyLight->GetComponentRotation();
			const FRotator MeshRotation = Rig.Character->GetComponentRotation();
			const int32 FaceSlot = Data->Slots[0].MaterialIndex;
			auto SetDirection = [&](bool bEnabled)
			{
				for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
					if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex)))
						MID->SetScalarParameterValue(TEXT("ToonFaceDirectionEnabled"), bEnabled ? 1.0f : 0.0f);
			};
			auto HeadFrame = [&](FVector& F, FVector& R, FVector& U)
			{
				UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				FLinearColor Forward, Right;
				Rig.Character->GetMaterial(FaceSlot)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadForward), Forward);
				Rig.Character->GetMaterial(FaceSlot)->GetVectorParameterValue(FHashedMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadRight), Right);
				F = FVector(Forward.R, Forward.G, Forward.B).GetSafeNormal();
				R = FVector(Right.R, Right.G, Right.B).GetSafeNormal();
				U = FVector::CrossProduct(R, F).GetSafeNormal();
			};
			FVector F, R, U;
			HeadFrame(F, R, U);
			for (const float Elevation : { -20.0f, 45.0f })
				for (const float Azimuth : { -65.0f, 65.0f })
				{
					const float Pitch = FMath::DegreesToRadians(Elevation), Yaw = FMath::DegreesToRadians(Azimuth);
					const FVector L = (F * FMath::Cos(Yaw) + R * FMath::Sin(Yaw)) * FMath::Cos(Pitch) + U * FMath::Sin(Pitch);
					Rig.Stage->KeyLight->SetWorldRotation((-L).Rotation());
					SetDirection(true);
					ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("fidelity_pitch_%d_yaw_%d"), FMath::RoundToInt(Elevation), FMath::RoundToInt(Azimuth)), EToonProbeShot::Face);
				}
			// 固定世界光，旋转整个骨骼组件，验证本帧头部旋转四元数的解码。
			Rig.Stage->KeyLight->SetWorldRotation((-(F * 0.8 + R * 0.5 + U * 0.3).GetSafeNormal()).Rotation());
			for (const float Yaw : { -35.0f, 35.0f })
			{
				Rig.Character->SetWorldRotation(MeshRotation + FRotator(0.0f, Yaw, 0.0f));
				ToonProbeCaptureShot(Rig, Sample, Log, FString::Printf(TEXT("fidelity_head_yaw_%d"), FMath::RoundToInt(Yaw)), EToonProbeShot::Face);
			}
			Rig.Character->SetWorldRotation(MeshRotation);
			Rig.Stage->KeyLight->SetWorldRotation(LightRotation);
			ToonProbeSetShowFlags(Rig, { TEXT("Diffuse") });
			for (const bool bEnabled : { true, false })
			{
				SetDirection(bEnabled);
				ToonProbeCaptureShot(Rig, Sample, Log, bEnabled ? TEXT("fidelity_spec_gi_direction_on") : TEXT("fidelity_spec_gi_direction_off"), EToonProbeShot::Face);
			}
			ToonProbeSetShowFlags(Rig, {});
			ToonProbeSetLights(Rig, true, true, true);
			UseToonBaseline();
			for (const EToonProbeShot Shot : { EToonProbeShot::Hero, EToonProbeShot::Hair, EToonProbeShot::Cloth })
				ToonProbeCaptureShot(Rig, Sample, Log, TEXT("fidelity_materials"), Shot);
			ToonProbeValidateSlots(Rig, Log, TEXT("fidelity_native_payload"));
			// 逐灯诊断：不透明路径读取 packed valid；透明附层使用 forward inline，不能统称 packed。
			ToonProbeSetLights(Rig, true, false, false);
			for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
			{
				UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex));
				if (!MID || !MID->ToonProfileOverride) continue;
				UToonProfile* Diagnostic = DuplicateObject<UToonProfile>(MID->ToonProfileOverride, GetTransientPackage());
				KeepAlive.Emplace(Diagnostic);
				Diagnostic->Guid = FGuid::NewGuid();
				FRichCurve& Alpha = Diagnostic->Settings.DiffuseRamp.ColorCurves[3];
				for (const float Time : { 0.6875f, 0.8125f }) Alpha.SetKeyInterpMode(Alpha.AddKey(Time, 17.0f), RCIM_Linear);
				MID->ToonProfileOverride = Diagnostic;
				MID->bOverrideToonProfile = true;
				MID->ClearParameterValues();
				MID->SetScalarParameterValue(TEXT("ToonSphereBlend"), 0.75f);
				ToonProbeRefreshProfile(Diagnostic);
			}
			for (const bool bEnabled : { true, false })
			{
				for (const FMMDGeneratedFaceSdfSlot& Slot : Data->Slots)
					if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot.MaterialIndex)))
						MID->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), bEnabled ? 1.0f : 0.0f);
				UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Rig.Character.Get());
				ToonProbeCaptureShot(Rig, Sample, Log, bEnabled ? TEXT("fidelity_packed_valid_on") : TEXT("fidelity_packed_valid_off"), EToonProbeShot::Face);
			}
			UseToonBaseline();
			ToonProbeSetLights(Rig, true, true, true);
		}
	}
	if (ToonProbeHasExp(Exps, TEXT("outline")) && NativePreset.IsValid())
	{
		UseToonBaseline();
		auto CheckOutline = [&](const TCHAR* Name, bool bOk)
		{
			Log.Note(Name, bOk ? TEXT("passed") : TEXT("failed"));
			Log.Failures += bOk ? 0 : 1;
		};
		CheckOutline(TEXT("outline_dedicated_parent_present"), IsValid(NativePreset->CharacterOutlineMaterial.Get()));
		TStrongObjectPtr<UMMDStageRenderPreset> OutlinePreset(DuplicateObject<UMMDStageRenderPreset>(ToonPreset.Get(), GetTransientPackage()));
		TArray<TArray<FColor>> OutlineOffPixels, OutlineOnPixels;
		OutlineOffPixels.SetNum(UE_ARRAY_COUNT(AllShots));
		OutlineOnPixels.SetNum(UE_ARRAY_COUNT(AllShots));
		OutlinePreset->bCharacterOutline = false;
		CheckOutline(TEXT("outline_off_applied"), ToonProbeApplyPreset(Rig, OutlinePreset.Get()));
		ToonProbeFinishMaterialSwap(Rig, Sample);
		bool bOff = Rig.Character->GetOverlayMaterial() == nullptr;
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			bOff &= Rig.Character->GetOverlayMaterial(true, Slot) == nullptr;
		CheckOutline(TEXT("outline_off_has_no_overlay"), bOff);
		for (int32 Shot = 0; Shot < UE_ARRAY_COUNT(AllShots); ++Shot)
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("outline_off"), AllShots[Shot], -1, &OutlineOffPixels[Shot]);

		OutlinePreset->bCharacterOutline = true;
		CheckOutline(TEXT("outline_on_applied"), ToonProbeApplyPreset(Rig, OutlinePreset.Get()));
		ToonProbeFinishMaterialSwap(Rig, Sample);
		const UMMDSkeletalMeshUserData* UserData = Sample.Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>();
		int32 BoundCount = 0;
		TArray<TSharedPtr<FJsonValue>> OutlineSlots;
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInterface* Imported = UserData && UserData->OutlineMaterials.IsValidIndex(Slot)
				? UserData->OutlineMaterials[Slot].Get() : nullptr;
			UMaterialInstanceDynamic* Outline = Cast<UMaterialInstanceDynamic>(Rig.Character->GetOverlayMaterial(true, Slot));
			if (!Imported)
			{
				CheckOutline(TEXT("outline_pmx_disabled_slot_empty"), Outline == nullptr);
				continue;
			}
			++BoundCount;
			const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Rig.Character.Get(), Slot);
			const FMMDStageOutlineRoleSettings DefaultSettings;
			const FMMDStageOutlineRoleSettings* Settings = OutlinePreset->CharacterOutlineRoles.Find(Role);
			if (!Settings) Settings = OutlinePreset->CharacterOutlineRoles.Find(EMMDMaterialRole::Generic);
			if (!Settings) Settings = &DefaultSettings;
			UTexture* SurfaceTexture = nullptr;
			UTexture* OutlineTexture = nullptr;
			FLinearColor SurfaceBase = FLinearColor::White;
			FLinearColor OutlineBase = FLinearColor::Black;
			UMaterialInterface* Surface = Rig.Character->GetMaterial(Slot);
			if (Surface)
			{
				Surface->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), SurfaceTexture);
				Surface->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), SurfaceBase);
			}
			float Brightness = -1.0f, Saturation = -1.0f, SourceBlend = -1.0f;
			float AlphaMode = -1.0f, Opacity = -1.0f, Cutoff = -1.0f;
			const EBlendMode SurfaceBlend = Surface ? Surface->GetBlendMode() : BLEND_Opaque;
			const float ExpectedAlphaMode = SurfaceBlend == BLEND_Opaque ? 0.0f : SurfaceBlend == BLEND_Masked ? 1.0f : 2.0f;
			const float ExpectedCutoff = SurfaceBlend == BLEND_Masked ? Surface->GetOpacityMaskClipValue() : 0.01f;
			float ExpectedOpacity = SurfaceBase.A;
			if (Surface) Surface->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), ExpectedOpacity);
			float ImportedEdgeSize = 1.0f, BoundEdgeSize = -1.0f;
			FLinearColor ImportedEdge = FLinearColor::Black, BoundEdge = FLinearColor::Transparent;
			Imported->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("EdgeSize")), ImportedEdgeSize);
			Imported->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("EdgeColor")), ImportedEdge);
			if (Outline)
			{
				Outline->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("base_color_tex")), OutlineTexture);
				Outline->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), OutlineBase);
				Outline->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("EdgeColor")), BoundEdge);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("EdgeSize")), BoundEdgeSize);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::SourceBrightness), Brightness);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::SourceSaturation), Saturation);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::SourceBlend), SourceBlend);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::AlphaMode), AlphaMode);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::Opacity), Opacity);
				Outline->GetScalarParameterValue(FHashedMaterialParameterInfo(MMDEndfieldOutlineParameters::Cutoff), Cutoff);
			}
			bool bOverlayFallback = true;
			if (const FMaterialRenderProxy* Proxy = Outline ? Outline->GetRenderProxy() : nullptr)
			{
				ENQUEUE_RENDER_COMMAND(ToonProbeCheckOutlineProxy)(
					[Proxy, &bOverlayFallback](FRHICommandListImmediate& RHICmdList)
					{
						const FMaterialRenderProxy* Actual = Proxy;
						const FMaterial& Material = Proxy->GetMaterialWithFallback(GMaxRHIFeatureLevel, Actual);
						bOverlayFallback = Actual != Proxy || Material.IsDefaultMaterial();
					});
				FlushRenderingCommands();
			}
			const bool bPassed = Outline && Outline->Parent == OutlinePreset->CharacterOutlineMaterial && !bOverlayFallback
				&& (!SurfaceTexture || SurfaceTexture == OutlineTexture) && SurfaceBase.Equals(OutlineBase, 0.0001f)
				&& FMath::IsNearlyEqual(Brightness, Settings->SourceBrightness) && FMath::IsNearlyEqual(Saturation, Settings->SourceSaturation)
				&& FMath::IsNearlyEqual(SourceBlend, OutlinePreset->OutlineSourceColorBlend)
				&& FMath::IsNearlyEqual(AlphaMode, ExpectedAlphaMode) && FMath::IsNearlyEqual(Opacity, ExpectedOpacity)
				&& FMath::IsNearlyEqual(Cutoff, ExpectedCutoff)
				&& FMath::IsNearlyEqual(ImportedEdgeSize, BoundEdgeSize) && FMath::IsNearlyEqual(ImportedEdge.A, BoundEdge.A);
			Log.Failures += bPassed ? 0 : 1;
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetNumberField(TEXT("slot"), Slot);
			Item->SetNumberField(TEXT("role"), static_cast<uint8>(Role));
			Item->SetStringField(TEXT("parent"), Outline ? Outline->Parent->GetPathName() : TEXT("none"));
			Item->SetStringField(TEXT("surface_texture"), SurfaceTexture ? SurfaceTexture->GetPathName() : TEXT("none"));
			Item->SetStringField(TEXT("outline_texture"), OutlineTexture ? OutlineTexture->GetPathName() : TEXT("none"));
			Item->SetNumberField(TEXT("brightness"), Brightness);
			Item->SetNumberField(TEXT("saturation"), Saturation);
			Item->SetNumberField(TEXT("source_blend"), SourceBlend);
			Item->SetNumberField(TEXT("source_alpha_mode"), AlphaMode);
			Item->SetNumberField(TEXT("source_opacity"), Opacity);
			Item->SetNumberField(TEXT("source_cutoff"), Cutoff);
			Item->SetBoolField(TEXT("render_proxy_fallback"), bOverlayFallback);
			Item->SetBoolField(TEXT("passed"), bPassed);
			OutlineSlots.Add(MakeShared<FJsonValueObject>(Item));
		}
		CheckOutline(TEXT("outline_at_least_one_pmx_slot"), BoundCount > 0);
		Report->SetArrayField(TEXT("outline_slots"), OutlineSlots);
		Report->SetStringField(TEXT("outline_diagnostic_contract"), TEXT("off/on/source/alpha_hidden 每组均为当前角色的 full/face/hair/cloth 原图；source 只将外扩壳改为未压暗底色；alpha_hidden 强制描边 cutout mode 与 Opacity=0，主体和灯光不变。槽报告另校验父材质、底图、底色、角色亮度/饱和度、PMX 尺寸/alpha、表面覆盖语义与真实渲染代理。"));
		for (int32 Shot = 0; Shot < UE_ARRAY_COUNT(AllShots); ++Shot)
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("outline_on"), AllShots[Shot], -1, &OutlineOnPixels[Shot]);
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			if (UMaterialInstanceDynamic* Outline = Cast<UMaterialInstanceDynamic>(Rig.Character->GetOverlayMaterial(true, Slot)))
				Outline->SetScalarParameterValue(MMDEndfieldOutlineParameters::Diagnostic, 1.0f);
		int64 ChangedOffOn = 0, ChangedOnSource = 0;
		TArray<TSharedPtr<FJsonValue>> PixelChecks;
		for (int32 Shot = 0; Shot < UE_ARRAY_COUNT(AllShots); ++Shot)
		{
			TArray<FColor> SourcePixels;
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("outline_source"), AllShots[Shot], -1, &SourcePixels);
			for (const bool bSourceComparison : { false, true })
			{
				const TArray<FColor>& First = bSourceComparison ? OutlineOnPixels[Shot] : OutlineOffPixels[Shot];
				const TArray<FColor>& Last = bSourceComparison ? SourcePixels : OutlineOnPixels[Shot];
				const bool bComparable = First.Num() > 0 && First.Num() == Last.Num();
				int64 Changed = 0, SumDifference = 0;
				if (bComparable)
					for (int32 Pixel = 0; Pixel < First.Num(); ++Pixel)
					{
						const int32 R = FMath::Abs(int32(First[Pixel].R) - Last[Pixel].R);
						const int32 G = FMath::Abs(int32(First[Pixel].G) - Last[Pixel].G);
						const int32 B = FMath::Abs(int32(First[Pixel].B) - Last[Pixel].B);
						Changed += FMath::Max3(R, G, B) > 8 ? 1 : 0;
						SumDifference += R + G + B;
					}
				(bSourceComparison ? ChangedOnSource : ChangedOffOn) += Changed;
				CheckOutline(TEXT("outline_images_comparable"), bComparable);
				TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
				Item->SetStringField(TEXT("shot"), ToonProbeShotName(AllShots[Shot]));
				Item->SetStringField(TEXT("comparison"), bSourceComparison ? TEXT("on_to_source") : TEXT("off_to_on"));
				Item->SetNumberField(TEXT("changed_pixels_over_8"), Changed);
				Item->SetNumberField(TEXT("mean_rgb_difference_255"), bComparable ? double(SumDifference) / (3.0 * First.Num()) : 0.0);
				PixelChecks.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
		// 仅拒绝完全无可见变化；时序噪声与具体染色正确性仍须结合实际原图复核。
		CheckOutline(TEXT("outline_off_on_visible_difference"), ChangedOffOn >= 16);
		CheckOutline(TEXT("outline_on_source_visible_difference"), ChangedOnSource >= 16);
		Report->SetArrayField(TEXT("outline_pixel_checks"), PixelChecks);
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
		{
			UMaterialInstanceDynamic* Surface = Cast<UMaterialInstanceDynamic>(Rig.Character->GetMaterial(Slot));
			UMaterialInstanceDynamic* Outline = Cast<UMaterialInstanceDynamic>(Rig.Character->GetOverlayMaterial(true, Slot));
			if (!Surface || !Outline) continue;
			FLinearColor OriginalBase = FLinearColor::White;
			Surface->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), OriginalBase);
			const FLinearColor EditedBase(0.27f, 0.61f, 0.43f, OriginalBase.A);
			Surface->SetVectorParameterValue(TEXT("BaseColor"), EditedBase);
			UMMDOutlineLibrary::RefreshMMDOutlinePreset(Rig.Character.Get());
			FLinearColor BoundBase = FLinearColor::Black;
			Outline->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), BoundBase);
			CheckOutline(TEXT("outline_current_basecolor_edit_rebound"), BoundBase.Equals(EditedBase, 0.0001f));
			Surface->SetVectorParameterValue(TEXT("BaseColor"), OriginalBase);
			UMMDOutlineLibrary::RefreshMMDOutlinePreset(Rig.Character.Get());
			break;
		}
		for (int32 Slot = 0; Slot < Rig.Character->GetNumMaterials(); ++Slot)
			if (UMaterialInstanceDynamic* Outline = Cast<UMaterialInstanceDynamic>(Rig.Character->GetOverlayMaterial(true, Slot)))
			{
				Outline->SetScalarParameterValue(MMDEndfieldOutlineParameters::AlphaMode, 1.0f);
				Outline->SetScalarParameterValue(MMDEndfieldOutlineParameters::Opacity, 0.0f);
			}
		int64 AlphaChanged = 0;
		TArray<TSharedPtr<FJsonValue>> AlphaPixelChecks;
		for (int32 Shot = 0; Shot < UE_ARRAY_COUNT(AllShots); ++Shot)
		{
			TArray<FColor> HiddenPixels;
			ToonProbeCaptureShot(Rig, Sample, Log, TEXT("outline_alpha_hidden"), AllShots[Shot], -1, &HiddenPixels);
			const TArray<FColor>& Off = OutlineOffPixels[Shot];
			const TArray<FColor>& On = OutlineOnPixels[Shot];
			const bool bComparable = !HiddenPixels.IsEmpty() && Off.Num() == HiddenPixels.Num() && On.Num() == HiddenPixels.Num();
			CheckOutline(TEXT("outline_alpha_images_comparable"), bComparable);
			int64 Changed = 0, HiddenToOff = 0, HiddenToOn = 0;
			if (bComparable)
				for (int32 Pixel = 0; Pixel < HiddenPixels.Num(); ++Pixel)
				{
					const int32 R = FMath::Abs(int32(HiddenPixels[Pixel].R) - On[Pixel].R);
					const int32 G = FMath::Abs(int32(HiddenPixels[Pixel].G) - On[Pixel].G);
					const int32 B = FMath::Abs(int32(HiddenPixels[Pixel].B) - On[Pixel].B);
					Changed += FMath::Max3(R, G, B) > 8 ? 1 : 0;
					HiddenToOn += R + G + B;
					HiddenToOff += FMath::Abs(int32(HiddenPixels[Pixel].R) - Off[Pixel].R)
						+ FMath::Abs(int32(HiddenPixels[Pixel].G) - Off[Pixel].G)
						+ FMath::Abs(int32(HiddenPixels[Pixel].B) - Off[Pixel].B);
				}
			AlphaChanged += Changed;
			TSharedRef<FJsonObject> Item = ToonProbeJsonObject();
			Item->SetStringField(TEXT("shot"), ToonProbeShotName(AllShots[Shot]));
			Item->SetNumberField(TEXT("on_to_alpha_hidden_changed_pixels_over_8"), Changed);
			Item->SetNumberField(TEXT("hidden_to_off_mean_rgb_difference_255"), bComparable ? double(HiddenToOff) / (3.0 * Off.Num()) : 0.0);
			Item->SetNumberField(TEXT("hidden_to_on_mean_rgb_difference_255"), bComparable ? double(HiddenToOn) / (3.0 * On.Num()) : 0.0);
			AlphaPixelChecks.Add(MakeShared<FJsonValueObject>(Item));
		}
		CheckOutline(TEXT("outline_zero_opacity_visible_difference"), AlphaChanged >= 16);
		Report->SetArrayField(TEXT("outline_alpha_pixel_checks"), AlphaPixelChecks);

		const UMMDOutlineState* OutlineState = Cast<UMMDOutlineState>(Rig.Character->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass()));
		UMaterialInterface* SourceGlobal = OutlineState ? OutlineState->SourceGlobalOverlay.Get() : nullptr;
		const TArray<TObjectPtr<UMaterialInterface>> SourceSlots = OutlineState ? OutlineState->SourceSlotOverlays : TArray<TObjectPtr<UMaterialInterface>>();
		const float SourceBounds = OutlineState ? OutlineState->SourceBoundsScale : Rig.Character->BoundsScale;
		CheckOutline(TEXT("outline_restore_applied"), UMMDStageLibrary::RestoreCharacterMaterials(Rig.Character.Get()));
		CheckOutline(TEXT("outline_global_overlay_restored"), Rig.Character->GetOverlayMaterial() == SourceGlobal);
		CheckOutline(TEXT("outline_slot_overlays_restored"), Rig.Character->MaterialSlotsOverlayMaterial == SourceSlots);
		CheckOutline(TEXT("outline_bounds_restored"), FMath::IsNearlyEqual(Rig.Character->BoundsScale, SourceBounds));
		UseToonBaseline();
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
