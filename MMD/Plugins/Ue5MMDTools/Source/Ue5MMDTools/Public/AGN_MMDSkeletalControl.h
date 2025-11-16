// AGN_MMDSkeletalControl.h
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
// ✅ Editor相关头文件要在 .generated.h 之前
#if WITH_EDITORONLY_DATA
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimBlueprint.h"
#endif
#include "TPMXParser.h"
#include "MMDPhysicsSimulator.h"
#include "btBulletDynamicsCommon.h"
#include "AGN_MMDSkeletalControl.generated.h"

USTRUCT(BlueprintInternalUseOnly)
struct UE5MMDTOOLS_API FAGN_MMDSkeletalControl : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    FAGN_MMDSkeletalControl();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
    bool bEnablePhysics=true;
    bool bIsInitialized=false;
    // 基本物理参数（与模拟器默认一致，可在蓝图调）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float UnitScale = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    int32 MaxSubSteps = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
    float FixedTimeStep = 1.f / 60.f;

    PMXDatas PMXData;
    //MMD数据
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
    void InitializedMMDPhysics(const PMXDatas& InPMXData, USkeletalMeshComponent* SkelComp);
private:
    TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> Simulator;
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