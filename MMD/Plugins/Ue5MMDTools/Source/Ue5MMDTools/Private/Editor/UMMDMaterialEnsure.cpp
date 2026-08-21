#include "Editor/UMMDMaterialEnsure.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"

// ----------------------------------------------------------------------------
// 输入规格表（与各 usf 头注释严格一致；默认值取头注释建议值）
// ----------------------------------------------------------------------------
namespace
{
	enum class EMMDInputKind : uint8 { Float, Float2, Float3, Texture };

	struct FMMDCustomInputSpec
	{
		const TCHAR* Name;
		EMMDInputKind Kind;
		float A, B, C; // 常量默认值（Float 用 A；Float2 用 A/B；Float3 用 A/B/C）
	};

	struct FMMDMaterialSpec
	{
		const TCHAR* AssetName;              // 资产名（如 M_MMD_Base_Opaque）
		const TCHAR* UsfFileName;            // 新建 Custom 时 include 的 usf 文件名
		const TCHAR* CodeTokens[2];          // 识别已有 Custom 节点的 Code 子串
		const FMMDCustomInputSpec* Inputs;
		int32 InputCount;
	};

	// ---- MMDBaseToon.usf（基础/透明共用）----
	static const FMMDCustomInputSpec GBaseToonInputs[] = {
		{ TEXT("BaseColor"),         EMMDInputKind::Float3,  1.0f, 1.0f, 1.0f },
		{ TEXT("LightDataTex"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("ShadowStep"),        EMMDInputKind::Float,   0.5f, 0.0f, 0.0f },
		{ TEXT("HighlightStep"),     EMMDInputKind::Float,   0.82f, 0.0f, 0.0f },
		{ TEXT("ShadowColor"),       EMMDInputKind::Float3,  0.55f, 0.55f, 0.55f },
		{ TEXT("SpecularPower"),     EMMDInputKind::Float,   32.0f, 0.0f, 0.0f },
		{ TEXT("NormalMap"),         EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("NormalMapStrength"), EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
		{ TEXT("MatCap"),            EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("SphereMode"),        EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
		{ TEXT("MMDShadowMap"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("MMDShadowBias"),     EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
	};

	// ---- TMMDAnimeFace.usf（脸：柔和阴影 + 边缘光）----
	static const FMMDCustomInputSpec GFaceInputs[] = {
		{ TEXT("BaseColor"),         EMMDInputKind::Float3,  1.0f, 1.0f, 1.0f },
		{ TEXT("LightDataTex"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("ShadowStep"),        EMMDInputKind::Float,   0.55f, 0.0f, 0.0f },
		{ TEXT("HighlightStep"),     EMMDInputKind::Float,   0.85f, 0.0f, 0.0f },
		{ TEXT("ShadowColor"),       EMMDInputKind::Float3,  0.65f, 0.65f, 0.65f },
		{ TEXT("SpecularPower"),     EMMDInputKind::Float,   64.0f, 0.0f, 0.0f },
		{ TEXT("NormalMap"),         EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("NormalMapStrength"), EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
		{ TEXT("RimColor"),          EMMDInputKind::Float3,  0.9f, 0.95f, 1.0f },
		{ TEXT("RimPower"),          EMMDInputKind::Float,   3.0f, 0.0f, 0.0f },
		{ TEXT("RimStrength"),       EMMDInputKind::Float,   0.6f, 0.0f, 0.0f },
	};

	// ---- TMMDAnimeHair.usf（发：Kajiya-Kay 各向异性高光）----
	static const FMMDCustomInputSpec GHairInputs[] = {
		{ TEXT("BaseColor"),         EMMDInputKind::Float3,  1.0f, 1.0f, 1.0f },
		{ TEXT("LightDataTex"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("ShadowStep"),        EMMDInputKind::Float,   0.5f, 0.0f, 0.0f },
		{ TEXT("HighlightStep"),     EMMDInputKind::Float,   0.6f, 0.0f, 0.0f },
		{ TEXT("ShadowColor"),       EMMDInputKind::Float3,  0.55f, 0.55f, 0.55f },
		{ TEXT("SpecularPower"),     EMMDInputKind::Float,   48.0f, 0.0f, 0.0f },
		{ TEXT("MatCap"),            EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("SphereMode"),        EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
		{ TEXT("MMDShadowMap"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("MMDShadowBias"),     EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
	};

	// ---- MMDAnimeEye.usf（眼：虹膜自发光/白高光/睫毛阴影）----
	static const FMMDCustomInputSpec GEyeInputs[] = {
		{ TEXT("BaseColor"),         EMMDInputKind::Float3,  1.0f, 1.0f, 1.0f },
		{ TEXT("LightDataTex"),      EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("ShadowStep"),        EMMDInputKind::Float,   0.5f, 0.0f, 0.0f },
		{ TEXT("HighlightStep"),     EMMDInputKind::Float,   0.85f, 0.0f, 0.0f },
		{ TEXT("ShadowColor"),       EMMDInputKind::Float3,  0.55f, 0.55f, 0.55f },
		{ TEXT("SpecularPower"),     EMMDInputKind::Float,   100.0f, 0.0f, 0.0f },
		{ TEXT("MatCap"),            EMMDInputKind::Texture, 0.0f, 0.0f, 0.0f },
		{ TEXT("SphereMode"),        EMMDInputKind::Float,   0.0f, 0.0f, 0.0f },
		{ TEXT("IrisGlow"),          EMMDInputKind::Float,   1.0f, 0.0f, 0.0f },
		{ TEXT("GlintStrength"),     EMMDInputKind::Float,   1.5f, 0.0f, 0.0f },
		{ TEXT("GlintPos"),          EMMDInputKind::Float2,  0.5f, 0.42f, 0.0f },
		{ TEXT("GlintSize"),         EMMDInputKind::Float,   0.05f, 0.0f, 0.0f },
		{ TEXT("TopShadow"),         EMMDInputKind::Float,   0.6f, 0.0f, 0.0f },
		{ TEXT("BottomLift"),        EMMDInputKind::Float,   0.6f, 0.0f, 0.0f },
	};

	static const FMMDMaterialSpec GMaterialSpecs[] = {
		{ TEXT("M_MMD_Base_Opaque"),      TEXT("MMDBaseToon.usf"),      { TEXT("MMDBaseToon"), TEXT("") },                     GBaseToonInputs, UE_ARRAY_COUNT(GBaseToonInputs) },
		{ TEXT("M_MMD_Base_Transparent"), TEXT("MMDBaseToon.usf"),      { TEXT("MMDAnimeToonLighting"), TEXT("MMDBaseToon") }, GBaseToonInputs, UE_ARRAY_COUNT(GBaseToonInputs) },
		{ TEXT("M_MMD_Face"),             TEXT("TMMDAnimeFace.usf"),    { TEXT("TMMDAnimeFace"), TEXT("") },                  GFaceInputs, UE_ARRAY_COUNT(GFaceInputs) },
		{ TEXT("M_MMD_Hair"),             TEXT("TMMDAnimeHair.usf"),    { TEXT("TMMDAnimeHair"), TEXT("") },                  GHairInputs, UE_ARRAY_COUNT(GHairInputs) },
		{ TEXT("M_MMD_Eye"),              TEXT("MMDAnimeEye.usf"),      { TEXT("MMDAnimeEye"), TEXT("") },                    GEyeInputs, UE_ARRAY_COUNT(GEyeInputs) },
	};

	const TCHAR* GLightDataRT_Path = TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT");
	const TCHAR* GShadowRT_Path   = TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT");

	/** 递归找包含目标 usf 的 Custom 节点（支持材质函数内嵌）。 */
	UMaterialExpressionCustom* FindCustomByToken(UObject* Owner, const FMMDMaterialSpec& Spec)
	{
		const TArray<TObjectPtr<UMaterialExpression>>* Exprs = nullptr;
		if (UMaterial* Mat = Cast<UMaterial>(Owner))
		{
			Exprs = &Mat->GetExpressionCollection().Expressions;
		}
		else if (UMaterialFunctionInterface* Func = Cast<UMaterialFunctionInterface>(Owner))
		{
			if (UMaterialFunctionEditorOnlyData* Data = Cast<UMaterialFunctionEditorOnlyData>(Func->GetEditorOnlyData()))
			{
				Exprs = &Data->ExpressionCollection.Expressions;
			}
		}
		if (!Exprs)
		{
			return nullptr;
		}

		for (UMaterialExpression* Expr : *Exprs)
		{
			if (UMaterialExpressionCustom* C = Cast<UMaterialExpressionCustom>(Expr))
			{
				for (const TCHAR* Token : Spec.CodeTokens)
				{
					if (Token[0] != 0 && C->Code.Contains(Token))
					{
						return C;
					}
				}
			}
			else if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
			{
				if (Call->MaterialFunction)
				{
					if (UMaterialExpressionCustom* Nested = FindCustomByToken(Call->MaterialFunction, Spec))
					{
						return Nested;
					}
				}
			}
		}
		return nullptr;
	}

	/** 输入名 → 默认纹理（LightDataTex/MMDShadowMap 用真实 RT，其余用引擎默认纹理兜底）。 */
	UTexture* DefaultTextureForInput(const TCHAR* Name)
	{
		if (FCString::Stricmp(Name, TEXT("LightDataTex")) == 0)
		{
			if (UTexture* RT = LoadObject<UTextureRenderTarget2D>(nullptr, GLightDataRT_Path))
			{
				return RT;
			}
		}
		if (FCString::Stricmp(Name, TEXT("MMDShadowMap")) == 0)
		{
			if (UTexture* RT = LoadObject<UTextureRenderTarget2D>(nullptr, GShadowRT_Path))
			{
				return RT;
			}
		}
		if (FCString::Stricmp(Name, TEXT("MatCap")) == 0)
		{
			if (UTexture* BlackTexture = LoadObject<UTexture>(
				nullptr, TEXT("/Engine/EngineResources/Black.Black")))
			{
				return BlackTexture;
			}
		}
		return GEngine ? GEngine->DefaultTexture : nullptr;
	}

	/**
	 * 为 Custom 输入创建默认表达式并连线。
	 * 与导入脚本（CreateMaterialFromMMDBase）写入的材质实例参数对齐：
	 *   BaseColor  -> DiffuseColor(VectorParam, 白) × BaseColorMap(TextureSampleParam2D, 默认白贴图) → RGB
	 *   MatCap     -> MatCap(TextureObjectParameter)   （实例可覆盖 sphere 贴图，sampler 由节点自动生成）
	 *   SphereMode -> ScalarParameter "SphereMode"
	 *   SpecularPower -> ScalarParameter "SpecularPower"
	 * 其余输入按类型给默认常量/纹理对象。
	 */
	UMaterialExpression* CreateInputExpression(UMaterial* Material, const FMMDCustomInputSpec& Spec)
	{
		const FName InName(Spec.Name);

		// ---- BaseColor：参数链（贴图 × 漫反射色），实例参数写入才生效 ----
		if (InName == TEXT("BaseColor"))
		{
			UMaterialExpressionTextureSampleParameter2D* TexSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
			TexSample->ParameterName = TEXT("BaseColorMap");
			TexSample->Texture = GEngine ? GEngine->DefaultTexture : nullptr;

			UMaterialExpressionVectorParameter* DiffuseParam = NewObject<UMaterialExpressionVectorParameter>(Material);
			DiffuseParam->ParameterName = TEXT("DiffuseColor");
			DiffuseParam->DefaultValue = FLinearColor::White;

			UMaterialExpressionMultiply* Multiply = NewObject<UMaterialExpressionMultiply>(Material);
			Multiply->A.Connect(0, DiffuseParam);
			Multiply->B.Connect(0, TexSample);

			UMaterialExpressionComponentMask* Mask = NewObject<UMaterialExpressionComponentMask>(Material);
			Mask->R = true; Mask->G = true; Mask->B = true; Mask->A = false;
			Mask->Input.Connect(0, Multiply);

			Material->GetExpressionCollection().Expressions.Add(TexSample);
			Material->GetExpressionCollection().Expressions.Add(DiffuseParam);
			Material->GetExpressionCollection().Expressions.Add(Multiply);
			Material->GetExpressionCollection().Expressions.Add(Mask);
			TexSample->Material = Material;
			DiffuseParam->Material = Material;
			Multiply->Material = Material;
			Mask->Material = Material;
			return Mask; // float3
		}

		// ---- MatCap：纹理对象参数（贴 shader 的 Texture2D 输入，不是采样值）----
		if (InName == TEXT("MatCap"))
		{
			UMaterialExpressionTextureObjectParameter* TexParam = NewObject<UMaterialExpressionTextureObjectParameter>(Material);
			TexParam->ParameterName = TEXT("MatCap");
			TexParam->Texture = DefaultTextureForInput(TEXT("MatCap"));
			Material->GetExpressionCollection().Expressions.Add(TexParam);
			TexParam->Material = Material;
			return TexParam;
		}

		// ---- 标量参数（实例可覆盖）----
		if (InName == TEXT("SphereMode") || InName == TEXT("SpecularPower"))
		{
			UMaterialExpressionScalarParameter* ScalarParam = NewObject<UMaterialExpressionScalarParameter>(Material);
			ScalarParam->ParameterName = Spec.Name;
			ScalarParam->DefaultValue = Spec.A;
			Material->GetExpressionCollection().Expressions.Add(ScalarParam);
			ScalarParam->Material = Material;
			return ScalarParam;
		}

		switch (Spec.Kind)
		{
		case EMMDInputKind::Float:
		{
			UMaterialExpressionConstant* Const = NewObject<UMaterialExpressionConstant>(Material);
			Const->R = Spec.A;
			return Const;
		}
		case EMMDInputKind::Float2:
		{
			UMaterialExpressionConstant2Vector* Const = NewObject<UMaterialExpressionConstant2Vector>(Material);
			Const->R = Spec.A;
			Const->G = Spec.B;
			return Const;
		}
		case EMMDInputKind::Float3:
		{
			UMaterialExpressionConstant3Vector* Const = NewObject<UMaterialExpressionConstant3Vector>(Material);
			Const->Constant = FLinearColor(Spec.A, Spec.B, Spec.C);
			return Const;
		}
		case EMMDInputKind::Texture:
		{
			UMaterialExpressionTextureObject* TexObj = NewObject<UMaterialExpressionTextureObject>(Material);
			TexObj->Texture = DefaultTextureForInput(Spec.Name);
			return TexObj;
		}
		}
		return nullptr;
	}

	const TCHAR* KindToString(EMMDInputKind Kind)
	{
		switch (Kind)
		{
		case EMMDInputKind::Float:   return TEXT("float");
		case EMMDInputKind::Float2:  return TEXT("float2");
		case EMMDInputKind::Float3:  return TEXT("float3");
		case EMMDInputKind::Texture: return TEXT("Texture2D");
		}
		return TEXT("?");
	}

	bool SaveMaterial(UMaterial* Material)
	{
		FString FileName;
		if (FPackageName::TryConvertLongPackageNameToFilename(Material->GetOutermost()->GetName(), FileName, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			return UPackage::SavePackage(Material->GetOutermost(), Material, *FileName, SaveArgs);
		}
		return false;
	}

	int32 EnsureMaterialSpec(const FMMDMaterialSpec& Spec)
	{
		const FString PackageName = FString::Printf(TEXT("/Ue5MMDTools/Resources/MaterialInstance/%s"), Spec.AssetName);
		const FString ObjectPath  = FString::Printf(TEXT("%s.%s"), *PackageName, Spec.AssetName);

		UMaterial* Material = LoadObject<UMaterial>(nullptr, *ObjectPath);
		bool bCreated = false;
		if (!Material)
		{
			// 缺失的父材质（如 M_MMD_Face / M_MMD_Hair）：程序化从零创建
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				UE_LOG(LogTemp, Error, TEXT("[MMDMatEnsure] CreatePackage failed: %s"), *PackageName);
				return 0;
			}
			Material = NewObject<UMaterial>(Package, Spec.AssetName, RF_Public | RF_Standalone);
			if (!Material)
			{
				UE_LOG(LogTemp, Error, TEXT("[MMDMatEnsure] NewObject<UMaterial> failed: %s"), Spec.AssetName);
				return 0;
			}
			bCreated = true;
			UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] 缺失父材质，程序化创建: %s"), Spec.AssetName);
		}

		bool bChanged = bCreated;

		// ---- Custom 节点：找到则复用，找不到则新建 ----
		UMaterialExpressionCustom* Custom = FindCustomByToken(Material, Spec);
		if (!Custom)
		{
			Custom = NewObject<UMaterialExpressionCustom>(Material);
			Custom->Code = FString::Printf(TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/%s\"\nreturn 0;"), Spec.UsfFileName);
			Custom->OutputType = CMOT_Float3;
			Custom->Description = FString::Printf(TEXT("%s 着色入口"), Spec.UsfFileName);
			Material->GetExpressionCollection().Expressions.Add(Custom);
			Custom->Material = Material;
			bChanged = true;
			UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: 新建 Custom 节点 (%s)"), Spec.AssetName, Spec.UsfFileName);
		}

		// ---- 输入补齐：漏连/缺失的输入接默认常量或纹理 ----
		for (int32 i = 0; i < Spec.InputCount; ++i)
		{
			const FMMDCustomInputSpec& InSpec = Spec.Inputs[i];

			FCustomInput* Existing = nullptr;
			for (FCustomInput& In : Custom->Inputs)
			{
				if (In.InputName == InSpec.Name)
				{
					Existing = &In;
					break;
				}
			}

			if (Existing)
			{
				if (Existing->Input.Expression)
				{
					// ---- 连线健康检查：验证已有连线是否符合预期 ----
					const FString ActualClass = Existing->Input.Expression->GetClass()->GetName();
					bool bHealthy = true;

					// BaseColor (Float3) 必须是 ComponentMask（链末端），不能是裸 TextureSample
					if (InSpec.Kind == EMMDInputKind::Float3 && FCString::Stricmp(InSpec.Name, TEXT("BaseColor")) == 0)
					{
						if (ActualClass.Contains(TEXT("TextureSample")))
						{
							bHealthy = false; // 裸纹理采样，缺 DiffuseColor × 乘 → 需要重连
						}
					}

					// Texture 输入：必须是 TextureObject / TextureObjectParameter，不能是 TextureSampleParameter2D
					if (InSpec.Kind == EMMDInputKind::Texture)
					{
						if (ActualClass.Contains(TEXT("TextureSampleParameter")))
						{
							bHealthy = false; // 采样参数不是纹理对象
						}
					}

					if (bHealthy)
					{
						UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s.%s 已连线 <- %s (期望 %s)"),
							Spec.AssetName, InSpec.Name, *ActualClass, KindToString(InSpec.Kind));
						continue;
					}

					// 不健康：断开旧连线，走下方重建逻辑
					UE_LOG(LogTemp, Warning, TEXT("[MMDMatEnsure] %s.%s 连线异常(%s)，强制重连"),
						Spec.AssetName, InSpec.Name, *ActualClass);
					Existing->Input.Expression = nullptr;
					Existing->Input.OutputIndex = 0;
				}
			}
			else
			{
				Existing = &Custom->Inputs.AddDefaulted_GetRef();
				Existing->InputName = InSpec.Name;
				UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: 补输入 %s (%s)"), Spec.AssetName, InSpec.Name, KindToString(InSpec.Kind));
			}

			UMaterialExpression* DefExpr = CreateInputExpression(Material, InSpec);
			if (DefExpr)
			{
				// 参数链（BaseColor 等）已在链内注册子节点；这里只补顶层节点，避免重复入图
				if (!Material->GetExpressionCollection().Expressions.Contains(DefExpr))
				{
					Material->GetExpressionCollection().Expressions.Add(DefExpr);
				}
				DefExpr->Material = Material;
				Existing->Input.Expression = DefExpr;
				Existing->Input.OutputIndex = 0;
				bChanged = true;
				UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: 接线 %s <- 默认%s"), Spec.AssetName, InSpec.Name, KindToString(InSpec.Kind));
			}
		}

		// ---- 输出连线：Unlit 材质只有 EmissiveColor 生效 ----
		if (UMaterialEditorOnlyData* EOD = Material->GetEditorOnlyData())
		{
			if (EOD->EmissiveColor.Expression != Custom)
			{
				EOD->EmissiveColor.Expression = Custom;
				EOD->EmissiveColor.OutputIndex = 0;
				bChanged = true;
				UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: Custom 输出接入 EmissiveColor"), Spec.AssetName);
			}
		}

		// ---- 材质属性：Unlit + SkeletalMesh 用法 ----
		if (Material->GetShadingModels().GetFirstShadingModel() != MSM_Unlit)
		{
			Material->SetShadingModel(MSM_Unlit);
			bChanged = true;
			UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: ShadingModel -> Unlit"), Spec.AssetName);
		}
		if (!Material->GetUsageByFlag(EMaterialUsage::MATUSAGE_SkeletalMesh))
		{
			Material->SetMaterialUsage(EMaterialUsage::MATUSAGE_SkeletalMesh);
			bChanged = true;
			UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: 开启 SkeletalMesh 用法标记"), Spec.AssetName);
		}

		// ---- 保存 ----
		if (bChanged)
		{
			Material->PostEditChange();
			Material->MarkPackageDirty();

			if (bCreated)
			{
				FAssetRegistryModule::AssetCreated(Material);
			}

			if (SaveMaterial(Material))
			{
				UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] %s: 已保存%s"), Spec.AssetName, bCreated ? TEXT("（新建）") : TEXT(""));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[MMDMatEnsure] %s: 保存失败"), Spec.AssetName);
			}
			return 1;
		}
		return 0;
	}
} // namespace

int32 UMMDMaterialEnsure::EnsureAllMaterials()
{
	UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] ===== 校验 MMD 父材质开始 ====="));

	// 注册表可能未扫描插件 Content，先补一次同步扫描
	if (FAssetRegistryModule* ARM = FModuleManager::Get().GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
	{
		ARM->Get().ScanPathsSynchronous(TArray<FString>{ TEXT("/Ue5MMDTools/Resources/MaterialInstance") });
	}

	int32 ModifiedCount = 0;
	for (const FMMDMaterialSpec& Spec : GMaterialSpecs)
	{
		ModifiedCount += EnsureMaterialSpec(Spec);
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDMatEnsure] ===== 完成：修改 %d 个父材质 ====="), ModifiedCount);
	return ModifiedCount;
}

void UMMDMaterialEnsure::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 延迟数秒执行：等资产注册表 / 引擎资源就绪（编辑器刚启动时 LoadObject 可能抢跑）
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) -> bool
	{
		EnsureAllMaterials();
		return false; // 只跑一次
	}), 5.0f);
}

void UMMDMaterialEnsure::Deinitialize()
{
	Super::Deinitialize();
}

// 控制台命令：MMD.EnsureMaterials
static FAutoConsoleCommand CmdMMDEnsureMaterials(
	TEXT("MMD.EnsureMaterials"),
	TEXT("校验并修复 MMD 父材质 Custom 节点（输入补齐/输出接 Emissive/Unlit）。"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UMMDMaterialEnsure::EnsureAllMaterials();
	}));
