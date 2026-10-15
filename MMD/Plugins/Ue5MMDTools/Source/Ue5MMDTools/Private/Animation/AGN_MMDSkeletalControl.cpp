// AGN_MMDSkeletalControl.cpp
#include "AGN_MMDSkeletalControl.h"
#include "TPMXParser.h"
#include "Animation/AnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
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
    if (!bEnablePhysics || RigidBodySaveDataArray.IsEmpty())
    {
        return;
    }
    if (SimulatorPtr.IsValid())
    {
        SimulatorPtr->ConfigureSimulation(FixedTimeStep, MaxSubSteps);
        SimulatorPtr->TickMMDPhysics(Output, OutBoneTransforms);
        OutBoneTransforms.Sort(FCompareBoneTransformIndex());
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("MMDPhysicsSimulator is not initialized!"));
    }
}
void FAGN_MMDSkeletalControl::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);

	// 无物理模型正常透传；独立刚体可以没有关节。
	if (RigidBodySaveDataArray.IsEmpty())
	{
		SimulatorPtr.Reset();
		bIsInitialized = false;
		return;
	}
	if (!SimulatorPtr.IsValid())
	{
		SimulatorPtr = MakeShared<FMMDPhysicsSimulator, ESPMode::ThreadSafe>();
	}
    bIsInitialized = SimulatorPtr->InitializeFromPMX(RigidBodySaveDataArray, JointSaveDataArray,
        Context.AnimInstanceProxy->GetSkelMeshComponent());
    SimulatorPtr->ConfigureSimulation(FixedTimeStep, MaxSubSteps);
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
