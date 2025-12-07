// AGN_MMDSkeletalControl.cpp
#include "AGN_MMDSkeletalControl.h"
#include "TPMXParser.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"     
#include "MMDPhysicsSimulator.h"
#include "DrawDebugHelpers.h"
#include "Async/Async.h" 
#include "Animation/AnimTypes.h"


FAGN_MMDSkeletalControl::FAGN_MMDSkeletalControl()
    : bEnablePhysics(true)
{
}

bool FAGN_MMDSkeletalControl::IsValidToEvaluate(
    const USkeleton* Skeleton,
    const FBoneContainer& RequiredBones)
{
    return true;
}
void FAGN_MMDSkeletalControl::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output,
    TArray<FBoneTransform>& OutBoneTransforms)
{   
    if (PMXData.ModelRigids.Num() == 0) {
        return;
        //checkf(false, TEXT("PMXData is empty when initializing the simulator!"));
    }
	if (!bSimulatorInitialized || !SimulatorStrongPtr.IsValid())
    {
        
		TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> NewSimulator = MakeShared<FMMDPhysicsSimulator, ESPMode::ThreadSafe>();
		const bool bInitSuccess = NewSimulator->InitializeFromPMX(PMXData, Output.AnimInstanceProxy->GetSkelMeshComponent());
        if (bInitSuccess) {
			UE_LOG(LogTemp, Log, TEXT("MMD Physics Simulator initialized successfully."));
        }
		SimulatorStrongPtr = MoveTemp(NewSimulator);
		bSimulatorInitialized = bInitSuccess;
		if (SimulatorStrongPtr.IsValid())
        UE_LOG(LogTemp, Log, TEXT("[MMDPhysics] ✅ 从 PMXData 创建 Simulator: Rigids=%d"),
            PMXData.ModelRigids.Num())
    }
    if (SimulatorStrongPtr.IsValid()) {
        checkf(false, TEXT("PMXData is empty at first evaluation!"));
        SimulatorStrongPtr->TickMMDPhysics(Output, OutBoneTransforms);
        SimulatorStrongPtr->SetDebugEnabled(bDrawDebug);
        OutBoneTransforms.Sort(FCompareBoneTransformIndex());
    }
	
   
}
void FAGN_MMDSkeletalControl::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
   
}
void FAGN_MMDSkeletalControl::BuildBoneWorldArray(FComponentSpacePoseContext& Output, TArray<FTransform>& OutWorld)
{
    const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetSkelMeshComponent() : nullptr;
    if (!SkelComp) { OutWorld.Reset(); return; }

    const USkeletalMesh* Mesh = SkelComp->GetSkeletalMeshAsset();
    const int32 NumMeshBones = SkelComp->GetNumBones();
    OutWorld.SetNum(NumMeshBones);

    const FTransform C2W = SkelComp->GetComponentTransform();
    const FBoneContainer& BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();

    // 仅当有 Mesh 才构建 RefCS
    TArray<FTransform> RefCS;
    const bool bHaveRef = (Mesh != nullptr);
    if (bHaveRef)
    {
        const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
        const TArray<FTransform>& RefLocal = RefSkel.GetRefBonePose();
        RefCS.SetNum(NumMeshBones);
        for (int32 i = 0; i < NumMeshBones; ++i)
        {
            const int32 Parent = RefSkel.GetParentIndex(i);
            RefCS[i] = (Parent >= 0) ? (RefLocal[i] * RefCS[Parent]) : RefLocal[i];
        }
    }

    for (int32 MeshIndex = 0; MeshIndex < NumMeshBones; ++MeshIndex)
    {
        const FCompactPoseBoneIndex CPIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
        if (CPIndex.IsValid())
        {
            const FTransform BoneCS = Output.Pose.GetComponentSpaceTransform(CPIndex);
            OutWorld[MeshIndex] = BoneCS * C2W;
        }
        else if (bHaveRef)
        {
            OutWorld[MeshIndex] = RefCS[MeshIndex] * C2W;
        }
        else
        {
            // 用 Identity 标记“无效”，上层据此跳过，不制造“世界=组件”的假数据
            OutWorld[MeshIndex] = FTransform::Identity;
        }
    }
}
#if WITH_EDITORONLY_DATA

#include "Kismet2/KismetEditorUtilities.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "EdGraph/EdGraphPin.h"

#define LOCTEXT_NAMESPACE "MMDSkeletalControl"
#pragma region 节点名字
FText UAnimGraphNode_MMDSkeletalControl::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("NodeTitle", "MMD Skeletal Control");
}

FText UAnimGraphNode_MMDSkeletalControl::GetTooltipText() const
{
    return LOCTEXT("NodeTooltip", "MMD风格的骨骼控制节点");
}

FString UAnimGraphNode_MMDSkeletalControl::GetNodeCategory() const
{
    return TEXT("Animation|Skeletal Control");
}

FLinearColor UAnimGraphNode_MMDSkeletalControl::GetNodeTitleColor() const
{
    return FLinearColor(0.2f, 0.8f, 0.3f);
}

const FAnimNode_SkeletalControlBase* UAnimGraphNode_MMDSkeletalControl::GetNode() const
{
    return &Node;
}
#pragma endregion



