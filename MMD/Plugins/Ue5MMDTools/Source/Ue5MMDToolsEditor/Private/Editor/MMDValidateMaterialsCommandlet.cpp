#include "MMDValidateMaterialsCommandlet.h"
#include "MMDRenderingValidation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor/MMDOutlineMaterial.h"
#include "Editor/UMMDMaterialEnsure.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "MaterialShared.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "PixelFormat.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/ArchiveUObject.h"
#include "ShaderCompiler.h"

namespace
{
	const TCHAR* GMaterialRoot = TEXT("/Ue5MMDTools/Resources/MaterialInstance");
	const TCHAR* GBaseMaskedPath = TEXT("/Ue5MMDTools/Resources/MaterialInstance/M_MMD_Base_Masked.M_MMD_Base_Masked");

	void LogBaseMaskedCustomInputs(const TCHAR* Stage)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, GBaseMaskedPath);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials] %s: cannot load %s"), Stage, GBaseMaskedPath);
			return;
		}
		const UMaterialEditorOnlyData* EditorOnlyData = Material->GetEditorOnlyData();
		const UMaterialExpression* OpacityMaskExpression = EditorOnlyData
			? EditorOnlyData->OpacityMask.Expression
			: nullptr;
		UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] %s: BaseMasked BlendMode=%d OpacityMask=%s"),
			Stage, static_cast<int32>(Material->GetBlendMode()),
			OpacityMaskExpression ? *OpacityMaskExpression->GetClass()->GetName() : TEXT("unconnected"));

		int32 MatchingCustomCount = 0;
		for (UMaterialExpression* Expression : Material->GetExpressionCollection().Expressions)
		{
			UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
			if (!Custom || !Custom->Code.Contains(TEXT("MMDBaseToon")))
			{
				continue;
			}

			++MatchingCustomCount;
			UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] %s: BaseMasked Custom has %d input(s)"),
				Stage, Custom->Inputs.Num());
			for (const FCustomInput& Input : Custom->Inputs)
			{
				const UMaterialExpression* InputExpression = Input.Input.Expression;
				UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] %s:   %s <- %s"), Stage,
					*Input.InputName.ToString(),
					InputExpression ? *InputExpression->GetClass()->GetName() : TEXT("unconnected"));
			}
		}

		if (MatchingCustomCount == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MMDValidateMaterials] %s: no MMDBaseToon Custom found in BaseMasked"), Stage);
		}
	}


}

