#pragma once
#include "CoreMinimal.h"
#include "SceneViewExtension.h"

/**
 * Lightweight view extension that collects light data on the game thread from
 * SetupViewFamily. SetupViewFamily runs right before the current frame's scene
 * render is dispatched, so the light data pushed to the render thread is consumed
 * by PostOpaque in the SAME frame (no one-frame latency).
 */
class FMMDAnimeLightViewExtension : public FSceneViewExtensionBase
{
public:
	FMMDAnimeLightViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return true; }
};
