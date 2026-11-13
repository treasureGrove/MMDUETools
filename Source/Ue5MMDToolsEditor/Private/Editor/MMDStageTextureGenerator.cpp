#include "Editor/MMDStageTextureGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	float PeriodicNoise(float U, float V, int32 Seed)
	{
		// 整数频率保证平铺边界一致，种子只改变相位。
		FRandomStream Random(Seed);
		float Value = 0.0f;
		float Weight = 0.0f;
		for (int32 Octave = 0; Octave < 5; ++Octave)
		{
			const float Frequency = static_cast<float>(1 << Octave);
			const float Amplitude = 1.0f / Frequency;
			const float PhaseA = Random.FRand() * UE_TWO_PI;
			const float PhaseB = Random.FRand() * UE_TWO_PI;
			Value += Amplitude * FMath::Sin(UE_TWO_PI * Frequency * U + PhaseA)
				* FMath::Cos(UE_TWO_PI * Frequency * V + PhaseB);
			Weight += Amplitude;
		}
		return FMath::Clamp(0.5f + 0.5f * Value / Weight, 0.0f, 1.0f);
	}

	FColor LinearPixel(const FLinearColor& Color)
	{
		return Color.GetClamped().ToFColor(false);
	}

	bool SaveTexture(const TCHAR* Name, int32 Width, int32 Height, const TArray<FColor>& Pixels,
		bool bSRGB, bool bNormal, bool bOverwrite, UTexture2D*& Result, FString& Error)
	{
		const FString PackageName = FString(TEXT("/Ue5MMDTools/Stages/Textures/")) + Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		const bool bExists = FPackageName::DoesPackageExist(PackageName);
		Result = bExists ? LoadObject<UTexture2D>(nullptr, *ObjectPath) : FindObject<UTexture2D>(nullptr, *ObjectPath);
		if (bExists && !Result)
		{
			Error = FString::Printf(TEXT("无法加载已有生成贴图：%s"), *ObjectPath);
			return false;
		}
		if (Result && !bOverwrite)
		{
			return true;
		}
		UPackage* Package = CreatePackage(*PackageName);
		const bool bNew = !Result;
		if (bNew)
		{
			Result = NewObject<UTexture2D>(Package, FName(Name), RF_Public | RF_Standalone);
		}
		Result->Source.Init(Width, Height, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Result->SRGB = bSRGB;
		Result->CompressionSettings = bNormal ? TC_Normalmap : TC_Default;
		Result->MipGenSettings = Height == 1 ? TMGS_NoMipmaps : TMGS_FromTextureGroup;
		Result->AddressX = Height == 1 || bSRGB ? TA_Clamp : TA_Wrap;
		Result->AddressY = Height == 1 || bSRGB ? TA_Clamp : TA_Wrap;
		Result->Filter = TF_Bilinear;
		Result->NeverStream = Height == 1 || Width <= 4;
		Result->PostEditChange();
		Result->MarkPackageDirty();
		if (bNew)
		{
			FAssetRegistryModule::AssetCreated(Result);
		}
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Result, *Filename, Args))
		{
			Error = FString::Printf(TEXT("生成贴图保存失败：%s"), *Filename);
			return false;
		}
		return true;
	}
}

bool UMMDStageTextureGenerator::GenerateGenericStageTextures(TArray<UTexture2D*>& Textures,
	FString& Error, int32 Resolution, int32 Seed, bool bOverwrite)
{
	Textures.Reset();
	Error.Reset();
	if (!IsInGameThread() || Resolution < 32 || Resolution > 1024 || !FMath::IsPowerOfTwo(Resolution))
	{
		Error = TEXT("贴图生成必须在主线程运行，分辨率须为 32～1024 的二次幂。");
		return false;
	}
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Resolution * Resolution);
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float U = static_cast<float>(X) / (Resolution - 1);
			const float V = static_cast<float>(Y) / (Resolution - 1);
			const float Noise = PeriodicNoise(U, V, Seed);
			Pixels[Y * Resolution + X] = LinearPixel(FLinearColor(Noise, Noise, Noise, 1.0f));
		}
	}
	UTexture2D* Texture = nullptr;
	if (!SaveTexture(TEXT("T_StageNoiseSeamless"), Resolution, Resolution, Pixels, false, false, bOverwrite, Texture, Error)) return false;
	Textures.Add(Texture);

	Pixels.SetNumUninitialized(Resolution);
	for (int32 X = 0; X < Resolution; ++X)
	{
		const float Light = static_cast<float>(X) / (Resolution - 1);
		const float Ramp = FMath::SmoothStep(0.32f, 0.52f, Light);
		Pixels[X] = LinearPixel(FMath::Lerp(FLinearColor(0.48f, 0.54f, 0.66f), FLinearColor(1.0f, 0.98f, 0.94f), Ramp));
	}
	if (!SaveTexture(TEXT("T_StageToonRamp"), Resolution, 1, Pixels, false, false, bOverwrite, Texture, Error)) return false;
	Textures.Add(Texture);

	Pixels.SetNumUninitialized(Resolution * Resolution);
	const FVector LightDirection = FVector(-0.4f, -0.3f, 0.86f).GetSafeNormal();
	for (int32 Y = 0; Y < Resolution; ++Y)
	{
		for (int32 X = 0; X < Resolution; ++X)
		{
			const float NX = 2.0f * X / (Resolution - 1) - 1.0f;
			const float NY = 1.0f - 2.0f * Y / (Resolution - 1);
			const FVector Normal = FVector(NX, NY, FMath::Sqrt(FMath::Max(1.0f - NX * NX - NY * NY, 0.0f))).GetSafeNormal();
			const float Diffuse = FMath::Max(FVector::DotProduct(Normal, LightDirection), 0.0);
			const float Specular = FMath::Pow(Diffuse, 24.0f) * 0.22f;
			const FLinearColor Color = FLinearColor(0.2f, 0.24f, 0.3f) + FLinearColor(0.62f, 0.66f, 0.7f) * Diffuse + FLinearColor::White * Specular;
			Pixels[Y * Resolution + X] = Color.GetClamped().ToFColor(true);
		}
	}
	if (!SaveTexture(TEXT("T_StageMatCapPearl"), Resolution, Resolution, Pixels, true, false, bOverwrite, Texture, Error)) return false;
	Textures.Add(Texture);

	Pixels.Init(FColor(128, 128, 255, 255), 16);
	if (!SaveTexture(TEXT("T_StageFlatNormal"), 4, 4, Pixels, false, true, bOverwrite, Texture, Error)) return false;
	Textures.Add(Texture);
	return true;
}
