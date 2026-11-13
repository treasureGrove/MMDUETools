#pragma once
#include "AGN_MMDSkeletalControl.h"
#include "AnimGraphNode_SkeletalControlBase.h"
#include "Animation/AnimBlueprint.h"
#include "AGN_MMDSkeletalControlEditor.generated.h"

UCLASS()
class UE5MMDTOOLSEDITOR_API UAnimGraphNode_MMDSkeletalControl : public UAnimGraphNode_SkeletalControlBase
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


class UE5MMDTOOLSEDITOR_API FMMDAnimGraphHelper
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
