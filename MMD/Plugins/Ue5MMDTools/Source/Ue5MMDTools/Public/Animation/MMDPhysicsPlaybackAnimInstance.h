#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AGN_MMDSkeletalControl.h"
#include "MMDPhysicsPlaybackAnimInstance.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
struct FMMDPhysicsPlaybackProxy;

/** 保留单动画播放和 Morph，动画姿态计算后执行 MMD Bullet 物理。 */
UCLASS(Transient, BlueprintType)
class UE5MMDTOOLS_API UMMDPhysicsPlaybackAnimInstance : public UAnimSingleNodeInstance
{
	GENERATED_BODY()

public:
	/** 默认带物理；无刚体的模型正常播放，不创建空物理世界。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Animation")
	static UMMDPhysicsPlaybackAnimInstance* PlayMMDAnimation(USkeletalMeshComponent* Component,
		UAnimationAsset* Animation, bool bLoop = true, bool bEnablePhysics = true);

	UFUNCTION(BlueprintCallable, Category = "MMD|Physics")
	void SetPhysicsEnabled(bool bEnabled);
	UFUNCTION(BlueprintPure, Category = "MMD|Physics")
	bool IsPhysicsEnabled() const { return bEnablePhysics; }
	UFUNCTION(BlueprintPure, Category = "MMD|Physics")
	bool IsPhysicsInitialized() const { return bPhysicsInitialized; }
	UFUNCTION(BlueprintPure, Category = "MMD|Physics")
	int32 GetPhysicsRigidBodyCount() const { return Rigids.Num(); }
	UFUNCTION(BlueprintCallable, Category = "MMD|Physics")
	void ResetPhysics();
	UFUNCTION(BlueprintCallable, Category = "MMD|Animation")
	void SeekMMDAnimation(float TimeSeconds, bool bFireNotifies = false);

	void ConfigurePhysicsFromPMX(const PMXDatas& PMXData);
	void SetPhysicsData(const TArray<FMMDPhysicsRigidBodyData>& InRigids,
		const TArray<FMMDPhysicsJointData>& InJoints);
	virtual void SetAnimationAsset(UAnimationAsset* NewAsset, bool bIsLooping = true, float InPlayRate = 1.0f) override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	virtual void NativePostEvaluateAnimation() override;

private:
	friend struct FMMDPhysicsPlaybackProxy;
	UPROPERTY(Transient)
	TArray<FMMDPhysicsRigidBodyData> Rigids;
	UPROPERTY(Transient)
	TArray<FMMDPhysicsJointData> Joints;
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> PhysicsSourceMesh;
	bool bEnablePhysics = true;
	bool bPhysicsInitialized = false;
	uint32 PhysicsRevision = 1;
};
