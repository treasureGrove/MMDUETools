#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h" 
#include "AMMDActor.generated.h"
USTRUCT(BlueprintType)
struct FMMDPhysicsState {
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector CurrentPosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector PreviousPosition = FVector::ZeroVector;

    FVector GetVelocity(float DeltaTime) const
    {
        return DeltaTime > KINDA_SMALL_NUMBER ? (CurrentPosition - PreviousPosition) / DeltaTime : FVector::ZeroVector;
    }

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector RestPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ConstraintDistance = 100.0f;  // 最大偏移距离

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsActive = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bCollided = false;
};

USTRUCT(BlueprintType)
struct FMMDPhysicsBone {
	GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName BoneName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 PMXRigidIndex = -1;

    // 从PMX直接读取的物理参数
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 PhysicsMode = 0;  // 0=Static, 1=Dynamic, 2=BoneTracked

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Mass = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float LinearDamping = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float AngularDamping = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Restitution = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Friction = 0.5f;

    // 碰撞形状数据
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    uint8 ShapeType = 0;  // 0=Sphere, 1=Box, 2=Capsule

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector CollisionSize = FVector::OneVector;

    // MMD坐标系位置和旋转
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector MMDPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector MMDRotation = FVector::ZeroVector;

    // 运行时物理状态
    FMMDPhysicsState PhysicsState;

    // MMD特有参数
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float BoneFollowStrength = 0.8f;  // 骨骼跟随强度

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEnablePhysics = true;
};

USTRUCT(BlueprintType)
struct FMMDPhysicsConstraint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName ConstraintName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 RigidBodyA = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 RigidBodyB = -1;

    // Spring约束参数
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector SpringConstant = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector LinearLowerLimit = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector LinearUpperLimit = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector AngularLowerLimit = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector AngularUpperLimit = FVector::ZeroVector;
};

UCLASS()
class UE5MMDTOOLS_API AMMDActor : public AActor
{
    GENERATED_BODY()
public:

	AMMDActor();

    void BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath);

    UFUNCTION(BlueprintCallable, Category = "MMD")
    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }

    void SetupComponents(const FString& FilePath);


protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
	TArray<FMMDPhysicsBone> PhysicsBones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
	TArray<FMMDPhysicsConstraint> PhysicsConstraints;

    // 全局物理设置
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics", meta = (ClampMin = "0.1", ClampMax = "10.0"))
    float GlobalPhysicsScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
    FVector GlobalGravity = FVector(0, 0, -980.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics")
    bool bEnableMMDPhysics = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Physics", meta = (ClampMin = "1", ClampMax = "10"))
    int32 PhysicsIterations = 3;  // 约束求解迭代次数
    
private:
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    PMXDatas LoadedPMX;
    FString  LoadedPMXPath;


    void Cleanup();
};