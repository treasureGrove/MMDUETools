#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UMMDHeadBoneSubsystem.generated.h"

/**
 * 自动把每个角色头骨的世界朝向写到材质向量参数 MMDHeadForward / MMDHeadRight，
 * 供脸部 SDF 阴影 shader（TMMDAnimeFace）使用。
 *
 * 不依赖任何特定 Actor 类型：每帧遍历世界里所有 USkeletalMeshComponent，
 * 只处理"材质里包含 MMDHeadForward 参数"的 component（用 SetVectorParameterValueOnMaterials
 * 找不到参数就自动忽略，零副作用）。
 *
 * 头骨名按备选顺序匹配：頭 / Head / head / C_HEAD / head_top / HeadTop。
 * 找不到时打一次 warning，之后跳过这个 component（缓存到 TMap 避免重复警告）。
 *
 * 触发条件：材质里有 MMDHeadForward 参数就自动启用，没有就完全不参与（自动适配）。
 */
UCLASS()
class UE5MMDTOOLS_API UMMDHeadBoneSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMMDHeadBoneSubsystem, STATGROUP_Tickables); }

	/** 材质参数名（字符串字面量集中在这里，方便改）。 */
	static constexpr const TCHAR* ParamHeadForward = TEXT("MMDHeadForward");
	static constexpr const TCHAR* ParamHeadRight  = TEXT("MMDHeadRight");

private:
	/** 头骨备选名（按优先级）。 */
	static constexpr const TCHAR* HeadBoneCandidates[] = {
		TEXT("頭"),       // 标准 MMD
		TEXT("Head"),
		TEXT("head"),
		TEXT("C_HEAD"),
		TEXT("head_top"),
		TEXT("HeadTop")
	};

	// 已警告过的 component（避免每帧 spam 日志）
	TSet<TWeakObjectPtr<UPrimitiveComponent>> WarnedComponents;

	// 头骨索引缓存：<component, boneIndex>，第一次匹配成功后就不再查
	TMap<TWeakObjectPtr<UPrimitiveComponent>, int32> BoneIndexCache;

	// 统计
	int32 ProcessedCount = 0;
};
