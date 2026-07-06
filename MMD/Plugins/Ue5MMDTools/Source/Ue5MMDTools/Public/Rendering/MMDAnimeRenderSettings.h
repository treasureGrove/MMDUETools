// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MMDAnimeRenderSettings.generated.h"

/**
 * Preset styles for anime rendering
 */
UENUM(BlueprintType)
enum class EMMDAnimePreset : uint8
{
	SoftAnime       UMETA(DisplayName="Soft Anime"),       // Soft shadows, gentle rim, weak specular
	CelShading      UMETA(DisplayName="Cel Shading"),      // Hard shadows, strong rim, sharp specular
	SemiRealistic   UMETA(DisplayName="Semi Realistic"),   // Medium softness, strong env response
	HighContrast    UMETA(DisplayName="High Contrast"),    // Deep shadows, wide rim, large specular
};

/**
 * Runtime parameters for the MMD anime post-process rendering system.
 * Create as a DataAsset and assign to configure the look.
 * Parameters are pushed to the SceneViewExtension every frame.
 */
UCLASS(BlueprintType)
class UE5MMDTOOLS_API UMMDAnimeRenderSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	// === Shadow Control ===

	/** Primary shadow threshold - higher = more shadow area (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow", meta=(ClampMin=0.0, ClampMax=1.0))
	float ShadowThreshold = 0.35f;

	/** Shadow edge softness - higher = softer transition (0-0.5) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow", meta=(ClampMin=0.0, ClampMax=0.5))
	float ShadowSoftness = 0.05f;

	/** Shadow tint color (multiplied with base color in shadow regions) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow")
	FLinearColor ShadowColor = FLinearColor(0.6f, 0.5f, 0.7f, 1.0f);

	/** Deep shadow color for second shadow layer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Shadow")
	FLinearColor DeepShadowColor = FLinearColor(0.3f, 0.25f, 0.4f, 1.0f);

	// === Rim Light ===

	/** Rim light width - higher = wider rim (0-1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim", meta=(ClampMin=0.0, ClampMax=1.0))
	float RimWidth = 0.3f;

	/** Rim light color */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rim")
	FLinearColor RimColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	// === Environment Response ===

	/** How much environment/ambient light tints the shadow color (0=no tint, 1=full tint) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Environment", meta=(ClampMin=0.0, ClampMax=1.0))
	float EnvironmentStrength = 0.5f;

	// === Specular ===

	/** Anime specular highlight size (0.01-1, smaller = larger highlight) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specular", meta=(ClampMin=0.01, ClampMax=1.0))
	float SpecularSize = 0.15f;

	/** Specular edge hardness (0=soft gradient, 1=hard cel edge) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specular", meta=(ClampMin=0.0, ClampMax=1.0))
	float SpecularHardness = 0.85f;

	// === Global Control ===

	/** Master enable for the anime post-process */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global")
	bool bEnabled = true;

	/** Overall intensity blend (0=original rendering, 1=full anime effect) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Global", meta=(ClampMin=0.0, ClampMax=1.0))
	float EffectIntensity = 1.0f;

	// === Presets ===

	/** Apply a preset configuration */
	UFUNCTION(BlueprintCallable, Category="MMD|AnimeShader")
	void ApplyPreset(EMMDAnimePreset Preset);

	/** Get current settings as a parameter struct for passing to render thread */
	FLinearColor GetShadowColorVector() const { return ShadowColor; }
	FLinearColor GetDeepShadowColorVector() const { return DeepShadowColor; }
	FLinearColor GetRimColorVector() const { return RimColor; }
};
