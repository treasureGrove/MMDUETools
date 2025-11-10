// AGN_MMDSkeletalControl.cpp
#include "AGN_MMDSkeletalControl.h"
#include "TPMXParser.h"
#include "Animation/AnimInstanceProxy.h"
#include "MMDPhysicsSimulator.h"
#include "DrawDebugHelpers.h" // 新增：用于绘制调试刚体
#include "Async/Async.h" 

FAGN_MMDSkeletalControl::FAGN_MMDSkeletalControl()
    : bEnablePhysics(true)
{
}

void FAGN_MMDSkeletalControl::EvaluateSkeletalControl_AnyThread(
    FComponentSpacePoseContext& Output,
    TArray<FBoneTransform>& OutBoneTransforms)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_ANIMNODE(EvaluateComponentPose_AnyThread)

    if (!bEnablePhysics)
        return;

    if (RuntimeRigidBodies.Num() == 0)
        return;

    const float DeltaTime = Output.AnimInstanceProxy->GetDeltaSeconds();

    if (DeltaTime <= KINDA_SMALL_NUMBER)
        return;

    // ✅ 使用新的物理模拟器
    FMMDPhysicsSimulator::SimulatePhysics(
        RuntimeRigidBodies,
        RuntimeJoints,
        RuntimeSoftBodies,
        Output,
        DeltaTime
    );

    // ✅ 将物理结果写回骨骼，并绘制刚体调试可视化（仅在编辑/预览时）
    for (const FMMDRigidBodyRuntime& Rigid : RuntimeRigidBodies)
    {
        if (Rigid.PhysicsMode == 0 || !Rigid.CompactBoneIndex.IsValid())
            continue;

        // 刚体世界变换
        FTransform RigidWorldTransform(Rigid.PrevRotation, Rigid.PrevPosition);

        // 反算骨骼世界：刚体世界 * 偏移逆
        FTransform OffsetInverse = Rigid.RigidBodyOffset.Inverse();
        FTransform BoneWorldTransform = OffsetInverse * RigidWorldTransform;

        // 父骨世界变换
        FCompactPoseBoneIndex ParentBoneIndex = Output.Pose.GetPose().GetParentBoneIndex(Rigid.CompactBoneIndex);
        FTransform ParentWorldTransform = FTransform::Identity;
        if (ParentBoneIndex.IsValid())
        {
            ParentWorldTransform = Output.Pose.GetComponentSpaceTransform(ParentBoneIndex);
        }

        // 计算骨骼局部变换（物理结果）
        FTransform BoneLocalTransform = BoneWorldTransform.GetRelativeTransform(ParentWorldTransform);
        BoneLocalTransform.NormalizeRotation();

        // 获取当前动画/原始局部变换（用于保护与混合）
        FTransform OriginalLocal = Output.Pose.GetLocalSpaceTransform(Rigid.CompactBoneIndex);

        // 1) 保留原始缩放，避免无意间改写缩放造成拉伸
        BoneLocalTransform.SetScale3D(OriginalLocal.GetScale3D());

        // 2) 限制位置偏移（防止单帧过大跳跃导致拉伸）
        const float MaxTranslationCm = 50.0f; // 最大允许偏移（厘米，可根据需求调小）
        FVector Delta = BoneLocalTransform.GetLocation() - OriginalLocal.GetLocation();
        if (Delta.Size() > MaxTranslationCm)
        {
            BoneLocalTransform.SetLocation(OriginalLocal.GetLocation() + Delta.GetSafeNormal() * MaxTranslationCm);
        }

        // 3) 对于混合模式 (Physics+Bone)，做平滑混合（位置线性、旋转球面插值）
        if (Rigid.PhysicsMode == 2)
        {
            const float PhysicsWeight = 0.8f; // 0..1，越大越由物理主导
            FVector BlendedLoc = FMath::Lerp(OriginalLocal.GetLocation(), BoneLocalTransform.GetLocation(), PhysicsWeight);
            FQuat BlendedRot = FQuat::Slerp(OriginalLocal.GetRotation(), BoneLocalTransform.GetRotation(), PhysicsWeight);
            BlendedRot.Normalize();
            BoneLocalTransform.SetLocation(BlendedLoc);
            BoneLocalTransform.SetRotation(BlendedRot);
            BoneLocalTransform.SetScale3D(OriginalLocal.GetScale3D());
        }

        // 4) 最后有效性检查并写回
        if (!BoneLocalTransform.ContainsNaN())
        {
            OutBoneTransforms.Add(FBoneTransform(Rigid.CompactBoneIndex, BoneLocalTransform));
        }

        // ============== 调试绘制部分 ==============
#if WITH_EDITOR
        // 在编辑器/预览模式下绘制刚体形状和速度矢量（在游戏线程执行）
        UPrimitiveComponent* SkelComp = Output.AnimInstanceProxy->GetSkelMeshComponent();
        if (SkelComp)
        {
            UWorld* World = SkelComp->GetWorld();
            // 拷贝到本地变量以在线程间传递
            const FVector Center = RigidWorldTransform.GetLocation();
            const FQuat Rot = RigidWorldTransform.GetRotation();
            const FVector Size = Rigid.Size;
            const FVector Velocity = Rigid.Velocity;
            const FString Name = Rigid.NameEN;
            const int32 ShapeType = Rigid.ShapeType;

            const float Duration = 0.1f; // 给一个短持续时间，方便在 Preview 中看到

            if (World)
            {
                // 绘制必须在游戏线程
                AsyncTask(ENamedThreads::GameThread, [World, Center, Rot, Size, Velocity, Name, ShapeType, Duration]()
                    {
                        if (!World) return;
                        if (ShapeType == 0)
                        {
                            float Radius = FMath::Max(Size.X, 1.0f);
                            DrawDebugSphere(World, Center, Radius, 12, FColor::Green, false, Duration, 0, 1.5f);
                        }
                        else
                        {
                            DrawDebugBox(World, Center, Size, Rot, FColor::Blue, false, Duration, 0, 1.5f);
                        }

                        if (!Velocity.IsNearlyZero(1e-4f))
                        {
                            DrawDebugLine(World, Center, Center + Velocity * 0.05f, FColor::Red, false, Duration, 0, 1.5f);
                        }

#if !UE_BUILD_SHIPPING
                        DrawDebugString(World, Center + FVector(0, 0, 5.0f), Name, nullptr, FColor::White, Duration, false, 0.8f);
#endif
                    });
            }
            else
            {
                // 如果没有 World，输出日志作为回退诊断
                UE_LOG(LogTemp, Warning, TEXT("MMD Debug: No World for drawing. Rigid %s at %s, Vel=%s"), *Name, *Center.ToString(), *Velocity.ToString());
            }
        }
#endif
        // ============== 调试绘制结束 ==============
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