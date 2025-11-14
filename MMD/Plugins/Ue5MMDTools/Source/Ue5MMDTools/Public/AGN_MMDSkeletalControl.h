// AGN_MMDSkeletalControl.h
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
// ✅ Editor相关头文件要在 .generated.h 之前
#if WITH_EDITORONLY_DATA
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimBlueprint.h"
#endif
#include "btBulletDynamicsCommon.h"
#include "AGN_MMDSkeletalControl.generated.h"

#pragma region 物理数据结构体
USTRUCT(BlueprintType)
struct  FMMDRigidBodyRuntime
{
    GENERATED_BODY()

    UPROPERTY()
    FString NameJP="";

    UPROPERTY()
    FString NameEN="";

    UPROPERTY()
    int32 RelatedBoneIndex = -1;

    UPROPERTY()
    uint8 Group=0;

    UPROPERTY()
    uint16 CollisionMask= 0xFFFF;

    UPROPERTY()
    uint8 ShapeType= 0;

    UPROPERTY()
    FVector Size=FVector::ZeroVector;

    UPROPERTY()
    FVector Position = FVector::ZeroVector;

    UPROPERTY()
    FVector Rotation = FVector::ZeroVector;

    UPROPERTY()
    float Mass=1;

    UPROPERTY()
    float LinearDamping=0;

    UPROPERTY()
	float AngularDamping=0;

    UPROPERTY()
    float Restitution=0;

    UPROPERTY()
    float Friction=0.5;

    UPROPERTY()
    uint8 PhysicsMode=0;

    FCompactPoseBoneIndex CompactBoneIndex= FCompactPoseBoneIndex(INDEX_NONE);  // 编译后的骨骼索引
    FVector Velocity=FVector::ZeroVector;                        // 当前速度
    FVector AngularVelocity= FVector::ZeroVector;                 // 角速度
    FVector PrevPosition= FVector::ZeroVector;                    // 上一帧位置
    FQuat PrevRotation=FQuat::Identity;                      // 上一帧旋转
    FVector FilteredPosition = FVector::ZeroVector;
	FQuat FilteredRotation = FQuat::Identity;
    FTransform RigidBodyOffset = FTransform::Identity;
    
   
    btRigidBody* BulletBody=nullptr;
	float FollowStrength = 0.35f; // 追随力系数，仅 PhysicsMode=2 有效
    bool bInitialized = false;
    float InvMass = 0;
};
//约束数据结构体
USTRUCT(BlueprintType)
struct FMMDJointRuntime
{
    GENERATED_BODY()

    UPROPERTY()
    FString NameJP="";
    UPROPERTY()
    FString NameEN="";
    UPROPERTY()
    uint8 JointType = 0;
    UPROPERTY()
    int32 RigidA = -1;
    UPROPERTY()
    int32 RigidB = -1;
    UPROPERTY()
    FVector Position = FVector::ZeroVector;
    UPROPERTY()
    FVector Rotation = FVector::ZeroVector;
    UPROPERTY()
    FVector LimitPosLower = FVector::ZeroVector;
    UPROPERTY()
    FVector LimitPosUpper = FVector::ZeroVector;
    UPROPERTY()
    FVector LimitRotLower = FVector::ZeroVector;
    UPROPERTY()
    FVector LimitRotUpper = FVector::ZeroVector;
    UPROPERTY()
    FVector SpringPos = FVector::ZeroVector;
    UPROPERTY()
    FVector SpringRot = FVector::ZeroVector;

    btTypedConstraint* BulletConstraint = nullptr;
    bool bConstraintInitialized = false;

};
//软体锚点结构体
USTRUCT(BlueprintType)
struct FMMDSoftBodyAnchor
{
	GENERATED_BODY()

    UPROPERTY()
    int32 RigidIndex = -1;
    UPROPERTY()
    int32 VertexIndex = -1;
    UPROPERTY()
    uint8 NearMode = 0;
};

