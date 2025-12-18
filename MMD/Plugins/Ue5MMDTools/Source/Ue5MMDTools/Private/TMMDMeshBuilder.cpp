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
//动画蓝图
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
//IKRig
#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#include "Rig/Solvers/IKRig_FBIKSolver.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargeterController.h"   
#endif
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

FString GetMaterialTexturePath(const FPMXMaterial& Material, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
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
	if (!SafeOutPath.EndsWith(TEXT("/")))
		SafeOutPath += TEXT("/");
	FString PackageName = SafeOutPath + CleanMaterialName;
	PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));
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
	FVector3f TempPos(PMXVector.Z * 8.0f, PMXVector.X * 8.0f, PMXVector.Y * 8.0f);

	return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
}
FVector3f ConvertPMXBonePositionToUnreal(const FVector& PMXPosition, float Scale = 8.0f) {
	FVector3f TempPos(PMXPosition.Z * Scale, PMXPosition.X * Scale, PMXPosition.Y * Scale);

	return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
}
static FVector3f ConvertPMXNormalToUnreal(const FVector& PMXNormal, bool bForceFlip = true)
{
	// 与 Position 同轴交换 (Z,X,Y)，不缩放
	FVector3f N(PMXNormal.Z, PMXNormal.X, PMXNormal.Y);
	float LenSq = N.SizeSquared();
	if (LenSq > KINDA_SMALL_NUMBER)
		N *= 1.0f / FMath::Sqrt(LenSq);
	else
		N = FVector3f(0, 0, 1);
	if (bForceFlip) N = -N;
	return N;
}
#pragma endregion


void LoadPMXImportData(FSkeletalMeshImportData& PMXImportData, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
	FString PMXPath = FPaths::GetPath(PMXFilePath);
	FString PMXModelName = FPaths::GetBaseFilename(PMXFilePath);
	PMXImportData.bHasNormals = true;
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
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[VertexIndex];
		//int32 InvalidIndexCount = 0;
		//if (VertexIndex < 0 || VertexIndex >= VertexIndex) {
		//	UE_LOG(LogTemp, Error, TEXT("Invalid vertex index at ModelIndices[%d]: %d (VertexCount=%d)"),
		//		i, VertexIndex, VertexIndex);
		//	InvalidIndexCount++;
		//	continue; // 跳过无效索引
		//}
		SkeletalMeshImportData::FVertex Wedge;
		Wedge.VertexIndex = VertexIndex;
		Wedge.UVs[0] = FVector2f(Vertex.UV.X, Vertex.UV.Y);
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
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		PMXImportData.Points.Add(ConvertPMXVectorToUnreal(Vertex.Position));
	}
#pragma endregion

