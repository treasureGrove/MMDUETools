#pragma once
#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * 从 SetupView 取得实际主相机视图，并在当前帧渲染提交前收集灯光与级联阴影数据。
 * PreRenderViewFamily 在 BasePass 前写入 LightDataRT，保证阴影图与投影基严格同帧。
 */
class FMMDAnimeLightViewExtension : public FSceneViewExtensionBase
{
public:
	FMMDAnimeLightViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return true; }
};
