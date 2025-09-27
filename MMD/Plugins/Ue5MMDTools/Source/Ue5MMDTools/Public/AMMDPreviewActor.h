#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h" 
#include "AMMDPreviewActor.generated.h"

UCLASS()
class UE5MMDTOOLS_API AAMMDPreviewActor : public AActor
{
    GENERATED_BODY()
public:
    AAMMDPreviewActor();

    void BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath);

    UFUNCTION(BlueprintCallable, Category = "MMD")
    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    PMXDatas LoadedPMX;
    FString  LoadedPMXPath;

    void SetupComponents();
    void Cleanup();
};