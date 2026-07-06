#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

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
		SHADER_PARAMETER(float, ShadowThreshold)
		SHADER_PARAMETER(float, ShadowSoftness)
		SHADER_PARAMETER(FVector3f, ShadowColor)
		SHADER_PARAMETER(FVector3f, DeepShadowColor)
		SHADER_PARAMETER(float, RimWidth)
		SHADER_PARAMETER(FVector3f, RimColor)
		SHADER_PARAMETER(float, EnvironmentStrength)
		SHADER_PARAMETER(FVector3f, AmbientColor)
		SHADER_PARAMETER(float, SpecularSize)
		SHADER_PARAMETER(float, SpecularHardness)
		SHADER_PARAMETER(FVector3f, MainLightDirection)
		SHADER_PARAMETER(FVector3f, MainLightColor)
		SHADER_PARAMETER(FVector3f, CameraPosition)
		SHADER_PARAMETER(FVector2f, ViewportSize)
		SHADER_PARAMETER(FVector2f, InvViewportSize)
		SHADER_PARAMETER(int32, StencilBodyCloth)
		SHADER_PARAMETER(int32, StencilSkin)
		SHADER_PARAMETER(int32, StencilHair)
		SHADER_PARAMETER(int32, StencilFace)
		SHADER_PARAMETER(int32, StencilEyes)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}
};
