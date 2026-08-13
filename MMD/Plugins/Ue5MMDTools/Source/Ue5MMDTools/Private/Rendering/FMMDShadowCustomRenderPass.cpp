#include "Rendering/FMMDShadowCustomRenderPass.h"

#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "RenderGraphBuilder.h"

FMMDShadowCustomRenderPass::FMMDShadowCustomRenderPass(
	UTextureRenderTarget2D* InRenderTarget, const FIntPoint& InRenderTargetSize)
	: FCustomRenderPassBase(
		TEXT("MMDDirectionalShadowDepth"),
		ERenderMode::DepthPass,
		ERenderOutput::SceneDepth,
		InRenderTargetSize)
	, ExternalRenderTarget(InRenderTarget ? InRenderTarget->GameThread_GetRenderTargetResource() : nullptr)
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
		// 注意：FTextureRenderTarget2DResource::Resize 是 FSceneCapturePass 的 friend，
		// 插件调用不了。但我们的 RT 大小在 SetShadowMapRenderTarget 里固定为 2048²，运行时不变，
		// 不需要 Resize，直接注册即可。
		RenderTargetTexture = ExternalRenderTarget->GetRenderTargetTexture(GraphBuilder);
	}
}

void FMMDShadowCustomRenderPass::OnEndPass(FRDGBuilder& GraphBuilder)
{
	// 让 RDG 在本 pass 结束时把 RT 切回 SRV，材质在主视角渲染时可以直接采样，
	// 不必等整帧 RDG 结束才可用。
	if (RenderTargetTexture)
	{
		GraphBuilder.UseExternalAccessMode(RenderTargetTexture, ERHIAccess::SRVMask);
	}
}
