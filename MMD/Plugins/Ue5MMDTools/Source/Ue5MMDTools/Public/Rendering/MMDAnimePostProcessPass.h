#pragma once
#include "CoreMinimal.h"
#include "RenderGraph.h"
#include "SceneView.h"
#include "Rendering/MMDAnimeEnvironmentUniformBuffer.h"

struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

FScreenPassTexture AddMMDAnimePostProcessPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs,
    TRDGUniformBufferRef<FAnimeEnvironmentParameters> AnimeEnvironmentUB);
