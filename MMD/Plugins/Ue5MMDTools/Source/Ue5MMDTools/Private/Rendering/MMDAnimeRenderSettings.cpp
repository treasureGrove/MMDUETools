// Copyright (c) 2024-2026 MMDUETools. All Rights Reserved.

#include "Rendering/MMDAnimeRenderSettings.h"

void UMMDAnimeRenderSettings::ApplyPreset(EMMDAnimePreset Preset)
{
	switch (Preset)
	{
	case EMMDAnimePreset::SoftAnime:
		ShadowThreshold    = 0.3f;
		ShadowSoftness     = 0.12f;
		ShadowColor        = FLinearColor(0.65f, 0.55f, 0.75f, 1.0f);
		DeepShadowColor    = FLinearColor(0.4f, 0.35f, 0.5f, 1.0f);
		RimWidth           = 0.2f;
		RimColor           = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		EnvironmentStrength = 0.6f;
		SpecularSize       = 0.2f;
		SpecularHardness   = 0.6f;
		break;

	case EMMDAnimePreset::CelShading:
		ShadowThreshold    = 0.4f;
		ShadowSoftness     = 0.02f;
		ShadowColor        = FLinearColor(0.5f, 0.4f, 0.6f, 1.0f);
		DeepShadowColor    = FLinearColor(0.2f, 0.15f, 0.3f, 1.0f);
		RimWidth           = 0.4f;
		RimColor           = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		EnvironmentStrength = 0.3f;
		SpecularSize       = 0.1f;
		SpecularHardness   = 0.95f;
		break;

	case EMMDAnimePreset::SemiRealistic:
		ShadowThreshold    = 0.35f;
		ShadowSoftness     = 0.08f;
		ShadowColor        = FLinearColor(0.7f, 0.65f, 0.75f, 1.0f);
		DeepShadowColor    = FLinearColor(0.45f, 0.4f, 0.5f, 1.0f);
		RimWidth           = 0.15f;
		RimColor           = FLinearColor(0.9f, 0.95f, 1.0f, 1.0f);
		EnvironmentStrength = 0.8f;
		SpecularSize       = 0.15f;
		SpecularHardness   = 0.7f;
		break;

	case EMMDAnimePreset::HighContrast:
		ShadowThreshold    = 0.45f;
		ShadowSoftness     = 0.03f;
		ShadowColor        = FLinearColor(0.4f, 0.3f, 0.55f, 1.0f);
		DeepShadowColor    = FLinearColor(0.15f, 0.1f, 0.25f, 1.0f);
		RimWidth           = 0.5f;
		RimColor           = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
		EnvironmentStrength = 0.4f;
		SpecularSize       = 0.25f;
		SpecularHardness   = 0.9f;
		break;
	}
}
