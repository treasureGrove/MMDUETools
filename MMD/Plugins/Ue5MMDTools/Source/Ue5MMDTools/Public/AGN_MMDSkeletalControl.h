#pragma once  

#include "CoreMinimal.h"
#include "BoneIndices.h"
#include "BoneContainer.h"
#include "Engine.h"
#include "TPMXParser.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "AGN_MMDSkeletalControl.generated.h"

USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FPMXPhysicsData
{
	GENERATED_BODY()

	UPROPERTY()
	FName BoneName;

	UPROPERTY()
	int32 RigidIndex;

	UPROPERTY()
	FVector Size;

	UPROPERTY()
	FVector Position;

	UPROPERTY()
	FVector Rotation;

	UPROPERTY()
	float Mass;

	UPROPERTY()
	float LinearDamping;

	UPROPERTY()
	float AngularDamping;

	UPROPERTY()
	float Restitution;

	UPROPERTY()
	float Friction;

	UPROPERTY()
	uint8 PhysicsMode;//0-static 1-dynamic 2-bonetracked

	UPROPERTY()
	uint8 ShapeType;//0-sphere 1-box 2-capsule

	FPMXPhysicsData()
		: BoneName(NAME_None)           // 空名称，表示未绑定到具体骨骼
		, RigidIndex(-1)                // -1 表示无效/未分配的索引
		, Size(FVector::ZeroVector)     // 注意：这里用SIze是因为你的声明中有拼写错误
		, Position(FVector::ZeroVector) // 原点位置
		, Rotation(FVector::ZeroVector) // 无旋转
		, Mass(1.0f)                    // 1kg，合理的默认质量
		, LinearDamping(0.0f)           // 无线性阻尼
		, AngularDamping(0.0f)          // 无角度阻尼  
		, Restitution(0.0f)             // 完全非弹性碰撞
		, Friction(0.5f)                // 中等摩擦力
		, PhysicsMode(1)                // 1=Dynamic，最常用的动态物理模式
		, ShapeType(0)                  // 0=Sphere，球体是最简单的碰撞形状
	{
	}

};


// MMD 物理粒子结构
USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDParticle
{
	GENERATED_BODY()


	UPROPERTY()
	FVector Position;
	UPROPERTY()
	FVector PrevPosition;  // 修复：改为 PrevPosition，保持一致
	UPROPERTY()
	float Mass;
	UPROPERTY()
	float InvMass; // 质量倒数，优化计算用

	FCompactPoseBoneIndex BoneIndex;

	FMMDParticle();

};

// MMD 物理动画节点
USTRUCT(BlueprintInternalUseOnly)
struct UE5MMDTOOLS_API FAnimNode_MMDPhysics : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	FAnimNode_MMDPhysics();

	// 修复：PinShownByDefault 而不是 PinShowByDefault
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
	bool bEnablePhysics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	TArray<FName> TargetBoneNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	FVector Gravity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Damping;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	bool bApplyPMXPhysics; // 是否应用PMX物理参数

	// 必须实现的虚函数
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;

private:
	// 运行时数据
	TArray<FMMDParticle> Particles;
	bool bNeedsRebuild;

	// 内部函数声明
	void RebuildParticles(FComponentSpacePoseContext& Output);
	void SimulatePhysics(FComponentSpacePoseContext& Output);
	void WriteBackTransforms(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms);
	void ApplyPMXPhysicsData(const TPMXParser& PMXParser,const FBoneContainer& BoneContainer);
};

#if WITH_EDITOR


UCLASS()
class UE5MMDTOOLS_API UAGN_MMDSkeletalControl : public UAnimGraphNode_SkeletalControlBase  // 修复：类名应该是 UAGN_MMDSkeletalControl
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Settings")
	FAnimNode_MMDPhysics Node;

	virtual const FAnimNode_SkeletalControlBase* GetNode() const override { return &Node; }
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FString GetNodeCategory() const override;
};
#endif

