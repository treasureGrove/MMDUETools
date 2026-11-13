#pragma once
#include "CoreMinimal.h"
class AMMDActor;
class USkeletalMesh;
struct PMXDatas;

class UE5MMDTOOLS_API IMMDActorImportService
{
public:
    virtual ~IMMDActorImportService() = default;
    virtual void SetupComponents(AMMDActor* Actor, const FString& FilePath) = 0;
    virtual void FinishSetupComponents(AMMDActor* Actor, const FString& FilePath, USkeletalMesh* BuiltMesh, const PMXDatas* PMXData) = 0;
    static IMMDActorImportService* Get();
    static void Set(IMMDActorImportService* Service);
};
