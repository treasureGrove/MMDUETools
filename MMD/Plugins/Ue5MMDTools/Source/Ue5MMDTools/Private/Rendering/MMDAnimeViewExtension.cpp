// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#include "Rendering/MMDAnimeViewExtension.h"
#include "Rendering/MMDAnimePostProcessPass.h"

#include "PostProcess/PostProcessing.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraph/RenderGraphBuilder.h"
#include "ScreenPass.h"
#include "SceneView.h"

FMMDAnimeViewExtension::FMMDAnimeViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
}

bool FMMDAnimeViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return bEnabled;
}

void FMMDAnimeViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass PassId,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	// Inject BEFORE the tonemap pass so we receive HDR scene color.
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallback::CreateRaw(
			this, &FMMDAnimeViewExtension::PostProcessCallback_RenderThread));
	}
}

FScreenPassTexture FMMDAnimeViewExtension::PostProcessCallback_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
	// Extract the scene color texture from the post-process inputs.
	FScreenPassTexture SceneColorInput = Inputs.OverrideOutput;
	if (!SceneColorInput.IsValid())
	{
		return Inputs.OverrideOutput;
	}

	FRDGTextureRef SceneColor = SceneColorInput.Texture;
	if (!SceneColor)
	{
		return Inputs.OverrideOutput;
	}

	// Build render parameters (defaults for now; later driven by UMMDAnimeRenderSettings).
	FMMDAnimeRenderParams Params;

	// Try to extract the main directional light direction from the view.
	// The view's family may contain directional lights; use a sensible default
	// (overhead sun) when none is found.
	Params.MainLightDirection = FVector3f(0.0f, -0.3f, 0.95f); // slight angle, mostly overhead
	Params.MainLightColor     = FVector3f(1.0f, 1.0f, 1.0f);

	// Camera position from the view
	const FVector ViewLocation = View.GetViewLocation();
	Params.CameraPosition = FVector3f(
		static_cast<float>(ViewLocation.X),
		static_cast<float>(ViewLocation.Y),
		static_cast<float>(ViewLocation.Z));

	// Dispatch the toon-remap compute pass
	AddMMDAnimePostProcessPass(GraphBuilder, View, SceneColor, Params);

	// Return the (now modified) scene color
	return FScreenPassTexture(SceneColor, SceneColorInput.Subresource);
}
