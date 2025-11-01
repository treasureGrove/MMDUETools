#include "AGN_MMDSkeletalControl.h"
#include "Animation/AnimInstanceProxy.h"

#define LOCTEXT_NAMESPACE "AGN_MMDSkeletalControl"

FMMDParticle::FMMDParticle()
    : BoneIndex(INDEX_NONE)
    , Position(FVector::ZeroVector)
    , PrevPosition(FVector::ZeroVector)
    , Mass(1.0f)
    , InvMass(1.0f)
{
}
FAnimNode_MMDPhysics::FAnimNode_MMDPhysics()
    : bEnablePhysics(true)
    , Damping(0.1f) 
    , bApplyPMXPhysics(false)  
    , Gravity(0.f, 0.f, -980.f)
    , bNeedsRebuild(true)
{
}

void FAnimNode_MMDPhysics::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	bNeedsRebuild = true;
    UE_LOG(LogTemp, Log, TEXT("MMD Physics Node Initialized"));
}
bool FAnimNode_MMDPhysics::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
   return bEnablePhysics && TargetBoneNames.Num() > 0;
}
void FAnimNode_MMDPhysics::RebuildParticles(FComponentSpacePoseContext& Output)
{
    Particles.Reset();

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();

    for (const FName& BoneName : TargetBoneNames) {
        if(BoneName.IsNone()) {
            continue;
		}
        const int32 SkeletonBoneIndex = BoneContainer.GetReferenceSkeleton().FindBoneIndex(BoneName);
        if (SkeletonBoneIndex != INDEX_NONE) {
            const FCompactPoseBoneIndex CompactBoneIndex = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(SkeletonBoneIndex);

            if (CompactBoneIndex != INDEX_NONE) {
				FMMDParticle NewParticle;
				NewParticle.BoneIndex = CompactBoneIndex;

                const FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(CompactBoneIndex);
				NewParticle.Position = BoneTransform.GetLocation();
                NewParticle.PrevPosition = NewParticle.Position;
                
				Particles.Add(NewParticle);

				UE_LOG(LogTemp, Log, TEXT("Added particle for bone: %s"), *BoneName.ToString());
                
            }
            else
            {
				UE_LOG(LogTemp, Warning, TEXT("Bone %s not found in Compact Pose"), *BoneName.ToString());

            }
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("MMD Physics: Could not find bone '%s' in reference skeleton"), *BoneName.ToString());

        }
    }

}
void FAnimNode_MMDPhysics::SimulatePhysics(FComponentSpacePoseContext& Output)
{
    if(Particles.Num() == 0) {
        return;
	}
    //获取时间步长
    const float DeltaTime = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetDeltaSeconds():(1.0f/60.0f);
	const float ClampedDeltaTime = FMath::Clamp(DeltaTime, 0.001f, 0.033f); // Clamp to reasonable range
    //Verlet积分更新每个粒子
    for (FMMDParticle& Particle : Particles) {
        //Verlet积分的核心公式
		//velocity = current_position - previous_position
		//new_position = current_position + velocity + acceleration * delta_time^2

        //计算阻尼
		const float DampingFactor = FMath::Pow(1.0f - FMath::Clamp(Damping, 0.0f, 1.0f), ClampedDeltaTime);

        //1.计算当前速度
        const FVector CurrentVelocity = (Particle.Position - Particle.PrevPosition)*DampingFactor;
        
        //2.保存当前位置作为下一帧的“前一位置”
        const FVector CurrentPosition = Particle.Position;

		//3.计算加速度（重力/质量）
        const FVector Acceleration = Gravity * Particle.InvMass;

        //4.Verlet积分：计算新位置
		Particle.Position = CurrentPosition + CurrentVelocity + Acceleration * ClampedDeltaTime * ClampedDeltaTime;
    
        //5.更新前一位置
		Particle.PrevPosition = CurrentPosition;

        UE_LOG(LogTemp, VeryVerbose, TEXT("Particle moved from %s to %s"),
            *CurrentPosition.ToString(), *Particle.Position.ToString());
    }

}
void FAnimNode_MMDPhysics::WriteBackTransforms(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
    for (const FMMDParticle& Particle : Particles) {
		FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(Particle.BoneIndex);

		BoneTransform.SetLocation(Particle.Position);

		OutBoneTransforms.Emplace(Particle.BoneIndex, BoneTransform);
		//验证转换：CompactPoseBoneIndex -> SkeletonBoneIndex(用于调试)
        const int32 SkeletonIndex = BoneContainer.GetSkeletonIndex(Particle.BoneIndex);
		const FName BoneName = BoneContainer.GetReferenceSkeleton().GetBoneName(SkeletonIndex);

        UE_LOG(LogTemp, VeryVerbose, TEXT("Writing back transform for bone '%s' at position %s"),
            *BoneName.ToString(), *Particle.Position.ToString());
    }
	//按照骨骼层次排序（父骨骼在前，子骨骼在后）
    OutBoneTransforms.Sort([&BoneContainer](const FBoneTransform& A, const FBoneTransform& B)
        {
            return BoneContainer.BoneIsChildOf(B.BoneIndex, A.BoneIndex);
        });
	UE_LOG(LogTemp, Log, TEXT("Wrote back %d bone transforms from MMD Physics"), OutBoneTransforms.Num());

}
void FAnimNode_MMDPhysics::ApplyPMXPhysicsData(const TPMXParser& PMXParser,const FBoneContainer& BoneContainer)
{
    if (!bApplyPMXPhysics)return;
    //遍历PMX的刚体数据
    
}
void FAnimNode_MMDPhysics::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	OutBoneTransforms.Reset();

    if (!bEnablePhysics) {
		return;
    }
	UE_LOG(LogTemp, Log, TEXT("Evaluating MMD Physics Node"));
    //重建粒子系统
    if (bNeedsRebuild) {
        RebuildParticles(Output);
		bNeedsRebuild = false;
    }
	//第二步：模拟物理
	SimulatePhysics(Output);

	//第三步：将粒子位置写回骨骼
    WriteBackTransforms(Output, OutBoneTransforms);


}
#if WITH_EDITOR
FText UAGN_MMDSkeletalControl::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
    return LOCTEXT("MMDPhysicsTitle", "MMD Physics (Working!)");
}

FString UAGN_MMDSkeletalControl::GetNodeCategory() const
{
    return TEXT("MMD Tools");
}

#endif
#undef LOCTEXT_NAMESPACE

