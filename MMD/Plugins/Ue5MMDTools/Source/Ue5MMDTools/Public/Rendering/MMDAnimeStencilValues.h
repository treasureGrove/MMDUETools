#pragma once

#include "CoreMinimal.h"

// Custom Stencil values for MMD anime post-processing identification.
// These values are written into the Custom Depth / Stencil buffer so the
// anime post-process material can apply the correct shading model per region.
namespace MMDAnimeStencil
{
	constexpr uint8 None      = 0;  // Not processed by anime post-process
	constexpr uint8 BodyCloth = 1;  // General toon shading
	constexpr uint8 Skin      = 2;  // SSS + warm shadow terminator
	constexpr uint8 Hair      = 3;  // Anisotropic specular
	constexpr uint8 Face      = 4;  // Special shadow handling
	constexpr uint8 EyesMetal = 5;  // High reflection, minimal toon
}

// Utility: classify a PMX material name (JP or EN) into a stencil category.
// Falls back to BodyCloth when no keyword matches.
inline uint8 ClassifyMMDMaterialName(const FString& NameJP, const FString& NameEN)
{
	// Combine both names for matching; check lower-case.
	auto Contains = [](const FString& Haystack, const TCHAR* Needle) -> bool
	{
		return Haystack.Contains(Needle, ESearchCase::IgnoreCase);
	};

	// --- Face (check before skin because "face" is more specific) ---
	if (Contains(NameJP, TEXT("顔"))  || Contains(NameEN, TEXT("face"))
	 || Contains(NameJP, TEXT("フェイス")) || Contains(NameEN, TEXT("Face")))
	{
		// Exclude "face" when it's clearly body/neck prefix
		if (!Contains(NameEN, TEXT("body")) && !Contains(NameJP, TEXT("体")))
		{
			return MMDAnimeStencil::Face;
		}
	}

	// --- Eyes / Metal ---
	if (Contains(NameJP, TEXT("目"))   || Contains(NameEN, TEXT("eye"))
	 || Contains(NameJP, TEXT("瞳"))   || Contains(NameEN, TEXT("Eye"))
	 || Contains(NameJP, TEXT("金属")) || Contains(NameEN, TEXT("metal"))
	 || Contains(NameEN, TEXT("Metal")) || Contains(NameEN, TEXT("gold"))
	 || Contains(NameJP, TEXT("金"))   || Contains(NameJP, TEXT("アイ"))
	 || Contains(NameEN, TEXT("iris")))
	{
		return MMDAnimeStencil::EyesMetal;
	}

	// --- Hair ---
	if (Contains(NameJP, TEXT("髪"))  || Contains(NameEN, TEXT("hair"))
	 || Contains(NameJP, TEXT(" Hair")) || Contains(NameEN, TEXT("Hair"))
	 || Contains(NameJP, TEXT("ヘア")) || Contains(NameJP, TEXT("前髪"))
	 || Contains(NameJP, TEXT("後ろ髪")) || Contains(NameJP, TEXT("ツインテール")))
	{
		return MMDAnimeStencil::Hair;
	}

	// --- Skin ---
	if (Contains(NameJP, TEXT("肌"))  || Contains(NameEN, TEXT("skin"))
	 || Contains(NameJP, TEXT("スキン")) || Contains(NameEN, TEXT("Skin"))
	 || Contains(NameJP, TEXT("裸"))  || Contains(NameJP, TEXT("体"))
	 || Contains(NameEN, TEXT("body")) || Contains(NameEN, TEXT("Body"))
	 || Contains(NameJP, TEXT("ボディ")) || Contains(NameEN, TEXT("arm"))
	 || Contains(NameEN, TEXT("leg")) || Contains(NameEN, TEXT("hand"))
	 || Contains(NameJP, TEXT("手"))  || Contains(NameJP, TEXT("足"))
	 || Contains(NameJP, TEXT("腕"))  || Contains(NameJP, TEXT("脚")))
	{
		return MMDAnimeStencil::Skin;
	}

	// Default: body / cloth
	return MMDAnimeStencil::BodyCloth;
}
