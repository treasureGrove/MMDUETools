// AGN_MMDSkeletalControl.h
#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"

// ✅ Editor相关头文件要在 .generated.h 之前
#if WITH_EDITORONLY_DATA
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimBlueprint.h"
#endif

#include "AGN_MMDSkeletalControl.generated.h"

// ============================================
// Runtime 节点（游戏运行时）
// ============================================

USTRUCT(BlueprintInternalUseOnly)
struct UE5MMDTOOLS_API FAGN_MMDSkeletalControl : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()

public:
    FAGN_MMDSkeletalControl();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (PinShownByDefault))
    bool bEnablePhysics;

    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
};

// ============================================
// Editor 节点（仅编辑器）
// ============================================

#if WITH_EDITORONLY_DATA

UCLASS(MinimalAPI)  // ✅ 不要用 UE5MMDTOOLS_API
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

// ============================================
// ✅ 工具类：不要导出API
// ============================================

class FMMDAnimGraphHelper  // ✅ 去掉 UE5MMDTOOLS_API
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