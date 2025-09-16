#pragma once

#include "CoreMinimal.h"
#include "TPMXParser.h" // 需要获取 PMXDatas 定义

class TMMDMeshBuilder
{
public:
    static USkeletalMesh *BuildSkeletalMeshFromPMX(const PMXDatas &PMXInfo, const FString &PackagePath, const FString &AssetName, const FString& PMXFilePath);
    
};

