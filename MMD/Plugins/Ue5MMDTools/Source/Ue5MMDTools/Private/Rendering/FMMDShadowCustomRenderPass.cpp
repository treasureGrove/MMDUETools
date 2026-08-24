#include "Rendering/FMMDShadowCustomRenderPass.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

FMMDShadowCustomRenderPass::FMMDShadowCustomRenderPass(
	UTextureRenderTarget2D* InRenderTarget,
	const FIntPoint& InRenderTargetSize,
	UTextureRenderTarget2D* InAtlasRenderTarget,
	const FIntPoint& InAtlasOffset,
	bool bInFinalizeAtlas)
	: FCustomRenderPassBase(
		TEXT("MMDDirectionalShadowDepth"),
		ERenderMode::DepthPass,
		ERenderOutput::SceneDepth,
		InRenderTargetSize)
	, ExternalRenderTarget(InRenderTarget ? InRenderTarget->GameThread_GetRenderTargetResource() : nullptr)
	, AtlasRenderTarget(InAtlasRenderTarget ? InAtlasRenderTarget->GameThread_GetRenderTargetResource() : nullptr)
	, AtlasOffset(InAtlasOffset)
	, bFinalizeAtlas(bInFinalizeAtlas)
{
	// 挂上 bMainViewFamily=true 的 user data —— 让引擎只在主视角渲染时消费本 pass，
	// 避免 pass 被 SceneCapture 渲染器吃掉导致 RDG 外部访问状态冲突。
	SetUserData(TUniquePtr<FMMDShadowPassUserData>(new FMMDShadowPassUserData()));
}

void FMMDShadowCustomRenderPass::OnPreRender(FRDGBuilder& GraphBuilder)
{
	if (ExternalRenderTarget)
	{
		// 与 FSceneCapturePass 一致：把外部 RT 注册进 RDG（GetRenderTargetTexture 返回 RDG 纹理），
		// 引擎把深度渲染写入它，材质本帧即可采样。
		// 尺寸由 SetShadowMapResolution 在游戏线程统一重建，不在渲染线程 Resize。
		RenderTargetTexture = ExternalRenderTarget->GetRenderTargetTexture(GraphBuilder);

		// 关键修复：若上一帧 OnEndPass 的 UseExternalAccessMode(SRVMask) 或别的消费方
		// （如 MMD 预览渲染器的 SceneCapture）把本 RT 切成了 external access，必须先切回
		// internal，否则本 pass 再把它当 RTV/深度目标写入会触发 RDG 校验崩溃：
		//   "is in external access mode ... but is being used with access RTV"
		// 与引擎 FSceneCapturePass::OnPreRender 的做法完全一致（已处于 internal 时是 no-op）。
		GraphBuilder.UseInternalAccessMode(RenderTargetTexture);
	}
}

void FMMDShadowCustomRenderPass::OnEndPass(FRDGBuilder& GraphBuilder)
{
	if (!RenderTargetTexture || !AtlasRenderTarget)
	{
		return;
	}

	// 四个级联共用 scratch RT。每个 pass 完成后立刻复制到 atlas 对应象限，
	// RDG 会按 写 scratch -> copy -> 下一次写 scratch 的依赖顺序执行。
	FRDGTextureRef AtlasTexture = AtlasRenderTarget->GetRenderTargetTexture(GraphBuilder);
	GraphBuilder.UseInternalAccessMode(AtlasTexture);
	AddCopyTexturePass(
		GraphBuilder,
		RenderTargetTexture,
		AtlasTexture,
		FIntPoint::ZeroValue,
		AtlasOffset,
		RenderTargetSize);

	// 最后一级完成后再把 atlas 切到 SRV，供主视角材质本帧采样。
	if (bFinalizeAtlas)
	{
		GraphBuilder.UseExternalAccessMode(AtlasTexture, ERHIAccess::SRVMask);
	}
}
