#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h"
#include "MMDPhysicsComponent.h"
#include "AMMDActor.generated.h"

UCLASS()
class UE5MMDTOOLS_API AMMDActor : public AActor
{
    GENERATED_BODY()
public:

	AMMDActor();

    void BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath);

    UFUNCTION(BlueprintCallable, Category = "MMD")
    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }

    UFUNCTION(BlueprintCallable, Category = "MMD")
    UMMDPhysicsComponent* GetPhysicsComponent() const { return PhysicsComponent; }

    UFUNCTION(BlueprintCallable, Category = "MMD")
    void SetPhysicsEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "MMD")
    bool IsPhysicsEnabled() const;

    void SetupComponents(const FString& FilePath);
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    
private:
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "MMD")
    UMMDPhysicsComponent* PhysicsComponent = nullptr;

    PMXDatas LoadedPMX;
    FString  LoadedPMXPath;


    void Cleanup();
};