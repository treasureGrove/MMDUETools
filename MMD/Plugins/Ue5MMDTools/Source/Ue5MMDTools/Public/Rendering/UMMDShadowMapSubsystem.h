#pragma once
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "UObject/WeakObjectPtr.h"
#include "UMMDShadowMapSubsystem.generated.h"

class AActor;
class UTextureRenderTarget2D;
class UMMDAnimeLightDataSubsystem;
class FSceneInterface;
class FSceneView;
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
 *   游戏线程每帧 UpdateShadowForFrame(InViewFamily, InView)：
 *     按主相机视锥计算 4 个稳定级联 -> 光空间 texel snapping ->
 *     逐级渲染 scratch RT 并复制到 2x2 MMDShadowMapRT atlas ->
 *     Scene->AddCustomRenderPass(&InViewFamily, Input) ->
 *     引擎本帧渲染时为主视角顺便跑这个深度 pass 写入 MMDShadowMapRT。
 *   同时把 4 组阴影相机基以及主相机位置/前向交给 UMMDAnimeLightDataSubsystem 写进
 *   LightDataRT 第 2 行（y=1），材质侧 SampleMMDShadow 据此把世界点投到阴影空间。
 *
 * 默认排除所有 AMMDActor（MMD 模型不参与投射，避免自阴影与 toon 叠加双重变暗）；
 * 其它任意 mesh 只要给 actor / 组件挂上名为 "MMDShadowExclude" 的 Tag，也会被同样剔除。
 */
UCLASS()
class UE5MMDTOOLS_API UMMDShadowMapSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	static UMMDShadowMapSubsystem* Get();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 游戏线程每帧调用（SetupView）：按主相机视锥提交四级方向光深度 pass。 */
	void UpdateShadowForFrame(FSceneViewFamily* InViewFamily, const FSceneView* InView);

	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowMapRenderTarget(UTextureRenderTarget2D* InRenderTarget);

	/** 主相机阴影距离（cm），四个级联只覆盖该距离内的可见视锥。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowDistance(float InDistance);

	/** 阴影投射深度（cm），控制每级沿光线方向向前搜索遮挡物的距离。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetOrthoWidth(float InWidth);

	/** 全局深度偏移（cm），越大越不容易自遮挡/漏光。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetGlobalBias(float InBias);

	/** 设置每个级联的分辨率。最终 atlas 边长为该值的 2 倍。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowMapResolution(int32 InResolution);

	/** 设置 practical split 的对数权重：0=线性，1=对数，默认 0.70。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetCascadeSplitLambda(float InLambda);

	/** 设置法线偏移强度（以当前级联 texel 的世界尺寸为单位）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetNormalBias(float InBiasInTexels);

	/** 是否从阴影捕获里排除 MMD 模型自身（默认 true，不做自阴影）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetHideMMDActors(bool bInHide);

	/**
	 * 从阴影深度 pass 里排除指定的 actor 集合：
	 * 这些 actor 不参与投射（既不投影到其它物体，也不自投影），但仍然会接收环境阴影。
	 * 用于"某个物体不要自己给自己投 shadow"——比如盒子自身遮挡、模型自身部件互遮。
	 * 与 SetHideMMDActors 叠加；传空数组表示清空。
	 */
	UFUNCTION(BlueprintCallable, Category = "MMD Anime|ShadowMap")
	void SetShadowMapExcludedActors(const TArray<AActor*>& Actors);

private:
	/** 找第一盏可见平行光，输出光传播方向（Actor forward）。 */
	bool GetMainLightDirection(UWorld* World, FVector& OutDir);

	/** 清理关卡里残留的旧版 SceneCapture2D actor（命名兼容 MMDShadowMapCapture / MMDShadowCapture）。带节流。 */
	void CleanupStaleCaptureActors(UWorld* World);

	/** 自动创建阴影深度 RT 资产 /Ue5MMDTools/Rendering/MMDShadowMapRT。 */
	void AutoSetupShadowMapRT();

	/** 把"要剔除的 primitive"收集到 HiddenPrimitives：MMD 模型(bHideMMDActors) / 显式排除(ExcludedActors) / 挂 GMMDShadowExcludeTag 的 actor/组件，避免自阴影。 */
	void CollectHiddenMMDPrimitives(UWorld* World, TSet<FPrimitiveComponentId>& OutHidden);

	bool bEnabled = true;
	bool bAutoSetupDone = false;

	float ShadowDistance = 5000.0f;
	float OrthoWidth = 4000.0f;
	float GlobalBias = 2.0f;
	bool bHideMMDActors = true;

	float CascadeSplitLambda = 0.70f;
	float NormalBiasInTexels = 1.50f;

	/** 阴影深度 RT 分辨率（边长；默认 2048。想角色附近更细可调高，想更快可调低）。 */
	int32 ShadowMapResolution = 2048;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ShadowMapRT = nullptr;

	/** 四级联共用的临时深度 RT，每个 pass 后复制到 ShadowMapRT 的对应象限。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ShadowMapScratchRT = nullptr;

	/** 上一次提交 CustomRenderPass 时的 HiddenPrimitives 集合（避免每帧重算）。 */
	TSet<FPrimitiveComponentId> CachedHiddenPrimitives;

	/** 用户显式排除的 actor（不参与投射，避免"自己给自己投 shadow"）。TWeakObjectPtr 防 actor 销毁后悬垂。 */
	TArray<TWeakObjectPtr<AActor>> ExcludedActors;
};