#pragma region 面
	PMXImportData.Faces.Reserve(PMXInfo.ModelIndicesCount / 3);
	int32 BaseIndex = 0;
	int32 ZeroNormalCount = 0;
	for (int32 MatIndex = 0; MatIndex < PMXInfo.ModelMaterials.Num(); MatIndex++)
	{
		const FPMXMaterial& Material = PMXInfo.ModelMaterials[MatIndex];
		int32 FaceIndexCount = Material.FaceIndexCount;
		int32 TriangleCount = FaceIndexCount / 3;

		for (int32 f = 0; f < TriangleCount; f++)
		{
			int32 w0 = BaseIndex + f * 3 + 0;
			int32 w1 = BaseIndex + f * 3 + 1;
			int32 w2 = BaseIndex + f * 3 + 2;

			int32 vi0 = PMXInfo.ModelIndices[w0];
			int32 vi1 = PMXInfo.ModelIndices[w1];
			int32 vi2 = PMXInfo.ModelIndices[w2];

			// 退化剔除
			if (vi0 == vi1 || vi1 == vi2 || vi0 == vi2)
				continue;
			if (vi0 < 0 || vi1 < 0 || vi2 < 0 ||
				vi0 >= PMXInfo.ModelVertices.Num() ||
				vi1 >= PMXInfo.ModelVertices.Num() ||
				vi2 >= PMXInfo.ModelVertices.Num())
				continue;

			SkeletalMeshImportData::FTriangle Tri;
			Tri.WedgeIndex[0] = w0;
			Tri.WedgeIndex[1] = w1;
			Tri.WedgeIndex[2] = w2;
			Tri.MatIndex = MatIndex;
			Tri.AuxMatIndex = 0;
			Tri.SmoothingGroups = 1; // 统一一组；硬边依靠 PMX 已拆分的重复顶点

			const FVector& N0 = PMXInfo.ModelVertices[vi0].Normal;
			const FVector& N1 = PMXInfo.ModelVertices[vi1].Normal;
			const FVector& N2 = PMXInfo.ModelVertices[vi2].Normal;

			Tri.TangentZ[0] = ConvertPMXNormalToUnreal(N0);
			Tri.TangentZ[1] = ConvertPMXNormalToUnreal(N1);
			Tri.TangentZ[2] = ConvertPMXNormalToUnreal(N2);

			if (Tri.TangentZ[0].IsNearlyZero()) { Tri.TangentZ[0] = FVector3f(0, 0, 1); ++ZeroNormalCount; }
			if (Tri.TangentZ[1].IsNearlyZero()) { Tri.TangentZ[1] = FVector3f(0, 0, 1); ++ZeroNormalCount; }
			if (Tri.TangentZ[2].IsNearlyZero()) { Tri.TangentZ[2] = FVector3f(0, 0, 1); ++ZeroNormalCount; }

			PMXImportData.Faces.Add(Tri);
		}
		BaseIndex += FaceIndexCount;
	}

	// 不再调用 ComputeSmoothGroupFromNormals / SplitVertices...
	UE_LOG(LogTemp, Log, TEXT("Original PMX normals applied. Triangles=%d ZeroFixed=%d"),
		PMXImportData.Faces.Num(), ZeroNormalCount);
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
		const FPMXBone& Bone = PMXInfo.ModelBones[i];
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
			const FPMXBone& ParentPMXBone = PMXInfo.ModelBones[Bone.ParentBoneIndex];
			FVector3f ParentGlobalPos = ConvertPMXBonePositionToUnreal(ParentPMXBone.Position);
			BoneLocalPos = BoneGlobalPos - ParentGlobalPos;
		}
		NewBone.BonePos.Transform = FTransform3f(FQuat4f::Identity, BoneLocalPos);
		NewBone.BonePos.Length = NewBone.BonePos.XSize = NewBone.BonePos.YSize = 1;
		PMXImportData.RefBonesBinary.Add(NewBone);
	}
#pragma endregion

