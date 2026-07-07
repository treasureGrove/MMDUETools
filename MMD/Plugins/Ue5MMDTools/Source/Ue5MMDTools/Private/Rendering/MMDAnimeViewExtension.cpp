// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#include "Rendering/MMDAnimeViewExtension.h"
#include "Rendering/MMDAnimePostProcessPass.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
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
	const FSceneView& View,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	if (PassId == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(
			this, &FMMDAnimeViewExtension::PostProcessCallback_RenderThread));
	}
}

FScreenPassTexture FMMDAnimeViewExtension::PostProcessCallback_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs)
{
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

	FMMDAnimeRenderParams Params;

	// Extract main directional light from the view (use default overhead sun)
	Params.MainLightDirection = FVector3f(0.0f, -0.3f, 0.95f);
	Params.MainLightColor     = FVector3f(1.0f, 1.0f, 1.0f);

	// Camera position from the view
	const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
	Params.CameraPosition = FVector3f(
		static_cast<float>(ViewOrigin.X),
		static_cast<float>(ViewOrigin.Y),
		static_cast<float>(ViewOrigin.Z));

	AddMMDAnimePostProcessPass(GraphBuilder, View, Inputs, SceneColor, Params);

	return FScreenPassTexture(SceneColor);
}
