#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MMDPhysicsSimulatorHolder.h"
#include "MMDPhysicsRegistry.generated.h"

UCLASS(Transient)
class UE5MMDTOOLS_API UMMDPhysicsRegistry : public UObject
{
    GENERATED_BODY()
public:
    static UMMDPhysicsRegistry* Get();

    UFUNCTION()
    UMMDPhysicsSimulatorHolder* FindHolder(const FString& Key) const;

    UFUNCTION()
    UMMDPhysicsSimulatorHolder* GetOrCreateHolder(const FString& Key);

    UFUNCTION()
    void RemoveHolder(const FString& Key);

    static FString BuildKeyFromMesh(const USkeletalMesh* Mesh);

private:
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UMMDPhysicsSimulatorHolder>> Holders;
};