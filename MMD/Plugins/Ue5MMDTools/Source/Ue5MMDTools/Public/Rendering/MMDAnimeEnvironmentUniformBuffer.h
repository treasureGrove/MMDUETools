#pragma once
#include "CoreMinimal.h"
#include "UniformBuffer.h"

#define MMD_ANIME_MAX_POINT_LIGHTS  8
#define MMD_ANIME_MAX_SPOT_LIGHTS   8
#define MMD_ANIME_MAX_RECT_LIGHTS   8

BEGIN_UNIFORM_BUFFER_STRUCT(FAnimeEnvironmentParameters, )
    // directional light
    SHADER_PARAMETER(FVector4f, DirectionalLightDirection)
    SHADER_PARAMETER(FVector4f, DirectionalLightColor)
    // point light
    SHADER_PARAMETER(FVector4f, PointLightCount)
    SHADER_PARAMETER_ARRAY(FVector4f, PointLightPosition, [MMD_ANIME_MAX_POINT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, PointLightColor, [MMD_ANIME_MAX_POINT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, PointLightRadius, [MMD_ANIME_MAX_POINT_LIGHTS])
    // spot light
    SHADER_PARAMETER(FVector4f, SpotLightCount)
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightPosition, [MMD_ANIME_MAX_SPOT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightDirection, [MMD_ANIME_MAX_SPOT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightColor, [MMD_ANIME_MAX_SPOT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightRadius, [MMD_ANIME_MAX_SPOT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightInnerConeCos, [MMD_ANIME_MAX_SPOT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, SpotLightOuterConeCos, [MMD_ANIME_MAX_SPOT_LIGHTS])
    // rect light (占位)
    SHADER_PARAMETER(FVector4f, RectLightCount)
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightPosition, [MMD_ANIME_MAX_RECT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightNormal, [MMD_ANIME_MAX_RECT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightColor, [MMD_ANIME_MAX_RECT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightSize, [MMD_ANIME_MAX_RECT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightBarnCosAngle, [MMD_ANIME_MAX_RECT_LIGHTS])
    SHADER_PARAMETER_ARRAY(FVector4f, RectLightBarnLength, [MMD_ANIME_MAX_RECT_LIGHTS])
    // camera
    SHADER_PARAMETER(FVector4f, ViewPosition)
    // fog
    SHADER_PARAMETER(FVector4f, FogColor)
    SHADER_PARAMETER(FVector4f, FogParams)
    // toon ramp
    SHADER_PARAMETER(FVector4f, ToonShadowColor)
    SHADER_PARAMETER(FVector4f, ToonShadowStep)       // x: shadow step, y: highlight step
    SHADER_PARAMETER(FVector4f, ToonRimColor)
    SHADER_PARAMETER(FVector4f, ToonRimParams)        // x: power, y: offset
END_UNIFORM_BUFFER_STRUCT()