#pragma region 骨骼权重RawBoneInfluence
	TArray<TArray<TPair<int32, float>>> VertexInfluences;
	VertexInfluences.SetNum(PMXInfo.ModelVertexCount);

	for (int32 i = 0; i < PMXInfo.ModelVertexCount; ++i) {
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		const FPMXVertexWeight& Weight = Vertex.Weight;

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

		LODInfo.BuildSettings.bRecomputeNormals = false;
		LODInfo.BuildSettings.bRecomputeTangents = true;
		LODInfo.BuildSettings.bUseMikkTSpace = true;
		LODInfo.BuildSettings.bComputeWeightedNormals = false;
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
	BuildOptions.bComputeNormals = false;
	BuildOptions.bComputeTangents = true;
	BuildOptions.bUseMikkTSpace = true;
	BuildOptions.bComputeWeightedNormals = false;
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

UIKRigDefinition* TMMDMeshBuilder::BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
#if WITH_EDITOR
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}
	auto FindMMDBone = [SkeletalMesh](const TArray<FString>& PossibleNames) -> FName
		{
			const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
			for (const FString& BoneName : PossibleNames)
			{
				FName FoundName = FName(*BoneName);
				int32 BoneIndex = RefSkeleton.FindBoneIndex(FoundName);
				if (BoneIndex != INDEX_NONE)
				{
					UE_LOG(LogTemp, Log, TEXT("Found bone: %s"), *BoneName);
					return FoundName;
				}
			}
			return NAME_None;
		};
	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString IKRigPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString IKRigName = PMXModelName + TEXT("_IKRig");

	FString UniquePackageName, UniqueAssetName; {
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(IKRigPath + TEXT("/") + IKRigName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(
		Package,
		UIKRigDefinition::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);
	if (!IKRig)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRigDefinition"));
		return nullptr;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRigController"));
		return nullptr;
	}

	Controller->SetSkeletalMesh(SkeletalMesh);
	UE_LOG(LogTemp, Log, TEXT("Set SkeletalMesh for IKRig: %s"), *SkeletalMesh->GetName());

	FName RetargetRootBone = FindMMDBone({
			TEXT("腰"),      
			TEXT("Waist"),
			TEXT("センター"), 
			TEXT("Center"),
			TEXT("下半身"),    
			TEXT("LowerBody"),
			TEXT("Hips")
		});
	if (RetargetRootBone != NAME_None)
	{
		Controller->SetRetargetRoot(RetargetRootBone);
		UE_LOG(LogTemp, Log, TEXT("✅ Set Retarget Root: %s"), *RetargetRootBone.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find Retarget Root bone"));
	}
	// 添加重定向链
	{
		struct FMMDRetargetChain
		{
			FString ChainName;
			TArray<FString> StartBoneNames;
			TArray<FString> EndBoneNames;
		};
		TArray<FMMDRetargetChain> Chains = {
			{ TEXT("Root"),        { TEXT("Root") }, { TEXT("Root") } },

			{ TEXT("Spine"),       { TEXT("上半身") }, { TEXT("上半身2") } },

			{ TEXT("Neck"),        { TEXT("首") }, { TEXT("首") } },

			{ TEXT("Head"),        { TEXT("頭") }, { TEXT("頭") } },

			{ TEXT("RightArm"),    { TEXT("右腕") }, { TEXT("右手首") } },

			// ✅ 左腕、左手首（日文）- 注意截图是"左肩"开始
			{ TEXT("LeftArm"),     { TEXT("左腕") }, { TEXT("左手首") } },

			// ✅ 右足、右足先（日文）- EX是英文
			{ TEXT("RightLeg"),    { TEXT("右足D") }, { TEXT("右足先EX") } },

			// ✅ 左足、左足先（日文）
			{ TEXT("LeftLeg"),     { TEXT("左足D") }, { TEXT("左足先EX") } },

			// ✅ 手指（日文）- 親指、人指、中指、薬指、小指
			{ TEXT("LeftThumb"),   { TEXT("左親指０") }, { TEXT("左親指２") } },
			{ TEXT("LeftIndex"),   { TEXT("左人指１") }, { TEXT("左人指３") } },
			{ TEXT("LeftMiddle"),  { TEXT("左中指１") }, { TEXT("左中指３") } },
			{ TEXT("LeftRing"),    { TEXT("左薬指１") }, { TEXT("左薬指３") } },
			{ TEXT("LeftPinky"),   { TEXT("左小指１") }, { TEXT("左小指３") } },

			{ TEXT("RightThumb"),  { TEXT("右親指０") }, { TEXT("右親指２") } },
			{ TEXT("RightIndex"),  { TEXT("右人指１") }, { TEXT("右人指３") } },
			{ TEXT("RightMiddle"), { TEXT("右中指１") }, { TEXT("右中指３") } },
			{ TEXT("RightRing"),   { TEXT("右薬指１") }, { TEXT("右薬指３") } },
			{ TEXT("RightPinky"),  { TEXT("右小指１") }, { TEXT("右小指３") } },

			// ✅ 肩（日文）
			{ TEXT("LeftClavicle"), { TEXT("左肩") }, { TEXT("左肩") } },
			{ TEXT("RightClavicle"),{ TEXT("右肩") }, { TEXT("右肩") } }
		};

		int32 ChainCount = 0;
		for (const FMMDRetargetChain& Chain : Chains) {
			FName StartBone = FindMMDBone(Chain.StartBoneNames);
			FName EndBone = FindMMDBone(Chain.EndBoneNames);

			if (StartBone != NAME_None && EndBone != NAME_None)
			{
				FName ChainFName = FName(*Chain.ChainName);

				Controller->AddRetargetChain(ChainFName, StartBone, EndBone, NAME_None);
				ChainCount++;

				UE_LOG(LogTemp, Log, TEXT("✅ Added Retarget Chain: %s (%s -> %s)"),
					*Chain.ChainName, *StartBone.ToString(), *EndBone.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bones for chain: %s"), *Chain.ChainName);
			}
		}
	}
	// 添加IK目标
	{
		struct FMMDIKGoal
		{
			FString GoalName;
			TArray<FString> BoneNames;
		};
		TArray<FMMDIKGoal> IKGoals = {
			// 左脚IK
			{
				TEXT("LeftFoot_IK"),
				{TEXT("左足首"), TEXT("LeftAnkle"), TEXT("LeftFoot")}
			},
			// 右脚IK
			{
				TEXT("RightFoot_IK"),
				{TEXT("右足首"), TEXT("RightAnkle"), TEXT("RightFoot")}
			},
			// 左手IK
			{
				TEXT("LeftHand_IK"),
				{TEXT("左手首"), TEXT("LeftWrist"), TEXT("LeftHand")}
			},
			// 右手IK
			{
				TEXT("RightHand_IK"),
				{TEXT("右手首"), TEXT("RightWrist"), TEXT("RightHand")}
			}
		};
		int32 GoalCount = 0;
		for (const FMMDIKGoal& Goal : IKGoals)
		{
			FName GoalBone = FindMMDBone(Goal.BoneNames);

			if (GoalBone != NAME_None)
			{
				FName GoalName = FName(*Goal.GoalName);

				Controller->AddNewGoal(GoalName, GoalBone);
				GoalCount++;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bone for goal: %s"), *Goal.GoalName);
			}
		}
	}
	// 创建FBIK求解器
	{
		int32 SolverIndex = Controller->AddSolver(UIKRigFBIKSolver::StaticClass());

		if (SolverIndex != INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("✅ Added FBIK Solver at index: %d"), SolverIndex);

			// 连接Goals到求解器
			TArray<FName> GoalNames = {
				TEXT("LeftFoot_IK"),
				TEXT("RightFoot_IK"),
				TEXT("LeftHand_IK"),
				TEXT("RightHand_IK")
			};

			int32 ConnectedCount = 0;
			for (const FName& GoalName : GoalNames)
			{
				bool bConnected = Controller->ConnectGoalToSolver(GoalName, SolverIndex);

				if (bConnected)
				{
					ConnectedCount++;
					UE_LOG(LogTemp, Log, TEXT("✅ Connected Goal to Solver: %s"), *GoalName.ToString());
				}
			}

			UE_LOG(LogTemp, Log, TEXT("📊 Connected %d/%d Goals to Solver"), ConnectedCount, GoalNames.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Failed to add FBIK Solver"));
		}
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(IKRig);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, IKRig, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully saved IKRig: %s"), *FilePath);
		UE_LOG(LogTemp, Log, TEXT("IKRig created with MMD bone mapping"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save IKRig: %s"), *FilePath);
	}

	return IKRig;

#else
	UE_LOG(LogTemp, Error, TEXT("IKRig can only be created in the editor."));
	return nullptr;
#endif
}

UAnimBlueprint* TMMDMeshBuilder::BuildAnimBlueprint(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString AnimBPPackagePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString AnimBPName = PMXModelName + TEXT("_AnimBP");

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton) {
		UE_LOG(LogTemp, Error, TEXT("TargetMesh has no skeleton"));
		return nullptr;
	}

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(AnimBPPackagePath + TEXT("/") + AnimBPName, TEXT(""), UniquePackageName, UniqueAssetName);
	}
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	//Factory->ParentClass = UMMDAnimInstance::StaticClass();
	Factory->PreviewSkeletalMesh = SkeletalMesh;

	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(),
		Package,
		*UniqueAssetName,
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewAnimBP) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create AnimBlueprint"));
		return nullptr;
	}
	//if (NewAnimBP->ParentClass != UMMDAnimInstance::StaticClass()) {
	//	NewAnimBP->ParentClass = UMMDAnimInstance::StaticClass();
	//}
	FKismetEditorUtilities::CompileBlueprint(NewAnimBP);

	// Set PMX source path on AnimInstance CDO for preview auto physics rebuild
    //if (NewAnimBP->GeneratedClass)
    //{
    //    //if (UMMDAnimInstance* AnimCDO = Cast<UMMDAnimInstance>(NewAnimBP->GeneratedClass->GetDefaultObject()))
    //    //{
    //    //    AnimCDO->SetSourcePMXFilePath(PMXFilePath);
    //    //    AnimCDO->Modify();
    //    //    UE_LOG(LogTemp, Verbose, TEXT("[TMMDMeshBuilder] Set SourcePMXFilePath on AnimInstance CDO: %s"), *PMXFilePath);
    //    //}
    //}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAnimBP);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs)) {
		UE_LOG(LogTemp, Log, TEXT("Successfully created and saved AnimBlueprint: %s"), *FilePath);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("AnimBlueprint created but failed to save: %s"), *FilePath);
	}

	return NewAnimBP;
}
#if WITH_EDITOR
UIKRetargeter* TMMDMeshBuilder::BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath)
{
	if (!IKRigTarget) {
		UE_LOG(LogTemp, Error, TEXT("IKRigTarget is null"));
		return nullptr;
	}

	FString MannequinIKRigPath = FString("/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin");

	UIKRigDefinition* IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *MannequinIKRigPath);
	if (!IKRigSource)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load Mannequin IKRig from: %s"), *MannequinIKRigPath);
		UE_LOG(LogTemp, Warning, TEXT("Trying alternative paths..."));

		TArray<FString> AlternativePaths = {
			TEXT("/Game/Characters/Mannequins/Rigs/IK_Mannequin"),
			TEXT("/Engine/Characters/Mannequin/Rigs/IK_Mannequin"),
			TEXT("/Script/Engine.IKRigDefinition'/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin'")
		};

		for (const FString& AltPath : AlternativePaths)
		{
			IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *AltPath);
			if (IKRigSource)
			{
				UE_LOG(LogTemp, Log, TEXT("Loaded Mannequin IKRig from: %s"), *AltPath);
				break;
			}
		}

		if (!IKRigSource)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load Mannequin IKRig. Please check the path."));
			return nullptr;
		}
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString RetargeterPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString RetargeterName = PMXModelName + TEXT("_RTG_FromMannequin");

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(RetargeterPath + TEXT("/") + RetargeterName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if(!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRetargeter* Retargeter = NewObject<UIKRetargeter>(
		Package,
		UIKRetargeter::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);

	if(!Retargeter)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRetargeter"));
		return nullptr;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRetargeterController"));
		return nullptr;
	}

	Controller->SetIKRig(ERetargetSourceOrTarget::Source, IKRigSource);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, IKRigTarget);
	UE_LOG(LogTemp, Log, TEXT("✅ Set Source IKRig: %s"), *IKRigSource->GetName());
	UE_LOG(LogTemp, Log, TEXT("✅ Set Target IKRig: %s"), *IKRigTarget->GetName());

	Retargeter->PostEditChange();

	// 自动映射骨骼链
	Controller->AutoMapChains(EAutoMapChainType::Fuzzy,true);
	UE_LOG(LogTemp, Log, TEXT("✅ Auto-mapped chains"));
	{
		FTargetChainSettings RootSettings = Controller->GetRetargetChainSettings(TEXT("Root"));

		RootSettings.FK.TranslationMode = ERetargetTranslationMode::GloballyScaled;
		RootSettings.FK.RotationMode = ERetargetRotationMode::Interpolated;

		Controller->SetRetargetChainSettings(TEXT("Root"), RootSettings);

		UE_LOG(LogTemp, Log, TEXT("✅ Root chain: TranslationMode=GloballyScaled"));
	}
	Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Retargeter);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, Retargeter, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("✅ Successfully saved IKRetargeter!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("📁 Path: %s"), *UniquePackageName);
		UE_LOG(LogTemp, Log, TEXT("📥 Source: UE5 Mannequin (IK_Mannequin)"));
		UE_LOG(LogTemp, Log, TEXT("📤 Target: %s"), *IKRigTarget->GetName());
		UE_LOG(LogTemp, Log, TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("🎉 You can now retarget Mannequin animations to your MMD model!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ Failed to save IKRetargeter: %s"), *FilePath);
	}

	return Retargeter;
}
#endif

