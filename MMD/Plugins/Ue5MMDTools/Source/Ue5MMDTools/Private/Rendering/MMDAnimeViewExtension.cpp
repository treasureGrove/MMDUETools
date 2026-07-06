// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.
// TODO: Re-enable after migrating to UE 5.5+ post-process API

#include "Rendering/MMDAnimeViewExtension.h"
#include "Rendering/MMDAnimePostProcessPass.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "ScreenPass.h"
#include "SceneView.h"

#if 0 // Disabled - needs UE 5.5 post-process API migration

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
		InOutPassCallbacks.Add(FAfterPassCallback::CreateRaw(
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
	Params.MainLightDirection = FVector3f(0.0f, -0.3f, 0.95f);
	Params.MainLightColor     = FVector3f(1.0f, 1.0f, 1.0f);

	const FVector ViewLocation = View.GetViewLocation();
	Params.CameraPosition = FVector3f(
		static_cast<float>(ViewLocation.X),
		static_cast<float>(ViewLocation.Y),
		static_cast<float>(ViewLocation.Z));

	AddMMDAnimePostProcessPass(GraphBuilder, View, SceneColor, Params);

	return FScreenPassTexture(SceneColor, SceneColorInput.Subresource);
}

#endif // Disabled - needs UE 5.5 post-process API migration
