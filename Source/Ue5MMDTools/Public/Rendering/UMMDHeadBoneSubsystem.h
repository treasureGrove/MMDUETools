#pragma once

#include "CoreMinimal.h"
#include "Rendering/MMDEndfieldHeadFrame.h"
#include "Subsystems/WorldSubsystem.h"
#include "UMMDHeadBoneSubsystem.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UMMDEndfieldFaceData;
class UMaterialInstanceDynamic;

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

protected:
	// 离屏与编辑器模型预览也需持久缓存，避免每次手动刷新都重新开启调试开关。
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override
	{
		return Super::DoesSupportWorldType(WorldType) || WorldType == EWorldType::EditorPreview;
	}

private:
	struct FHeadFrameCache
	{
		TWeakObjectPtr<USkeletalMesh> Mesh;
		TWeakObjectPtr<UMMDEndfieldFaceData> FaceData;
		FMMDReferenceHeadFrame Reference;
		TMap<int32, TWeakObjectPtr<UMaterialInstanceDynamic>> BoundMaterials;
		FString SkeletonSignature;
		FString LiveSkeletonSignature;
		FName BoneName;
		FVector CenterHeadLocal = FVector::ZeroVector;
		FQuat ReferenceRotation = FQuat::Identity;
		float Radius = 0.0f;
		int32 DataVersion = 0;
		uint32 BindingSignature = 0;
		int32 FrameVersion = 0;
		bool bInitialized = false;
	};
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FHeadFrameCache> HeadFrames;
	static void BuildReferenceFrame(USkeletalMesh* Mesh, FHeadFrameCache& Frame);
	static bool ApplyHeadFrame(USkeletalMeshComponent* Component, FHeadFrameCache& Frame);
};
