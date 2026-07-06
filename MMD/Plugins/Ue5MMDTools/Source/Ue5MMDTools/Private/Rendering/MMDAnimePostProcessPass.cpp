// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#include "Rendering/MMDAnimePostProcessPass.h"
#include "Rendering/MMDAnimeStencilValues.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "ScreenPass.h"

// ---------------------------------------------------------------------------
// Shader class declaration (matches MMDAnimeShaders.cpp definition)
// ---------------------------------------------------------------------------
class FMMDAnimeToonRemapCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMMDAnimeToonRemapCS);
	SHADER_USE_PARAMETER_STRUCT(FMMDAnimeToonRemapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, GBufferATexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, CustomStencilTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
		// Toon shadow params
		SHADER_PARAMETER(float, ShadowThreshold)
		SHADER_PARAMETER(float, ShadowSoftness)
		SHADER_PARAMETER(FVector3f, ShadowColor)
		SHADER_PARAMETER(FVector3f, DeepShadowColor)
		// Rim params
		SHADER_PARAMETER(float, RimWidth)
		SHADER_PARAMETER(FVector3f, RimColor)
		// Environment
		SHADER_PARAMETER(float, EnvironmentStrength)
		SHADER_PARAMETER(FVector3f, AmbientColor)
		// Specular
		SHADER_PARAMETER(float, SpecularSize)
		SHADER_PARAMETER(float, SpecularHardness)
		// View info
		SHADER_PARAMETER(FVector3f, MainLightDirection)
		SHADER_PARAMETER(FVector3f, MainLightColor)
		SHADER_PARAMETER(FVector3f, CameraPosition)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, InvViewportSize)
		// Stencil classification constants
		SHADER_PARAMETER(int32, StencilBodyCloth)
		SHADER_PARAMETER(int32, StencilSkin)
		SHADER_PARAMETER(int32, StencilHair)
		SHADER_PARAMETER(int32, StencilFace)
		SHADER_PARAMETER(int32, StencilEyes)
	END_SHADER_PARAMETER_STRUCT()
};

// ---------------------------------------------------------------------------
// Helper: create a 1x1 black fallback texture when a real texture is not
// available (e.g. GBufferA outside a deferred path).
// ---------------------------------------------------------------------------
static FRDGTextureSRVRef GetFallbackBlackTexture2D(FRDGBuilder& GraphBuilder)
{
	return GraphBuilder.CreateSRV(
		GSystemTextures.GetBlackDummy(GraphBuilder));
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------
void AddMMDAnimePostProcessPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneColor,
	const FMMDAnimeRenderParams& Params)
{
	if (!SceneColor)
	{
		return;
	}

	const FIntRect ViewportRect = View.ViewRect;
	const FIntPoint ViewportSize = ViewportRect.Size();

	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "MMDAnimeToonRemap");

	// ---- Retrieve the compute shader from the global shader map -----------
	const FShaderMapBase* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FMMDAnimeToonRemapCS> ComputeShader(ShaderMap);

	if (!ComputeShader.IsValid())
	{
		// Shader not compiled / not available – skip the pass silently.
		return;
	}

	// ---- Create the output UAV texture (same desc as SceneColor + UAV) ---
	FRDGTextureDesc OutputDesc = SceneColor->Desc;
	OutputDesc.Flags |= TexCreate_UAV;
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MMDAnimeOutput"));

	// ---- Allocate and fill parameter block --------------------------------
	FMMDAnimeToonRemapCS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FMMDAnimeToonRemapCS::FParameters>();

	// Scene color (SRV)
	PassParameters->SceneColorTexture = GraphBuilder.CreateSRV(SceneColor);

	// GBufferA – try to get from the view's scene textures.
	// In a full deferred pipeline these come from FSceneTextures; for now
	// we provide a fallback and refine the binding later.
	PassParameters->GBufferATexture = GetFallbackBlackTexture2D(GraphBuilder);

	// Scene depth – try from the view's override resources
	PassParameters->SceneDepthTexture = GetFallbackBlackTexture2D(GraphBuilder);

	// Custom stencil buffer (filled by the stencil marking pass)
	PassParameters->CustomStencilTexture = GetFallbackBlackTexture2D(GraphBuilder);

	// Output UAV
	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);

	// Linear clamp sampler
	PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	// --- Toon shadow parameters ---
	PassParameters->ShadowThreshold   = Params.ShadowThreshold;
	PassParameters->ShadowSoftness    = Params.ShadowSoftness;
	PassParameters->ShadowColor       = Params.ShadowColor;
	PassParameters->DeepShadowColor   = Params.DeepShadowColor;

	// --- Rim light parameters ---
	PassParameters->RimWidth  = Params.RimWidth;
	PassParameters->RimColor  = Params.RimColor;

	// --- Environment / ambient ---
	PassParameters->EnvironmentStrength = Params.EnvironmentStrength;
	PassParameters->AmbientColor        = Params.AmbientColor;

	// --- Specular ---
	PassParameters->SpecularSize     = Params.SpecularSize;
	PassParameters->SpecularHardness = Params.SpecularHardness;

	// --- View / camera ---
	PassParameters->MainLightDirection = Params.MainLightDirection;
	PassParameters->MainLightColor     = Params.MainLightColor;
	PassParameters->CameraPosition     = Params.CameraPosition;

	PassParameters->ViewportSize    = FVector2f(static_cast<float>(ViewportSize.X),
	                                            static_cast<float>(ViewportSize.Y));
	PassParameters->InvViewportSize = FVector2f(1.0f / static_cast<float>(ViewportSize.X),
	                                            1.0f / static_cast<float>(ViewportSize.Y));

	// --- Stencil classification constants (from MMDAnimeStencil namespace) --
	PassParameters->StencilBodyCloth = static_cast<int32>(MMDAnimeStencil::BodyCloth);
	PassParameters->StencilSkin      = static_cast<int32>(MMDAnimeStencil::Skin);
	PassParameters->StencilHair      = static_cast<int32>(MMDAnimeStencil::Hair);
	PassParameters->StencilFace      = static_cast<int32>(MMDAnimeStencil::Face);
	PassParameters->StencilEyes      = static_cast<int32>(MMDAnimeStencil::EyesMetal);

	// ---- Dispatch compute shader (8x8 thread groups) ---------------------
	const FIntVector GroupCount(
		FMath::DivideAndRoundUp(ViewportSize.X, 8),
		FMath::DivideAndRoundUp(ViewportSize.Y, 8),
		1);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("MMDAnimeToonRemapCS"),
		ComputeShader,
		PassParameters,
		GroupCount);

	// ---- Copy the compute output back into SceneColor ---------------------
	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor);
}
