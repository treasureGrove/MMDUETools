#include "Editor/MMDReferenceTextureGenerator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "TextureCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	constexpr int32 DetailResolution = 512;
	constexpr int32 DetailSeed = 20261009;
	constexpr double DetailTurn = 2.0 * UE_DOUBLE_PI;
	constexpr double DerivativeStep = 1.0 / DetailResolution;
	constexpr double PeriodicTolerance = 1.e-8;
	const TCHAR* DetailTextureRoot = TEXT("/Ue5MMDTools/Stages/Textures/");

	struct FDetailPattern
	{
		double Phases[8];

		FDetailPattern()
		{
			FRandomStream Random(DetailSeed);
			for (double& Phase : Phases) Phase = Random.FRand() * DetailTurn;
		}

		double HairHeight(double U, double V) const
		{
			const double Warp = 0.013 * FMath::Sin(DetailTurn * V + Phases[0])
				+ 0.008 * FMath::Sin(DetailTurn * (3.0 * U + V) + Phases[1])
				+ 0.004 * FMath::Sin(DetailTurn * (2.0 * V + U) + Phases[2]);
			const double AlongBundle = 0.75 + 0.25 * FMath::Sin(DetailTurn * (2.0 * U + V) + Phases[2]);
			const double BundlePhase = DetailTurn * 13.0 * (U + Warp) + Phases[3];
			// 疏密和截面变化集中到可读发束；细丝幅度受限，不以等距高频条纹承担塑形。
			return 0.0036 * AlongBundle * (FMath::Sin(BundlePhase) + 0.22 * FMath::Sin(2.0 * BundlePhase))
				+ 0.0006 * FMath::Sin(DetailTurn * (5.0 * U + V) + Phases[4])
				+ AlongBundle * 0.00022 * FMath::Sin(DetailTurn * 61.0 * (U + 0.70 * Warp) + Phases[5])
				+ 0.00012 * FMath::Sin(DetailTurn * 97.0 * (U + 0.40 * Warp) + Phases[6])
				+ 0.000055 * FMath::Sin(DetailTurn * (117.0 * U + V) + 0.35 * FMath::Sin(DetailTurn * 3.0 * V) + Phases[7]);
		}

		double CreaseProfile(double Phase) const
		{
			// 连续谐波形成窄折脊和宽谷面，保留可过滤的梯度而不生成断裂缝。
			return FMath::Sin(Phase) + 0.26 * FMath::Sin(2.0 * Phase) + 0.07 * FMath::Sin(3.0 * Phase);
		}

		void ClothCoordinates(double U, double V, double& FoldA, double& FoldB,
			double& ThreadU, double& ThreadV, double& FineWeave) const
		{
			const double WarpU = 0.11 * FMath::Sin(DetailTurn * V + Phases[0])
				+ 0.045 * FMath::Sin(DetailTurn * (2.0 * U + V) + Phases[1]);
			const double WarpV = 0.055 * FMath::Sin(DetailTurn * (2.0 * V - U) + Phases[2]);
			FoldA = CreaseProfile(DetailTurn * (8.0 * U + 2.0 * V + WarpU) + Phases[4]);
			FoldB = CreaseProfile(DetailTurn * (13.0 * U - 3.0 * V + WarpV) + Phases[5]);
			ThreadU = FMath::Cos(DetailTurn * 64.0 * U + Phases[6]);
			ThreadV = FMath::Cos(DetailTurn * 72.0 * V + Phases[7]);
			FineWeave = FMath::Sin(DetailTurn * 128.0 * U + Phases[1])
				* FMath::Sin(DetailTurn * 128.0 * V + Phases[2]);
		}

		double ClothHeight(double U, double V) const
		{
			double FoldA, FoldB, ThreadU, ThreadV, FineWeave;
			ClothCoordinates(U, V, FoldA, FoldB, ThreadU, ThreadV, FineWeave);
			// 主折痕约12cm一组，谐波收窄至3–6cm；次折痕弱且同向，避免等强横纵网格。
			return 0.0068 * FoldA + 0.0016 * FoldB
				+ 0.0005 * FMath::Sin(DetailTurn * (3.0 * U - 2.0 * V) + Phases[3])
				+ 0.00010 * (ThreadU + ThreadV) + 0.000035 * ThreadU * ThreadV
				+ 0.000025 * FineWeave;
		}

		FVector Normal(double U, double V, bool bHair) const
		{
			auto Height = [this, bHair](double X, double Y)
			{
				return bHair ? HairHeight(X, Y) : ClothHeight(X, Y);
			};
			const double DU = (Height(U + DerivativeStep, V) - Height(U - DerivativeStep, V)) / (2.0 * DerivativeStep);
			const double DV = (Height(U, V + DerivativeStep) - Height(U, V - DerivativeStep)) / (2.0 * DerivativeStep);
			if (!FMath::IsFinite(DU) || !FMath::IsFinite(DV)) return FVector::ZeroVector;
			const FVector Value(-DU, -DV, 1.0);
			return Value.GetSafeNormal(1.e-20, FVector::UpVector);
		}

		FVector ClothData(double U, double V) const
		{
			double FoldA, FoldB, ThreadU, ThreadV, FineWeave;
			ClothCoordinates(U, V, FoldA, FoldB, ThreadU, ThreadV, FineWeave);
			const double ThreadHeight = 0.5 * (ThreadU + ThreadV);
			return FVector(
				FMath::Clamp(0.765 + 0.032 * ThreadHeight + 0.018 * FoldA + 0.010 * FoldB + 0.010 * FineWeave, 0.65, 0.90),
				FMath::Clamp(0.966 + 0.012 * ThreadHeight + 0.004 * FoldA, 0.88, 1.0),
				FMath::Clamp(0.56 + 0.080 * ThreadHeight + 0.024 * FineWeave + 0.014 * FoldB, 0.35, 0.75));
		}
	};

	struct FDetailValidation
	{
		double NormalLengthError = 0.0;
		double NormalXYMaximum = 0.0;
		double MinimumNormalZ = 1.0;
		double PeriodicHeightError = 0.0;
		double PeriodicNormalError = 0.0;
		double PeriodicDataError = 0.0;
		double QuantizedNormalLengthError = 0.0;
	};

	uint8 EncodeUnit(double Value)
	{
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value * 255.0), 0, 255));
	}

	FColor EncodeNormal(const FVector& Normal)
	{
		return FColor(EncodeUnit(Normal.X * 0.5 + 0.5), EncodeUnit(Normal.Y * 0.5 + 0.5),
			EncodeUnit(Normal.Z * 0.5 + 0.5), 255);
	}

	FColor EncodeClothData(const FVector& Value)
	{
		// 8位量化也严格留在公开的物理范围内，A始终为1。
		return FColor(static_cast<uint8>(FMath::Clamp<int32>(EncodeUnit(Value.X), 166, 229)),
			static_cast<uint8>(FMath::Clamp<int32>(EncodeUnit(Value.Y), 225, 255)),
			static_cast<uint8>(FMath::Clamp<int32>(EncodeUnit(Value.Z), 90, 191)), 255);
	}

	bool CheckNormal(const FVector& Normal, FDetailValidation& Validation, FString& Error)
	{
		if (Normal.ContainsNaN())
		{
			Error = TEXT("通用细节法线包含非有限分量，未保存资产");
			return false;
		}
		const double LengthError = FMath::Abs(Normal.SizeSquared() - 1.0);
		const double XY = FMath::Sqrt(Normal.X * Normal.X + Normal.Y * Normal.Y);
		Validation.NormalLengthError = FMath::Max(Validation.NormalLengthError, LengthError);
		Validation.NormalXYMaximum = FMath::Max(Validation.NormalXYMaximum, XY);
		Validation.MinimumNormalZ = FMath::Min(Validation.MinimumNormalZ, Normal.Z);
		const FColor Encoded = EncodeNormal(Normal);
		const FVector Decoded(Encoded.R / 127.5 - 1.0, Encoded.G / 127.5 - 1.0, Encoded.B / 127.5 - 1.0);
		Validation.QuantizedNormalLengthError = FMath::Max(Validation.QuantizedNormalLengthError,
			FMath::Abs(Decoded.SizeSquared() - 1.0));
		if (LengthError > 1.e-10 || Normal.Z < 0.70 || XY >= 0.72
			|| Decoded.ContainsNaN() || Decoded.X * Decoded.X + Decoded.Y * Decoded.Y >= 1.0)
		{
			Error = TEXT("通用细节法线包含非法或过强的斜率，未保存资产");
			return false;
		}
		return true;
	}

	bool CheckPeriodicity(const FDetailPattern& Pattern, bool bHair, FDetailValidation& Validation, FString& Error)
	{
		auto Height = [&Pattern, bHair](double U, double V)
		{
			return bHair ? Pattern.HairHeight(U, V) : Pattern.ClothHeight(U, V);
		};
		for (int32 Index = 0; Index <= DetailResolution; ++Index)
		{
			const double T = static_cast<double>(Index) / DetailResolution;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double U = Axis == 0 ? 0.0 : T;
				const double V = Axis == 0 ? T : 0.0;
				const double OtherU = Axis == 0 ? 1.0 : T;
				const double OtherV = Axis == 0 ? T : 1.0;
				Validation.PeriodicHeightError = FMath::Max(Validation.PeriodicHeightError,
					FMath::Abs(Height(U, V) - Height(OtherU, OtherV)));
				Validation.PeriodicNormalError = FMath::Max(Validation.PeriodicNormalError,
					(Pattern.Normal(U, V, bHair) - Pattern.Normal(OtherU, OtherV, bHair)).GetAbsMax());
				if (!bHair)
					Validation.PeriodicDataError = FMath::Max(Validation.PeriodicDataError,
						(Pattern.ClothData(U, V) - Pattern.ClothData(OtherU, OtherV)).GetAbsMax());
			}
		}
		if (!FMath::IsFinite(Validation.PeriodicHeightError) || !FMath::IsFinite(Validation.PeriodicNormalError)
			|| !FMath::IsFinite(Validation.PeriodicDataError) || Validation.PeriodicHeightError > PeriodicTolerance
			|| Validation.PeriodicNormalError > PeriodicTolerance || Validation.PeriodicDataError > PeriodicTolerance)
		{
			Error = TEXT("通用细节贴图周期边界检验失败，未保存资产");
			return false;
		}
		return true;
	}

	bool ValidateExisting(UTexture2D* Texture, bool bNormal, FString& Error)
	{
		if (!Texture || !Texture->Source.IsValid() || Texture->Source.GetSizeX() != DetailResolution
			|| Texture->Source.GetSizeY() != DetailResolution || Texture->Source.GetNumLayers() != 1
			|| Texture->SRGB || Texture->CompressionSettings != (bNormal ? TC_Normalmap : TC_VectorDisplacementmap)
			|| Texture->AddressX != TA_Wrap || Texture->AddressY != TA_Wrap || Texture->Filter != TF_Trilinear
			|| Texture->VirtualTextureStreaming || Texture->MipGenSettings == TMGS_NoMipmaps
			|| (bNormal && Texture->bFlipGreenChannel))
		{
			Error = FString::Printf(TEXT("已有通用细节贴图类型、尺寸或采样设置不兼容；不会覆盖用户资产：%s"),
				Texture ? *Texture->GetPathName() : TEXT("无法加载"));
			return false;
		}
		return true;
	}

	bool FindTexture(const TCHAR* Name, bool bNormal, UTexture2D*& Texture, bool& bNeedsGeneration, FString& Error)
	{
		const FString PackageName = FString(DetailTextureRoot) + Name;
		const FString ObjectPath = PackageName + TEXT(".") + Name;
		const bool bOnDisk = FPackageName::DoesPackageExist(PackageName);
		UObject* Existing = bOnDisk ? LoadObject<UObject>(nullptr, *ObjectPath)
			: FindObject<UObject>(nullptr, *ObjectPath);
		if (bOnDisk || Existing)
		{
			Texture = Cast<UTexture2D>(Existing);
			bNeedsGeneration = false;
			if (!bOnDisk)
			{
				Error = FString::Printf(TEXT("同名未保存纹理已存在，不能视为已完成生成且不会覆盖它：%s"), *ObjectPath);
				return false;
			}
			return ValidateExisting(Texture, bNormal, Error);
		}
		Texture = nullptr;
		bNeedsGeneration = true;
		return true;
	}

	bool CreateTexture(const TCHAR* Name, bool bNormal, const TArray<FColor>& Pixels,
		const FDetailValidation& Validation, UTexture2D*& Texture, FString& Error)
	{
		const FString PackageName = FString(DetailTextureRoot) + Name;
		UPackage* Package = CreatePackage(*PackageName);
		Texture = NewObject<UTexture2D>(Package, FName(Name), RF_Public | RF_Standalone);
		Texture->Source.Init(DetailResolution, DetailResolution, 1, 1, TSF_BGRA8,
			reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = bNormal ? TC_Normalmap : TC_VectorDisplacementmap;
		Texture->CompressionNoAlpha = bNormal;
		Texture->bFlipGreenChannel = false;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Wrap;
		Texture->Filter = TF_Trilinear;
		Texture->LODGroup = bNormal ? TEXTUREGROUP_CharacterNormalMap : TEXTUREGROUP_CharacterSpecular;
		Texture->MipGenSettings = TMGS_SimpleAverage;
		Texture->VirtualTextureStreaming = false;
		Texture->NeverStream = false;
		Texture->PostEditChange();
		UTexture* CompilingTextures[] = { Texture };
		FTextureCompilingManager::Get().FinishCompilation(MakeArrayView(CompilingTextures));
		if (!ValidateExisting(Texture, bNormal, Error)) return false;
		FMetaData& Metadata = Package->GetMetaData();
		Metadata.SetValue(Texture, TEXT("MMDDetailGenerator"), TEXT("PeriodicReferenceDetailsV2"));
		Metadata.SetValue(Texture, TEXT("MMDDetailPattern"), bNormal
			? TEXT("CurvedBundlesOrDominantCreases;BoundedContinuousHeight;CentralDifference")
			: TEXT("R=Roughness;G=ReflectanceCavity;B=WeaveGloss;AlignedClothNormalV2"));
		Metadata.SetValue(Texture, TEXT("MMDDetailSeed"), *FString::FromInt(DetailSeed));
		Metadata.SetValue(Texture, TEXT("MMDDetailResolution"), *FString::FromInt(DetailResolution));
		Metadata.SetValue(Texture, TEXT("MMDDetailNormalLengthError"), *FString::Printf(TEXT("%.17g"), Validation.NormalLengthError));
		Metadata.SetValue(Texture, TEXT("MMDDetailNormalXYMaximum"), *FString::Printf(TEXT("%.17g"), Validation.NormalXYMaximum));
		Metadata.SetValue(Texture, TEXT("MMDDetailMinimumNormalZ"), *FString::Printf(TEXT("%.17g"), Validation.MinimumNormalZ));
		Metadata.SetValue(Texture, TEXT("MMDDetailPeriodicHeightError"), *FString::Printf(TEXT("%.17g"), Validation.PeriodicHeightError));
		Metadata.SetValue(Texture, TEXT("MMDDetailPeriodicNormalError"), *FString::Printf(TEXT("%.17g"), Validation.PeriodicNormalError));
		Metadata.SetValue(Texture, TEXT("MMDDetailPeriodicDataError"), *FString::Printf(TEXT("%.17g"), Validation.PeriodicDataError));
		Metadata.SetValue(Texture, TEXT("MMDDetailQuantizedNormalLengthError"), *FString::Printf(TEXT("%.17g"), Validation.QuantizedNormalLengthError));
		Metadata.SetValue(Texture, TEXT("MMDDetailPixelSampling"), TEXT("PixelCenters;IntegerPeriodicFrequency;CentralDifference1/512"));
		Texture->MarkPackageDirty();
		FAssetRegistryModule::AssetCreated(Texture);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
		{
			Error = FString::Printf(TEXT("无法创建通用细节贴图目录：%s"), *Filename);
			return false;
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Texture, *Filename, Args))
		{
			Error = FString::Printf(TEXT("无法保存通用细节贴图：%s"), *Filename);
			return false;
		}
		UE_LOG(LogTemp, Display, TEXT("MMDDetail %s seed=%d periodic=(%.3g,%.3g,%.3g) normalXYmax=%.6f normalZmin=%.6f normalizedError=%.3g"),
			Name, DetailSeed, Validation.PeriodicHeightError, Validation.PeriodicNormalError, Validation.PeriodicDataError,
			Validation.NormalXYMaximum, Validation.MinimumNormalZ, Validation.NormalLengthError);
		return true;
	}
}

