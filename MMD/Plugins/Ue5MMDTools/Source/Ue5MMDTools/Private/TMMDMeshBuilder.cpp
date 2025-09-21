#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
//构建
#include "ImportUtils/SkelImport.h"                  
#include "ImportUtils/SkeletalMeshImportUtils.h"       
#include "MeshUtilities.h"                             
#include "Engine/SkinnedAssetCommon.h"     
#include "Rendering/SkeletalMeshModel.h"             
#include "Rendering/SkeletalMeshLODModel.h"           
#include "Components/SkinnedMeshComponent.h"  
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
FString FixMMDName(const FString& InName, const FString& Prefix = TEXT(""))
{
    FString Name = InName;
    Name = Name.Replace(TEXT(" "), TEXT("_"))
        .Replace(TEXT("."), TEXT("_"))
        .Replace(TEXT("-"), TEXT("_"))
        .Replace(TEXT("("), TEXT("_"))
        .Replace(TEXT(")"), TEXT("_"))
        .Replace(TEXT("["), TEXT("_"))
        .Replace(TEXT("]"), TEXT("_"))
        .Replace(TEXT("中"), TEXT("ZH"))
        .Replace(TEXT("文"), TEXT("WEN"))
        .Replace(TEXT("<"), TEXT("_"))
        .Replace(TEXT(">"), TEXT("_"))
        .Replace(TEXT(":"), TEXT("_"))
        .Replace(TEXT("*"), TEXT("_"))
        .Replace(TEXT("?"), TEXT("_"))
        .Replace(TEXT("\""), TEXT("_"))
        .Replace(TEXT("|"), TEXT("_"))
        .Replace(TEXT(","), TEXT("_"))
        .Replace(TEXT("&"), TEXT("_"))
        .Replace(TEXT("!"), TEXT("_"))
        .Replace(TEXT("~"), TEXT("_"))
        .Replace(TEXT("@"), TEXT("_"))
        .Replace(TEXT("#"), TEXT("_"))
        .Replace(TEXT("'"), TEXT("_"));
    while (Name.Contains(TEXT("__"))) Name = Name.Replace(TEXT("__"), TEXT("_"));
    if (!Name.IsEmpty() && !FChar::IsAlpha(Name[0])) Name = Prefix + Name;
    if (Name.IsEmpty()) Name = Prefix + TEXT("Unknown");
    return Name;
}
UTexture2D* CreateTextureFromFile(const FString& TexturePath, const FString& OutPath, const FString& AssetName) {

    if (!FPaths::FileExists(TexturePath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Texture file does not exist: %s"), *TexturePath);
        return nullptr;
    }

    UE_LOG(LogTemp, Warning, TEXT("=== CreateTextureFromFile Debug ==="));
    UE_LOG(LogTemp, Warning, TEXT("原始资源名: %s"), *AssetName);

    FString CleanAssetName = FixMMDName(AssetName, TEXT("M_"));
    

    UE_LOG(LogTemp, Warning, TEXT("清理后的资源名: %s"), *CleanAssetName);

    FString SafeOutPath = FixMMDName(OutPath);
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

    FString CleanMaterialName = FixMMDName(MaterialName, TEXT("M_"));

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
#pragma region 顶点
FVector3f ConvertPMXVectorToUnreal(const FVector& PMXVector) {
    return FVector3f(PMXVector.Z * 8.0f, PMXVector.X * 8.0f, PMXVector.Y * 8.0f);
}
FVector3f ConvertPMXBonePositionToUnreal(const FVector& PMXPosition, float Scale = 8.0f) {
    return FVector3f(PMXPosition.Z * Scale, PMXPosition.X * Scale, PMXPosition.Y * Scale);
}
#pragma endregion


void LoadPMXImportData(FSkeletalMeshImportData& PMXImportData, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
    FString PMXPath = FPaths::GetPath(PMXFilePath);
    FString PMXModelName = FPaths::GetBaseFilename(PMXFilePath);
    PMXImportData.bHasNormals = false;
	PMXImportData.bHasTangents = false;
	PMXImportData.bHasVertexColors = false;
	PMXImportData.NumTexCoords = 1 + PMXInfo.PMXGlobals.ExtraUV; // 主UV加上额外的UV
    PMXImportData.MaxMaterialIndex = PMXInfo.ModelMaterialCount;

#pragma region 材质
    PMXImportData.Materials.Reserve(PMXInfo.ModelMaterials.Num());

    // 确保至少有一个默认材质
    if (PMXInfo.ModelMaterials.Num() == 0) {
        SkeletalMeshImportData::FMaterial DefaultMaterial;
        DefaultMaterial.MaterialImportName = TEXT("DefaultMaterial");
        DefaultMaterial.Material = nullptr; // 使用引擎默认材质
        PMXImportData.Materials.Add(DefaultMaterial);
        UE_LOG(LogTemp, Warning, TEXT("没有材质，创建默认材质"));
    }
    else {
        for (int32 MaterialIndex = 0; MaterialIndex < PMXInfo.ModelMaterials.Num(); ++MaterialIndex) {
            const auto& Material = PMXInfo.ModelMaterials[MaterialIndex];
            SkeletalMeshImportData::FMaterial MaterialData;

            // 清理材质名称
            FString CleanMaterialName = Material.NameEN.IsEmpty() ? Material.NameJP : Material.NameEN;
            CleanMaterialName = FixMMDName(CleanMaterialName, TEXT("M_"));

            MaterialData.MaterialImportName = CleanMaterialName;

            FString TexturePath = GetMaterialTexturePath(Material, PMXInfo, PMXPath);

            // 只有当纹理存在时才创建材质
            if (!TexturePath.IsEmpty() && FPaths::FileExists(TexturePath)) {
                FString CleanFileName = FPaths::GetCleanFilename(TexturePath);
                // 也要清理文件名
                CleanFileName = CleanFileName.Replace(TEXT(" "), TEXT("_"));
                CleanFileName = CleanFileName.Replace(TEXT("-"), TEXT("_"));

                UTexture2D* Texture = CreateTextureFromFile(TexturePath,
                    FString("/Game/MMDModels/") + PMXModelName + FString("/Textures"),
                    CleanFileName);

                if (Texture) {
                    MaterialData.Material = CreateMaterialFromTexture(*Texture,
                        CleanMaterialName,
                        FString("/Game/MMDModels/") + PMXModelName + FString("/Materials"));
                }
            }

            PMXImportData.Materials.Add(MaterialData);
        }
    }

    PMXImportData.MaxMaterialIndex = FMath::Max(0, PMXImportData.Materials.Num() - 1);

    UE_LOG(LogTemp, Warning, TEXT("材质处理完成，共 %d 个材质"), PMXImportData.Materials.Num());
#pragma endregion

#pragma region Wedges
	PMXImportData.Wedges.Reserve(PMXInfo.ModelIndicesCount);
    for (int32 i = 0; i < PMXInfo.ModelIndicesCount; i++) {
		int32 VertexIndex = PMXInfo.ModelIndices[i];
		const PMXVertex& Vertex = PMXInfo.ModelVertices[VertexIndex];

		SkeletalMeshImportData::FVertex Wedge;
		Wedge.VertexIndex = VertexIndex;
		Wedge.UVs[0] = FVector2f(Vertex.UV.X,Vertex.UV.Y); 
		for (int32 UVIndex = 0; UVIndex < Vertex.AdditionalUVs.Num() && UVIndex < 7; UVIndex++)
		{
            if (Vertex.AdditionalUVs.IsValidIndex(UVIndex))
            {
                Wedge.UVs[UVIndex + 1] = FVector2f(
                    Vertex.AdditionalUVs[UVIndex].X,
                    Vertex.AdditionalUVs[UVIndex].Y
                );
            }
		}
        Wedge.Color = FColor::White;
        PMXImportData.Wedges.Add(Wedge);
    }
#pragma endregion

#pragma region 顶点Points
    PMXImportData.Points.Reserve(PMXInfo.ModelVertices.Num());

    for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
        const PMXVertex& Vertex = PMXInfo.ModelVertices[i];
        PMXImportData.Points.Add(ConvertPMXVectorToUnreal(Vertex.Position));
        UE_LOG(LogTemp, Warning, TEXT("顶点位置: Pos=(%.4f,%.4f,%.4f)"), ConvertPMXVectorToUnreal(Vertex.Position).X, ConvertPMXVectorToUnreal(Vertex.Position).Y, ConvertPMXVectorToUnreal(Vertex.Position).Z);
    }
    UE_LOG(LogTemp, Warning, TEXT("生成顶点数: %d"), PMXImportData.Points.Num());

    UE_LOG(LogTemp, Warning, TEXT("生成PointToRawMap数: %d"), PMXImportData.PointToRawMap.Num());
#pragma endregion

#pragma region 面
    PMXImportData.Faces.Reserve(PMXInfo.ModelIndicesCount / 3);
    int32 BaseIndex = 0;
    for (int32 MatIndex = 0; MatIndex < PMXInfo.ModelMaterials.Num(); MatIndex++)
    {
        const PMXMaterial& Material = PMXInfo.ModelMaterials[MatIndex];
        int32 FaceIndexCount = Material.FaceIndexCount;
        int32 TriangleCount = FaceIndexCount / 3;

        for (int32 f = 0; f < TriangleCount; f++)
        {
            // 每个三角形对应 3 个 Wedge
            int32 w0 = BaseIndex + f * 3 + 0;
            int32 w1 = BaseIndex + f * 3 + 1;
            int32 w2 = BaseIndex + f * 3 + 2;

            // wedge 里记录的是顶点号
            int32 idx0 = PMXImportData.Wedges[w0].VertexIndex;
            int32 idx1 = PMXImportData.Wedges[w1].VertexIndex;
            int32 idx2 = PMXImportData.Wedges[w2].VertexIndex;

            // 过滤掉退化三角形
            if (idx0 == idx1 || idx1 == idx2 || idx0 == idx2) {
                UE_LOG(LogTemp, Error, TEXT("退化三角形: 顶点索引 %d, %d, %d (MatIndex=%d, Face=%d)"), idx0, idx1, idx2, MatIndex, f);
                continue;
            }

            // 越界检查
            if (idx0 < 0 || idx1 < 0 || idx2 < 0 ||
                idx0 >= PMXInfo.ModelVertices.Num() ||
                idx1 >= PMXInfo.ModelVertices.Num() ||
                idx2 >= PMXInfo.ModelVertices.Num()) {
                UE_LOG(LogTemp, Error, TEXT("三角面索引越界: %d, %d, %d (MatIndex=%d, Face=%d)"), idx0, idx1, idx2, MatIndex, f);
                continue;
            }

            SkeletalMeshImportData::FTriangle Triangle;
            Triangle.WedgeIndex[0] = w0;
            Triangle.WedgeIndex[1] = w1;
            Triangle.WedgeIndex[2] = w2;
            Triangle.MatIndex = MatIndex;
            Triangle.AuxMatIndex = 0;
            Triangle.SmoothingGroups = 1;

            PMXImportData.Faces.Add(Triangle);
        }

        BaseIndex += FaceIndexCount;
    }
#pragma endregion

#pragma region 骨骼Bone
    // 建立一个集合防止骨骼重名
    TSet<FString> BoneNameSet;
    {
        SkeletalMeshImportData::FBone Root;
        Root.Name = TEXT("Root");
        Root.ParentIndex = INDEX_NONE;
        Root.NumChildren = 0;
        Root.BonePos.Transform = FTransform3f::Identity;
        Root.BonePos.Length = Root.BonePos.XSize = Root.BonePos.YSize = 1;
        PMXImportData.RefBonesBinary.Add(Root);
        BoneNameSet.Add(Root.Name);
    }

    for (int32 i = 0; i < PMXInfo.ModelBoneCount; ++i) {
        const PMXBone& Bone = PMXInfo.ModelBones[i];
        SkeletalMeshImportData::FBone NewBone;

        ;

        // 确保名字唯一
        int32 Suffix = 1;
        FString UniqueName = Bone.NameJP;
        while (BoneNameSet.Contains(UniqueName)) {
            UniqueName = Bone.NameJP + FString::Printf(TEXT("_%d"), Suffix++);
        }
        BoneNameSet.Add(UniqueName);

        NewBone.Name = UniqueName;
        int32 Parent = Bone.ParentBoneIndex;
        if (Parent >= 0 && Parent < PMXInfo.ModelBoneCount && Parent != i) {
            NewBone.ParentIndex = Parent + 1; // +1 因为Root
        }
        else {
            NewBone.ParentIndex = 0; // 默认挂在Root
        }
        FVector3f BoneGlobalPos = ConvertPMXBonePositionToUnreal(Bone.Position);

        // 计算相对父骨骼的位置（local）
        FVector3f BoneLocalPos = BoneGlobalPos;
        if (Bone.ParentBoneIndex >= 0 && Bone.ParentBoneIndex < PMXInfo.ModelBoneCount) {
            const PMXBone& ParentPMXBone = PMXInfo.ModelBones[Bone.ParentBoneIndex];
            FVector3f ParentGlobalPos = ConvertPMXBonePositionToUnreal(ParentPMXBone.Position);
            BoneLocalPos = BoneGlobalPos - ParentGlobalPos;
        }
        NewBone.BonePos.Transform = FTransform3f(FQuat4f::Identity, BoneLocalPos);
		UE_LOG(LogTemp, Warning, TEXT("Bone[%d] %s Parent=%d Pos=(%.4f,%.4f,%.4f)"),
			i, *NewBone.Name, NewBone.ParentIndex,
			NewBone.BonePos.Transform.GetLocation().X,
			NewBone.BonePos.Transform.GetLocation().Y,
			NewBone.BonePos.Transform.GetLocation().Z);
        NewBone.BonePos.Length = NewBone.BonePos.XSize = NewBone.BonePos.YSize = 1;

        if (NewBone.Name == TEXT("頭")) {
            int32 ParentIdx = Bone.ParentBoneIndex;
            if (ParentIdx >= 0 && ParentIdx < PMXInfo.ModelBoneCount) {
                const PMXBone& ParentBone = PMXInfo.ModelBones[ParentIdx];
                FVector3f ParentGlobalPos = ConvertPMXBonePositionToUnreal(ParentBone.Position);
                UE_LOG(LogTemp, Warning, TEXT("头骨父骨: %s, 全局位置: (%.4f,%.4f,%.4f)"),
                    *ParentBone.NameJP, ParentGlobalPos.X, ParentGlobalPos.Y, ParentGlobalPos.Z);
            }
        }
        PMXImportData.RefBonesBinary.Add(NewBone);
    }
#pragma endregion

#pragma region 骨骼权重RawBoneInfluence
    TArray<TArray<TPair<int32, float>>> VertexInfluences;
    VertexInfluences.SetNum(PMXInfo.ModelVertexCount);

    for (int32 i = 0; i < PMXInfo.ModelVertexCount; ++i) {
        const PMXVertex& Vertex = PMXInfo.ModelVertices[i];
        const PMXVertexWeight& Weight = Vertex.Weight;

        switch (Weight.WeightDeformType)
        {
        case 0: // BDEF1
            if (Weight.BoneIndices[0] >= 0) {
                VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, 1.0f));
            }
            break;
        case 1: // BDEF2
        case 3: // SDEF (basically two bones)
        {
            if (Weight.BoneIndices[0] >= 0) {
                float w0 = Weight.Weights[0];
                VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, w0));
            }
            if (Weight.BoneIndices[1] >= 0) {
                float w1 = 1.0f - Weight.Weights[0];
                VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[1] + 1, w1));
            }
        }
        break;
        case 2: // BDEF4
        case 4: // QDEF
            for (int j = 0; j < 4; ++j) {
                if (Weight.BoneIndices[j] >= 0 && Weight.Weights[j] > 0.0f) {
                    VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[j] + 1, Weight.Weights[j]));
                }
            }
            break;
        default:
            // 绑定到 Root（0）
            VertexInfluences[i].Add(TPair<int32, float>(0, 1.0f));
            break;
        }
    }

    // 归一化并写入 PMXImportData.Influences
    for (int32 i = 0; i < VertexInfluences.Num(); ++i) {
        float Sum = 0.0f;
        for (auto& P : VertexInfluences[i]) Sum += P.Value;
        if (Sum <= 0.0f) {
            // 保底绑定到 Root
            SkeletalMeshImportData::FRawBoneInfluence Inf;
            Inf.VertexIndex = i;
            Inf.BoneIndex = 0;
            Inf.Weight = 1.0f;
            PMXImportData.Influences.Add(Inf);
            continue;
        }
        // 写入并归一化
        for (auto& P : VertexInfluences[i]) {
            SkeletalMeshImportData::FRawBoneInfluence Inf;
            Inf.VertexIndex = i;              // 顶点索引（对应 PMXImportData.Points 的索引）
            Inf.BoneIndex = P.Key;            // 已经 +1 以对应 Root 在前的 RefBonesBinary
            Inf.Weight = P.Value / Sum;
            if (Inf.Weight > KINDA_SMALL_NUMBER) {
                PMXImportData.Influences.Add(Inf);
            }
        }
    }
