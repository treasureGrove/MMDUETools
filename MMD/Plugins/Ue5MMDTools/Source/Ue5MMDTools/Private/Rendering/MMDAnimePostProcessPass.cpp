#include "Rendering/MMDAnimePostProcessPass.h"
#include "MMDAnimeToonRemapCS.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "PixelShaderUtils.h"

FScreenPassTexture AddMMDAnimePostProcessPass(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs,
    TRDGUniformBufferRef<FAnimeEnvironmentParameters> AnimeEnvironmentUB)
{
    FRDGTextureRef SceneColor = Inputs.OverrideOutput.Texture;
    if (!SceneColor)
    {
        return FScreenPassTexture();
    }

    // ---- create output ----
    FRDGTextureDesc OutputDesc = SceneColor->Desc;
    OutputDesc.Reset();
    OutputDesc.Flags |= TexCreate_UAV;
    FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MMDToonLightingOutput"));

    // ---- create UAV for output ----
    FRDGTextureUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutputTexture));

    // ---- set up shader params ----
    FMMDAnimeToonRemapCS::FParameters* PassParams =
        GraphBuilder.AllocParameters<FMMDAnimeToonRemapCS::FParameters>();
    PassParams->View             = View.ViewUniformBuffer;
    PassParams->SceneTextures    = Inputs.SceneTextures;
    PassParams->OutputTexture    = OutputUAV;
    PassParams->AnimeEnvironment = AnimeEnvironmentUB;

    // ---- dispatch ----
    FIntPoint OutputSize = OutputDesc.Extent;
    FIntVector GroupCount = FIntVector(
        FMath::DivideAndRoundUp(OutputSize.X, 8),
        FMath::DivideAndRoundUp(OutputSize.Y, 8),
        1);

    TShaderMapRef<FMMDAnimeToonRemapCS> ComputeShader(GetGlobalShaderMap(View.FeatureLevel));
    FComputeShaderUtils::AddPass(
        GraphBuilder,
        RDG_EVENT_NAME("MMDAnimeToonLighting"),
        ComputeShader,
        PassParams,
        GroupCount);

    return FScreenPassTexture(OutputTexture);
}
