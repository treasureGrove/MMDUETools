// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FMMDAnimeViewExtension : public FSceneViewExtensionBase
{
public:
	FMMDAnimeViewExtension(const FAutoRegister& AutoRegister);
	virtual ~FMMDAnimeViewExtension() = default;

	// FSceneViewExtensionBase interface
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass PassId,
		const FSceneView& View,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

	// Post-process callback - placeholder that passes through scene color.
	// Task 3 will replace this with the actual RDG toon-shading pass dispatch.
	FScreenPassTexture PostProcessCallback_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessMaterialInputs& Inputs);

	// Control
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }
	bool IsEnabled() const { return bEnabled; }

private:
	bool bEnabled = true;
};
