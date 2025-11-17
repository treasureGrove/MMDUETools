#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MMDPhysicsSimulator.h"
#include "MMDPhysicsSimulatorHolder.generated.h"

UCLASS(Transient, NotBlueprintType)
class UE5MMDTOOLS_API UMMDPhysicsSimulatorHolder : public UObject
{
    GENERATED_BODY()
public:
    TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> Simulator;

    UPROPERTY(EditAnywhere, SaveGame)
    FMMDPhysicsSimSnapshot LastSnapshot;

    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    void CaptureNow();

    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    bool ApplyNow(bool bRespectKinematic = true);

    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    bool SaveSnapshotToDisk(const FString& Identifier);

    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    bool LoadSnapshotFromDisk(const FString& Identifier, bool bApply = true, bool bForce = false);

    virtual void BeginDestroy() override;
private:
    FString BuildPath(const FString& Identifier) const;
    bool SerializeToJson(const FMMDPhysicsSimSnapshot& Snapshot, FString& OutJson) const;
    bool DeserializeFromJson(const FString& InJson, FMMDPhysicsSimSnapshot& OutSnapshot) const;
};
