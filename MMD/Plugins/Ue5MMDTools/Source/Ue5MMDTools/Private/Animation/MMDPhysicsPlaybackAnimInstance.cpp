#include "Animation/MMDPhysicsPlaybackAnimInstance.h"

#include "Actors/AMMDActor.h"
#include "Animation/AnimSingleNodeInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Import/MMDModelDataAsset.h"
#include "Import/MMDPhysicsDataUtils.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

struct FMMDPhysicsPlaybackProxy : public FAnimSingleNodeInstanceProxy
{
	explicit FMMDPhysicsPlaybackProxy(UAnimInstance* Instance) : FAnimSingleNodeInstanceProxy(Instance) {}

	virtual void Initialize(UAnimInstance* Instance) override
	{
		FAnimSingleNodeInstanceProxy::Initialize(Instance);
		ResetNode();
		Revision = 0;
	}

	virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
	{
		FAnimSingleNodeInstanceProxy::PreUpdate(Instance, IsPlaying() ? DeltaSeconds : 0.0f);
		UMMDPhysicsPlaybackAnimInstance* Playback = CastChecked<UMMDPhysicsPlaybackAnimInstance>(Instance);
		USkeletalMeshComponent* Component = Playback->GetSkelMeshComponent();
		if (Component && Playback->PhysicsSourceMesh != Component->GetSkeletalMeshAsset())
		{
			// 更换网格后不得继续套用旧骨架的刚体，下一次播放从新模型重新取配置。
			Playback->Rigids.Reset();
			Playback->Joints.Reset();
			Playback->PhysicsSourceMesh = Component->GetSkeletalMeshAsset();
			Playback->ResetPhysics();
		}
		if (Revision != Playback->PhysicsRevision || bPendingReset)
		{
			ResetNode();
			PhysicsNode.RigidBodySaveDataArray = Playback->Rigids;
			PhysicsNode.JointSaveDataArray = Playback->Joints;
			bPhysicsEnabled = Playback->bEnablePhysics;
			Revision = Playback->PhysicsRevision;
		}
		// 初始化需要读取组件和参考骨架，仅在游戏线程的 PreUpdate 执行。
		if (bPhysicsEnabled && !PhysicsNode.RigidBodySaveDataArray.IsEmpty() && !bNodeInitialized)
		{
			PhysicsNode.Initialize_AnyThread(FAnimationInitializeContext(this));
			bNodeInitialized = PhysicsNode.bIsInitialized;
		}
	}

	virtual bool Evaluate(FPoseContext& Output) override
	{
		const bool bEvaluated = FAnimSingleNodeInstanceProxy::Evaluate(Output);
		if (!bEvaluated || !bPhysicsEnabled || PhysicsNode.RigidBodySaveDataArray.IsEmpty()
			|| Output.Pose.GetNumBones() == 0)
		{
			return bEvaluated;
		}
		const float PlaybackTime = GetCurrentTime();
		const float ExpectedAdvance = FMath::Abs(GetDeltaSeconds() * GetPlayRate());
		// 工作线程发现时间跳转时透传本帧，下次 PreUpdate 在游戏线程重建刚体。
		if (bHasPreviousTime && FMath::Abs(PlaybackTime - PreviousTime) > ExpectedAdvance + 0.002f)
		{
			bPendingReset = true;
			LastPhysicsTransforms.Reset();
			return bEvaluated;
		}
		PreviousTime = PlaybackTime;
		bHasPreviousTime = true;
		if (!bNodeInitialized)
		{
			return bEvaluated;
		}
		if (CachedBoneCount != Output.Pose.GetNumBones())
		{
			LastPhysicsTransforms.Reset();
			CachedBoneCount = Output.Pose.GetNumBones();
		}
		FComponentSpacePoseContext ComponentPose(this);
		ComponentPose.Pose.InitPose(Output.Pose);
		// 暂停保留最后的物理姿态，避免每帧继续推进 Bullet 或突然回到参考姿态。
		if (IsPlaying() || LastPhysicsTransforms.IsEmpty())
		{
			LastPhysicsTransforms.Reset();
			PhysicsNode.EvaluateSkeletalControl_AnyThread(ComponentPose, LastPhysicsTransforms);
			LastPhysicsTransforms.Sort(FCompareBoneTransformIndex());
		}
		if (!LastPhysicsTransforms.IsEmpty())
		{
			ComponentPose.Pose.SafeSetCSBoneTransforms(LastPhysicsTransforms);
		}
		FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(ComponentPose.Pose), Output.Pose);
		return bEvaluated;
	}

	void ResetNode()
	{
		PhysicsNode = FAGN_MMDSkeletalControl();
		bNodeInitialized = false;
		bPendingReset = false;
		LastPhysicsTransforms.Reset();
		bHasPreviousTime = false;
		CachedBoneCount = 0;
	}

	FAGN_MMDSkeletalControl PhysicsNode;
	TArray<FBoneTransform> LastPhysicsTransforms;
	uint32 Revision = 0;
	bool bPhysicsEnabled = true;
	bool bNodeInitialized = false;
	bool bPendingReset = false;
	bool bHasPreviousTime = false;
	float PreviousTime = 0.0f;
	int32 CachedBoneCount = 0;
};

