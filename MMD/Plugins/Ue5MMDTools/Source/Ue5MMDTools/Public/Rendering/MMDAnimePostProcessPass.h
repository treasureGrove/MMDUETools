// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RenderGraph.h"
#include "SceneView.h"

struct FPostProcessMaterialInputs;

/**
 * Runtime parameter block passed from the game thread (via UMMDAnimeRenderSettings)
 * down to the RDG compute pass on the render thread.
 */
struct FMMDAnimeRenderParams
{
	float ShadowThreshold = 0.35f;
	float ShadowSoftness = 0.05f;
	FVector3f ShadowColor = FVector3f(0.6f, 0.5f, 0.7f);
	FVector3f DeepShadowColor = FVector3f(0.3f, 0.25f, 0.4f);

	float RimWidth = 0.3f;
	FVector3f RimColor = FVector3f(1.0f, 1.0f, 1.0f);

	float EnvironmentStrength = 0.5f;
	FVector3f AmbientColor = FVector3f(0.5f, 0.6f, 0.8f);

	float SpecularSize = 0.15f;
	float SpecularHardness = 0.85f;

	FVector3f MainLightDirection = FVector3f(0.0f, 0.0f, 1.0f);
	FVector3f MainLightColor = FVector3f(1.0f, 1.0f, 1.0f);
	FVector3f CameraPosition = FVector3f::ZeroVector;
};

/**
 * Add the MMD anime toon-remap post-process pass to the render graph.
 *
 * Reads SceneColor + GBuffer data, dispatches the FMMDAnimeToonRemapCS
 * compute shader, and copies the result back into SceneColor.
 */
void AddMMDAnimePostProcessPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs,
	FRDGTextureRef SceneColor,
	const FMMDAnimeRenderParams& Params);
