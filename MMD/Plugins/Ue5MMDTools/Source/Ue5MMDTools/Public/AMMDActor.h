#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h" 
#include "Components/CapsuleComponent.h"
#include <AGN_MMDSkeletalControl.h>
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

	void InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode,const PMXDatas& PMXData);

protected:

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
	//TArray<FMMDPhysicsBone> PhysicsBones;

	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
	//TArray<FMMDPhysicsConstraint> PhysicsConstraints;

 //   // 全局物理设置
 //   UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics", meta = (ClampMin = "0.1", ClampMax = "10.0"))
 //   float GlobalPhysicsScale = 1.0f;

 //   UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
 //   FVector GlobalGravity = FVector(0, 0, -980.0f);

 //   UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
 //   bool bEnableMMDPhysics = true;

 //   UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics", meta = (ClampMin = "1", ClampMax = "10"))
 //   int32 PhysicsIterations = 3;  // 约束求解迭代次数
 //   
private:
    //蓝图组成
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "MMD")
	class UCapsuleComponent* CapsuleComponent = nullptr;
    UPROPERTY(VisibleAnywhere, Category = "MMD")
	USceneComponent* RootSceneComponent = nullptr;
};