#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"

#include "ImportUtils/SkelImport.h"                  
#include "ImportUtils/SkeletalMeshImportUtils.h"       
#include "MeshUtilities.h"                             
#include "Engine/SkinnedAssetCommon.h"                
//材质
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialInstanceConstant.h"
//转换
#include "Factories/TextureFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"

#pragma region 材质贴图
UTexture2D* CreateTextureFromFile(const FString& TexturePath, const FString& OutPath, const FString& AssetName) {

    if (!FPaths::FileExists(TexturePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Texture file does not exist: %s"), *TexturePath);
        return nullptr;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== CreateTextureFromFile Debug ==="));
    UE_LOG(LogTemp, Warning, TEXT("原始资源名: %s"), *AssetName);

    FString CleanAssetName = AssetName;
    CleanAssetName = CleanAssetName.Replace(TEXT(" "), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("."), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("-"), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("("), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT(")"), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("["), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("]"), TEXT("_"));
    CleanAssetName = CleanAssetName.Replace(TEXT("中"), TEXT("ZH"));
    CleanAssetName = CleanAssetName.Replace(TEXT("文"), TEXT("WEN"));

    while (CleanAssetName.Contains(TEXT("__"))) {
        CleanAssetName = CleanAssetName.Replace(TEXT("__"), TEXT("_"));
    }

    if (!CleanAssetName.IsEmpty() && !FChar::IsAlpha(CleanAssetName[0])) {
        CleanAssetName = TEXT("T_") + CleanAssetName;
    }

    if (CleanAssetName.IsEmpty()) {
        CleanAssetName = TEXT("T_Unknown_Texture");
    }

    UE_LOG(LogTemp, Warning, TEXT("清理后的资源名: %s"), *CleanAssetName);

    FString SafeOutPath = OutPath;
    if (!SafeOutPath.EndsWith(TEXT("/"))) {
        SafeOutPath += TEXT("/");
    }
    FString PackageName = SafeOutPath + CleanAssetName;

    PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));

    UE_LOG(LogTemp, Warning, TEXT("最终包名: %s"), *PackageName);

    if (PackageName.Contains(TEXT(" "))) {
        UE_LOG(LogTemp, Error, TEXT("包名仍然包含空格，这会导致错误: %s"), *PackageName);
        return nullptr;
    }
    UTextureFactory::SuppressImportOverwriteDialog(true);
    UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
    TextureFactory->bCreateMaterial = false;

    TArray<uint8> FileData;
    if (!FFileHelper::LoadFileToArray(FileData, *TexturePath)) {
        UE_LOG(LogTemp, Warning, TEXT("Failed to load texture file: %s"), *TexturePath);
        return nullptr;
    }

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package) {
        UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
        return nullptr;
    }

    const uint8* FileBuffer = FileData.GetData();

    UTexture2D* ImportedTexture = Cast<UTexture2D>(TextureFactory->FactoryCreateBinary(
        UTexture2D::StaticClass(),
        Package,
        FName(*CleanAssetName),  // 使用清理后的名称
        RF_Public | RF_Standalone,
        nullptr,
        *FPaths::GetExtension(TexturePath),
        FileBuffer,
        FileBuffer + FileData.Num(),
        nullptr)
    );

    if (ImportedTexture) {
        FAssetRegistryModule::AssetCreated(ImportedTexture);
        Package->MarkPackageDirty();
        UE_LOG(LogTemp, Log, TEXT("Successfully imported texture: %s"), *PackageName);
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("Failed to import texture: %s"), *PackageName);
    }

    return ImportedTexture;
}

