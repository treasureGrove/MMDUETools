#pragma once
#include "CoreMinimal.h"
#include "RenderGraph.h"
#include "SceneView.h"
#include "Rendering/MMDAnimeEnvironmentUniformBuffer.h"

struct FPostProcessMaterialInputs;

void AddMMDAnimePostProcessPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs,
	FRDGTextureRef SceneColor,
	FRDGUniformBufferRef AnimeEnvironmentUB);