//软体数据结构体
USTRUCT(BlueprintType)
struct  FMMDSoftBodyRuntime
{
    GENERATED_BODY()
    UPROPERTY()
    FString NameJP;
    UPROPERTY()
    FString NameEN;
    UPROPERTY()
    uint8 ShapeType = 0;
    UPROPERTY()
    int32 MaterialIndex = -1;
    UPROPERTY()
    uint8 Group = 0;
    UPROPERTY()
    uint16 CollisionMask = 0;
    UPROPERTY()
    uint8 Flags = 0;
    UPROPERTY()
    // 多个 sim 参数（按规范顺序读取）
    int32 BLinkDistance = 0;
    UPROPERTY()
    int32 ClusterCount;
    UPROPERTY()
    int32 TotalMass = 0;
    UPROPERTY()
    int32 Margin = 0;
    UPROPERTY()
    int32 AeroModel = 0;
    UPROPERTY()
    int32 VCF = 0;//Velocities Correction Factor - 速度修正因子
    UPROPERTY()
    int32 DP = 0;//Damping Parameter - 阻尼参数
    UPROPERTY()
    int32 DG = 0;//Drag Coefficient - 拖曳系数
    UPROPERTY()
    int32 LF = 0;//Lift Force - 升力
    UPROPERTY()
    int32 PR = 0;//Pressure - 压力
    UPROPERTY()
    int32 VC = 0;//Volume Conservation - 体积守恒
    UPROPERTY()
    int32 DF = 0;//Dynamic Friction - 动态摩擦
    UPROPERTY()
    int32 MT = 0;//Pose Matching - 姿态匹配
    UPROPERTY()
    int32 CHR = 0;//Rigid Contact Hardness - 与刚体碰撞硬度
    UPROPERTY()
    int32 KHR = 0;//Kinetic Contact Hardness - 动态碰撞硬度
    UPROPERTY()
    int32 SHR = 0;//Soft Contact Hardness - 与柔性体碰撞硬度
    UPROPERTY()
    int32 AHR = 0;//Anchor Hardness - 与锚点碰撞硬度
    UPROPERTY()
    int32 SRHR_CL = 0;//Soft vs Rigid Hardness (Cluster) - 柔性体与刚体碰撞硬度（Cluster）
    UPROPERTY()
    int32 SKHR_CL = 0;//Soft vs Kinetic Hardness (Cluster) - 柔性体与动态碰撞硬度（Cluster）
    UPROPERTY()
    int32 SSHR_CL = 0;//Soft vs Soft Hardness (Cluster) - 柔性体与柔性体碰撞硬度（Cluster）
    UPROPERTY()
    int32 SR_SPLT_CL = 0;//Soft vs Rigid Impulse Splitting (Cluster) - 柔性体与刚体冲量分割（Cluster）
    UPROPERTY()
    int32 SK_SPLT_CL = 0;//Soft vs Kinetic Impulse Splitting (Cluster) - 柔性体与动态冲量分割（Cluster）
    UPROPERTY()
    int32 SS_SPLT_CL = 0;//Soft vs Soft Impulse Splitting (Cluster) - 柔性体与柔性体冲量分割（Cluster）
    UPROPERTY()
    int32 V_IT = 0;//Velocities Iterations - 速度迭代
    UPROPERTY()
    int32 P_IT = 0;//Positions Iterations - 位置迭代
    UPROPERTY()
    int32 D_IT = 0;//Densities Iterations - 密度迭代
    UPROPERTY()
    int32 C_IT = 0;//Cluster Iterations - 簇迭代
    UPROPERTY()
    float LST = 0.0f;//Lift Coefficient - 升力系数
    UPROPERTY()
    float AST = 0.0f;//Aero Surface Tension - 空气表面
    UPROPERTY()
    float VST = 0.0f;//Volume Surface Tension - 体积表面
    UPROPERTY()
    TArray<FMMDSoftBodyAnchor> Anchors;

    UPROPERTY()
    TArray<int32> PinVertices;


};

#pragma endregion




USTRUCT(BlueprintInternalUseOnly)
struct UE5MMDTOOLS_API FAGN_MMDSkeletalControl : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    FAGN_MMDSkeletalControl();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
    bool bEnablePhysics=true;
    bool bIsInitialized=false;
    //MMD数据
    UPROPERTY()
    TArray<FMMDRigidBodyRuntime> RuntimeRigidBodies;

    UPROPERTY()
    TArray<FMMDJointRuntime> RuntimeJoints;

    UPROPERTY()
    TArray<FMMDSoftBodyRuntime> RuntimeSoftBodies;
    
    
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
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
        UAnimBlueprint* AnimBP,
        bool bConnectToRoot = true
    );

    static UAnimGraphNode_MMDSkeletalControl* InsertMMDNodeBetween(
        UAnimBlueprint* AnimBP,
        UAnimGraphNode_Base* UpstreamNode,
        UAnimGraphNode_Base* DownstreamNode
    );
};

#endif // WITH_EDITORONLY_DATA