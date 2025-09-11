#pragma once

#include "CoreMinimal.h"
#include "TPMXParser.h"  // 需要获取 PMXDatas 定义

// 前向声明 UE5 类
class USkeletalMesh;
class UStaticMesh;
class UTexture2D;
class UMaterialInterface;
class USkeleton;
struct FStaticMeshLODResources;

class TMMDMeshBuilder
{
public:
    static USkeletalMesh* CreateSkeletalMeshFromPMX(const PMXDatas& PMXInfo, const FString& AssetName);
    
private:
    // 🔧 几何数据转换
    static void ConvertPMXVertices(const PMXDatas& PMXData, FSkeletalMeshLODModel& LODModel);
    static void ConvertPMXIndices(const PMXDatas& PMXData, FSkeletalMeshLODModel& LODModel);
    
    // 🔧 骨骼系统
    static USkeleton* CreateSkeletonFromPMX(const PMXDatas& PMXData, const FString& AssetName);
    static void BuildBoneHierarchy(const PMXDatas& PMXData, USkeleton* Skeleton);
    
    // 🔧 材质和贴图系统
    static TArray<UMaterialInterface*> CreateMaterialsFromPMX(const PMXDatas& PMXData, const FString& BaseName, const FString& PMXFilePath);
    static UTexture2D* ImportTextureFromPath(const FString& TexturePath, const FString& AssetName);
    static UMaterialInterface* CreateMaterialFromPMXData(const PMXMaterial& PMXMat, const FString& AssetName, const FString& PMXBasePath);
    
    // 🔧 材质段和LOD构建
    static void BuildMaterialSections(const PMXDatas& PMXData, FSkeletalMeshLODModel& LODModel, const TArray<UMaterialInterface*>& Materials);
    static void SetupSkeletalMeshMaterials(USkeletalMesh* SkeletalMesh, const TArray<UMaterialInterface*>& Materials);
    
    // 🔧 网格最终化
    static void FinalizeMeshBuild(USkeletalMesh* SkeletalMesh);
    static void CalculateBounds(USkeletalMesh* SkeletalMesh, const PMXDatas& PMXData);
    
    // 🔧 辅助函数
    static FString GetTexturePathFromPMX(const FString& TextureName, const FString& PMXFilePath);
    static FLinearColor ConvertPMXColor(const FVector& PMXColor, float Alpha = 1.0f);

};