FString GetMaterialTexturePath(const PMXMaterial& Material, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
    if (Material.TextureIndex >= 0 && Material.TextureIndex < PMXInfo.ModelTextureCount) {
        FString PMXDirectory = PMXFilePath;

        FString RelativeTexturePath = PMXInfo.ModelTexturePaths[Material.TextureIndex];
        // 使用 FPaths::Combine 安全拼接路径
        FString FullTexturePath = FPaths::Combine(PMXDirectory, RelativeTexturePath);

        UE_LOG(LogTemp, Warning, TEXT("拼接后的完整路径: '%s'"), *FullTexturePath);
        UE_LOG(LogTemp, Warning, TEXT("文件是否存在: %s"), FPaths::FileExists(FullTexturePath) ? TEXT("是") : TEXT("否"));

        return FullTexturePath;
    }
    else if (Material.TextureIndex == -1) {
        // 没有贴图
        return FString();
    }
    else {
        UE_LOG(LogTemp, Warning, TEXT("Invalid texture index %d for material %s"), Material.TextureIndex, *Material.NameEN);
        return FString();
    }
}
UMaterialInterface* CreateMaterialFromTexture(UTexture2D& Texture2D, const FString& MaterialName, const FString& OutPath) {

	static const FString BaseMaterialPath = TEXT("/Ue5MMDTools/Resources/MaterialInstance/Mat_MMD_Base.Mat_MMD_Base");
	UMaterial* BaseMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *BaseMaterialPath));

	if (!BaseMaterial) {
		UE_LOG(LogTemp, Error, TEXT("Failed to load base material from path: %s"), *BaseMaterialPath);
		return nullptr;
	}

	FString CleanMaterialName = MaterialName;
    CleanMaterialName = CleanMaterialName.Replace(TEXT(" "), TEXT("_"));
    if (!CleanMaterialName.IsEmpty() && !FChar::IsAlpha(CleanMaterialName[0]))
        CleanMaterialName = TEXT("M_") + CleanMaterialName;
    if (CleanMaterialName.IsEmpty())
        CleanMaterialName = TEXT("M_Unknown");

    FString SafeOutPath = OutPath;
    if(!SafeOutPath.EndsWith(TEXT("/")))
        SafeOutPath += TEXT("/");
    FString PackageName = SafeOutPath + CleanMaterialName;
    PackageName =PackageName.Replace(TEXT("//"), TEXT("/"));
    PackageName = PackageName.Replace(TEXT(" "), TEXT("_"));

    UPackage* Package = CreatePackage(*PackageName);
    if (!Package)
    {
        UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
        return nullptr;
    }
	UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(Package, *CleanMaterialName, RF_Public | RF_Standalone);
    MaterialInstance->SetParentEditorOnly(BaseMaterial);

	FMaterialParameterInfo ParamInfo("BaseColorMap");
    MaterialInstance->SetTextureParameterValueEditorOnly(ParamInfo, &Texture2D);

    FAssetRegistryModule::AssetCreated(MaterialInstance);
    Package->MarkPackageDirty();

    return MaterialInstance;
}

#pragma endregion


void LoadPMXImportData(FSkeletalMeshImportData& PMXImportData, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
#pragma region 材质
    FString PMXPath=FPaths::GetPath(PMXFilePath);
    FString PMXModelName = FPaths::GetBaseFilename(PMXFilePath);
	for (const auto& Material : PMXInfo.ModelMaterials) {
		SkeletalMeshImportData::FMaterial MaterialData;
		MaterialData.MaterialImportName = Material.NameEN.IsEmpty()?Material.NameJP:Material.NameEN;
		FString TexturePath = GetMaterialTexturePath(Material, PMXInfo, PMXPath);
		UTexture2D* Texture = CreateTextureFromFile(TexturePath, FString("/Game/MMDModels/")+ PMXModelName +FString("/Textures"), FPaths::GetCleanFilename(TexturePath));
        MaterialData.Material = CreateMaterialFromTexture(*Texture, MaterialData.MaterialImportName, FString("/Game/MMDModels/") + PMXModelName + FString("/Materials"));
	}
#pragma endregion

}

USkeletalMesh* TMMDMeshBuilder::BuildSkeletalMeshFromPMX(const PMXDatas& PMXInfo, const FString& PackagePath, const FString& AssetName,const FString& PMXFilePath)
{

	//创建数据包和对象
	FString PackageName = PackagePath + TEXT("/") + AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, *AssetName, RF_Public | RF_Standalone);

	//2.填充FSkeletalMeshImportData

	FSkeletalMeshImportData PMXImportData;
	LoadPMXImportData(PMXImportData, PMXInfo, PMXFilePath);


    return nullptr;
}
