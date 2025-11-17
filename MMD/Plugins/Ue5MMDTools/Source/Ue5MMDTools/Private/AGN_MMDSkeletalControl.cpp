// AGN_MMDSkeletalControl.cpp
#include "AGN_MMDSkeletalControl.h"
#include "TPMXParser.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"     
#include "MMDPhysicsSimulator.h"
#include "DrawDebugHelpers.h"
#include "MMDAnimInstance.h"
#include "Async/Async.h" 
#include "HAL/IConsoleManager.h"
#include "Animation/AnimTypes.h"

static TAutoConsoleVariable<int32> CVarMMDPhysDebugNode(TEXT("mmd.PhysDebug"),0,TEXT("Draw MMD Bullet rigid bodies and joints in UE space. 0:off, 1:on"),ECVF_Default);

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

    OutBoneTransforms.Reset();
    if (!bEnablePhysics) return;
    if (!Output.AnimInstanceProxy) return;

    const UObject* AnimObj = Output.AnimInstanceProxy->GetAnimInstanceObject();
    if (!AnimObj || !AnimObj->IsA<UMMDAnimInstance>()) return;

    auto* MMDProxy = static_cast<FMMDAnimInstanceProxy*>(Output.AnimInstanceProxy);
    TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> StrongSimulator = MMDProxy->CacheSimulator.Pin();

    if (!StrongSimulator.IsValid())
    {
        // Fallback: query from AnimInstance on game thread-safe path
        if (const UMMDAnimInstance* MI = Cast<UMMDAnimInstance>(AnimObj))
        {
            StrongSimulator = MI->GetSimulator();
        }
    }

    if (!StrongSimulator.IsValid())
    {
        return;
    }

    USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();
    if (!SkelComp) return;

    TArray<FTransform> BoneWorldUE;
    BuildBoneWorldArray(Output, BoneWorldUE);

    const float DeltaSeconds = Output.AnimInstanceProxy->GetDeltaSeconds();
    StrongSimulator->TickMMDPhysics(DeltaSeconds, BoneWorldUE);

    if (CVarMMDPhysDebugNode->GetInt() != 0)
    {
        TWeakPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> WeakSim = StrongSimulator;
        AsyncTask(ENamedThreads::GameThread, [WeakSim]()
        {
            if (auto S = WeakSim.Pin()) S->DebugDraw();
        });
    }

    const FTransform W2C = SkelComp->GetComponentTransform().Inverse();
    const FBoneContainer& BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();

    const int32 NumCompact = BoneContainer.GetCompactPoseNumBones();
    OutBoneTransforms.Reserve(NumCompact);

    // Start from current component-space pose
    TArray<FTransform> CompSpaceFinal; CompSpaceFinal.SetNum(NumCompact);
    for (int32 CompactIdx = 0; CompactIdx < NumCompact; ++CompactIdx)
    {
        const FCompactPoseBoneIndex CPIndex(CompactIdx);
        CompSpaceFinal[CompactIdx] = Output.Pose.GetComponentSpaceTransform(CPIndex);
    }

    // Track which compact bones were updated by physics (based on actual change)
    TArray<bool> bChanged; bChanged.Init(false, NumCompact);

    // Overwrite with physics results using mesh->compact mapping
    const int32 NumMeshBones = SkelComp->GetNumBones();
    for (int32 MeshIndex = 0; MeshIndex < NumMeshBones; ++MeshIndex)
    {
        FCompactPoseBoneIndex CPIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
        if (CPIndex.IsValid() && BoneWorldUE.IsValidIndex(MeshIndex))
        {
            const int32 Cpi = CPIndex.GetInt();
            const FTransform NewCS = BoneWorldUE[MeshIndex] * W2C;
            // mark changed only if pose actually differs
            const bool bDifferent = !NewCS.Equals(CompSpaceFinal[Cpi], 1e-3f);
            if (bDifferent)
            {
                bChanged[Cpi] = true;
                CompSpaceFinal[Cpi] = NewCS;
            }
        }
    }

    // Convert to local space and emit only changed bones
    for (int32 CompactIdx = 0; CompactIdx < NumCompact; ++CompactIdx)
    {
        if (!bChanged[CompactIdx])
        {
            continue;
        }
        const FCompactPoseBoneIndex CPIndex(CompactIdx);
        FCompactPoseBoneIndex Parent = BoneContainer.GetParentBoneIndex(CPIndex);
        const FTransform& ThisCS = CompSpaceFinal[CompactIdx];
        FTransform LocalT = Parent.IsValid() ? ThisCS.GetRelativeTransform(CompSpaceFinal[Parent.GetInt()]) : ThisCS;
        OutBoneTransforms.Emplace(CPIndex, LocalT);
    }

    OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
void FAGN_MMDSkeletalControl::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
   
}
void FAGN_MMDSkeletalControl::BuildBoneWorldArray(FComponentSpacePoseContext& Output, TArray<FTransform>& OutWorld)
{
    const USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetSkelMeshComponent() : nullptr;
    const int32 NumMeshBones = SkelComp ? SkelComp->GetNumBones() : 0;
    OutWorld.SetNum(NumMeshBones);

    const FTransform C2W = SkelComp ? SkelComp->GetComponentTransform() : FTransform::Identity;

    const FBoneContainer& BoneContainer = Output.AnimInstanceProxy->GetRequiredBones();
    // Iterate mesh bones and map to compact pose index
    for (int32 MeshIndex = 0; MeshIndex < NumMeshBones; ++MeshIndex)
    {
        FCompactPoseBoneIndex CPIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
        if (CPIndex.IsValid())
        {
            const FTransform BoneCS = Output.Pose.GetComponentSpaceTransform(CPIndex);
            OutWorld[MeshIndex] = BoneCS * C2W;
        }
        else
        {
            // leave as identity if not part of the current LOD/pose
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
    UAnimBlueprint* AnimBP,
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

    // ✅ 创建 Local To Component 节点
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

    // ✅ 创建 MMD 节点
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

    // ✅ 创建 Component To Local 节点
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