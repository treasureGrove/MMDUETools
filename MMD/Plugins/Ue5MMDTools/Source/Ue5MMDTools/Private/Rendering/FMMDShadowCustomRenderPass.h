// FMMDShadowCustomRenderPass.h
//
// MMD 方向光阴影深度 pass —— 派生自引擎公开扩展点 FCustomRenderPassBase。
//
// 走 FSceneInterface::AddCustomRenderPass 注入主渲染器内部执行：
// 引擎为本 pass 跑一次深度渲染（视点=光源、正交投影），
// 输出直接写入外部 UTextureRenderTarget2D（MMDShadowMapRT），
// 主视角材质本帧即可采样，无需任何 AActor / USceneCaptureComponent2D。

#pragma once

#include "CoreMinimal.h"
#include "Rendering/CustomRenderPass.h"

class UTextureRenderTarget2D;

// 与引擎私有头 CustomRenderPassSceneCapture.h 里 FSceneCaptureCustomRenderPassUserData
// 的字段布局完全一致的最小 user data。
//
// 用途：让本 pass 只在"主视角渲染"（PIE / 编辑器 viewport，ViewFamily.bIsMainViewFamily==true）
// 时被消费，跳过 SceneCapture 渲染器 —— 否则我们的 UseExternalAccessMode 调用会干扰
// SceneCapture 自身的 GraphBuilder 外部访问状态，触发 ValidateSetAccessFinal 崩溃
// （崩溃资源名 "SceneCaptureTarget"）。
//
// 机制：宏 IMPLEMENT_CUSTOM_RENDER_PASS_USER_DATA(FSceneCaptureCustomRenderPassUserData)
// 字符串化后产生 FName("FSceneCaptureCustomRenderPassUserData")，与引擎
// SceneRendering.cpp:2979 GetUserDataTyped<FSceneCaptureCustomRenderPassUserData>() 查询的
// FName 一致 —— 引擎会通过 GetUserData(FName) 找到本对象，reinterpret_cast 后读取
// bMainViewFamily（SceneRendering.cpp:2981 的过滤条件）。
//
// 内存布局对齐：两个类都从 ICustomRenderPassUserData 单继承（vtable 相同），
// IMPLEMENT_CUSTOM_RENDER_PASS_USER_DATA 宏不引入字段，下面字段顺序与引擎类一字不差，
// 包括 #if !UE_BUILD_SHIPPING 条件字段 —— 保证强转后访问任意字段都安全。
class FMMDShadowPassUserData : public ICustomRenderPassUserData
{
public:
	IMPLEMENT_CUSTOM_RENDER_PASS_USER_DATA(FSceneCaptureCustomRenderPassUserData);

	bool bMainViewFamily = true;		// ← 关键：本 pass 限定主视角消费
	bool bMainViewResolution = false;
	bool bMainViewCamera = false;
	bool bIgnoreScreenPercentage = false;
	bool bExcludeFromSceneTextureExtents = false;
	FIntPoint SceneTextureDivisor = FIntPoint(1, 1);
	FName UserSceneTextureBaseColor;
	FName UserSceneTextureNormal;
	FName UserSceneTextureSceneColor;
#if !UE_BUILD_SHIPPING
	FString CaptureActorName;
#endif
};

class FMMDShadowCustomRenderPass final : public FCustomRenderPassBase
{
public:
	IMPLEMENT_CUSTOM_RENDER_PASS(FMMDShadowCustomRenderPass);

	FMMDShadowCustomRenderPass(UTextureRenderTarget2D* InRenderTarget, const FIntPoint& InRenderTargetSize);

	virtual void OnPreRender(FRDGBuilder& GraphBuilder) override;
	virtual void OnEndPass(FRDGBuilder& GraphBuilder) override;

private:
	// 外部 RT 的 render target resource（game thread 取一次，render thread 用）。
	FRenderTarget* ExternalRenderTarget = nullptr;
};