int32 UMMDValidateMaterialsCommandlet::Main(const FString& Params)
{
	UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] Platform=%s Root=%s"),
		*LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString(), GMaterialRoot);
	const bool bCanCompileShaders = FApp::CanEverRender() && GShaderCompilingManager != nullptr;
	if (!bCanCompileShaders)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MMDValidateMaterials] Shader validation requires a rendering-capable commandlet and a shader compiling manager. Run with -AllowCommandletRendering -RenderOffscreen."));
	}

	// 修复必须显式请求；默认验证绝不保存资产。
	if (FParse::Param(*Params, TEXT("RepairBaseMasked")))
	{
		LogBaseMaskedCustomInputs(TEXT("before repair"));
		UMMDMaterialEnsure::EnsureMaterialByAssetName(TEXT("M_MMD_Base_Masked"));
		LogBaseMaskedCustomInputs(TEXT("after repair"));
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	AssetRegistry.ScanPathsSynchronous(TArray<FString>{GMaterialRoot});

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByPath(FName(GMaterialRoot), AssetDataList, true);
	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.GetObjectPathString() < B.GetObjectPathString();
	});

	TArray<UMaterial*> Materials;
	for (const FAssetData& AssetData : AssetDataList)
	{
		// 只验证原生 UMaterial；材质实例和材质函数由其父材质覆盖。
		if (AssetData.AssetClassPath != UMaterial::StaticClass()->GetClassPathName())
		{
			continue;
		}

		if (UMaterial* Material = Cast<UMaterial>(AssetData.GetAsset()))
		{
			Materials.Add(Material);
			if (bCanCompileShaders)
			{
				// 使用公开接口刷新渲染资源；shader 源码哈希会使过期 DDC 条目失效。
				Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
			}
		}
	}
	const int32 SavedMaterialCount = Materials.Num();

	// 构造仅含 base_color 的临时材质，直接验证 fallback 不需要任何 Texture 输入。
	UMaterial* FallbackMaterial = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMDBaseToonFallbackValidation"), RF_Transient);
	{
		// 新建材质需要经过一次空序列化来创建平台资源，做法与材质编辑器预览材质一致。
		FArchiveUObject DummyArchive;
		FallbackMaterial->Serialize(DummyArchive);
	}
	FallbackMaterial->MaterialDomain = MD_Surface;
	FallbackMaterial->SetShadingModel(MSM_Unlit);

	UMaterialExpressionConstant3Vector* BaseColorExpression = NewObject<UMaterialExpressionConstant3Vector>(FallbackMaterial);
	BaseColorExpression->Constant = FLinearColor(0.6f, 0.4f, 0.3f, 1.0f);
	BaseColorExpression->Material = FallbackMaterial;

	UMaterialExpressionCustom* FallbackExpression = NewObject<UMaterialExpressionCustom>(FallbackMaterial);
	FallbackExpression->Material = FallbackMaterial;
	FallbackExpression->OutputType = CMOT_Float3;
	FallbackExpression->Description = TEXT("MMD base-color-only fallback validation");
	FallbackExpression->Code = TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/MMDBaseToonFallback.usf\"\nreturn 0;");
	FallbackExpression->Inputs.Reset();
	FCustomInput& BaseColorInput = FallbackExpression->Inputs.AddDefaulted_GetRef();
	BaseColorInput.InputName = TEXT("base_color");
	BaseColorInput.Input.Expression = BaseColorExpression;

	FallbackMaterial->GetExpressionCollection().Expressions.Add(BaseColorExpression);
	FallbackMaterial->GetExpressionCollection().Expressions.Add(FallbackExpression);
	bool bFallbackGraphBuilt = false;
	if (UMaterialEditorOnlyData* EditorOnlyData = FallbackMaterial->GetEditorOnlyData())
	{
		EditorOnlyData->EmissiveColor.Connect(0, FallbackExpression);
		bFallbackGraphBuilt = true;
	}
	Materials.Add(FallbackMaterial);
	if (bCanCompileShaders)
	{
		FallbackMaterial->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
	}

	// 构造完整描边父材质，验证 WPO、遮罩、双面与骨骼网格用法的真实 shader permutation。
	UMaterial* OutlineMaterial = CreateMMDOutlineMaterial(
		GetTransientPackage(), TEXT("MMDOutlineValidation"), RF_Transient);
	const bool bOutlineGraphValid = OutlineMaterial && IsMMDOutlineMaterialGraphValid(OutlineMaterial);
	if (OutlineMaterial)
	{
		Materials.Add(OutlineMaterial);
		if (bCanCompileShaders)
		{
			OutlineMaterial->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
		}
	}

	if (bCanCompileShaders)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}

	int32 PassedCount = 0;
	int32 FailedCount = 0;
	TArray<TSharedPtr<FJsonValue>> MaterialResults;
	for (UMaterial* Material : Materials)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("asset"), Material->GetPathName());
		Result->SetBoolField(TEXT("transient"), Material->HasAnyFlags(RF_Transient));

		TArray<FString> Errors;
		if (Material == FallbackMaterial && (!bFallbackGraphBuilt || FallbackExpression->Inputs.Num() != 1))
		{
			Errors.Add(TEXT("Transient base-color-only fallback graph was not built correctly"));
		}
		if (Material == OutlineMaterial && !bOutlineGraphValid)
		{
			Errors.Add(TEXT("Transient MMD outline material graph is invalid"));
		}
		if (!bCanCompileShaders)
		{
			Errors.Add(TEXT("Shader compilation is unavailable; use -AllowCommandletRendering -RenderOffscreen"));
		}
		const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (Resource)
		{
			Errors.Append(Resource->GetCompileErrors());
			if (!Resource->IsCompilationFinished())
			{
				Errors.Add(TEXT("Material resource compilation did not finish"));
			}

			const FMaterialShaderMap* ShaderMap = Resource->GetGameThreadShaderMap();
			if (!ShaderMap)
			{
				Errors.Add(TEXT("Material resource has no shader map for the current RHI platform"));
			}
			else
			{
				if (ShaderMap->GetShaderPlatform() != GMaxRHIShaderPlatform)
				{
					Errors.Add(TEXT("Material shader map platform does not match the current RHI platform"));
				}
				if (!ShaderMap->IsCompilationFinalized())
				{
					Errors.Add(TEXT("Material shader map compilation is not finalized"));
				}
				if (!ShaderMap->CompiledSuccessfully())
				{
					Errors.Add(TEXT("Material shader map reports an unsuccessful compilation"));
				}
				if (!ShaderMap->IsValidForRendering(false))
				{
					Errors.Add(TEXT("Material shader map is not valid for rendering"));
				}
			}
		}
		else
		{
			Errors.Add(TEXT("GetMaterialResource returned null for the current RHI shader platform"));
		}

		const bool bPassed = Errors.IsEmpty();
		Result->SetBoolField(TEXT("passed"), bPassed);
		TArray<TSharedPtr<FJsonValue>> JsonErrors;
		for (const FString& Error : Errors)
		{
			JsonErrors.Add(MakeShared<FJsonValueString>(Error));
		}
		Result->SetArrayField(TEXT("errors"), JsonErrors);
		MaterialResults.Add(MakeShared<FJsonValueObject>(Result));

		if (bPassed)
		{
			++PassedCount;
			UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] PASS %s"), *Material->GetPathName());
		}
		else
		{
			++FailedCount;
			UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials] FAIL %s"), *Material->GetPathName());
			for (const FString& Error : Errors)
			{
				UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials]   %s"), *Error);
			}
		}
	}

	// 验证 transient 副本的运行态配置与实际 GPU 资源，保持原资产不变。
	const TSharedRef<FJsonObject> RenderingChecks = ValidateMMDRenderTargets();
	const TArray<TSharedPtr<FJsonValue>> RenderTargetResults = RenderingChecks->GetArrayField(TEXT("tests"));
	PassedCount += static_cast<int32>(RenderingChecks->GetNumberField(TEXT("passed")));
	FailedCount += static_cast<int32>(RenderingChecks->GetNumberField(TEXT("failed")));
	const TSharedRef<FJsonObject> OutlineSlotChecks = ValidateMMDOutlineSlots();
	PassedCount += static_cast<int32>(OutlineSlotChecks->GetNumberField(TEXT("passed")));
	FailedCount += static_cast<int32>(OutlineSlotChecks->GetNumberField(TEXT("failed")));
	TSharedPtr<FJsonObject> PreviewScreenshots;
	if (FParse::Param(*Params, TEXT("PreviewScreenshots")))
	{
		// 所有材质 shader job 已完成后才进行真实离屏模型截图。
		PreviewScreenshots = CaptureMMDPreviewScreenshots();
		PassedCount += static_cast<int32>(PreviewScreenshots->GetNumberField(TEXT("passed")));
		FailedCount += static_cast<int32>(PreviewScreenshots->GetNumberField(TEXT("failed")));
	}
	if (!OutlineMaterial)
	{
		++FailedCount;
		UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials] FAIL transient outline material creation returned null"));
	}
	if (SavedMaterialCount == 0)
	{
		++FailedCount;
		UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials] FAIL no native UMaterial assets found under %s"), GMaterialRoot);
	}

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("shader_platform"), LegacyShaderPlatformToShaderFormat(GMaxRHIShaderPlatform).ToString());
	Report->SetStringField(TEXT("material_root"), GMaterialRoot);
	Report->SetBoolField(TEXT("shader_compilation_available"), bCanCompileShaders);
	Report->SetNumberField(TEXT("material_count"), SavedMaterialCount);
	Report->SetNumberField(TEXT("transient_validation_count"), 2);
	Report->SetNumberField(TEXT("passed"), PassedCount);
	Report->SetNumberField(TEXT("failed"), FailedCount);
	Report->SetBoolField(TEXT("success"), FailedCount == 0);
	Report->SetArrayField(TEXT("materials"), MaterialResults);
	Report->SetArrayField(TEXT("render_targets"), RenderTargetResults);
	Report->SetObjectField(TEXT("rendering_checks"), RenderingChecks);
	Report->SetObjectField(TEXT("outline_slot_checks"), OutlineSlotChecks);
	if (PreviewScreenshots.IsValid())
	{
		Report->SetObjectField(TEXT("preview_screenshots"), PreviewScreenshots.ToSharedRef());
	}

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Report, Writer);
	const FString OutputDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation"));
	IFileManager::Get().MakeDirectory(*OutputDirectory, true);
	const FString OutputPath = FPaths::Combine(OutputDirectory, TEXT("materials.json"));
	if (!FFileHelper::SaveStringToFile(Json, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		++FailedCount;
		UE_LOG(LogTemp, Error, TEXT("[MMDValidateMaterials] FAIL cannot write %s"), *OutputPath);
	}

	UE_LOG(LogTemp, Display, TEXT("[MMDValidateMaterials] Done: %d material(s), %d passed, %d failed. Report: %s"),
		Materials.Num(), PassedCount, FailedCount, *OutputPath);
	return FailedCount == 0 ? 0 : 1;
}
