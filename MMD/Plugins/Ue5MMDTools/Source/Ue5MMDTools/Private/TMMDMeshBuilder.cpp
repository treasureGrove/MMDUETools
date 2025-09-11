#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"
#include "MMDImportSetting.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "StaticMeshDescription.h"
#include "StaticMeshAttributes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Rendering/SkeletalMeshModel.h"	   // FSkeletalMeshModel
#include "Rendering/SkeletalMeshLODModel.h"	   // FSkeletalMeshLODModel
#include "GPUSkinVertexFactory.h"			   // FSoftSkinVertex
#include "Rendering/MultiSizeIndexContainer.h" // 索引容器
#include "Rendering/SkeletalMeshVertexBuffer.h"

USkeletalMesh *TMMDMeshBuilder::CreateSkeletalMeshFromPMX(const PMXDatas &PMXInfo, const FString &AssetName)
{
	MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("正在创建骨骼网格体: %s"), *AssetName), EMMDMessageType::Info);
	UE_LOG(LogTemp, Warning, TEXT("=== 开始创建SkeletalMesh ==="));
	UE_LOG(LogTemp, Warning, TEXT("模型名: %s"), *PMXInfo.ModelNameJP);
	UE_LOG(LogTemp, Warning, TEXT("顶点数: %d"), PMXInfo.ModelVertices.Num());
	UE_LOG(LogTemp, Warning, TEXT("骨骼数: %d"), PMXInfo.ModelBones.Num());
	UE_LOG(LogTemp, Warning, TEXT("材质数: %d"), PMXInfo.ModelMaterials.Num());
	// 验证数据
	if (PMXInfo.ModelVertices.Num() == 0)
	{
		MMDImportSetting::ShowGlobalImportProgress(TEXT("错误: 没有顶点数据"), EMMDMessageType::Error);
		return nullptr;
	}
	if (PMXInfo.ModelBones.Num() == 0)
	{
		MMDImportSetting::ShowGlobalImportProgress(TEXT("警告: 没有骨骼数据，将创建静态网格"), EMMDMessageType::Error);
		return nullptr;
	}
	USkeletalMesh *SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), *AssetName);
	if (!SkeletalMesh)
	{
		MMDImportSetting::ShowGlobalImportProgress(TEXT("创建SkeletalMesh失败"), EMMDMessageType::Error);
		return nullptr;
	}
	MMDImportSetting::ShowGlobalImportProgress(TEXT("开始转换顶点数据..."), EMMDMessageType::Info);

	// 🔧 转换PMX顶点数据
	FSkeletalMeshModel *ImportedModel = SkeletalMesh->GetImportedModel();
	if (!ImportedModel)
	{
		MMDImportSetting::ShowGlobalImportProgress(TEXT("获取FSkeletalMeshModel失败"), EMMDMessageType::Error);
		return nullptr;
	}
	if (ImportedModel->LODModels.Num() == 0)
	{
		ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
	}
	FSkeletalMeshLODModel &LODModel = ImportedModel->LODModels[0];
	ConvertPMXVertices(PMXInfo, LODModel);

	return nullptr;
}
// 这里实现顶点数据的转换逻辑
// 将PMX的顶点格式转换为Unreal Engine的顶点格式
void TMMDMeshBuilder::ConvertPMXVertices(const PMXDatas &PMXData, FSkeletalMeshLODModel &LODModel)
{
	const int32 VertexCount = PMXData.ModelVertices.Num();
	const int32 MaxBoneCount = PMXData.ModelBones.Num();
	MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("顶点数: %d"), VertexCount), EMMDMessageType::Info);

	LODModel.NumVertices = VertexCount;

	TArray<FSoftSkinVertex> Vertices;
	Vertices.Reserve(VertexCount);

	for (int32 i = 0; i < VertexCount; i++)
	{
		const PMXVertex &PMXVert = PMXData.ModelVertices[i];
		// 🔧 添加这行
		FSoftSkinVertex NewVert;
		const float PMXConvertScale = 100.0f;
		NewVert.UVs[0] = FVector2f(PMXVert.UV.X, 1 - PMXVert.UV.Y);
		NewVert.Position = FVector3f(PMXVert.Position.X * PMXConvertScale, -PMXVert.Position.Z * PMXConvertScale, PMXVert.Position.Y * PMXConvertScale);
		NewVert.TangentZ = FVector4f(PMXVert.Normal.X, -PMXVert.Normal.Z, PMXVert.Normal.Y, 1.0f);
		for (int j = 0; j < PMXVert.AdditionalUVs.Num(); ++j)
		{
			if (j + 1 < MAX_TEXCOORDS)
			{
				NewVert.UVs[j + 1] = FVector2f(PMXVert.AdditionalUVs[j].X, 1 - PMXData.ModelVertices[i].AdditionalUVs[j].Y);
			}
		}
		for (int32 j = 0; j < MAX_TOTAL_INFLUENCES; j++)
		{
			NewVert.InfluenceBones[j] = 0;
			NewVert.InfluenceWeights[j] = 0;
		}
		const PMXVertexWeight &Weight = PMXVert.Weight;
		switch (Weight.WeightDeformType)
		{
		case 0: // BDEF1 - 单骨骼
			NewVert.InfluenceBones[0] = FMath::Clamp(Weight.BoneIndices[0], 0, MaxBoneCount - 1);
			NewVert.InfluenceWeights[0] = 255; // UE5使用0-255范围
			break;
		case 1:
			// BDEF2 - 双骨骼
			NewVert.InfluenceBones[0] = FMath::Clamp(Weight.BoneIndices[0], 0, MaxBoneCount - 1);
			NewVert.InfluenceBones[1] = FMath::Clamp(Weight.BoneIndices[1], 0, MaxBoneCount - 1);
			NewVert.InfluenceWeights[0] = FMath::Clamp(static_cast<int32>(Weight.Weights[0] * 255), 0, 255);
			NewVert.InfluenceWeights[1] = 255 - NewVert.InfluenceWeights[0];
			break;
		case 2:
			// BDEF4 - 四骨骼
			{
				int32 TotalWeight = 0;
				for (int32 j = 0; j < 4; j++)
				{
					NewVert.InfluenceBones[j] = FMath::Clamp(Weight.BoneIndices[j], 0, MaxBoneCount - 1);
					NewVert.InfluenceWeights[j] = FMath::Clamp(static_cast<int32>(Weight.Weights[j] * 255), 0, 255);
					TotalWeight += NewVert.InfluenceWeights[j];
				}
				// 确保权重总和为255
				if (TotalWeight != 255 && TotalWeight > 0)
				{
					// 找到最大的权重进行调整
					int32 MaxWeightIndex = 0;
					for (int32 j = 1; j < 4 && j < MAX_TOTAL_INFLUENCES; j++)
					{
						if (NewVert.InfluenceWeights[j] > NewVert.InfluenceWeights[MaxWeightIndex])
						{
							MaxWeightIndex = j;
						}
					}

					int32 WeightDiff = 255 - TotalWeight;
					NewVert.InfluenceWeights[MaxWeightIndex] = FMath::Clamp(
						NewVert.InfluenceWeights[MaxWeightIndex] + WeightDiff,
						0, 255);
				}
				else if (TotalWeight == 0)
				{
					// 所有权重都是0，默认绑定
					NewVert.InfluenceBones[0] = 0;
					NewVert.InfluenceWeights[0] = 255;
				}
			}
			break;
		case 3:
			// SDEF - 特殊双骨骼
			NewVert.InfluenceBones[0] = FMath::Clamp(Weight.BoneIndices[0], 0, MaxBoneCount - 1);
			NewVert.InfluenceBones[1] = FMath::Clamp(Weight.BoneIndices[1], 0, MaxBoneCount - 1);
			NewVert.InfluenceWeights[0] = FMath::Clamp(static_cast<int32>(Weight.Weights[0] * 255), 0, 255);
			NewVert.InfluenceWeights[1] = 255 - NewVert.InfluenceWeights[0];
			// SDEF特有的参数暂时忽略
			break;
		default:
			MMDImportSetting::ShowGlobalImportProgress(FString::Printf(TEXT("警告: 顶点 %d 使用未知的变形类型 %d，设置为默认值"), i, Weight.WeightDeformType), EMMDMessageType::Warning);
			// 未知的变形类型，设置为默认值
		}

		Vertices.Add(NewVert);
		if (i < 3)
		{
			UE_LOG(LogTemp, Log, TEXT("顶点[%d]: 权重类型=%d"), i, Weight.WeightDeformType);
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("顶点转换完成: %d 个顶点"), VertexCount);
}
