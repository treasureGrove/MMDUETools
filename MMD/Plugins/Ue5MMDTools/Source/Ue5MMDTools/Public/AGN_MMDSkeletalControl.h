// AGN_MMDSkeletalControl.h
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#if WITH_EDITORONLY_DATA
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimBlueprint.h"
#endif
#include "TPMXParser.h"
#include "btBulletDynamicsCommon.h"
#include "AGN_MMDSkeletalControl.generated.h"

class FMMDPhysicsSimulator;

USTRUCT()
struct UE5MMDTOOLS_API FMMDPhysicsRigidBodyData
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() FString NameEN;
    UPROPERTY() int32 RelatedBoneIndex;
    UPROPERTY() int32 ShapeType; // Sphere/Box/Capsule
    UPROPERTY() FVector ShapeSize;
    UPROPERTY() FVector ShapePosition;
    UPROPERTY() FVector ShapeRotation;
    UPROPERTY() float Mass;
    UPROPERTY() float Friction;
    UPROPERTY() float Restitution;
    UPROPERTY() int32 CollisionGroup;
    UPROPERTY() int32 CollisionMask;
    UPROPERTY() int32 PhysicsMode;

    UPROPERTY()
    float LinearDamping = 0.0f;
    UPROPERTY()
    float AngularDamping = 0.0f;
};
USTRUCT()
struct UE5MMDTOOLS_API FMMDPhysicsJointData
{
    GENERATED_BODY()
    UPROPERTY() FString Name;
	UPROPERTY() FString NameEN;
    UPROPERTY() int32 JointType;
    UPROPERTY() int32 RigidBodyIndexA;
    UPROPERTY() int32 RigidBodyIndexB;
    UPROPERTY() FVector Position;
    UPROPERTY() FVector Rotation;
    UPROPERTY() FVector LimitPositionMin;
    UPROPERTY() FVector LimitPositionMax;
    UPROPERTY() FVector LimitRotationMin;
    UPROPERTY() FVector LimitRotationMax;
    UPROPERTY() FVector SpringPosition;
    UPROPERTY() FVector SpringRotation;
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
    int32 MaxSubSteps = 1;

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


#if WITH_EDITORONLY_DATA

UCLASS(MinimalAPI) 
class UAnimGraphNode_MMDSkeletalControl : public UAnimGraphNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Settings")
    FAGN_MMDSkeletalControl Node;

    virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
    virtual FText GetTooltipText() const override;
    virtual FString GetNodeCategory() const override;
    virtual FLinearColor GetNodeTitleColor() const override;

protected:
    virtual const FAnimNode_SkeletalControlBase* GetNode() const override;
};


class FMMDAnimGraphHelper  
{
public:
    static UAnimGraphNode_MMDSkeletalControl* AddMMDNodeToAnimBP(
		UAnimBlueprint* AnimBP,const PMXDatas& PMXData,
        bool bConnectToRoot = true
    );

    static UAnimGraphNode_MMDSkeletalControl* InsertMMDNodeBetween(
        UAnimBlueprint* AnimBP,
        UAnimGraphNode_Base* UpstreamNode,
        UAnimGraphNode_Base* DownstreamNode
    );
};

#endif // WITH_EDITORONLY_DATA