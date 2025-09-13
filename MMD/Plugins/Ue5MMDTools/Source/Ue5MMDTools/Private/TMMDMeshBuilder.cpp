#include "Factories/FbxSkeletalMeshImportData.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "TPMXParser.h"
#include "TMMDMeshBuilder.h"

// 正确的MMD→UE5坐标转换
static FVector3f ConvertPMXPositionToUnreal(const FVector &InPosition)
{
	// MMD右手坐标系 → UE5左手坐标系
	// MMD: X(右), Y(上), Z(前) → UE5: X(前), Y(右), Z(上)
	const float PMXConvertScale = 1.0f; // MMD和UE5都使用厘米，无需缩放

	return FVector3f(
		InPosition.X * PMXConvertScale,	 // X保持不变
		-InPosition.Z * PMXConvertScale, // MMD的Z → UE5的-Y
		InPosition.Y * PMXConvertScale	 // MMD的Y → UE5的Z
	);
}

static FVector3f ConvertPMXNormalToUnreal(const FVector &InNormal)
{
	// 法线向量转换（无缩放）
	return FVector3f(
		InNormal.X,	 // X保持不变
		-InNormal.Z, // MMD的Z → UE5的-Y
		InNormal.Y	 // MMD的Y → UE5的Z
	);
}

