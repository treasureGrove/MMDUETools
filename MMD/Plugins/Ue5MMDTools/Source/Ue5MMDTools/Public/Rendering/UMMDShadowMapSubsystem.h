#pragma once
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "UMMDShadowMapSubsystem.generated.h"

class UTextureRenderTarget2D;
class UMMDAnimeLightDataSubsystem;
class FSceneInterface;
class FSceneViewFamily;

/**
 * MMD 场景阴影（主平行光遮挡）子系统。
 *
 * 实现路径（无 AActor / 无 USceneCaptureComponent2D / 不污染关卡）：
 *   通过引擎公开扩展点 FCustomRenderPassBase（Rendering/CustomRenderPass.h）
 *   和 FSceneInterface::AddCustomRenderPass，把一个深度 pass 注入主渲染器内部，
 *   由引擎共享 frustum culling / LOD 跑一次方向光视角的深度渲染，
 *   输出直接写到 MMDShadowMapRT（材质侧采样同一张 RT）。
 *
 * 数据流：
 *   游戏线程每帧 UpdateShadowForFrame(InViewFamily)：
 *     找主平行光 -> 算光视角 ViewRotationMatrix + 正交 ProjectionMatrix ->
 *     new FMMDShadowCustomRenderPass(RT, Size) ->
 *     Scene->AddCustomRenderPass(&InViewFamily, Input) ->
 *     引擎本帧渲染时为主视角顺便跑这个深度 pass 写入 MMDShadowMapRT。
 *   同时算"阴影相机基" 4 个 float4 交给 UMMDAnimeLightDataSubsystem 写进
 *   LightDataRT 第 2 行（y=1），材质侧 SampleMMDShadow 据此把世界点投到阴影空间。
 *
 * 默认排除所有 AMMDActor（MMD 模型不参与投射，避免自阴影与 toon 叠加双重变暗）。
 */
UCLASS()
class UE5MMDTOOLS_API UMMDShadowMapSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	static UMMDShadowMapSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 游戏线程每帧调用（SetupViewFamily）：提交方向光深度 pass 到主渲染器。 */
	void UpdateShadowForFrame(FSceneViewFamily* InViewFamily);

	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowMapRenderTarget(UTextureRenderTarget2D* InRenderTarget);

	/** 阴影相机放在相机后多远（cm），同时决定正交 far 距离。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowDistance(float InDistance);

	/** 正交投影宽度（cm），即阴影覆盖范围。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetOrthoWidth(float InWidth);

	/** 全局深度偏移（cm），越大越不容易自遮挡/漏光。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetGlobalBias(float InBias);

	/** 是否从阴影捕获里排除 MMD 模型自身（默认 true，不做自阴影）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetHideMMDActors(bool bInHide);

private:
	/** 找第一盏可见平行光，输出光传播方向（Actor forward）。 */
	bool GetMainLightDirection(UWorld* World, FVector& OutDir);

	/** 取当前渲染相机位置（与灯光子系统一致的回退逻辑）。 */
	bool GetCameraPosition(UWorld* World, FVector& OutPos);

	/** 清理关卡里残留的旧版 SceneCapture2D actor（命名兼容 MMDShadowMapCapture / MMDShadowCapture）。带节流。 */
	void CleanupStaleCaptureActors(UWorld* World);

	/** 自动创建阴影深度 RT 资产 /Ue5MMDTools/Rendering/MMDShadowMapRT。 */
	void AutoSetupShadowMapRT();

	/** 把当前帧的 MMD 模型 primitive id 收集到 HiddenPrimitives（避免自阴影）。 */
	void CollectHiddenMMDPrimitives(UWorld* World, TSet<FPrimitiveComponentId>& OutHidden);

	bool bEnabled = true;
	bool bAutoSetupDone = false;

	float ShadowDistance = 4000.0f;
	float OrthoWidth = 4000.0f;
	float GlobalBias = 5.0f;
	bool bHideMMDActors = true;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ShadowMapRT = nullptr;

	/** 缓存上一帧的隐藏 primitive 数量，避免每帧反复构造 TSet 提交。 */
	int32 LastHiddenPrimitiveCount = -1;

	/** 上一次提交 CustomRenderPass 时的 HiddenPrimitives 集合（避免每帧重算）。 */
	TSet<FPrimitiveComponentId> CachedHiddenPrimitives;
};
