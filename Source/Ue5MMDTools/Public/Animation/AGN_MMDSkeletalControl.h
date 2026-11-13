// AGN_MMDSkeletalControl.h
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

#include "TPMXParser.h"
#include "btBulletDynamicsCommon.h"
#include "AGN_MMDSkeletalControl.generated.h"

class FMMDPhysicsSimulator;

USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDPhysicsRigidBodyData
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() FString NameEN;
    UPROPERTY() int32 RelatedBoneIndex = INDEX_NONE;
    UPROPERTY() int32 ShapeType = 0; // Sphere/Box/Capsule
    UPROPERTY() FVector ShapeSize = FVector::ZeroVector;
    UPROPERTY() FVector ShapePosition = FVector::ZeroVector;
    UPROPERTY() FVector ShapeRotation = FVector::ZeroVector;
    UPROPERTY() float Mass = 0.0f;
    UPROPERTY() float Friction = 0.0f;
    UPROPERTY() float Restitution = 0.0f;
    UPROPERTY() int32 CollisionGroup = 0;
    UPROPERTY() int32 CollisionMask = 0xffff;
    UPROPERTY() int32 PhysicsMode = 0;

    UPROPERTY()
    float LinearDamping = 0.0f;
    UPROPERTY()
    float AngularDamping = 0.0f;
};
USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDPhysicsJointData
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
	UPROPERTY() FString NameEN;
    UPROPERTY() int32 JointType = 0;
    UPROPERTY() int32 RigidBodyIndexA = INDEX_NONE;
    UPROPERTY() int32 RigidBodyIndexB = INDEX_NONE;
    UPROPERTY() FVector Position = FVector::ZeroVector;
    UPROPERTY() FVector Rotation = FVector::ZeroVector;
    UPROPERTY() FVector LimitPositionMin = FVector::ZeroVector;
    UPROPERTY() FVector LimitPositionMax = FVector::ZeroVector;
    UPROPERTY() FVector LimitRotationMin = FVector::ZeroVector;
    UPROPERTY() FVector LimitRotationMax = FVector::ZeroVector;
    UPROPERTY() FVector SpringPosition = FVector::ZeroVector;
    UPROPERTY() FVector SpringRotation = FVector::ZeroVector;
};

USTRUCT(BlueprintInternalUseOnly)
struct UE5MMDTOOLS_API FAGN_MMDSkeletalControl : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    FAGN_MMDSkeletalControl();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
    bool bEnablePhysics=true;
    bool bIsInitialized=false;

    // 在蓝图里开关调试绘制
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bDrawDebug = false;

    // 基本物理参数（与模拟器默认一致，可在蓝图调）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float UnitScale = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    int32 MaxSubSteps = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float FixedTimeStep = 1.f / 60.f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FMMDPhysicsRigidBodyData> RigidBodySaveDataArray;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FMMDPhysicsJointData> JointSaveDataArray;

    //MMD数据
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
private:
	TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> SimulatorPtr;
    bool bSimulatorInitialized = false;
    static void BuildBoneWorldArray(FComponentSpacePoseContext& Output, TArray<FTransform>& OutWorld);

};
