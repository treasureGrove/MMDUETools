// AGN_MMDSkeletalControl.cpp
#include "AGN_MMDSkeletalControl.h"
#include "TPMXParser.h"
#include "Animation/AnimInstanceProxy.h"
#include "MMDPhysicsSimulator.h"
#include "DrawDebugHelpers.h" // 新增：用于绘制调试刚体
#include "Async/Async.h" 

static FMMDPhysicsSimulator GMMDPhysicsSim;
FAGN_MMDSkeletalControl::FAGN_MMDSkeletalControl()
    : bEnablePhysics(true)
{
}

void FAGN_MMDSkeletalControl::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output,
    TArray<FBoneTransform>& OutBoneTransforms)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateComponentPose_AnyThread)

    if (!bEnablePhysics) return;
    if (RuntimeRigidBodies.Num() == 0) return;

    const float DeltaTime = Output.AnimInstanceProxy->GetDeltaSeconds();
    if (DeltaTime <= KINDA_SMALL_NUMBER) return;

    // 1) 物理步进（固定步长在模拟器内部实现）
    // 注意：如果你已在其他系统里每帧调用了 SimulatePhysics，就不要在这里重复调用。
    GMMDPhysicsSim.SimulatePhysics(
        RuntimeRigidBodies,
        RuntimeJoints,
        RuntimeSoftBodies,
        Output,
        DeltaTime);

    // 2) 用物理结果写回骨骼（你已有的逻辑，保留并增强了滤波与混合）
    OutBoneTransforms.Reset();

    for (FMMDRigidBodyRuntime& Rigid : RuntimeRigidBodies)
    {
        if (!Rigid.CompactBoneIndex.IsValid())
            continue;

        if (Rigid.PhysicsMode == 0)
            continue;

        // 物理结果滤波
        if (Rigid.FilteredRotation.Equals(FQuat::Identity) && Rigid.FilteredPosition.IsZero())
        {
            Rigid.FilteredPosition = Rigid.PrevPosition;
            Rigid.FilteredRotation = Rigid.PrevRotation;
        }
        const float FilterAlpha = 0.6f;
        Rigid.FilteredPosition = FMath::Lerp(Rigid.FilteredPosition, Rigid.PrevPosition, FilterAlpha);
        Rigid.FilteredRotation = FQuat::Slerp(Rigid.FilteredRotation, Rigid.PrevRotation, FilterAlpha).GetNormalized();

        // 刚体世界 -> 骨骼世界
        const FTransform RigidWorldTransform(Rigid.FilteredRotation, Rigid.FilteredPosition);
        const FTransform BoneWorldTransform = Rigid.RigidBodyOffset.Inverse() * RigidWorldTransform;

        // 转局部
        FCompactPoseBoneIndex ParentBoneIndex = Output.Pose.GetPose().GetParentBoneIndex(Rigid.CompactBoneIndex);
        FTransform ParentWorldTransform = FTransform::Identity;
        if (ParentBoneIndex.IsValid())
        {
            ParentWorldTransform = Output.Pose.GetComponentSpaceTransform(ParentBoneIndex);
        }
        FTransform BoneLocalTransform = BoneWorldTransform.GetRelativeTransform(ParentWorldTransform);
        BoneLocalTransform.NormalizeRotation();

        // 保护：保留缩放、限制单帧平移
        const FTransform OriginalLocal = Output.Pose.GetLocalSpaceTransform(Rigid.CompactBoneIndex);
        BoneLocalTransform.SetScale3D(OriginalLocal.GetScale3D());
        const float MaxTranslationCm = 50.0f;
        const FVector DeltaLoc = BoneLocalTransform.GetLocation() - OriginalLocal.GetLocation();
        if (DeltaLoc.Size() > MaxTranslationCm)
        {
            BoneLocalTransform.SetLocation(OriginalLocal.GetLocation() + DeltaLoc.GetSafeNormal() * MaxTranslationCm);
        }

        // 混合模式
        if (Rigid.PhysicsMode == 2)
        {
            const float W = FMath::Clamp(Rigid.FollowStrength, 0.0f, 1.0f);
            const FVector Loc = FMath::Lerp(OriginalLocal.GetLocation(), BoneLocalTransform.GetLocation(), W);
            const FQuat   Rot = FQuat::Slerp(OriginalLocal.GetRotation(), BoneLocalTransform.GetRotation(), W).GetNormalized();
            BoneLocalTransform.SetLocation(Loc);
            BoneLocalTransform.SetRotation(Rot);
            BoneLocalTransform.SetScale3D(OriginalLocal.GetScale3D());
        }

        if (!BoneLocalTransform.ContainsNaN())
        {
            OutBoneTransforms.Add(FBoneTransform(Rigid.CompactBoneIndex, BoneLocalTransform));
        }

#if WITH_EDITOR
        // 可选：调试绘制
        UPrimitiveComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();
        if (SkelComp)
        {
            UWorld* World = SkelComp->GetWorld();
            const FVector Center = RigidWorldTransform.GetLocation();
            const FQuat Rot = RigidWorldTransform.GetRotation();
            const FVector Size = Rigid.Size;
            const FVector Velocity = Rigid.Velocity;
            const FString Name = Rigid.NameEN;
            const int32 ShapeType = Rigid.ShapeType;
            const float Duration = 0.05f;

            if (World)
            {
                AsyncTask(ENamedThreads::GameThread, [World, Center, Rot, Size, Velocity, Name, ShapeType, Duration]()
                    {
                        if (!World) return;
                        if (ShapeType == 0)
                        {
                            float Radius = FMath::Max(Size.X, 1.0f);
                            DrawDebugSphere(World, Center, Radius, 12, FColor::Green, false, Duration, 0, 1.0f);
                        }
                        else
                        {
                            DrawDebugBox(World, Center, Size, Rot, FColor::Blue, false, Duration, 0, 1.0f);
                        }
                        if (!Velocity.IsNearlyZero(1e-3f))
                        {
                            DrawDebugLine(World, Center, Center + Velocity * 0.05f, FColor::Red, false, Duration, 0, 1.0f);
                        }
#if !UE_BUILD_SHIPPING
                        DrawDebugString(World, Center + FVector(0, 0, 5.0f), Name, nullptr, FColor::White, Duration, false, 0.8f);
#endif
                    });
            }
        }
#endif
    }

    if (OutBoneTransforms.Num() > 1)
    {
        OutBoneTransforms.Sort(FCompareBoneTransformIndex());
    }
}

bool FAGN_MMDSkeletalControl::IsValidToEvaluate(
    const USkeleton* Skeleton,
    const FBoneContainer& RequiredBones)
{
    return bEnablePhysics;
}

void FAGN_MMDSkeletalControl::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
    if (bIsInitialized) {
        return;
    }
    for (FMMDRigidBodyRuntime& Rigid : RuntimeRigidBodies)
    {
        FSkeletonPoseBoneIndex SkeletonBoneIndex(Rigid.RelatedBoneIndex + 1);
        Rigid.CompactBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonPoseIndex(SkeletonBoneIndex);

        if (Rigid.CompactBoneIndex.IsValid()) {
            Rigid.Velocity = FVector::ZeroVector;
            Rigid.AngularVelocity = FVector::ZeroVector;
        }
    }
    bIsInitialized = true;
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