UAnimGraphNode_MMDSkeletalControl* FMMDAnimGraphHelper::AddMMDNodeToAnimBP(
	UAnimBlueprint* AnimBP,const FPMXDatas& InputPMXData,
    bool bConnectToRoot)
{
    if (!AnimBP)
    {
        UE_LOG(LogTemp, Error, TEXT("AnimBP is null"));
        return nullptr;
    }
    // 获取AnimGraph
    UEdGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        if (Graph->GetFName() == TEXT("AnimGraph"))
        {
            AnimGraph = Graph;
            break;
        }
    }

    if (!AnimGraph)
    {
        UE_LOG(LogTemp, Error, TEXT("AnimGraph not found"));
        return nullptr;
    }

    // 查找Root节点
    UAnimGraphNode_Root* RootNode = nullptr;
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        RootNode = Cast<UAnimGraphNode_Root>(Node);
        if (RootNode)
        {
            break;
        }
    }

    if (!RootNode)
    {
        UE_LOG(LogTemp, Error, TEXT("Root node not found"));
        return nullptr;
    }

    UAnimGraphNode_LocalToComponentSpace* LocalToComponentNode = NewObject<UAnimGraphNode_LocalToComponentSpace>(
        AnimGraph,
        UAnimGraphNode_LocalToComponentSpace::StaticClass(),
        NAME_None,
        RF_Transactional
    );

    LocalToComponentNode->CreateNewGuid();
    LocalToComponentNode->PostPlacedNewNode();
    LocalToComponentNode->AllocateDefaultPins();
    LocalToComponentNode->NodePosX = RootNode->NodePosX - 600;
    LocalToComponentNode->NodePosY = RootNode->NodePosY;
    AnimGraph->AddNode(LocalToComponentNode, false, false);

    UAnimGraphNode_MMDSkeletalControl* MMDNode = NewObject<UAnimGraphNode_MMDSkeletalControl>(
        AnimGraph,
        UAnimGraphNode_MMDSkeletalControl::StaticClass(),
        NAME_None,
        RF_Transactional
    );

    MMDNode->CreateNewGuid();
    MMDNode->PostPlacedNewNode();
    MMDNode->AllocateDefaultPins();
    MMDNode->NodePosX = RootNode->NodePosX - 400;
    MMDNode->NodePosY = RootNode->NodePosY;
    AnimGraph->AddNode(MMDNode, false, false);

	MMDNode->Node.PMXData = InputPMXData;
	MMDNode->MarkPackageDirty();

    UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalNode = NewObject<UAnimGraphNode_ComponentToLocalSpace>(
        AnimGraph,
        UAnimGraphNode_ComponentToLocalSpace::StaticClass(),
        NAME_None,
        RF_Transactional
    );

    ComponentToLocalNode->CreateNewGuid();
    ComponentToLocalNode->PostPlacedNewNode();
    ComponentToLocalNode->AllocateDefaultPins();
    ComponentToLocalNode->NodePosX = RootNode->NodePosX - 200;
    ComponentToLocalNode->NodePosY = RootNode->NodePosY;
    AnimGraph->AddNode(ComponentToLocalNode, false, false);

    if (bConnectToRoot)
    {
        // 连接1: LocalToComponent -> MMD
        UEdGraphPin* LocalToCompOutputPin = nullptr;
        for (UEdGraphPin* Pin : LocalToComponentNode->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("struct"))
            {
                LocalToCompOutputPin = Pin;
                break;
            }
        }

        UEdGraphPin* MMDInputPin = nullptr;
        for (UEdGraphPin* Pin : MMDNode->Pins)
        {
            if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("struct"))
            {
                MMDInputPin = Pin;
                break;
            }
        }

        if (LocalToCompOutputPin && MMDInputPin)
        {
            LocalToCompOutputPin->MakeLinkTo(MMDInputPin);
            UE_LOG(LogTemp, Log, TEXT("Connected: LocalToComponent -> MMD"));
        }

        // 连接2: MMD -> ComponentToLocal
        UEdGraphPin* MMDOutputPin = nullptr;
        for (UEdGraphPin* Pin : MMDNode->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("struct"))
            {
                MMDOutputPin = Pin;
                break;
            }
        }

        UEdGraphPin* CompToLocalInputPin = nullptr;
        for (UEdGraphPin* Pin : ComponentToLocalNode->Pins)
        {
            if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("struct"))
            {
                CompToLocalInputPin = Pin;
                break;
            }
        }

        if (MMDOutputPin && CompToLocalInputPin)
        {
            MMDOutputPin->MakeLinkTo(CompToLocalInputPin);
            UE_LOG(LogTemp, Log, TEXT("Connected: MMD -> ComponentToLocal"));
        }

        // 连接3: ComponentToLocal -> Root
        UEdGraphPin* CompToLocalOutputPin = nullptr;
        for (UEdGraphPin* Pin : ComponentToLocalNode->Pins)
        {
            if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("struct"))
            {
                CompToLocalOutputPin = Pin;
                break;
            }
        }

        UEdGraphPin* RootInputPin = nullptr;
        for (UEdGraphPin* Pin : RootNode->Pins)
        {
            if (Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Result"))
            {
                RootInputPin = Pin;
                break;
            }
        }

        if (CompToLocalOutputPin && RootInputPin)
        {
            if (RootInputPin->LinkedTo.Num() > 0)
            {
                RootInputPin->BreakAllPinLinks();
            }

            CompToLocalOutputPin->MakeLinkTo(RootInputPin);
            UE_LOG(LogTemp, Log, TEXT("Connected: ComponentToLocal -> Root"));
        }
    }

    AnimGraph->NotifyGraphChanged();
    FKismetEditorUtilities::CompileBlueprint(AnimBP);

    UE_LOG(LogTemp, Log, TEXT("Successfully added MMD node chain"));

    return MMDNode;
}

UAnimGraphNode_MMDSkeletalControl* FMMDAnimGraphHelper::InsertMMDNodeBetween(
    UAnimBlueprint* AnimBP,
    UAnimGraphNode_Base* UpstreamNode,
    UAnimGraphNode_Base* DownstreamNode)
{
    return nullptr;
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITORONLY_DATA