namespace
{
	bool ReadOriginalAnimNodeData(USkeletalMeshComponent* Component,
		TArray<FMMDPhysicsRigidBodyData>& Rigids, TArray<FMMDPhysicsJointData>& Joints)
	{
		UAnimInstance* Source = Component->GetAnimInstance();
		if (!Source && Component->GetAnimClass())
		{
			Source = Cast<UAnimInstance>(Component->GetAnimClass()->GetDefaultObject());
		}
		if (!Source)
		{
			return false;
		}
		for (TFieldIterator<FStructProperty> It(Source->GetClass()); It; ++It)
		{
			if (It->Struct == FAGN_MMDSkeletalControl::StaticStruct())
			{
				const FAGN_MMDSkeletalControl* Node = It->ContainerPtrToValuePtr<FAGN_MMDSkeletalControl>(Source);
				if (Node && !Node->RigidBodySaveDataArray.IsEmpty())
				{
					Rigids = Node->RigidBodySaveDataArray;
					Joints = Node->JointSaveDataArray;
					return true;
				}
			}
		}
		return false;
	}
}

UMMDPhysicsPlaybackAnimInstance* UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(
	USkeletalMeshComponent* Component, UAnimationAsset* Animation, bool bLoop, bool bInEnablePhysics)
{
	if (!IsInGameThread() || !IsValid(Component) || !Component->IsRegistered()
		|| !Component->GetSkeletalMeshAsset() || !IsValid(Animation))
	{
		return nullptr;
	}
	TArray<FMMDPhysicsRigidBodyData> SourceRigids;
	TArray<FMMDPhysicsJointData> SourceJoints;
	if (const UMMDSkeletalMeshUserData* UserData = Component->GetSkeletalMeshAsset()->GetAssetUserData<UMMDSkeletalMeshUserData>())
	{
		if (UserData->ModelDataAsset)
		{
			SourceRigids = UserData->ModelDataAsset->Rigids;
			SourceJoints = UserData->ModelDataAsset->Joints;
		}
	}
	UMMDPhysicsPlaybackAnimInstance* Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance());
	if (SourceRigids.IsEmpty() && Playback && Playback->PhysicsSourceMesh == Component->GetSkeletalMeshAsset())
	{
		SourceRigids = Playback->Rigids;
		SourceJoints = Playback->Joints;
	}
	if (SourceRigids.IsEmpty())
	{
		ReadOriginalAnimNodeData(Component, SourceRigids, SourceJoints);
	}