#pragma endregion

#pragma region 顶点映射
    PMXImportData.PointToRawMap.Reserve(PMXInfo.ModelVertices.Num());
    for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
        PMXImportData.PointToRawMap.Add(i);
    }
#pragma endregion



#pragma region MeshInfo
    SkeletalMeshImportData::FMeshInfo MeshInfo;
    MeshInfo.Name = FName(*PMXModelName);
    MeshInfo.NumVertices = PMXInfo.ModelVertices.Num();
    MeshInfo.StartImportedVertex = 0;
    PMXImportData.MeshInfos.Add(MeshInfo);
#pragma endregion

    UE_LOG(LogTemp, Warning, TEXT("LoadPMXImportData 完成: 顶点=%d, 面=%d, 骨骼=%d, Influences=%d"),
        PMXImportData.Points.Num(), PMXImportData.Faces.Num(),
        PMXImportData.RefBonesBinary.Num(), PMXImportData.Influences.Num());
}


USkeletalMesh* TMMDMeshBuilder::BuildSkeletalMeshFromPMX(const PMXDatas& PMXInfo, const FString& PackagePath, const FString& AssetName, const FString& PMXFilePath)
{
    FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
    FString CleanAssetName = FixMMDName(AssetName);

    UE_LOG(LogTemp, Warning, TEXT("=== 开始构建骨骼网格：%s -> %s ==="), *AssetName, *CleanAssetName);

    FString BasePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/");
    FString ModelPath = BasePath + TEXT("Model/");
    FString SkeletonPath = BasePath + TEXT("Skeleton/");

    FString PackageName = ModelPath + CleanAssetName;
    UPackage* Package = CreatePackage(*PackageName);
    USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, *CleanAssetName, RF_Public | RF_Standalone);

    FSkeletalMeshImportData PMXImportData;
    LoadPMXImportData(PMXImportData, PMXInfo, PMXFilePath);

    if (PMXImportData.Points.Num() == 0 || PMXImportData.Faces.Num() == 0) {
        UE_LOG(LogTemp, Error, TEXT("导入数据无效"));
        return nullptr;
    }

    FString SkeletonName = CleanAssetName + TEXT("_Skeleton");
    FString SkeletonPackageName = SkeletonPath + SkeletonName;
    UPackage* SkeletonPackage = CreatePackage(*SkeletonPackageName);
    USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, *SkeletonName, RF_Public | RF_Standalone);

    int32 SkeletalDepth = 0;
    FReferenceSkeleton RefSkeleton;

    SkeletalMeshImportUtils::ProcessImportMeshInfluences(PMXImportData, CleanAssetName);
    SkeletalMeshImportUtils::ProcessImportMeshMaterials(SkeletalMesh->GetMaterials(), PMXImportData);
    SkeletalMeshImportUtils::ProcessImportMeshSkeleton(Skeleton, RefSkeleton, SkeletalDepth, PMXImportData);

    SkeletalMesh->SetRefSkeleton(RefSkeleton);

    if (RefSkeleton.GetNum() > 0) {
        Skeleton->RecreateBoneTree(SkeletalMesh);
    }

    if (SkeletalMesh->GetLODNum() == 0)
    {
        FSkeletalMeshLODInfo LODInfo;
        LODInfo.ScreenSize.Default = 1.0f;
        LODInfo.LODHysteresis = 0.02f;

        LODInfo.BuildSettings.bRecomputeNormals = true;
        LODInfo.BuildSettings.bRecomputeTangents = true;
        LODInfo.BuildSettings.bUseMikkTSpace = true;
        LODInfo.BuildSettings.bComputeWeightedNormals = true;
        LODInfo.BuildSettings.bRemoveDegenerates = true;
        LODInfo.BuildSettings.bUseFullPrecisionUVs = false;
        LODInfo.BuildSettings.bUseHighPrecisionTangentBasis = false;

        LODInfo.bAllowCPUAccess = true;
        LODInfo.bSupportUniformlyDistributedSampling = false;

        SkeletalMesh->AddLODInfo(LODInfo);
        UE_LOG(LogTemp, Warning, TEXT("LODInfo 创建完成"));
    }

    FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
    if (!ImportedModel) {
        SkeletalMesh->AllocateResourceForRendering();
        ImportedModel = SkeletalMesh->GetImportedModel();
    }

    if (ImportedModel->LODModels.Num() == 0) {
        ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
    }

    FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];

    // 使用 MeshUtilities 构建骨骼网格
    IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

    IMeshUtilities::MeshBuildOptions BuildOptions;
    BuildOptions.bComputeNormals = true;
    BuildOptions.bComputeTangents = true;
    BuildOptions.bUseMikkTSpace = true;
    BuildOptions.bComputeWeightedNormals = true;
    BuildOptions.bRemoveDegenerateTriangles = true;

    TArray<FVector3f> LODPoints;
    TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
    TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
    TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
    TArray<int32> LODPointToRawMap;

    PMXImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

    UE_LOG(LogTemp, Warning, TEXT("LOD数据: 顶点=%d, 楔形点=%d, 面=%d, 影响=%d"),
        LODPoints.Num(), LODWedges.Num(), LODFaces.Num(), LODInfluences.Num());

    bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(
        LODModel,
        CleanAssetName,
        RefSkeleton,
        LODInfluences,
        LODWedges,
        LODFaces,
        LODPoints,
        LODPointToRawMap,
        BuildOptions
    );

    if (!bBuildSuccess) {
        UE_LOG(LogTemp, Error, TEXT("骨骼网格构建失败"));
        return nullptr;
    }

    LODModel.NumTexCoords = FMath::Max<uint32>(1, PMXImportData.NumTexCoords);


    SkeletalMesh->SetSkeleton(Skeleton);
    Skeleton->SetPreviewMesh(SkeletalMesh);

    SkeletalMesh->CalculateInvRefMatrices();

    const TArray<FMatrix44f>& RefBasesInvMatrix = SkeletalMesh->GetRefBasesInvMatrix();
    UE_LOG(LogTemp, Warning, TEXT("RefBasesInvMatrix 数量: %d"), RefBasesInvMatrix.Num());

    if (RefBasesInvMatrix.Num() == 0) {
        UE_LOG(LogTemp, Error, TEXT("RefBasesInvMatrix 未正确初始化！"));
    }

    if (PMXImportData.Points.Num() > 0) {
        FBox BoundingBox(ForceInit);
        for (const FVector3f& Point : PMXImportData.Points) {
            BoundingBox += FVector(Point);
        }
        FBoxSphereBounds ActualBounds(BoundingBox);
        SkeletalMesh->SetImportedBounds(ActualBounds);
    }
    else {
        FBoxSphereBounds DefaultBounds(FBox(FVector(-100, -100, -100), FVector(100, 100, 100)));
        SkeletalMesh->SetImportedBounds(DefaultBounds);
    }

    // 完成构建和初始化
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    // 注册资源
    FAssetRegistryModule::AssetCreated(SkeletalMesh);
    FAssetRegistryModule::AssetCreated(Skeleton);
    SkeletonPackage->MarkPackageDirty();

    UE_LOG(LogTemp, Log, TEXT("骨骼网格创建成功: %s"), *PackageName);
    return SkeletalMesh;
}