bool EnsureMMDReferenceDetailTextures(UTexture2D*& OutHairNormal, UTexture2D*& OutClothNormal,
	UTexture2D*& OutClothData, FString& OutError)
{
	OutHairNormal = nullptr;
	OutClothNormal = nullptr;
	OutClothData = nullptr;
	OutError.Reset();
	if (!IsInGameThread())
	{
		OutError = TEXT("通用细节贴图必须在游戏线程生成");
		return false;
	}
	// 新版使用独立资产名，既有图和用户修改过的图都不会被重新生成覆盖。
	const TCHAR* Names[] = { TEXT("T_MMD_GenericHairNormalV2"), TEXT("T_MMD_GenericClothNormalV2"), TEXT("T_MMD_GenericClothDataV2") };
	UTexture2D* Textures[] = { nullptr, nullptr, nullptr };
	bool bGenerate[] = { false, false, false };
	for (int32 Index = 0; Index < 3; ++Index)
		if (!FindTexture(Names[Index], Index < 2, Textures[Index], bGenerate[Index], OutError)) return false;

	if (bGenerate[0] || bGenerate[1] || bGenerate[2])
	{
		const FDetailPattern Pattern;
		FDetailValidation HairValidation;
		FDetailValidation ClothValidation;
		if (!CheckPeriodicity(Pattern, true, HairValidation, OutError)
			|| !CheckPeriodicity(Pattern, false, ClothValidation, OutError)) return false;
		TArray<FColor> HairPixels, ClothPixels, DataPixels;
		if (bGenerate[0]) HairPixels.SetNumUninitialized(DetailResolution * DetailResolution);
		if (bGenerate[1]) ClothPixels.SetNumUninitialized(DetailResolution * DetailResolution);
		if (bGenerate[2]) DataPixels.SetNumUninitialized(DetailResolution * DetailResolution);
		for (int32 Y = 0; Y < DetailResolution; ++Y)
		{
			for (int32 X = 0; X < DetailResolution; ++X)
			{
				// 首末行是相邻采样点，不复制边缘；验证的是跨整数平移后函数值和梯度连续。
				const double U = (static_cast<double>(X) + 0.5) / DetailResolution;
				const double V = (static_cast<double>(Y) + 0.5) / DetailResolution;
				const int32 PixelIndex = Y * DetailResolution + X;
				if (bGenerate[0])
				{
					const FVector Normal = Pattern.Normal(U, V, true);
					if (!CheckNormal(Normal, HairValidation, OutError)) return false;
					HairPixels[PixelIndex] = EncodeNormal(Normal);
				}
				if (bGenerate[1] || bGenerate[2])
				{
					const FVector Normal = Pattern.Normal(U, V, false);
					if (!CheckNormal(Normal, ClothValidation, OutError)) return false;
					if (bGenerate[1]) ClothPixels[PixelIndex] = EncodeNormal(Normal);
					if (bGenerate[2])
					{
						const FVector Value = Pattern.ClothData(U, V);
						if (Value.ContainsNaN() || Value.X < 0.65 || Value.X > 0.90 || Value.Y < 0.88 || Value.Y > 1.0
							|| Value.Z < 0.35 || Value.Z > 0.75)
						{
							OutError = TEXT("通用布料数据通道范围无效，未保存资产");
							return false;
						}
						DataPixels[PixelIndex] = EncodeClothData(Value);
					}
				}
			}
		}
		if (bGenerate[0] && !CreateTexture(Names[0], true, HairPixels, HairValidation, Textures[0], OutError)) return false;
		if (bGenerate[1] && !CreateTexture(Names[1], true, ClothPixels, ClothValidation, Textures[1], OutError)) return false;
		if (bGenerate[2] && !CreateTexture(Names[2], false, DataPixels, ClothValidation, Textures[2], OutError)) return false;
	}
	OutHairNormal = Textures[0];
	OutClothNormal = Textures[1];
	OutClothData = Textures[2];
	return true;
}
