#pragma once
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FMMDAnimeWriteLightsCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMMDAnimeWriteLightsCS);
	SHADER_USE_PARAMETER_STRUCT(FMMDAnimeWriteLightsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, light_data)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, output_texture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
