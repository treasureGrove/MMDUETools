#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UMMDHeadBoneSubsystem.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;

/** 为需要的材质提供可选头部几何基；缺少头骨时保留通用法线着色。 */
UCLASS()
class UE5MMDTOOLS_API UMMDHeadBoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMMDHeadBoneSubsystem, STATGROUP_Tickables); }

	/** 手动刷新骨骼后的预览和离屏渲染也可立即同步，不必等待世界 Tick。 */
	static bool UpdateComponentHeadFrame(USkeletalMeshComponent* Component);
	static constexpr const TCHAR* ParamHeadForward = TEXT("MMDHeadForward");
	static constexpr const TCHAR* ParamHeadRight = TEXT("MMDHeadRight");
	static constexpr const TCHAR* ParamHeadPositionRadius = TEXT("MMDHeadPositionRadius");

private:
	struct FHeadFrameCache
	{
		TWeakObjectPtr<USkeletalMesh> Mesh;
		int32 HeadIndex = INDEX_NONE;
		FQuat ReferenceHeadRotation = FQuat::Identity;
		FVector EyeOffset = FVector::ZeroVector;
		float ReferenceRadius = 0.0f;
		bool bHasEyes = false;
		bool bMMD = false;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FHeadFrameCache> HeadFrames;
	static void BuildReferenceFrame(USkeletalMesh* Mesh, FHeadFrameCache& Frame);
	static bool ApplyHeadFrame(USkeletalMeshComponent* Component, FHeadFrameCache& Frame);
};