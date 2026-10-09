#pragma once
#include "EditorBridge/MMDActorImportService.h"
#include "TMMDMeshBuilder.h"
#include "AMMDActor.h"
class FMMDActorImportService final : public IMMDActorImportService
{
public:
    virtual void SetupComponents(AMMDActor* Actor, const FString& FilePath) override;
    virtual void FinishSetupComponents(AMMDActor* Actor, const FString& FilePath, USkeletalMesh* BuiltMesh, const PMXDatas* PMXData) override;
private:
    TMap<TWeakObjectPtr<AMMDActor>, TSharedPtr<FMMDAsyncMeshImportHandle>> PendingImports;
};
