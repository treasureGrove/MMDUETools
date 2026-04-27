#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h" 
#include "Components/CapsuleComponent.h"
#include "AGN_MMDSkeletalControl.h"
#include "AMMDActor.generated.h"

UCLASS()
class UE5MMDTOOLS_API AMMDActor : public AActor
{
    GENERATED_BODY()
public:

	AMMDActor();

    UFUNCTION(BlueprintCallable, Category = "MMD")
    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }


    void SetupComponents(const FString& FilePath);

	/*void InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode,const PMXDatas& PMXData);*/
    UPROPERTY(EditAnywhere, Category = "MMD")
    FString SourcePMXFilePath;
protected:

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    void InitSimulatorForPreviewIfNeeded();
#endif
private:
    //��ͼ���
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "MMD")
	class UCapsuleComponent* CapsuleComponent = nullptr;
    UPROPERTY(VisibleAnywhere, Category = "MMD")
	USceneComponent* RootSceneComponent = nullptr;
};
