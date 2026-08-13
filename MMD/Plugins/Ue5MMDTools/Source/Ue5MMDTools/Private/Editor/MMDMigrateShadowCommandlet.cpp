#include "MMDMigrateShadowCommandlet.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Engine/TextureRenderTarget2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* GShadowRT_AssetPath = TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT");

	/** 递归找包含 MMDBaseToon 的 Custom 节点（支持材质函数内嵌）。 */
	UMaterialExpressionCustom* FindBaseToonCustom(UObject* Owner)
	{
		// UMaterial / UMaterialFunction 的表达式都藏在 EditorOnlyData 里（UE5.5）
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
				if (C->Code.Contains(TEXT("MMDBaseToon")))
				{
					return C;
				}
			}
			else if (UMaterialExpressionMaterialFunctionCall* Call = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
			{
				if (Call->MaterialFunction)
				{
					if (UMaterialExpressionCustom* Nested = FindBaseToonCustom(Call->MaterialFunction))
					{
						return Nested;
					}
				}
			}
		}
		return nullptr;
	}
}

int32 UMMDMigrateShadowCommandlet::Main(const FString& Params)
{
	UTextureRenderTarget2D* ShadowRT = LoadObject<UTextureRenderTarget2D>(nullptr, GShadowRT_AssetPath);
	if (!ShadowRT)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDMigrateShadow] MMDShadowMapRT not found at %s"), GShadowRT_AssetPath);
		return 1;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// 命令let 里注册表可能未扫描插件 Content，强制同步扫描
	AssetRegistry.ScanPathsSynchronous(TArray<FString>{ TEXT("/Ue5MMDTools/Resources/MaterialInstance") });

	TArray<FAssetData> MaterialAssets;
	AssetRegistry.GetAssetsByPath(TEXT("/Ue5MMDTools/Resources/MaterialInstance"), MaterialAssets, true);

	int32 ModifiedCount = 0;
	for (const FAssetData& AssetData : MaterialAssets)
	{
		UObject* Asset = AssetData.GetAsset();
		UMaterial* Material = Cast<UMaterial>(Asset);
		if (!Material)
		{
			UE_LOG(LogTemp, Log, TEXT("[MMDMigrateShadow] Skip (not a material): %s"), *AssetData.GetObjectPathString());
			continue;
		}

		UMaterialExpressionCustom* Custom = FindBaseToonCustom(Material);
		if (!Custom)
		{
			UE_LOG(LogTemp, Log, TEXT("[MMDMigrateShadow] No MMDBaseToon custom node: %s"), *Material->GetName());
			continue;
		}

		bool bChanged = false;

		// MMDShadowMap 贴图输入（连到 MMDShadowMapRT）
		auto FindOrCreateInput = [&](const FName& InputName) -> FCustomInput*
		{
			for (FCustomInput& In : Custom->Inputs)
			{
				if (In.InputName == InputName)
				{
					return &In;
				}
			}
			FCustomInput& NewIn = Custom->Inputs.AddDefaulted_GetRef();
			NewIn.InputName = InputName;
			bChanged = true;
			return &NewIn;
		};

		{
			FCustomInput* ShadowMapIn = FindOrCreateInput(TEXT("MMDShadowMap"));
			if (!ShadowMapIn->Input.Expression)
			{
				UMaterialExpressionTextureObject* TexObj = NewObject<UMaterialExpressionTextureObject>(Material);
				TexObj->Texture = ShadowRT;
				Material->GetExpressionCollection().Expressions.Add(TexObj);
				TexObj->Material = Material;
				ShadowMapIn->Input.Expression = TexObj;
				ShadowMapIn->Input.OutputIndex = 0;
				bChanged = true;
			}
		}

		auto EnsureFloatInput = [&](const FName& InputName, float Value)
		{
			FCustomInput* In = FindOrCreateInput(InputName);
			if (!In->Input.Expression)
			{
				UMaterialExpressionConstant* Const = NewObject<UMaterialExpressionConstant>(Material);
				Const->R = Value;
				Material->GetExpressionCollection().Expressions.Add(Const);
				Const->Material = Material;
				In->Input.Expression = Const;
				In->Input.OutputIndex = 0;
				bChanged = true;
			}
		};

		// 阴影默认开启（无平行光/子系统关闭时相机基 Valid=0，SampleMMDShadow 自动返回 1）
		EnsureFloatInput(TEXT("MMDShadowBias"), 0.0f);

		if (bChanged)
		{
			Material->PostEditChange();
			Material->MarkPackageDirty();

			FString FileName;
			if (FPackageName::TryConvertLongPackageNameToFilename(Material->GetOutermost()->GetName(), FileName, FPackageName::GetAssetPackageExtension()))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
				SaveArgs.SaveFlags = SAVE_NoError;
				UPackage::SavePackage(Material->GetOutermost(), Material, *FileName, SaveArgs);
			}

			ModifiedCount++;
			UE_LOG(LogTemp, Log, TEXT("[MMDMigrateShadow] Updated %s"), *Material->GetName());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[MMDMigrateShadow] Done. Modified %d material(s)."), ModifiedCount);
	return 0;
}