#if WITH_EDITOR
	// 旧资产尚未保存物理数据时，只读取已有来源配置运行态，不改写旧模型资产。
	if (SourceRigids.IsEmpty())
	{
		FString SourcePath;
		if (const AMMDActor* Actor = Cast<AMMDActor>(Component->GetOwner()))
		{
			SourcePath = Actor->SourcePMXFilePath;
		}
		if (SourcePath.IsEmpty())
		{
			const UMMDSkeletalMeshUserData* UserData = Component->GetSkeletalMeshAsset()->GetAssetUserData<UMMDSkeletalMeshUserData>();
			if (UserData && UserData->ModelDataAsset) SourcePath = UserData->ModelDataAsset->SourcePMXFilePath;
		}
		if (!SourcePath.IsEmpty() && FPaths::FileExists(SourcePath))
		{
			TPMXParser Parser;
			if (Parser.ParsePMXFile(SourcePath)) BuildMMDPhysicsData(Parser.PMXInfo, SourceRigids, SourceJoints);
		}
	}
#endif
	if (!Playback)
	{
		Component->SetAnimInstanceClass(StaticClass());
		Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance());
	}
	if (!Playback)
	{
		return nullptr;
	}
	Playback->SetPhysicsData(SourceRigids, SourceJoints);
	Playback->SetPhysicsEnabled(bInEnablePhysics);
	Playback->SetAnimationAsset(Animation, bLoop);
	if (Playback->GetCurrentAsset() != Animation)
	{
		return nullptr;
	}
	Playback->SetPlaying(true);
#if WITH_EDITOR
	Component->SetUpdateAnimationInEditor(true);
#endif
	return Playback;
}

void UMMDPhysicsPlaybackAnimInstance::SetPhysicsData(const TArray<FMMDPhysicsRigidBodyData>& InRigids,
	const TArray<FMMDPhysicsJointData>& InJoints)
{
	Rigids = InRigids;
	Joints = InJoints;
	PhysicsSourceMesh = GetSkelMeshComponent() ? GetSkelMeshComponent()->GetSkeletalMeshAsset() : nullptr;
	ResetPhysics();
}

void UMMDPhysicsPlaybackAnimInstance::ConfigurePhysicsFromPMX(const PMXDatas& PMXData)
{
	BuildMMDPhysicsData(PMXData, Rigids, Joints);
	PhysicsSourceMesh = GetSkelMeshComponent() ? GetSkelMeshComponent()->GetSkeletalMeshAsset() : nullptr;
	ResetPhysics();
}

void UMMDPhysicsPlaybackAnimInstance::SetPhysicsEnabled(bool bEnabled)
{
	if (bEnablePhysics != bEnabled)
	{
		bEnablePhysics = bEnabled;
		ResetPhysics();
	}
}

void UMMDPhysicsPlaybackAnimInstance::ResetPhysics()
{
	++PhysicsRevision;
	bPhysicsInitialized = false;
}

void UMMDPhysicsPlaybackAnimInstance::SeekMMDAnimation(float TimeSeconds, bool bFireNotifies)
{
	if (!FMath::IsFinite(TimeSeconds)) return;
	SetPosition(FMath::Clamp(TimeSeconds, 0.0f, GetLength()), bFireNotifies);
	ResetPhysics();
}

void UMMDPhysicsPlaybackAnimInstance::SetAnimationAsset(UAnimationAsset* NewAsset, bool bIsLooping, float InPlayRate)
{
	Super::SetAnimationAsset(NewAsset, bIsLooping, InPlayRate);
	ResetPhysics();
}

FAnimInstanceProxy* UMMDPhysicsPlaybackAnimInstance::CreateAnimInstanceProxy()
{
	return new FMMDPhysicsPlaybackProxy(this);
}

void UMMDPhysicsPlaybackAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete static_cast<FMMDPhysicsPlaybackProxy*>(Proxy);
}

void UMMDPhysicsPlaybackAnimInstance::NativePostEvaluateAnimation()
{
	Super::NativePostEvaluateAnimation();
	const FMMDPhysicsPlaybackProxy& Proxy = GetProxyOnGameThread<FMMDPhysicsPlaybackProxy>();
	bPhysicsInitialized = Proxy.bNodeInitialized && !Proxy.bPendingReset;
}
