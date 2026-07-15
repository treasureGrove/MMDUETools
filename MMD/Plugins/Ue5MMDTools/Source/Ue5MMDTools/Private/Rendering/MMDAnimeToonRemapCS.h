#pragma once
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "Rendering/MMDAnimeEnvironmentUniformBuffer.h"

class FMMDAnimeToonRemapCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMMDAnimeToonRemapCS);
	SHADER_USE_PARAMETER_STRUCT(FMMDAnimeToonRemapCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FAnimeEnvironmentParameters, AnimeEnvironment)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
	}
};
