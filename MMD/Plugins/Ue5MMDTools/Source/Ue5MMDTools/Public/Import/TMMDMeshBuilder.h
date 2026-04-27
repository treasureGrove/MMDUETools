#pragma once

#include "CoreMinimal.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#if WITH_EDITOR              
#include "RetargetEditor/IKRetargeterController.h"    
#endif
class UIKRigDefinition;

class TMMDMeshBuilder
{
public:
    static USkeletalMesh *BuildSkeletalMeshFromPMX(const PMXDatas &PMXInfo, const FString &PackagePath, const FString &AssetName, const FString& PMXFilePath);
    
	static UIKRigDefinition* BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

    static UAnimBlueprint* BuildAnimBlueprint(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

	static UIKRetargeter* BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath);
	
	static UAnimSequence* BuildVMDAnimation(const VMDData &VmdData,const FString& VMDFilePath);
}; 

