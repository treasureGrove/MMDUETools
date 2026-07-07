// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#include "Rendering/MMDAnimePostProcessPass.h"
#include "Rendering/MMDAnimeStencilValues.h"
#include "MMDAnimeToonRemapCS.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

static FRDGTextureSRVRef GetFallbackBlackTexture2D(FRDGBuilder& GraphBuilder)
{
	return GraphBuilder.CreateSRV(
		GSystemTextures.GetBlackDummy(GraphBuilder));
}

void AddMMDAnimePostProcessPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessMaterialInputs& Inputs,
	FRDGTextureRef SceneColor,
	const FMMDAnimeRenderParams& Params)
{
	if (!SceneColor)
	{
		return;
	}

	const FIntRect ViewportRect = View.UnscaledViewRect;
	const FIntPoint ViewportSize = ViewportRect.Size();

	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return;
	}

	RDG_EVENT_SCOPE(GraphBuilder, "MMDAnimeToonRemap");

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FMMDAnimeToonRemapCS> ComputeShader(GlobalShaderMap);

	if (!ComputeShader.IsValid())
	{
		return;
	}

	FRDGTextureDesc OutputDesc = SceneColor->Desc;
	OutputDesc.Flags |= TexCreate_UAV;
	FRDGTextureRef OutputTexture = GraphBuilder.CreateTexture(OutputDesc, TEXT("MMDAnimeOutput"));

	FMMDAnimeToonRemapCS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<FMMDAnimeToonRemapCS::FParameters>();

	PassParameters->SceneColorTexture = GraphBuilder.CreateSRV(SceneColor);

	// GBufferA / SceneDepth: FSceneTextures is in Renderer/Internal/ (accessible),
	// but retrieving it requires FViewInfo which is in Renderer/Private/ (not accessible from plugin).
	PassParameters->GBufferATexture = GetFallbackBlackTexture2D(GraphBuilder);
	PassParameters->SceneDepthTexture = GetFallbackBlackTexture2D(GraphBuilder);

	// Custom stencil: directly available from post-process inputs.
	PassParameters->CustomStencilTexture = Inputs.CustomDepthTexture
		? GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Inputs.CustomDepthTexture))
		: GetFallbackBlackTexture2D(GraphBuilder);

	PassParameters->OutputTexture = GraphBuilder.CreateUAV(OutputTexture);
	PassParameters->LinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->ShadowThreshold   = Params.ShadowThreshold;
	PassParameters->ShadowSoftness    = Params.ShadowSoftness;
	PassParameters->ShadowColor       = Params.ShadowColor;
	PassParameters->DeepShadowColor   = Params.DeepShadowColor;
	PassParameters->RimWidth  = Params.RimWidth;
	PassParameters->RimColor  = Params.RimColor;
	PassParameters->EnvironmentStrength = Params.EnvironmentStrength;
	PassParameters->AmbientColor        = Params.AmbientColor;
	PassParameters->SpecularSize     = Params.SpecularSize;
	PassParameters->SpecularHardness = Params.SpecularHardness;
	PassParameters->MainLightDirection = Params.MainLightDirection;
	PassParameters->MainLightColor     = Params.MainLightColor;
	PassParameters->CameraPosition     = Params.CameraPosition;
	PassParameters->ViewportSize    = FVector2f(static_cast<float>(ViewportSize.X), static_cast<float>(ViewportSize.Y));
	PassParameters->InvViewportSize = FVector2f(1.0f / static_cast<float>(ViewportSize.X), 1.0f / static_cast<float>(ViewportSize.Y));
	PassParameters->StencilBodyCloth = static_cast<int32>(MMDAnimeStencil::BodyCloth);
	PassParameters->StencilSkin      = static_cast<int32>(MMDAnimeStencil::Skin);
	PassParameters->StencilHair      = static_cast<int32>(MMDAnimeStencil::Hair);
	PassParameters->StencilFace      = static_cast<int32>(MMDAnimeStencil::Face);
	PassParameters->StencilEyes      = static_cast<int32>(MMDAnimeStencil::EyesMetal);

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

	AddCopyTexturePass(GraphBuilder, OutputTexture, SceneColor);
}
