#pragma once
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "MMDPhysicsSimulator.h"
#include "MMDPhysicsSaveGame.generated.h"

UCLASS()
class UE5MMDTOOLS_API UMMDPhysicsSaveGame : public USaveGame
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FMMDPhysicsSimSnapshot Snapshot;
};