static FVector2f ConvertPMXUVToUnreal(const FVector2D &InUV)
{
	// UV坐标转换（V坐标翻转）
	return FVector2f(InUV.X, 1.0f - InUV.Y);
}
void TMMDMeshBuilder::CreatePMXSkeletalMesh(const PMXDatas &PMXInfo)
{
	FString PackagePath = FString::Printf(TEXT("/Game/MMD/Models/%s"), *PMXInfo.ModelNameEN);
	UPackage *Package = CreatePackage(*PackagePath);

	USkeletalMesh *SkeletalMesh = NewObject<USkeletalMesh>(Package, *PMXInfo.ModelNameEN, RF_Public | RF_Standalone);

	FSkeletalMeshImportData *ImportData = new FSkeletalMeshImportData();
	ImportData->Points.Reserve(PMXInfo.ModelVertexCount);
	ImportData->PointToRawMap.Reserve(PMXInfo.ModelVertexCount);
	ImportData->Wedges.Reserve(PMXInfo.ModelVertexCount);
	for (int32 i = 0; i < PMXInfo.ModelVertexCount; i++)
	{
		const PMXVertex &Vertex = PMXInfo.ModelVertices[i];

		ImportData->Points.Add(FVector3f(ConvertPMXPositionToUnreal(Vertex.Position)));
		ImportData->PointToRawMap.Add(i);

		SkeletalMeshImportData::FVertex WedgeVertex;
		WedgeVertex.VertexIndex = i;
		WedgeVertex.UVs[0] = ConvertPMXUVToUnreal(Vertex.UV);
		for (int32 j = 0; j < Vertex.AdditionalUVs.Num() && j < MAX_TEXCOORDS - 1; j++)
		{
			WedgeVertex.UVs[j + 1] = ConvertPMXUVToUnreal(FVector2D(Vertex.AdditionalUVs[j].X, Vertex.AdditionalUVs[j].Y));
		}
		WedgeVertex.MatIndex = 0;
		ImportData->Wedges.Add(WedgeVertex);
	}
	// 三角形索引 Face
	for (int32 i = 0; i < PMXInfo.ModelIndicesCount; i += 3)
	{
		SkeletalMeshImportData::FTriangle Tri;
		Tri.WedgeIndex[0] = PMXInfo.ModelIndices[i];
		Tri.WedgeIndex[1] = PMXInfo.ModelIndices[i + 1];
		Tri.WedgeIndex[2] = PMXInfo.ModelIndices[i + 2];
		Tri.MatIndex = 0;		 // 暂时不处理材质
		Tri.SmoothingGroups = 0; // 暂时不处理光滑组
		ImportData->Faces.Add(Tri);
	}
	// Bone
	for (int32 i = 0; i < PMXInfo.ModelBoneCount; i++)
	{
		const PMXBone &PMXBone = PMXInfo.ModelBones[i];
		SkeletalMeshImportData::FBone Bone;
		Bone.Name = ObjectTools::SanitizeObjectName(PMXBone.NameEN);
		if (Bone.Name.IsEmpty())
		{
			Bone.Name = FString::Printf(TEXT("Bone_%d"), i);
		}
		if (PMXBone.ParentBoneIndex >= 0 && PMXBone.ParentBoneIndex < PMXInfo.ModelBoneCount)
		{
			Bone.ParentIndex = PMXBone.ParentBoneIndex;
		}
		else
		{
			Bone.ParentIndex = INDEX_NONE; // 根骨骼
		}

		FVector3f BonePosition = ConvertPMXPositionToUnreal(PMXBone.Position);
		Bone.BonePos.Transform = FTransform3f(
			FQuat4f::Identity,	 // float精度四元数
			BonePosition,		 // FVector3f位置
			FVector3f::OneVector // FVector3f缩放
		);
		Bone.BonePos.Length = 10.0f; // 未知
		Bone.BonePos.XSize = 10.0f;
		Bone.BonePos.YSize = 1.0f;
		Bone.BonePos.ZSize = 1.0f;
		ImportData->RefBonesBinary.Add(Bone);
	}
	ImportData->Influences.Reserve(PMXInfo.ModelVertexCount);
	// SkinWeights
	for (int32 i = 0; i < PMXInfo.ModelVertexCount; i++)
	{
		const PMXVertex &Vertex = PMXInfo.ModelVertices[i];
		switch (Vertex.Weight.WeightDeformType)
		{
		case 0:
			if (Vertex.Weight.BoneIndices[0] >= 0)
			{
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.BoneIndex = Vertex.Weight.BoneIndices[0];
				Influence.VertexIndex = i;
				Influence.Weight = 1.0f;
				ImportData->Influences.Add(Influence);
			}
			break;
		case 1:
			if (Vertex.Weight.BoneIndices[0] >= 0 && Vertex.Weight.Weights[0] > 0.0f)
			{
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.Weight = Vertex.Weight.Weights[0];
				Influence.BoneIndex = Vertex.Weight.BoneIndices[0];
				Influence.VertexIndex = i;

				ImportData->Influences.Add(Influence);
			}
			if (Vertex.Weight.BoneIndices[1] >= 0)
			{
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.BoneIndex = Vertex.Weight.BoneIndices[1];
				Influence.VertexIndex = i;
				Influence.Weight = Vertex.Weight.Weights[1];
				ImportData->Influences.Add(Influence);
			}
		case 2:
			for (int32 j = 0; j < 4; j++)
			{
				if (Vertex.Weight.BoneIndices[j] >= 0 && Vertex.Weight.Weights[j] > 0.0f)
				{
					SkeletalMeshImportData::FRawBoneInfluence Influence;
					Influence.Weight = Vertex.Weight.Weights[j];
					Influence.BoneIndex = Vertex.Weight.BoneIndices[j];
					Influence.VertexIndex = i;

					ImportData->Influences.Add(Influence);
				}
			}
			break;
		case 3:
			if (Vertex.Weight.BoneIndices[0] >= 0 && Vertex.Weight.Weights[0] > 0.0f)
			{
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.BoneIndex = Vertex.Weight.BoneIndices[0];
				Influence.Weight = Vertex.Weight.Weights[0];
				Influence.VertexIndex = i;
				ImportData->Influences.Add(Influence);
			}
			if (Vertex.Weight.BoneIndices[1] >= 0 && Vertex.Weight.Weights[1] > 0.0f)
			{
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.BoneIndex = Vertex.Weight.BoneIndices[1];
				Influence.Weight = Vertex.Weight.Weights[1];
				Influence.VertexIndex = i;
				ImportData->Influences.Add(Influence);
			}
			break;
		default:
			break;
		}
	}
}