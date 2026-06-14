#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"
#include "MMDModelDataAsset.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/Skeleton.h"
#include "Curves/RichCurve.h"
#include "Misc/FrameRate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
//构建
#include "ImportUtils/SkelImport.h"                  
#include "ImportUtils/SkeletalMeshImportUtils.h"       
#include "MeshUtilities.h"                             
#include "Engine/SkinnedAssetCommon.h"     
#include "Rendering/SkeletalMeshModel.h"             
#include "Rendering/SkeletalMeshLODModel.h"           
#include "Components/SkinnedMeshComponent.h"  
//材质
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceBasePropertyOverrides.h"
//转换
#include "Factories/TextureFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
//动画蓝图
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "Animation/MorphTarget.h"
//IKRig
#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#include "Rig/Solvers/IKRig_FBIKSolver.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargeterController.h"   
#endif
#pragma region 材质贴图
static constexpr uint8 PMX_DRAW_DOUBLE_SIDED = 0x01;
static constexpr uint8 PMX_DRAW_CAST_SHADOW = 0x04;

static uint16 ReadBmpU16(const TArray<uint8>& Data, int32 Offset)
{
	return (Offset >= 0 && Offset + 1 < Data.Num())
		? static_cast<uint16>(Data[Offset] | (Data[Offset + 1] << 8))
		: 0;
}

static uint32 ReadBmpU32(const TArray<uint8>& Data, int32 Offset)
{
	return (Offset >= 0 && Offset + 3 < Data.Num())
		? static_cast<uint32>(Data[Offset] | (Data[Offset + 1] << 8) | (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24))
		: 0;
}

static int32 ReadBmpS32(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(ReadBmpU32(Data, Offset));
}

static uint8 ExtractBmpMaskChannel(uint32 Pixel, uint32 Mask)
{
	if (Mask == 0)
	{
		return 255;
	}

	uint32 Shift = 0;
	while (((Mask >> Shift) & 1u) == 0u && Shift < 32)
	{
		++Shift;
	}

	const uint32 Value = (Pixel & Mask) >> Shift;
	const uint32 MaxValue = Mask >> Shift;
	return MaxValue > 0 ? static_cast<uint8>((Value * 255u + MaxValue / 2u) / MaxValue) : 0;
}

static bool DecodeBmpToBGRA8(const TArray<uint8>& FileData, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (FileData.Num() < 54 || FileData[0] != 'B' || FileData[1] != 'M')
	{
		return false;
	}

	const int32 PixelOffset = static_cast<int32>(ReadBmpU32(FileData, 10));
	const int32 HeaderSize = static_cast<int32>(ReadBmpU32(FileData, 14));
	if (HeaderSize < 40 || PixelOffset <= 0 || PixelOffset >= FileData.Num())
	{
		return false;
	}

	const int32 Width = ReadBmpS32(FileData, 18);
	const int32 SignedHeight = ReadBmpS32(FileData, 22);
	const uint16 Planes = ReadBmpU16(FileData, 26);
	const uint16 BitsPerPixel = ReadBmpU16(FileData, 28);
	const uint32 Compression = ReadBmpU32(FileData, 30);

	if (Width <= 0 || SignedHeight == 0 || Planes != 1)
	{
		return false;
	}

	if (!((BitsPerPixel == 24 && Compression == 0) || (BitsPerPixel == 32 && (Compression == 0 || Compression == 3))))
	{
		return false;
	}

	const int32 Height = FMath::Abs(SignedHeight);
	const bool bTopDown = SignedHeight < 0;
	const int64 RowStride = ((static_cast<int64>(Width) * BitsPerPixel + 31) / 32) * 4;
	if (RowStride <= 0 || static_cast<int64>(PixelOffset) + RowStride * Height > FileData.Num())
	{
		return false;
	}

	uint32 RedMask = 0x00ff0000;
	uint32 GreenMask = 0x0000ff00;
	uint32 BlueMask = 0x000000ff;
	uint32 AlphaMask = 0xff000000;
	if (BitsPerPixel == 32 && Compression == 3)
	{
		const int32 MaskOffset = 14 + 40;
		if (MaskOffset + 11 < FileData.Num())
		{
			RedMask = ReadBmpU32(FileData, MaskOffset);
			GreenMask = ReadBmpU32(FileData, MaskOffset + 4);
			BlueMask = ReadBmpU32(FileData, MaskOffset + 8);
			AlphaMask = MaskOffset + 15 < FileData.Num() ? ReadBmpU32(FileData, MaskOffset + 12) : 0;
		}
	}

	OutWidth = Width;
	OutHeight = Height;
	OutPixels.SetNumUninitialized(Width * Height * 4);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 SourceY = bTopDown ? Y : (Height - 1 - Y);
		const int64 SourceRow = static_cast<int64>(PixelOffset) + static_cast<int64>(SourceY) * RowStride;
		uint8* Dest = OutPixels.GetData() + static_cast<int64>(Y) * Width * 4;

		for (int32 X = 0; X < Width; ++X)
		{
			if (BitsPerPixel == 24)
			{
				const int64 Source = SourceRow + static_cast<int64>(X) * 3;
				Dest[X * 4 + 0] = FileData[Source + 0];
				Dest[X * 4 + 1] = FileData[Source + 1];
				Dest[X * 4 + 2] = FileData[Source + 2];
				Dest[X * 4 + 3] = 255;
			}
			else
			{
				const uint32 Pixel = ReadBmpU32(FileData, static_cast<int32>(SourceRow + static_cast<int64>(X) * 4));
				Dest[X * 4 + 0] = ExtractBmpMaskChannel(Pixel, BlueMask);
				Dest[X * 4 + 1] = ExtractBmpMaskChannel(Pixel, GreenMask);
				Dest[X * 4 + 2] = ExtractBmpMaskChannel(Pixel, RedMask);
				Dest[X * 4 + 3] = AlphaMask != 0 ? ExtractBmpMaskChannel(Pixel, AlphaMask) : 255;
			}
		}
	}

	return true;
}

static UTexture2D* CreateTextureFromBGRA8(UPackage* Package, const FString& AssetName, const TArray<uint8>& Pixels, int32 Width, int32 Height)
{
	if (!Package || Pixels.Num() != Width * Height * 4)
	{
		return nullptr;
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->UpdateResource();
	Texture->PostEditChange();

	return Texture;
}

FString FixMMDName(const FString& InName, const FString& Prefix = TEXT(""))
{
	FString Name = InName;
	Name = Name.Replace(TEXT(" "), TEXT("_"))
		.Replace(TEXT("."), TEXT("_"))
		.Replace(TEXT("-"), TEXT("_"))
		.Replace(TEXT("("), TEXT("_"))
		.Replace(TEXT(")"), TEXT("_"))
		.Replace(TEXT("["), TEXT("_"))
		.Replace(TEXT("]"), TEXT("_"))
		.Replace(TEXT("中"), TEXT("ZH"))
		.Replace(TEXT("文"), TEXT("WEN"))
		.Replace(TEXT("<"), TEXT("_"))
		.Replace(TEXT(">"), TEXT("_"))
		.Replace(TEXT(":"), TEXT("_"))
		.Replace(TEXT("*"), TEXT("_"))
		.Replace(TEXT("?"), TEXT("_"))
		.Replace(TEXT("\""), TEXT("_"))
		.Replace(TEXT("|"), TEXT("_"))
		.Replace(TEXT(","), TEXT("_"))
		.Replace(TEXT("&"), TEXT("_"))
		.Replace(TEXT("!"), TEXT("_"))
		.Replace(TEXT("~"), TEXT("_"))
		.Replace(TEXT("@"), TEXT("_"))
		.Replace(TEXT("#"), TEXT("_"))
		.Replace(TEXT("'"), TEXT("_"));
	while (Name.Contains(TEXT("__"))) Name = Name.Replace(TEXT("__"), TEXT("_"));
	if (!Name.IsEmpty() && !FChar::IsAlpha(Name[0])) Name = Prefix + Name;
	if (Name.IsEmpty()) Name = Prefix + TEXT("Unknown");
	return Name;
}

FString FixAnimAssetName(const FString& InName)
{
	return FixMMDName(InName, TEXT("Anim_"));
}

FVector ConvertMMDPositionToUnreal(const FVector& InPos, float Scale)
{
	FVector TempPos(InPos.Z * Scale, InPos.X * Scale, InPos.Y * Scale);
	return FVector(TempPos.Y, -TempPos.X, TempPos.Z);
}

FQuat ConvertMMDQuatToUnreal(const FQuat& InQuat)
{
	const FQuat AxisSwap(InQuat.Z, InQuat.X, InQuat.Y, InQuat.W);
	return FQuat(AxisSwap.Y, -AxisSwap.X, AxisSwap.Z, AxisSwap.W);
}

FString BuildMMDAnimationFolderPath(const FMMDAnimationImportContext& Context)
{
	FString ModelName = TEXT("UnknownModel");
	if (Context.PMXData != nullptr)
	{
		ModelName = !Context.PMXData->ModelNameEN.IsEmpty() ? Context.PMXData->ModelNameEN : Context.PMXData->ModelNameJP;
	}
	if (!Context.SourcePMXFilePath.IsEmpty())
	{
		ModelName = FPaths::GetBaseFilename(Context.SourcePMXFilePath);
	}
	return FString("/Game/MMDModels/") + FixMMDName(ModelName) + TEXT("/Animation");
}

template<typename KeyType, typename NameAccessor>
TArray<TPair<FString, int32>> BuildTrackCounts(const TArray<KeyType>& Keys, NameAccessor&& Accessor)
{
	TMap<FString, int32> Counts;
	for (const KeyType& Key : Keys)
	{
		const FString Name = Accessor(Key);
		int32& Count = Counts.FindOrAdd(Name);
		++Count;
	}

	TArray<TPair<FString, int32>> Result;
	for (const TPair<FString, int32>& Pair : Counts)
	{
		Result.Add(Pair);
	}
	Result.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
	{
		return A.Key < B.Key;
	});
	return Result;
}

bool TryResolveMorphTargetName(const USkeletalMesh* SkeletalMesh, const FString& SourceName, FName& OutMorphName)
{
	if (SkeletalMesh == nullptr)
	{
		return false;
	}

	for (UMorphTarget* MorphTarget : SkeletalMesh->GetMorphTargets())
	{
		if (MorphTarget == nullptr)
		{
			continue;
		}

		const FString Candidate = MorphTarget->GetFName().ToString();
		if (Candidate == SourceName || FixMMDName(Candidate) == FixMMDName(SourceName))
		{
			OutMorphName = MorphTarget->GetFName();
			return true;
		}
	}

	return false;
}

const FPMXMorph* FindPMXMorphByName(const PMXDatas* PMXData, const FString& SourceName, int32* OutMorphIndex = nullptr)
{
	if (PMXData == nullptr)
	{
		return nullptr;
	}

	for (int32 MorphIndex = 0; MorphIndex < PMXData->ModelMorphs.Num(); ++MorphIndex)
	{
		const FPMXMorph& Morph = PMXData->ModelMorphs[MorphIndex];
		const bool bNameMatches =
			Morph.NameJP == SourceName ||
			Morph.NameEN == SourceName ||
			FixMMDName(Morph.NameJP) == FixMMDName(SourceName) ||
			FixMMDName(Morph.NameEN) == FixMMDName(SourceName);
		if (bNameMatches)
		{
			if (OutMorphIndex != nullptr)
			{
				*OutMorphIndex = MorphIndex;
			}
			return &Morph;
		}
	}

	return nullptr;
}

struct FMMDMorphTargetContribution
{
	FName TargetMorphName = NAME_None;
	float WeightScale = 1.0f;
};

void AddMorphTargetContribution(TArray<FMMDMorphTargetContribution>& Contributions, FName TargetMorphName, float WeightScale)
{
	if (TargetMorphName == NAME_None || FMath::IsNearlyZero(WeightScale))
	{
		return;
	}

	for (FMMDMorphTargetContribution& Contribution : Contributions)
	{
		if (Contribution.TargetMorphName == TargetMorphName)
		{
			Contribution.WeightScale += WeightScale;
			return;
		}
	}

	FMMDMorphTargetContribution NewContribution;
	NewContribution.TargetMorphName = TargetMorphName;
	NewContribution.WeightScale = WeightScale;
	Contributions.Add(NewContribution);
}

void ResolvePMXMorphContributionsRecursive(
	const USkeletalMesh* SkeletalMesh,
	const PMXDatas* PMXData,
	int32 MorphIndex,
	float WeightScale,
	TSet<int32>& VisitingMorphs,
	TArray<FMMDMorphTargetContribution>& OutContributions)
{
	if (SkeletalMesh == nullptr || PMXData == nullptr || !PMXData->ModelMorphs.IsValidIndex(MorphIndex) || FMath::IsNearlyZero(WeightScale))
	{
		return;
	}

	if (VisitingMorphs.Contains(MorphIndex))
	{
		return;
	}
	VisitingMorphs.Add(MorphIndex);

	const FPMXMorph& Morph = PMXData->ModelMorphs[MorphIndex];
	const FString PreferredName = !Morph.NameJP.IsEmpty() ? Morph.NameJP : Morph.NameEN;
	if (Morph.MorphType == 1)
	{
		FName TargetMorphName = NAME_None;
		if (TryResolveMorphTargetName(SkeletalMesh, PreferredName, TargetMorphName))
		{
			AddMorphTargetContribution(OutContributions, TargetMorphName, WeightScale);
		}
	}
	else if (Morph.MorphType == 0)
	{
		for (const FPMXMorphGroup& Group : Morph.Groups)
		{
			ResolvePMXMorphContributionsRecursive(SkeletalMesh, PMXData, Group.MorphIndex, WeightScale * Group.Weight, VisitingMorphs, OutContributions);
		}
	}
	else if (Morph.MorphType == 9)
	{
		for (const FPMXMorphFlip& Flip : Morph.Flips)
		{
			ResolvePMXMorphContributionsRecursive(SkeletalMesh, PMXData, Flip.MorphIndex, WeightScale * Flip.Weight, VisitingMorphs, OutContributions);
		}
	}

	VisitingMorphs.Remove(MorphIndex);
}

TArray<FMMDMorphTargetContribution> ResolveMorphTargetContributions(const USkeletalMesh* SkeletalMesh, const PMXDatas* PMXData, const FString& SourceName)
{
	TArray<FMMDMorphTargetContribution> Contributions;

	FName DirectMorphName = NAME_None;
	if (TryResolveMorphTargetName(SkeletalMesh, SourceName, DirectMorphName))
	{
		AddMorphTargetContribution(Contributions, DirectMorphName, 1.0f);
		return Contributions;
	}

	int32 PMXMorphIndex = INDEX_NONE;
	if (FindPMXMorphByName(PMXData, SourceName, &PMXMorphIndex) != nullptr)
	{
		TSet<int32> VisitingMorphs;
		ResolvePMXMorphContributionsRecursive(SkeletalMesh, PMXData, PMXMorphIndex, 1.0f, VisitingMorphs, Contributions);
	}

	return Contributions;
}

template<typename TReportArray>
void AppendUniqueMessage(TReportArray& Messages, const FString& Message)
{
	Messages.AddUnique(Message);
}

struct FResolvedVMDBoneKey
{
	int32 Frame = 0;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	uint8 Interpolation[64] = {};
};

struct FVMDWrittenTrackDiagnostic
{
	FName BoneName = NAME_None;
	int32 SourceKeyCount = 0;
	float MaxTranslationDelta = 0.0f;
	float MaxRotationDeltaDegrees = 0.0f;
	bool bSetKeysSucceeded = false;
};

struct FVMDMorphCurveDiagnostic
{
	FName MorphName = NAME_None;
	int32 SourceKeyCount = 0;
	bool bSetKeysSucceeded = false;
};

struct FMMDIKDebugLog
{
	int32 Remaining = 96;
	int32 Suppressed = 0;

	void Log(const FString& Message)
	{
		if (Remaining > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MMD IK Debugger] %s"), *Message);
			--Remaining;
		}
		else
		{
			++Suppressed;
		}
	}

	void LogAlways(const FString& Message)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MMD IK Debugger] %s"), *Message);
	}

	void FlushSuppressed()
	{
		if (Suppressed > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[MMD IK Debugger] Suppressed %d additional messages."), Suppressed);
		}
	}
};

enum class EVMDInterpolationChannel : uint8
{
	PositionX = 0,
	PositionY = 1,
	PositionZ = 2,
	Rotation = 3
};

struct FMMDIKBakeChain
{
	struct FLink
	{
		int32 BoneIndex = INDEX_NONE;
		bool bHasLimit = false;
		FVector LowerLimitDegrees = FVector::ZeroVector;
		FVector UpperLimitDegrees = FVector::ZeroVector;
	};

	FName IKBoneName = NAME_None;
	FName TargetBoneName = NAME_None;
	int32 PMXBoneIndex = INDEX_NONE;
	int32 IKBoneIndex = INDEX_NONE;
	int32 TargetBoneIndex = INDEX_NONE;
	TArray<FLink> Links;
	int32 PMXLoopCount = 0;
	int32 IterationCount = 0;
	float AngleLimitRadians = PI;
	FString InvalidReason;
};

struct FMMDPMXRuntimeBone
{
	int32 PMXBoneIndex = INDEX_NONE;
	int32 SkeletonBoneIndex = INDEX_NONE;
	int32 InheritParentSkeletonBoneIndex = INDEX_NONE;
	int32 DeformLayer = 0;
	bool bInheritRotation = false;
	bool bInheritTranslation = false;
	float InheritInfluence = 0.0f;
	bool bHasIK = false;
};

struct FMMDPMXSpaceRuntimeBone
{
	int32 PMXBoneIndex = INDEX_NONE;
	int32 InheritParentPMXBoneIndex = INDEX_NONE;
	int32 DeformLayer = 0;
	bool bInheritRotation = false;
	bool bInheritTranslation = false;
	float InheritInfluence = 0.0f;
	bool bHasIK = false;
};

struct FMMDPMXSpaceIKChain
{
	struct FLink
	{
		int32 PMXBoneIndex = INDEX_NONE;
		bool bHasLimit = false;
		FVector LowerLimitRadians = FVector::ZeroVector;
		FVector UpperLimitRadians = FVector::ZeroVector;
	};

	int32 PMXBoneIndex = INDEX_NONE;
	int32 TargetPMXBoneIndex = INDEX_NONE;
	TArray<FLink> Links;
	int32 IterationCount = 0;
	float AngleLimitRadians = PI;
};

struct FMMDBoneSpaceConverter
{
	FName BoneName = NAME_None;
	bool bUsesPMXLocalAxis = false;
	FVector PMXAxisX = FVector::ForwardVector;
	FVector PMXAxisY = FVector::RightVector;
	FVector PMXAxisZ = FVector::UpVector;

	FVector ConvertPosition(const FVector& MMDPosition, float Scale) const
	{
		return ConvertMMDPositionToUnreal(MMDPosition, Scale);
	}

	FQuat ConvertRotation(const FQuat& MMDRotation) const
	{
		return ConvertMMDQuatToUnreal(MMDRotation).GetNormalized();
	}
};

static float EvaluateUnitCubicBezier(float T, float Control1, float Control2)
{
	const float InvT = 1.0f - T;
	return (3.0f * InvT * InvT * T * Control1)
		+ (3.0f * InvT * T * T * Control2)
		+ (T * T * T);
}

static float EvaluateVMDBezierAlpha(const uint8 Interpolation[64], EVMDInterpolationChannel Channel, float LinearAlpha)
{
	LinearAlpha = FMath::Clamp(LinearAlpha, 0.0f, 1.0f);
	const int32 ChannelIndex = static_cast<int32>(Channel);
	const float X1 = FMath::Clamp(static_cast<float>(Interpolation[ChannelIndex]) / 127.0f, 0.0f, 1.0f);
	const float Y1 = FMath::Clamp(static_cast<float>(Interpolation[ChannelIndex + 4]) / 127.0f, 0.0f, 1.0f);
	const float X2 = FMath::Clamp(static_cast<float>(Interpolation[ChannelIndex + 8]) / 127.0f, 0.0f, 1.0f);
	const float Y2 = FMath::Clamp(static_cast<float>(Interpolation[ChannelIndex + 12]) / 127.0f, 0.0f, 1.0f);

	float Low = 0.0f;
	float High = 1.0f;
	float Param = LinearAlpha;
	for (int32 Iteration = 0; Iteration < 12; ++Iteration)
	{
		Param = (Low + High) * 0.5f;
		const float X = EvaluateUnitCubicBezier(Param, X1, X2);
		if (X < LinearAlpha)
		{
			Low = Param;
		}
		else
		{
			High = Param;
		}
	}

	return FMath::Clamp(EvaluateUnitCubicBezier(Param, Y1, Y2), 0.0f, 1.0f);
}

static float GetQuatDeltaDegrees(const FQuat& A, const FQuat& B)
{
	const float Dot = FMath::Clamp(static_cast<float>(FMath::Abs(A.GetNormalized() | B.GetNormalized())), 0.0f, 1.0f);
	return FMath::RadiansToDegrees(2.0f * FMath::Acos(Dot));
}

static void ApplyResolvedVMDKeyToRawTransform(const FResolvedVMDBoneKey& Key, FVector& OutMMDPosition, FQuat& OutMMDRotation)
{
	OutMMDPosition = Key.Position;
	OutMMDRotation = Key.Rotation.GetNormalized();
}

static void SampleResolvedVMDTrackAtFrame(const TArray<FResolvedVMDBoneKey>& Keys, int32 FrameIndex, FVector& OutMMDPosition, FQuat& OutMMDRotation)
{
	if (Keys.Num() == 0)
	{
		OutMMDPosition = FVector::ZeroVector;
		OutMMDRotation = FQuat::Identity;
		return;
	}

	int32 NextIndex = 0;
	while (NextIndex < Keys.Num() && Keys[NextIndex].Frame <= FrameIndex)
	{
		++NextIndex;
	}

	if (NextIndex > 0 && Keys[NextIndex - 1].Frame == FrameIndex)
	{
		ApplyResolvedVMDKeyToRawTransform(Keys[NextIndex - 1], OutMMDPosition, OutMMDRotation);
		return;
	}
	if (NextIndex == 0)
	{
		OutMMDPosition = FVector::ZeroVector;
		OutMMDRotation = FQuat::Identity;
		return;
	}
	if (NextIndex >= Keys.Num())
	{
		ApplyResolvedVMDKeyToRawTransform(Keys.Last(), OutMMDPosition, OutMMDRotation);
		return;
	}

	const FResolvedVMDBoneKey& PreviousKey = Keys[NextIndex - 1];
	const FResolvedVMDBoneKey& NextKey = Keys[NextIndex];
	const int32 FrameDelta = NextKey.Frame - PreviousKey.Frame;
	if (FrameDelta <= 0)
	{
		ApplyResolvedVMDKeyToRawTransform(PreviousKey, OutMMDPosition, OutMMDRotation);
		return;
	}

	const float LinearAlpha = static_cast<float>(FrameIndex - PreviousKey.Frame) / static_cast<float>(FrameDelta);
	const FVector InterpolatedPosition(
		FMath::Lerp(PreviousKey.Position.X, NextKey.Position.X, EvaluateVMDBezierAlpha(NextKey.Interpolation, EVMDInterpolationChannel::PositionX, LinearAlpha)),
		FMath::Lerp(PreviousKey.Position.Y, NextKey.Position.Y, EvaluateVMDBezierAlpha(NextKey.Interpolation, EVMDInterpolationChannel::PositionY, LinearAlpha)),
		FMath::Lerp(PreviousKey.Position.Z, NextKey.Position.Z, EvaluateVMDBezierAlpha(NextKey.Interpolation, EVMDInterpolationChannel::PositionZ, LinearAlpha)));
	const FQuat InterpolatedRotation = FQuat::Slerp(
		PreviousKey.Rotation,
		NextKey.Rotation,
		EvaluateVMDBezierAlpha(NextKey.Interpolation, EVMDInterpolationChannel::Rotation, LinearAlpha)).GetNormalized();

	OutMMDPosition = InterpolatedPosition;
	OutMMDRotation = InterpolatedRotation;
}

static int32 FindBoneInRefSkeletonSkippingImportRoot(const FReferenceSkeleton& RefSkeleton, const FString& BoneName)
{
	const int32 StartIndex = RefSkeleton.GetNum() > 0 && RefSkeleton.GetBoneName(0) == TEXT("Root") ? 1 : 0;
	const FName ExactName(*BoneName);
	for (int32 BoneIndex = StartIndex; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		if (RefSkeleton.GetBoneName(BoneIndex) == ExactName)
		{
			return BoneIndex;
		}
	}

	const FString SanitizedName = FixMMDName(BoneName);
	for (int32 BoneIndex = StartIndex; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		if (FixMMDName(RefSkeleton.GetBoneName(BoneIndex).ToString()) == SanitizedName)
		{
			return BoneIndex;
		}
	}
	return INDEX_NONE;
}

static int32 FindPMXBoneInRefSkeleton(const FReferenceSkeleton& RefSkeleton, const FPMXBone& Bone, int32 PMXBoneIndex)
{
	const int32 ExpectedSkeletonIndex = PMXBoneIndex + 1;
	if (RefSkeleton.GetNum() > 0
		&& RefSkeleton.GetBoneName(0) == TEXT("Root")
		&& ExpectedSkeletonIndex < RefSkeleton.GetNum())
	{
		const FString ExpectedBoneName = RefSkeleton.GetBoneName(ExpectedSkeletonIndex).ToString();
		if (ExpectedBoneName == Bone.NameJP || FixMMDName(ExpectedBoneName) == FixMMDName(Bone.NameJP))
		{
			return ExpectedSkeletonIndex;
		}
		if (!Bone.NameEN.IsEmpty() && (ExpectedBoneName == Bone.NameEN || FixMMDName(ExpectedBoneName) == FixMMDName(Bone.NameEN)))
		{
			return ExpectedSkeletonIndex;
		}
	}

	int32 BoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, Bone.NameJP);
	if (BoneIndex != INDEX_NONE)
	{
		return BoneIndex;
	}
	if (!Bone.NameEN.IsEmpty())
	{
		BoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, Bone.NameEN);
	}
	return BoneIndex;
}

static TArray<int32> BuildPMXToSkeletonBoneMap(const PMXDatas* PMXData, const FReferenceSkeleton& RefSkeleton)
{
	TArray<int32> PMXToSkeleton;
	if (PMXData == nullptr)
	{
		return PMXToSkeleton;
	}

	PMXToSkeleton.SetNum(PMXData->ModelBones.Num());
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < PMXData->ModelBones.Num(); ++PMXBoneIndex)
	{
		PMXToSkeleton[PMXBoneIndex] = FindPMXBoneInRefSkeleton(RefSkeleton, PMXData->ModelBones[PMXBoneIndex], PMXBoneIndex);
	}
	return PMXToSkeleton;
}

static FVector GetSafePMXLocalAxis(const FVector& Axis, const FVector& Fallback)
{
	const float SizeSquared = Axis.SizeSquared();
	return SizeSquared > KINDA_SMALL_NUMBER ? Axis * FMath::InvSqrt(SizeSquared) : Fallback;
}

static TMap<FName, FMMDBoneSpaceConverter> BuildVMDToUEBoneConverters(
	const PMXDatas* PMXData,
	const FReferenceSkeleton& RefSkeleton,
	const TArray<int32>& PMXToSkeleton,
	FMMDIKDebugLog* DebugLog)
{
	TMap<FName, FMMDBoneSpaceConverter> Converters;
	if (PMXData == nullptr)
	{
		return Converters;
	}

	for (int32 PMXBoneIndex = 0; PMXBoneIndex < PMXData->ModelBones.Num(); ++PMXBoneIndex)
	{
		if (!PMXToSkeleton.IsValidIndex(PMXBoneIndex) || PMXToSkeleton[PMXBoneIndex] == INDEX_NONE)
		{
			continue;
		}

		const FPMXBone& PMXBone = PMXData->ModelBones[PMXBoneIndex];
		FMMDBoneSpaceConverter Converter;
		Converter.BoneName = RefSkeleton.GetBoneName(PMXToSkeleton[PMXBoneIndex]);
		Converter.bUsesPMXLocalAxis = (PMXBone.Flags & 0x0800) != 0;
		if (Converter.bUsesPMXLocalAxis)
		{
			Converter.PMXAxisX = GetSafePMXLocalAxis(PMXBone.LocalAxisX, FVector::ForwardVector);
			Converter.PMXAxisZ = GetSafePMXLocalAxis(PMXBone.LocalAxisZ, FVector::UpVector);
			Converter.PMXAxisY = GetSafePMXLocalAxis(FVector::CrossProduct(Converter.PMXAxisZ, Converter.PMXAxisX), FVector::RightVector);
			Converter.PMXAxisZ = GetSafePMXLocalAxis(FVector::CrossProduct(Converter.PMXAxisX, Converter.PMXAxisY), Converter.PMXAxisZ);
		}

		Converters.Add(Converter.BoneName, Converter);
	}

	return Converters;
}

static void PopulateMMDModelDataAsset(UMMDModelDataAsset* ModelDataAsset, const PMXDatas& PMXInfo, const FReferenceSkeleton& RefSkeleton, const FString& PMXFilePath)
{
	if (ModelDataAsset == nullptr)
	{
		return;
	}

	ModelDataAsset->ModelId = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	ModelDataAsset->ModelNameJP = PMXInfo.ModelNameJP;
	ModelDataAsset->ModelNameEN = PMXInfo.ModelNameEN;
	ModelDataAsset->Bones.Reset(PMXInfo.ModelBones.Num());

	for (int32 PMXBoneIndex = 0; PMXBoneIndex < PMXInfo.ModelBones.Num(); ++PMXBoneIndex)
	{
		const FPMXBone& PMXBone = PMXInfo.ModelBones[PMXBoneIndex];
		FMMDModelBoneData& BoneData = ModelDataAsset->Bones.AddDefaulted_GetRef();
		BoneData.NameJP = PMXBone.NameJP;
		BoneData.NameEN = PMXBone.NameEN;
		BoneData.ParentBoneIndex = PMXBone.ParentBoneIndex;
		BoneData.Position = PMXBone.Position;
		BoneData.DeformLayer = PMXBone.DeformLayer;
		BoneData.Flags = static_cast<int32>(PMXBone.Flags);
		BoneData.InheritParentIndex = PMXBone.InheritParentIndex;
		BoneData.InheritInfluence = PMXBone.InheritInfluence;
		BoneData.FixedAxis = PMXBone.Axis;
		BoneData.LocalAxisX = PMXBone.LocalAxisX;
		BoneData.LocalAxisZ = PMXBone.LocalAxisZ;
		BoneData.IKTargetBoneIndex = PMXBone.IKTargetBoneIndex;
		BoneData.IKLoopCount = PMXBone.IKLoopCount;
		BoneData.IKLimitAngle = PMXBone.IKLimitAngle;
		BoneData.UEBoneIndex = FindPMXBoneInRefSkeleton(RefSkeleton, PMXBone, PMXBoneIndex);
		BoneData.UEBoneName = BoneData.UEBoneIndex != INDEX_NONE ? RefSkeleton.GetBoneName(BoneData.UEBoneIndex) : NAME_None;

		BoneData.IKLinks.Reserve(PMXBone.IKLinks.Num());
		for (const FPMXIKLink& PMXLink : PMXBone.IKLinks)
		{
			FMMDModelIKLinkData& LinkData = BoneData.IKLinks.AddDefaulted_GetRef();
			LinkData.LinkBoneIndex = PMXLink.LinkBoneIndex;
			LinkData.bHasLimit = PMXLink.HasLimit != 0;
			LinkData.LowerLimit = PMXLink.LowerLimit;
			LinkData.UpperLimit = PMXLink.UpperLimit;
		}
	}
}

static UMMDModelDataAsset* CreateMMDModelDataAssetForMesh(USkeletalMesh* SkeletalMesh, const PMXDatas& PMXInfo, const FReferenceSkeleton& RefSkeleton, const FString& BasePath, const FString& CleanAssetName, const FString& PMXFilePath)
{
#if WITH_EDITOR
	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	const FString DataPath = BasePath + TEXT("Data/");
	const FString AssetName = CleanAssetName + TEXT("_MMDModelData");
	const FString PackageName = DataPath + AssetName;
	UPackage* Package = CreatePackage(*PackageName);
	if (Package == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("MMDModelData: Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	UMMDModelDataAsset* ModelDataAsset = NewObject<UMMDModelDataAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	if (ModelDataAsset == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("MMDModelData: Failed to create asset: %s"), *AssetName);
		return nullptr;
	}

	PopulateMMDModelDataAsset(ModelDataAsset, PMXInfo, RefSkeleton, PMXFilePath);
	ModelDataAsset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(ModelDataAsset);

	UMMDSkeletalMeshUserData* UserData = NewObject<UMMDSkeletalMeshUserData>(SkeletalMesh, UMMDSkeletalMeshUserData::StaticClass(), NAME_None, RF_Public | RF_Transactional);
	UserData->ModelDataAsset = ModelDataAsset;
	SkeletalMesh->AddAssetUserData(UserData);

	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(Package, ModelDataAsset, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Warning, TEXT("MMDModelData: SavePackage failed: %s"), *FilePath);
	}

	return ModelDataAsset;
#else
	return nullptr;
#endif
}

static UMMDModelDataAsset* FindMMDModelDataAsset(USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMesh == nullptr)
	{
		return nullptr;
	}

	if (const UMMDSkeletalMeshUserData* UserData = SkeletalMesh->GetAssetUserData<UMMDSkeletalMeshUserData>())
	{
		return UserData->ModelDataAsset;
	}
	return nullptr;
}

static TArray<FMMDPMXRuntimeBone> BuildMMDPMXRuntimeBones(const PMXDatas* PMXData, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& PMXToSkeleton, FMMDIKDebugLog* DebugLog)
{
	TArray<FMMDPMXRuntimeBone> RuntimeBones;
	if (PMXData == nullptr)
	{
		return RuntimeBones;
	}

	RuntimeBones.Reserve(PMXData->ModelBones.Num());
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < PMXData->ModelBones.Num(); ++PMXBoneIndex)
	{
		if (!PMXToSkeleton.IsValidIndex(PMXBoneIndex) || PMXToSkeleton[PMXBoneIndex] == INDEX_NONE)
		{
			continue;
		}

		const FPMXBone& PMXBone = PMXData->ModelBones[PMXBoneIndex];
		FMMDPMXRuntimeBone RuntimeBone;
		RuntimeBone.PMXBoneIndex = PMXBoneIndex;
		RuntimeBone.SkeletonBoneIndex = PMXToSkeleton[PMXBoneIndex];
		RuntimeBone.DeformLayer = PMXBone.DeformLayer;
		RuntimeBone.bInheritRotation = (PMXBone.Flags & 0x0100) != 0;
		RuntimeBone.bInheritTranslation = (PMXBone.Flags & 0x0200) != 0;
		RuntimeBone.InheritInfluence = PMXBone.InheritInfluence;
		RuntimeBone.bHasIK = (PMXBone.Flags & 0x0020) != 0;
		if (PMXToSkeleton.IsValidIndex(PMXBone.InheritParentIndex))
		{
			RuntimeBone.InheritParentSkeletonBoneIndex = PMXToSkeleton[PMXBone.InheritParentIndex];
		}
		RuntimeBones.Add(RuntimeBone);
	}

	RuntimeBones.Sort([](const FMMDPMXRuntimeBone& A, const FMMDPMXRuntimeBone& B)
	{
		if (A.DeformLayer != B.DeformLayer)
		{
			return A.DeformLayer < B.DeformLayer;
		}
		if (A.bHasIK != B.bHasIK)
		{
			return !A.bHasIK && B.bHasIK;
		}
		return A.PMXBoneIndex < B.PMXBoneIndex;
	});

	return RuntimeBones;
}

static int32 ResolveModelDataBoneToSkeletonIndex(const FMMDModelBoneData& BoneData, const FReferenceSkeleton& RefSkeleton)
{
	if (BoneData.UEBoneIndex != INDEX_NONE
		&& BoneData.UEBoneIndex < RefSkeleton.GetNum()
		&& BoneData.UEBoneIndex != 0
		&& RefSkeleton.GetBoneName(BoneData.UEBoneIndex) == BoneData.UEBoneName)
	{
		return BoneData.UEBoneIndex;
	}

	if (BoneData.UEBoneName != NAME_None)
	{
		const int32 BoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, BoneData.UEBoneName.ToString());
		if (BoneIndex != INDEX_NONE)
		{
			return BoneIndex;
		}
	}

	int32 BoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, BoneData.NameJP);
	if (BoneIndex != INDEX_NONE)
	{
		return BoneIndex;
	}
	if (!BoneData.NameEN.IsEmpty())
	{
		BoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, BoneData.NameEN);
	}

	return BoneIndex;
}

static TArray<int32> BuildModelDataToSkeletonBoneMap(const UMMDModelDataAsset* ModelDataAsset, const FReferenceSkeleton& RefSkeleton)
{
	TArray<int32> ModelDataToSkeleton;
	if (ModelDataAsset == nullptr)
	{
		return ModelDataToSkeleton;
	}

	ModelDataToSkeleton.SetNum(ModelDataAsset->Bones.Num());
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		ModelDataToSkeleton[PMXBoneIndex] = ResolveModelDataBoneToSkeletonIndex(ModelDataAsset->Bones[PMXBoneIndex], RefSkeleton);
	}
	return ModelDataToSkeleton;
}

static TArray<FMMDPMXRuntimeBone> BuildMMDModelDataRuntimeBones(const UMMDModelDataAsset* ModelDataAsset, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& ModelDataToSkeleton)
{
	TArray<FMMDPMXRuntimeBone> RuntimeBones;
	if (ModelDataAsset == nullptr)
	{
		return RuntimeBones;
	}

	RuntimeBones.Reserve(ModelDataAsset->Bones.Num());
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		if (!ModelDataToSkeleton.IsValidIndex(PMXBoneIndex) || ModelDataToSkeleton[PMXBoneIndex] == INDEX_NONE)
		{
			continue;
		}

		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		FMMDPMXRuntimeBone RuntimeBone;
		RuntimeBone.PMXBoneIndex = PMXBoneIndex;
		RuntimeBone.SkeletonBoneIndex = ModelDataToSkeleton[PMXBoneIndex];
		RuntimeBone.DeformLayer = BoneData.DeformLayer;
		RuntimeBone.bInheritRotation = (BoneData.Flags & 0x0100) != 0;
		RuntimeBone.bInheritTranslation = (BoneData.Flags & 0x0200) != 0;
		RuntimeBone.InheritInfluence = BoneData.InheritInfluence;
		RuntimeBone.bHasIK = (BoneData.Flags & 0x0020) != 0;
		if (ModelDataToSkeleton.IsValidIndex(BoneData.InheritParentIndex))
		{
			RuntimeBone.InheritParentSkeletonBoneIndex = ModelDataToSkeleton[BoneData.InheritParentIndex];
		}
		RuntimeBones.Add(RuntimeBone);
	}

	RuntimeBones.Sort([](const FMMDPMXRuntimeBone& A, const FMMDPMXRuntimeBone& B)
	{
		if (A.DeformLayer != B.DeformLayer)
		{
			return A.DeformLayer < B.DeformLayer;
		}
		if (A.bHasIK != B.bHasIK)
		{
			return !A.bHasIK && B.bHasIK;
		}
		return A.PMXBoneIndex < B.PMXBoneIndex;
	});

	return RuntimeBones;
}

static void BuildComponentSpacePoseForFrame(const FReferenceSkeleton& RefSkeleton, const TArray<TArray<FTransform>>& LocalTransforms, int32 FrameIndex, TArray<FTransform>& OutComponentTransforms)
{
	const int32 NumBones = RefSkeleton.GetNum();
	OutComponentTransforms.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FTransform& LocalTransform = LocalTransforms[BoneIndex][FrameIndex];
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		OutComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
			? LocalTransform * OutComponentTransforms[ParentIndex]
			: LocalTransform;
	}
}

static void ConvertMMDIKLimitToUnrealDegrees(const FVector& LowerRadians, const FVector& UpperRadians, FVector& OutLowerDegrees, FVector& OutUpperDegrees)
{
	const FVector LowerAxes(LowerRadians.X, -UpperRadians.Z, LowerRadians.Y);
	const FVector UpperAxes(UpperRadians.X, -LowerRadians.Z, UpperRadians.Y);
	OutLowerDegrees = FVector(FMath::RadiansToDegrees(LowerAxes.X), FMath::RadiansToDegrees(LowerAxes.Y), FMath::RadiansToDegrees(LowerAxes.Z));
	OutUpperDegrees = FVector(FMath::RadiansToDegrees(UpperAxes.X), FMath::RadiansToDegrees(UpperAxes.Y), FMath::RadiansToDegrees(UpperAxes.Z));
}

static FQuat ClampLocalRotationByMMDIKLimit(const FQuat& LocalRotation, const FQuat& ReferenceLocalRotation, const FMMDIKBakeChain::FLink& Link)
{
	if (!Link.bHasLimit)
	{
		return LocalRotation.GetNormalized();
	}

	const FQuat DeltaFromRef = (ReferenceLocalRotation.Inverse() * LocalRotation).GetNormalized();
	const FRotator DeltaRotator = DeltaFromRef.Rotator();
	const FVector ClampedAxisDegrees(
		FMath::ClampAngle(DeltaRotator.Roll, Link.LowerLimitDegrees.X, Link.UpperLimitDegrees.X),
		FMath::ClampAngle(DeltaRotator.Pitch, Link.LowerLimitDegrees.Y, Link.UpperLimitDegrees.Y),
		FMath::ClampAngle(DeltaRotator.Yaw, Link.LowerLimitDegrees.Z, Link.UpperLimitDegrees.Z));
	const FQuat ClampedDelta = FRotator(ClampedAxisDegrees.Y, ClampedAxisDegrees.Z, ClampedAxisDegrees.X).Quaternion();
	return (ReferenceLocalRotation * ClampedDelta).GetNormalized();
}

static TArray<FMMDIKBakeChain> BuildMMDIKBakeChains(const PMXDatas* PMXData, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& PMXToSkeleton, TSet<FName>& InOutTracksToWrite, FMMDIKDebugLog* DebugLog)
{
	TArray<FMMDIKBakeChain> Chains;
	if (PMXData == nullptr)
	{
		return Chains;
	}

	for (int32 PMXBoneIndex = 0; PMXBoneIndex < PMXData->ModelBones.Num(); ++PMXBoneIndex)
	{
		const FPMXBone& PMXBone = PMXData->ModelBones[PMXBoneIndex];
		if ((PMXBone.Flags & 0x0020) == 0 || PMXBone.IKLinks.Num() == 0)
		{
			continue;
		}
		if (!PMXToSkeleton.IsValidIndex(PMXBoneIndex) || !PMXToSkeleton.IsValidIndex(PMXBone.IKTargetBoneIndex))
		{
			continue;
		}

		FMMDIKBakeChain Chain;
		Chain.PMXBoneIndex = PMXBoneIndex;
		Chain.IKBoneIndex = PMXToSkeleton[PMXBoneIndex];
		Chain.TargetBoneIndex = PMXToSkeleton[PMXBone.IKTargetBoneIndex];
		if (Chain.IKBoneIndex == INDEX_NONE || Chain.TargetBoneIndex == INDEX_NONE)
		{
			continue;
		}

		Chain.IKBoneName = RefSkeleton.GetBoneName(Chain.IKBoneIndex);
		Chain.TargetBoneName = RefSkeleton.GetBoneName(Chain.TargetBoneIndex);
		Chain.PMXLoopCount = PMXBone.IKLoopCount;
		Chain.IterationCount = FMath::Clamp(FMath::Max(PMXBone.IKLoopCount, 64), 1, 256);
		Chain.AngleLimitRadians = PMXBone.IKLimitAngle > SMALL_NUMBER ? PMXBone.IKLimitAngle : PI;

		int32 FirstLinkIndex = 0;
		if (PMXBone.IKLinks[0].LinkBoneIndex == PMXBone.IKTargetBoneIndex && PMXBone.IKLinks.Num() > 1)
		{
			FirstLinkIndex = 1;
		}

		bool bValidChain = true;
		int32 ExpectedChildBoneIndex = Chain.TargetBoneIndex;
		for (int32 LinkIndex = FirstLinkIndex; LinkIndex < PMXBone.IKLinks.Num(); ++LinkIndex)
		{
			const FPMXIKLink& Link = PMXBone.IKLinks[LinkIndex];
			if (!PMXToSkeleton.IsValidIndex(Link.LinkBoneIndex))
			{
				Chain.InvalidReason = FString::Printf(TEXT("link PMX index out of range: %d"), Link.LinkBoneIndex);
				bValidChain = false;
				break;
			}

			const int32 LinkBoneIndex = PMXToSkeleton[Link.LinkBoneIndex];
			if (LinkBoneIndex == INDEX_NONE)
			{
				Chain.InvalidReason = FString::Printf(TEXT("link bone is not in target skeleton: %d"), Link.LinkBoneIndex);
				bValidChain = false;
				break;
			}
			if (RefSkeleton.GetParentIndex(ExpectedChildBoneIndex) != LinkBoneIndex)
			{
				Chain.InvalidReason = FString::Printf(TEXT("invalid parent chain: expected parent of %s to be %s"),
					*RefSkeleton.GetBoneName(ExpectedChildBoneIndex).ToString(),
					*RefSkeleton.GetBoneName(LinkBoneIndex).ToString());
				bValidChain = false;
				break;
			}

			FMMDIKBakeChain::FLink ChainLink;
			ChainLink.BoneIndex = LinkBoneIndex;
			ChainLink.bHasLimit = Link.HasLimit != 0;
			if (ChainLink.bHasLimit)
			{
				ConvertMMDIKLimitToUnrealDegrees(Link.LowerLimit, Link.UpperLimit, ChainLink.LowerLimitDegrees, ChainLink.UpperLimitDegrees);
			}

			Chain.Links.Add(ChainLink);
			InOutTracksToWrite.Add(RefSkeleton.GetBoneName(LinkBoneIndex));
			ExpectedChildBoneIndex = LinkBoneIndex;
		}

		if (!bValidChain)
		{
			if (DebugLog != nullptr)
			{
				DebugLog->Log(FString::Printf(TEXT("IK chain skipped: %s -> %s | %s"),
					*Chain.IKBoneName.ToString(),
					*Chain.TargetBoneName.ToString(),
					*Chain.InvalidReason));
			}
			continue;
		}

		if (Chain.Links.Num() > 0)
		{
			InOutTracksToWrite.Add(Chain.IKBoneName);
			InOutTracksToWrite.Add(Chain.TargetBoneName);
			Chains.Add(MoveTemp(Chain));
		}
	}

	return Chains;
}

static TArray<FMMDIKBakeChain> BuildMMDModelDataIKBakeChains(const UMMDModelDataAsset* ModelDataAsset, const FReferenceSkeleton& RefSkeleton, const TArray<int32>& ModelDataToSkeleton, TSet<FName>& InOutTracksToWrite, FMMDIKDebugLog* DebugLog)
{
	TArray<FMMDIKBakeChain> Chains;
	if (ModelDataAsset == nullptr)
	{
		return Chains;
	}

	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		if ((BoneData.Flags & 0x0020) == 0 || BoneData.IKLinks.Num() == 0)
		{
			continue;
		}
		if (!ModelDataToSkeleton.IsValidIndex(PMXBoneIndex) || !ModelDataToSkeleton.IsValidIndex(BoneData.IKTargetBoneIndex))
		{
			continue;
		}

		FMMDIKBakeChain Chain;
		Chain.PMXBoneIndex = PMXBoneIndex;
		Chain.IKBoneIndex = ModelDataToSkeleton[PMXBoneIndex];
		Chain.TargetBoneIndex = ModelDataToSkeleton[BoneData.IKTargetBoneIndex];
		if (Chain.IKBoneIndex == INDEX_NONE || Chain.TargetBoneIndex == INDEX_NONE)
		{
			continue;
		}

		Chain.IKBoneName = RefSkeleton.GetBoneName(Chain.IKBoneIndex);
		Chain.TargetBoneName = RefSkeleton.GetBoneName(Chain.TargetBoneIndex);
		Chain.PMXLoopCount = BoneData.IKLoopCount;
		Chain.IterationCount = FMath::Clamp(FMath::Max(BoneData.IKLoopCount, 64), 1, 256);
		Chain.AngleLimitRadians = BoneData.IKLimitAngle > SMALL_NUMBER ? BoneData.IKLimitAngle : PI;

		int32 FirstLinkIndex = 0;
		if (BoneData.IKLinks[0].LinkBoneIndex == BoneData.IKTargetBoneIndex && BoneData.IKLinks.Num() > 1)
		{
			FirstLinkIndex = 1;
		}

		bool bValidChain = true;
		int32 ExpectedChildBoneIndex = Chain.TargetBoneIndex;
		for (int32 LinkIndex = FirstLinkIndex; LinkIndex < BoneData.IKLinks.Num(); ++LinkIndex)
		{
			const FMMDModelIKLinkData& Link = BoneData.IKLinks[LinkIndex];
			if (!ModelDataToSkeleton.IsValidIndex(Link.LinkBoneIndex))
			{
				Chain.InvalidReason = FString::Printf(TEXT("link PMX index out of range: %d"), Link.LinkBoneIndex);
				bValidChain = false;
				break;
			}

			const int32 LinkBoneIndex = ModelDataToSkeleton[Link.LinkBoneIndex];
			if (LinkBoneIndex == INDEX_NONE)
			{
				Chain.InvalidReason = FString::Printf(TEXT("link bone is not in target skeleton: %d"), Link.LinkBoneIndex);
				bValidChain = false;
				break;
			}
			if (RefSkeleton.GetParentIndex(ExpectedChildBoneIndex) != LinkBoneIndex)
			{
				Chain.InvalidReason = FString::Printf(TEXT("invalid parent chain: expected parent of %s to be %s"),
					*RefSkeleton.GetBoneName(ExpectedChildBoneIndex).ToString(),
					*RefSkeleton.GetBoneName(LinkBoneIndex).ToString());
				bValidChain = false;
				break;
			}

			FMMDIKBakeChain::FLink ChainLink;
			ChainLink.BoneIndex = LinkBoneIndex;
			ChainLink.bHasLimit = Link.bHasLimit;
			if (ChainLink.bHasLimit)
			{
				ConvertMMDIKLimitToUnrealDegrees(Link.LowerLimit, Link.UpperLimit, ChainLink.LowerLimitDegrees, ChainLink.UpperLimitDegrees);
			}

			Chain.Links.Add(ChainLink);
			InOutTracksToWrite.Add(RefSkeleton.GetBoneName(LinkBoneIndex));
			ExpectedChildBoneIndex = LinkBoneIndex;
		}

		if (!bValidChain)
		{
			if (DebugLog != nullptr)
			{
				DebugLog->Log(FString::Printf(TEXT("ModelData IK chain skipped: %s -> %s | %s"),
					*Chain.IKBoneName.ToString(),
					*Chain.TargetBoneName.ToString(),
					*Chain.InvalidReason));
			}
			continue;
		}

		if (Chain.Links.Num() > 0)
		{
			InOutTracksToWrite.Add(Chain.IKBoneName);
			InOutTracksToWrite.Add(Chain.TargetBoneName);
			Chains.Add(MoveTemp(Chain));
		}
	}

	return Chains;
}

static void SolveMMDIKChainForFrame(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& RefPose, const FMMDIKBakeChain& Chain, TArray<TArray<FTransform>>& LocalTransforms, int32 FrameIndex, TArray<FTransform>& ComponentTransforms)
{
	for (int32 IterationIndex = 0; IterationIndex < Chain.IterationCount; ++IterationIndex)
	{
		for (const FMMDIKBakeChain::FLink& Link : Chain.Links)
		{
			if (!ComponentTransforms.IsValidIndex(Link.BoneIndex)
				|| !ComponentTransforms.IsValidIndex(Chain.TargetBoneIndex)
				|| !ComponentTransforms.IsValidIndex(Chain.IKBoneIndex)
				|| !RefPose.IsValidIndex(Link.BoneIndex))
			{
				continue;
			}

			const FVector LinkPosition = ComponentTransforms[Link.BoneIndex].GetTranslation();
			const FVector EffectorVector = ComponentTransforms[Chain.TargetBoneIndex].GetTranslation() - LinkPosition;
			const FVector GoalVector = ComponentTransforms[Chain.IKBoneIndex].GetTranslation() - LinkPosition;
			if (EffectorVector.SizeSquared() <= KINDA_SMALL_NUMBER || GoalVector.SizeSquared() <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FQuat DeltaRotation = FQuat::FindBetweenNormals(EffectorVector.GetSafeNormal(), GoalVector.GetSafeNormal());
			float DeltaAngle = 0.0f;
			FVector DeltaAxis = FVector::ForwardVector;
			DeltaRotation.ToAxisAndAngle(DeltaAxis, DeltaAngle);
			DeltaAngle = FMath::UnwindRadians(DeltaAngle);
			DeltaRotation = FQuat(DeltaAxis.GetSafeNormal(), FMath::Clamp(DeltaAngle, -Chain.AngleLimitRadians, Chain.AngleLimitRadians)).GetNormalized();

			FTransform NewComponentTransform = ComponentTransforms[Link.BoneIndex];
			NewComponentTransform.SetRotation((DeltaRotation * ComponentTransforms[Link.BoneIndex].GetRotation()).GetNormalized());
			const int32 ParentIndex = RefSkeleton.GetParentIndex(Link.BoneIndex);
			FTransform NewLocalTransform = ParentIndex != INDEX_NONE
				? NewComponentTransform.GetRelativeTransform(ComponentTransforms[ParentIndex])
				: NewComponentTransform;
			// PMX link limits are expressed in MMD local bone axes. The current UE Euler clamp is not equivalent
			// and can collapse the leg chain on models with custom leg axes, so keep the CCD solution unclamped here.
			NewLocalTransform.SetRotation(NewLocalTransform.GetRotation().GetNormalized());
			LocalTransforms[Link.BoneIndex][FrameIndex].SetRotation(NewLocalTransform.GetRotation().GetNormalized());
			BuildComponentSpacePoseForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
		}
	}
}

static void ApplyMMDAppendAndIKByTransformOrder(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& RefPose, const TArray<FMMDPMXRuntimeBone>& RuntimeBones, const TArray<FMMDIKBakeChain>& IKChains, TArray<TArray<FTransform>>& LocalTransforms, int32 NumKeys)
{
	TMap<int32, const FMMDIKBakeChain*> IKChainByPMXBone;
	for (const FMMDIKBakeChain& Chain : IKChains)
	{
		IKChainByPMXBone.Add(Chain.PMXBoneIndex, &Chain);
	}

	TArray<FTransform> ComponentTransforms;
	for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
	{
		BuildComponentSpacePoseForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
		for (const FMMDPMXRuntimeBone& RuntimeBone : RuntimeBones)
		{
			const int32 BoneIndex = RuntimeBone.SkeletonBoneIndex;
			const int32 InheritParentIndex = RuntimeBone.InheritParentSkeletonBoneIndex;
			if ((RuntimeBone.bInheritRotation || RuntimeBone.bInheritTranslation)
				&& LocalTransforms.IsValidIndex(BoneIndex)
				&& LocalTransforms.IsValidIndex(InheritParentIndex)
				&& RefPose.IsValidIndex(BoneIndex)
				&& RefPose.IsValidIndex(InheritParentIndex))
			{
				FTransform& BoneLocal = LocalTransforms[BoneIndex][FrameIndex];
				const FTransform& ParentLocal = LocalTransforms[InheritParentIndex][FrameIndex];
				const FTransform& ParentRefLocal = RefPose[InheritParentIndex];
				if (RuntimeBone.bInheritTranslation)
				{
					BoneLocal.AddToTranslation((ParentLocal.GetTranslation() - ParentRefLocal.GetTranslation()) * RuntimeBone.InheritInfluence);
				}
				if (RuntimeBone.bInheritRotation)
				{
					const FQuat ParentRotationDelta = (ParentRefLocal.GetRotation().Inverse() * ParentLocal.GetRotation()).GetNormalized();
					const FQuat WeightedDelta = FQuat::Slerp(FQuat::Identity, ParentRotationDelta, FMath::Clamp(RuntimeBone.InheritInfluence, 0.0f, 1.0f)).GetNormalized();
					BoneLocal.SetRotation((BoneLocal.GetRotation() * WeightedDelta).GetNormalized());
				}
				BuildComponentSpacePoseForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
			}

			if (const FMMDIKBakeChain* const* Chain = IKChainByPMXBone.Find(RuntimeBone.PMXBoneIndex))
			{
				SolveMMDIKChainForFrame(RefSkeleton, RefPose, **Chain, LocalTransforms, FrameIndex, ComponentTransforms);
			}
		}
	}
}

static void LogVMDControlTrackStats(const TMap<FName, TArray<FResolvedVMDBoneKey>>& BoneTrackMap, FMMDIKDebugLog& DebugLog)
{
	const FName ControlBones[] = {
		FName(TEXT("\u5168\u3066\u306e\u89aa")),
		FName(TEXT("\u64cd\u4f5c\u4e2d\u5fc3")),
		FName(TEXT("\u30bb\u30f3\u30bf\u30fc")),
		FName(TEXT("\u30b0\u30eb\u30fc\u30d6")),
		FName(TEXT("\u8170")),
		FName(TEXT("\u53f3\u8db3\uff29\uff2b")),
		FName(TEXT("\u5de6\u8db3\uff29\uff2b")),
		FName(TEXT("\u53f3\u3064\u307e\u5148\uff29\uff2b")),
		FName(TEXT("\u5de6\u3064\u307e\u5148\uff29\uff2b")),
		FName(TEXT("\u53f3\u8db3IK\u89aa")),
		FName(TEXT("\u5de6\u8db3IK\u89aa")),
		FName(TEXT("\u4e21\u8db3IK\u89aa"))
	};

	for (const FName& BoneName : ControlBones)
	{
		const TArray<FResolvedVMDBoneKey>* Keys = BoneTrackMap.Find(BoneName);
		if (Keys == nullptr || Keys->Num() == 0)
		{
			DebugLog.Log(FString::Printf(TEXT("Track stats: %s | Keys=0"), *BoneName.ToString()));
			continue;
		}
		int32 MaxGap = 0;
		for (int32 KeyIndex = 1; KeyIndex < Keys->Num(); ++KeyIndex)
		{
			MaxGap = FMath::Max(MaxGap, (*Keys)[KeyIndex].Frame - (*Keys)[KeyIndex - 1].Frame);
		}
		DebugLog.Log(FString::Printf(TEXT("Track stats: %s | Keys=%d | First=%d | Last=%d | MaxGap=%d | VMDBezier=1"),
			*BoneName.ToString(),
			Keys->Num(),
			(*Keys)[0].Frame,
			Keys->Last().Frame,
			MaxGap));
	}
}

static float GetComponentSpaceBoneDistance(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<TArray<FTransform>>& LocalTransforms,
	int32 FrameIndex,
	const FName& BoneA,
	const FName& BoneB)
{
	const int32 BoneIndexA = RefSkeleton.FindBoneIndex(BoneA);
	const int32 BoneIndexB = RefSkeleton.FindBoneIndex(BoneB);
	if (BoneIndexA == INDEX_NONE || BoneIndexB == INDEX_NONE)
	{
		return -1.0f;
	}

	TArray<FTransform> ComponentTransforms;
	BuildComponentSpacePoseForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
	if (!ComponentTransforms.IsValidIndex(BoneIndexA) || !ComponentTransforms.IsValidIndex(BoneIndexB))
	{
		return -1.0f;
	}

	return static_cast<float>(FVector::Dist(ComponentTransforms[BoneIndexA].GetTranslation(), ComponentTransforms[BoneIndexB].GetTranslation()));
}

static FVector GetComponentSpaceBonePosition(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<TArray<FTransform>>& LocalTransforms,
	int32 FrameIndex,
	const FName& BoneName,
	bool& bOutFound)
{
	bOutFound = false;
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if (BoneIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}

	TArray<FTransform> ComponentTransforms;
	BuildComponentSpacePoseForFrame(RefSkeleton, LocalTransforms, FrameIndex, ComponentTransforms);
	if (!ComponentTransforms.IsValidIndex(BoneIndex))
	{
		return FVector::ZeroVector;
	}

	bOutFound = true;
	return ComponentTransforms[BoneIndex].GetTranslation();
}

static FVector SampleRawVMDPositionForBone(const TMap<FName, TArray<FResolvedVMDBoneKey>>& BoneTrackMap, const FName& BoneName, int32 FrameIndex)
{
	const TArray<FResolvedVMDBoneKey>* Keys = BoneTrackMap.Find(BoneName);
	if (Keys == nullptr)
	{
		return FVector::ZeroVector;
	}

	FVector RawMMDPosition = FVector::ZeroVector;
	FQuat RawMMDRotation = FQuat::Identity;
	SampleResolvedVMDTrackAtFrame(*Keys, FrameIndex, RawMMDPosition, RawMMDRotation);
	return RawMMDPosition;
}

static void LogMMDIKTargetFrameProbe(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<TArray<FTransform>>& LocalTransforms,
	const TMap<FName, TArray<FResolvedVMDBoneKey>>& BoneTrackMap,
	int32 FrameIndex,
	FMMDIKDebugLog& DebugLog)
{
	const FName RightIK(TEXT("\u53f3\u8db3\uff29\uff2b"));
	const FName LeftIK(TEXT("\u5de6\u8db3\uff29\uff2b"));
	const FName RightIKParent(TEXT("\u53f3\u8db3IK\u89aa"));
	const FName LeftIKParent(TEXT("\u5de6\u8db3IK\u89aa"));
	const FName Center(TEXT("\u30bb\u30f3\u30bf\u30fc"));
	const FName Groove(TEXT("\u30b0\u30eb\u30fc\u30d6"));

	bool bFoundRightIK = false;
	bool bFoundLeftIK = false;
	bool bFoundRightParent = false;
	bool bFoundLeftParent = false;
	const FVector RightIKPos = GetComponentSpaceBonePosition(RefSkeleton, LocalTransforms, FrameIndex, RightIK, bFoundRightIK);
	const FVector LeftIKPos = GetComponentSpaceBonePosition(RefSkeleton, LocalTransforms, FrameIndex, LeftIK, bFoundLeftIK);
	const FVector RightParentPos = GetComponentSpaceBonePosition(RefSkeleton, LocalTransforms, FrameIndex, RightIKParent, bFoundRightParent);
	const FVector LeftParentPos = GetComponentSpaceBonePosition(RefSkeleton, LocalTransforms, FrameIndex, LeftIKParent, bFoundLeftParent);

	const FVector RightIKRaw = SampleRawVMDPositionForBone(BoneTrackMap, RightIK, FrameIndex);
	const FVector LeftIKRaw = SampleRawVMDPositionForBone(BoneTrackMap, LeftIK, FrameIndex);
	const FVector RightParentRaw = SampleRawVMDPositionForBone(BoneTrackMap, RightIKParent, FrameIndex);
	const FVector LeftParentRaw = SampleRawVMDPositionForBone(BoneTrackMap, LeftIKParent, FrameIndex);
	const FVector CenterRaw = SampleRawVMDPositionForBone(BoneTrackMap, Center, FrameIndex);
	const FVector GrooveRaw = SampleRawVMDPositionForBone(BoneTrackMap, Groove, FrameIndex);

	DebugLog.LogAlways(FString::Printf(TEXT("IK target frame @%d | RIK%s=(%.3f, %.3f, %.3f) LIK%s=(%.3f, %.3f, %.3f) | RIKParent%s=(%.3f, %.3f, %.3f) LIKParent%s=(%.3f, %.3f, %.3f)"),
		FrameIndex,
		bFoundRightIK ? TEXT("") : TEXT("?"),
		RightIKPos.X,
		RightIKPos.Y,
		RightIKPos.Z,
		bFoundLeftIK ? TEXT("") : TEXT("?"),
		LeftIKPos.X,
		LeftIKPos.Y,
		LeftIKPos.Z,
		bFoundRightParent ? TEXT("") : TEXT("?"),
		RightParentPos.X,
		RightParentPos.Y,
		RightParentPos.Z,
		bFoundLeftParent ? TEXT("") : TEXT("?"),
		LeftParentPos.X,
		LeftParentPos.Y,
		LeftParentPos.Z));
	DebugLog.LogAlways(FString::Printf(TEXT("IK target raw @%d | RIK=(%.3f, %.3f, %.3f) LIK=(%.3f, %.3f, %.3f) | RIKParent=(%.3f, %.3f, %.3f) LIKParent=(%.3f, %.3f, %.3f) | Center=(%.3f, %.3f, %.3f) Groove=(%.3f, %.3f, %.3f)"),
		FrameIndex,
		RightIKRaw.X,
		RightIKRaw.Y,
		RightIKRaw.Z,
		LeftIKRaw.X,
		LeftIKRaw.Y,
		LeftIKRaw.Z,
		RightParentRaw.X,
		RightParentRaw.Y,
		RightParentRaw.Z,
		LeftParentRaw.X,
		LeftParentRaw.Y,
		LeftParentRaw.Z,
		CenterRaw.X,
		CenterRaw.Y,
		CenterRaw.Z,
		GrooveRaw.X,
		GrooveRaw.Y,
		GrooveRaw.Z));
}

static void LogMMDIKCrossDistanceProbe(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<TArray<FTransform>>& LocalTransforms,
	int32 FrameIndex,
	const TCHAR* Phase,
	FMMDIKDebugLog& DebugLog)
{
	const FName RightIK(TEXT("\u53f3\u8db3\uff29\uff2b"));
	const FName LeftIK(TEXT("\u5de6\u8db3\uff29\uff2b"));
	const FName RightAnkle(TEXT("\u53f3\u8db3\u9996"));
	const FName LeftAnkle(TEXT("\u5de6\u8db3\u9996"));

	DebugLog.LogAlways(FString::Printf(TEXT("IK cross %s @%d | RAnkle-RIK=%.3f RAnkle-LIK=%.3f | LAnkle-LIK=%.3f LAnkle-RIK=%.3f"),
		Phase,
		FrameIndex,
		GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, RightAnkle, RightIK),
		GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, RightAnkle, LeftIK),
		GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, LeftAnkle, LeftIK),
		GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, LeftAnkle, RightIK)));
}

static float GetRefPoseComponentSpaceBoneDistance(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FTransform>& RefPose,
	const FName& BoneA,
	const FName& BoneB)
{
	const int32 BoneIndexA = RefSkeleton.FindBoneIndex(BoneA);
	const int32 BoneIndexB = RefSkeleton.FindBoneIndex(BoneB);
	if (BoneIndexA == INDEX_NONE || BoneIndexB == INDEX_NONE)
	{
		return -1.0f;
	}

	TArray<FTransform> ComponentTransforms;
	ComponentTransforms.SetNum(RefSkeleton.GetNum());
	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
	{
		const FTransform& LocalTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		ComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
			? LocalTransform * ComponentTransforms[ParentIndex]
			: LocalTransform;
	}

	return static_cast<float>(FVector::Dist(ComponentTransforms[BoneIndexA].GetTranslation(), ComponentTransforms[BoneIndexB].GetTranslation()));
}

static void LogMMDIKSeparationProbe(
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FTransform>& RefPose,
	const TArray<TArray<FTransform>>& LocalTransforms,
	const TMap<FName, TArray<FResolvedVMDBoneKey>>& BoneTrackMap,
	int32 NumKeys,
	const TCHAR* Phase,
	FMMDIKDebugLog& DebugLog)
{
	const FName RightIK(TEXT("\u53f3\u8db3\uff29\uff2b"));
	const FName LeftIK(TEXT("\u5de6\u8db3\uff29\uff2b"));
	const FName RightAnkle(TEXT("\u53f3\u8db3\u9996"));
	const FName LeftAnkle(TEXT("\u5de6\u8db3\u9996"));

	float MinIKDistance = TNumericLimits<float>::Max();
	int32 MinIKFrame = 0;
	float MinAnkleDistance = TNumericLimits<float>::Max();
	int32 MinAnkleFrame = 0;
	TArray<float> IKDistances;
	TArray<float> AnkleDistances;
	IKDistances.SetNum(NumKeys);
	AnkleDistances.SetNum(NumKeys);

	for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
	{
		const float IKDistance = GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, RightIK, LeftIK);
		IKDistances[FrameIndex] = IKDistance;
		if (IKDistance >= 0.0f && IKDistance < MinIKDistance)
		{
			MinIKDistance = IKDistance;
			MinIKFrame = FrameIndex;
		}

		const float AnkleDistance = GetComponentSpaceBoneDistance(RefSkeleton, LocalTransforms, FrameIndex, RightAnkle, LeftAnkle);
		AnkleDistances[FrameIndex] = AnkleDistance;
		if (AnkleDistance >= 0.0f && AnkleDistance < MinAnkleDistance)
		{
			MinAnkleDistance = AnkleDistance;
			MinAnkleFrame = FrameIndex;
		}
	}

	const float RefIKDistance = GetRefPoseComponentSpaceBoneDistance(RefSkeleton, RefPose, RightIK, LeftIK);
	const float RefAnkleDistance = GetRefPoseComponentSpaceBoneDistance(RefSkeleton, RefPose, RightAnkle, LeftAnkle);
	const float AnkleAtMinIK = AnkleDistances.IsValidIndex(MinIKFrame) ? AnkleDistances[MinIKFrame] : -1.0f;
	const float IKAtMinAnkle = IKDistances.IsValidIndex(MinAnkleFrame) ? IKDistances[MinAnkleFrame] : -1.0f;

	DebugLog.LogAlways(FString::Printf(TEXT("IK probe %s | RefIK=%.3f RefAnkle=%.3f | MinIK=%.3f@%d AnkleAtMinIK=%.3f | MinAnkle=%.3f@%d IKAtMinAnkle=%.3f"),
		Phase,
		RefIKDistance,
		RefAnkleDistance,
		MinIKDistance == TNumericLimits<float>::Max() ? -1.0f : MinIKDistance,
		MinIKFrame,
		AnkleAtMinIK,
		MinAnkleDistance == TNumericLimits<float>::Max() ? -1.0f : MinAnkleDistance,
		MinAnkleFrame,
		IKAtMinAnkle));
	LogMMDIKCrossDistanceProbe(RefSkeleton, LocalTransforms, MinAnkleFrame, Phase, DebugLog);
	if (FCString::Strcmp(Phase, TEXT("BeforeBake")) == 0)
	{
		LogMMDIKTargetFrameProbe(RefSkeleton, LocalTransforms, BoneTrackMap, 0, DebugLog);
		LogMMDIKTargetFrameProbe(RefSkeleton, LocalTransforms, BoneTrackMap, MinIKFrame, DebugLog);
		if (MinAnkleFrame != MinIKFrame)
		{
			LogMMDIKTargetFrameProbe(RefSkeleton, LocalTransforms, BoneTrackMap, MinAnkleFrame, DebugLog);
		}
	}
}

bool BuildResolvedBoneKeyMap(const VMDData& VmdData, const FMMDAnimationImportReport& Report, const FMMDAnimationImportSettings& Settings, TMap<FName, TArray<FResolvedVMDBoneKey>>& OutTrackMap)
{
	OutTrackMap.Reset();

	TMap<FString, FName> SourceToTargetBoneMap;
	for (const FMMDResolvedBoneTrack& Track : Report.BoneTracks)
	{
		if (Track.bMatched && Track.TargetBoneName != NAME_None)
		{
			SourceToTargetBoneMap.Add(Track.SourceBoneName, Track.TargetBoneName);
		}
	}

	for (const VMDBoneKeyframe& Keyframe : VmdData.BoneKeyframes)
	{
		const FName* TargetBoneName = SourceToTargetBoneMap.Find(Keyframe.BoneName);
		if (TargetBoneName == nullptr || *TargetBoneName == NAME_None)
		{
			continue;
		}

		FResolvedVMDBoneKey ResolvedKey;
		ResolvedKey.Frame = static_cast<int32>(Keyframe.FrameNumber);
		ResolvedKey.Position = Keyframe.Position;
		ResolvedKey.Rotation = Keyframe.Rotation.GetNormalized();
		FMemory::Memcpy(ResolvedKey.Interpolation, Keyframe.Interpolation, UE_ARRAY_COUNT(ResolvedKey.Interpolation));
		OutTrackMap.FindOrAdd(*TargetBoneName).Add(MoveTemp(ResolvedKey));
	}

	for (TPair<FName, TArray<FResolvedVMDBoneKey>>& Pair : OutTrackMap)
	{
		Pair.Value.Sort([](const FResolvedVMDBoneKey& A, const FResolvedVMDBoneKey& B)
		{
			return A.Frame < B.Frame;
		});
	}

	return OutTrackMap.Num() > 0;
}

static bool BuildPMXResolvedBoneKeyMap(const VMDData& VmdData, const UMMDModelDataAsset* ModelDataAsset, TMap<int32, TArray<FResolvedVMDBoneKey>>& OutTrackMap)
{
	OutTrackMap.Reset();
	if (ModelDataAsset == nullptr)
	{
		return false;
	}

	TMap<FString, int32> NameToPMXBoneIndex;
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		if (!BoneData.NameJP.IsEmpty())
		{
			NameToPMXBoneIndex.FindOrAdd(BoneData.NameJP, PMXBoneIndex);
		}
		if (!BoneData.NameEN.IsEmpty())
		{
			NameToPMXBoneIndex.FindOrAdd(BoneData.NameEN, PMXBoneIndex);
		}
	}

	for (const VMDBoneKeyframe& Keyframe : VmdData.BoneKeyframes)
	{
		const int32* PMXBoneIndex = NameToPMXBoneIndex.Find(Keyframe.BoneName);
		if (PMXBoneIndex == nullptr)
		{
			continue;
		}

		FResolvedVMDBoneKey ResolvedKey;
		ResolvedKey.Frame = static_cast<int32>(Keyframe.FrameNumber);
		ResolvedKey.Position = Keyframe.Position;
		ResolvedKey.Rotation = Keyframe.Rotation.GetNormalized();
		FMemory::Memcpy(ResolvedKey.Interpolation, Keyframe.Interpolation, UE_ARRAY_COUNT(ResolvedKey.Interpolation));
		OutTrackMap.FindOrAdd(*PMXBoneIndex).Add(MoveTemp(ResolvedKey));
	}

	for (TPair<int32, TArray<FResolvedVMDBoneKey>>& Pair : OutTrackMap)
	{
		Pair.Value.Sort([](const FResolvedVMDBoneKey& A, const FResolvedVMDBoneKey& B)
		{
			return A.Frame < B.Frame;
		});
	}

	return OutTrackMap.Num() > 0;
}

static FVector ConvertMMDLocalTranslationWithAxis(const FVector& RawMMDPosition, const FMMDModelBoneData& BoneData, float Scale)
{
	return ConvertMMDPositionToUnreal(RawMMDPosition, Scale);
}

static FQuat ConvertMMDLocalRotationWithAxis(const FQuat& RawMMDRotation, const FMMDModelBoneData& BoneData)
{
	return ConvertMMDQuatToUnreal(RawMMDRotation).GetNormalized();
}

static FVector ConvertMMDLocalTranslationToPMXSpace(const FVector& RawMMDPosition, const FMMDModelBoneData& BoneData)
{
	return RawMMDPosition;
}

static FQuat ConvertMMDLocalRotationToPMXSpace(const FQuat& RawMMDRotation, const FMMDModelBoneData& BoneData)
{
	return RawMMDRotation.GetNormalized();
}

static FVector GetPMXRestLocalTranslation(const UMMDModelDataAsset* ModelDataAsset, int32 PMXBoneIndex)
{
	if (ModelDataAsset == nullptr || !ModelDataAsset->Bones.IsValidIndex(PMXBoneIndex))
	{
		return FVector::ZeroVector;
	}

	const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
	if (ModelDataAsset->Bones.IsValidIndex(BoneData.ParentBoneIndex))
	{
		return BoneData.Position - ModelDataAsset->Bones[BoneData.ParentBoneIndex].Position;
	}
	return BoneData.Position;
}

static void BuildPMXSpaceComponentPoseForFrame(const UMMDModelDataAsset* ModelDataAsset, const TArray<TArray<FTransform>>& PMXLocalTransforms, int32 FrameIndex, TArray<FTransform>& OutComponentTransforms)
{
	const int32 NumPMXBones = ModelDataAsset ? ModelDataAsset->Bones.Num() : 0;
	OutComponentTransforms.SetNum(NumPMXBones);
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < NumPMXBones; ++PMXBoneIndex)
	{
		const FTransform& LocalTransform = PMXLocalTransforms[PMXBoneIndex][FrameIndex];
		const int32 ParentIndex = ModelDataAsset->Bones[PMXBoneIndex].ParentBoneIndex;
		OutComponentTransforms[PMXBoneIndex] = ModelDataAsset->Bones.IsValidIndex(ParentIndex)
			? LocalTransform * OutComponentTransforms[ParentIndex]
			: LocalTransform;
	}
}

static TArray<FMMDPMXSpaceRuntimeBone> BuildPMXSpaceRuntimeBones(const UMMDModelDataAsset* ModelDataAsset)
{
	TArray<FMMDPMXSpaceRuntimeBone> RuntimeBones;
	if (ModelDataAsset == nullptr)
	{
		return RuntimeBones;
	}

	RuntimeBones.Reserve(ModelDataAsset->Bones.Num());
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		FMMDPMXSpaceRuntimeBone RuntimeBone;
		RuntimeBone.PMXBoneIndex = PMXBoneIndex;
		RuntimeBone.InheritParentPMXBoneIndex = BoneData.InheritParentIndex;
		RuntimeBone.DeformLayer = BoneData.DeformLayer;
		RuntimeBone.bInheritRotation = (BoneData.Flags & 0x0100) != 0;
		RuntimeBone.bInheritTranslation = (BoneData.Flags & 0x0200) != 0;
		RuntimeBone.InheritInfluence = BoneData.InheritInfluence;
		RuntimeBone.bHasIK = (BoneData.Flags & 0x0020) != 0;
		RuntimeBones.Add(RuntimeBone);
	}

	RuntimeBones.Sort([](const FMMDPMXSpaceRuntimeBone& A, const FMMDPMXSpaceRuntimeBone& B)
	{
		if (A.DeformLayer != B.DeformLayer)
		{
			return A.DeformLayer < B.DeformLayer;
		}
		if (A.bHasIK != B.bHasIK)
		{
			return !A.bHasIK && B.bHasIK;
		}
		return A.PMXBoneIndex < B.PMXBoneIndex;
	});

	return RuntimeBones;
}

static TArray<FMMDPMXSpaceIKChain> BuildPMXSpaceIKChains(const UMMDModelDataAsset* ModelDataAsset)
{
	TArray<FMMDPMXSpaceIKChain> Chains;
	if (ModelDataAsset == nullptr)
	{
		return Chains;
	}

	for (int32 PMXBoneIndex = 0; PMXBoneIndex < ModelDataAsset->Bones.Num(); ++PMXBoneIndex)
	{
		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		if ((BoneData.Flags & 0x0020) == 0 || BoneData.IKLinks.Num() == 0 || !ModelDataAsset->Bones.IsValidIndex(BoneData.IKTargetBoneIndex))
		{
			continue;
		}

		FMMDPMXSpaceIKChain Chain;
		Chain.PMXBoneIndex = PMXBoneIndex;
		Chain.TargetPMXBoneIndex = BoneData.IKTargetBoneIndex;
		Chain.IterationCount = FMath::Clamp(FMath::Max(BoneData.IKLoopCount, 64), 1, 256);
		Chain.AngleLimitRadians = BoneData.IKLimitAngle > SMALL_NUMBER ? BoneData.IKLimitAngle : PI;

		int32 FirstLinkIndex = 0;
		if (BoneData.IKLinks[0].LinkBoneIndex == BoneData.IKTargetBoneIndex && BoneData.IKLinks.Num() > 1)
		{
			FirstLinkIndex = 1;
		}

		for (int32 LinkIndex = FirstLinkIndex; LinkIndex < BoneData.IKLinks.Num(); ++LinkIndex)
		{
			const FMMDModelIKLinkData& SourceLink = BoneData.IKLinks[LinkIndex];
			if (!ModelDataAsset->Bones.IsValidIndex(SourceLink.LinkBoneIndex))
			{
				Chain.Links.Reset();
				break;
			}

			FMMDPMXSpaceIKChain::FLink Link;
			Link.PMXBoneIndex = SourceLink.LinkBoneIndex;
			Link.bHasLimit = SourceLink.bHasLimit;
			Link.LowerLimitRadians = SourceLink.LowerLimit;
			Link.UpperLimitRadians = SourceLink.UpperLimit;
			Chain.Links.Add(Link);
		}

		if (Chain.Links.Num() > 0)
		{
			Chains.Add(MoveTemp(Chain));
		}
	}

	return Chains;
}

static bool IsStandardMMDLegIKBoneName(const FString& BoneName)
{
	return BoneName == TEXT("\u53f3\u8db3\uff29\uff2b")
		|| BoneName == TEXT("\u5de6\u8db3\uff29\uff2b")
		|| BoneName == TEXT("\u53f3\u3064\u307e\u5148\uff29\uff2b")
		|| BoneName == TEXT("\u5de6\u3064\u307e\u5148\uff29\uff2b");
}

static bool IsStandardMMDLegIKBone(const UMMDModelDataAsset* ModelDataAsset, int32 PMXBoneIndex)
{
	if (ModelDataAsset == nullptr || !ModelDataAsset->Bones.IsValidIndex(PMXBoneIndex))
	{
		return false;
	}

	const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
	return IsStandardMMDLegIKBoneName(BoneData.NameJP) || IsStandardMMDLegIKBoneName(BoneData.NameEN);
}

static bool IsMMDArmOrHandBoneName(const FString& BoneName)
{
	return BoneName.Contains(TEXT("\u8155"))
		|| BoneName.Contains(TEXT("\u3072\u3058"))
		|| BoneName.Contains(TEXT("\u624b"))
		|| BoneName.Contains(TEXT("\u6307"))
		|| BoneName.Contains(TEXT("arm"), ESearchCase::IgnoreCase)
		|| BoneName.Contains(TEXT("elbow"), ESearchCase::IgnoreCase)
		|| BoneName.Contains(TEXT("hand"), ESearchCase::IgnoreCase)
		|| BoneName.Contains(TEXT("finger"), ESearchCase::IgnoreCase);
}

static bool IsMMDArmOrHandBone(const UMMDModelDataAsset* ModelDataAsset, int32 PMXBoneIndex)
{
	if (ModelDataAsset == nullptr || !ModelDataAsset->Bones.IsValidIndex(PMXBoneIndex))
	{
		return false;
	}

	const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
	return IsMMDArmOrHandBoneName(BoneData.NameJP) || IsMMDArmOrHandBoneName(BoneData.NameEN);
}

static void SolvePMXSpaceIKChainForFrame(const UMMDModelDataAsset* ModelDataAsset, const FMMDPMXSpaceIKChain& Chain, TArray<TArray<FTransform>>& PMXLocalTransforms, int32 FrameIndex, TArray<FTransform>& PMXComponentTransforms)
{
	for (int32 IterationIndex = 0; IterationIndex < Chain.IterationCount; ++IterationIndex)
	{
		for (const FMMDPMXSpaceIKChain::FLink& Link : Chain.Links)
		{
			if (!PMXComponentTransforms.IsValidIndex(Link.PMXBoneIndex)
				|| !PMXComponentTransforms.IsValidIndex(Chain.TargetPMXBoneIndex)
				|| !PMXComponentTransforms.IsValidIndex(Chain.PMXBoneIndex))
			{
				continue;
			}

			const FVector LinkPosition = PMXComponentTransforms[Link.PMXBoneIndex].GetTranslation();
			const FVector EffectorVector = PMXComponentTransforms[Chain.TargetPMXBoneIndex].GetTranslation() - LinkPosition;
			const FVector GoalVector = PMXComponentTransforms[Chain.PMXBoneIndex].GetTranslation() - LinkPosition;
			if (EffectorVector.SizeSquared() <= KINDA_SMALL_NUMBER || GoalVector.SizeSquared() <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FQuat DeltaRotation = FQuat::FindBetweenNormals(EffectorVector.GetSafeNormal(), GoalVector.GetSafeNormal());
			float DeltaAngle = 0.0f;
			FVector DeltaAxis = FVector::ForwardVector;
			DeltaRotation.ToAxisAndAngle(DeltaAxis, DeltaAngle);
			DeltaAngle = FMath::UnwindRadians(DeltaAngle);
			DeltaRotation = FQuat(DeltaAxis.GetSafeNormal(), FMath::Clamp(DeltaAngle, -Chain.AngleLimitRadians, Chain.AngleLimitRadians)).GetNormalized();

			FTransform NewComponentTransform = PMXComponentTransforms[Link.PMXBoneIndex];
			NewComponentTransform.SetRotation((DeltaRotation * NewComponentTransform.GetRotation()).GetNormalized());
			const int32 ParentIndex = ModelDataAsset->Bones[Link.PMXBoneIndex].ParentBoneIndex;
			FTransform NewLocalTransform = ModelDataAsset->Bones.IsValidIndex(ParentIndex)
				? NewComponentTransform.GetRelativeTransform(PMXComponentTransforms[ParentIndex])
				: NewComponentTransform;
			NewLocalTransform.SetRotation(NewLocalTransform.GetRotation().GetNormalized());
			PMXLocalTransforms[Link.PMXBoneIndex][FrameIndex].SetRotation(NewLocalTransform.GetRotation());
			BuildPMXSpaceComponentPoseForFrame(ModelDataAsset, PMXLocalTransforms, FrameIndex, PMXComponentTransforms);
		}
	}
}

static void ApplyPMXSpaceAppendAndIK(
	const UMMDModelDataAsset* ModelDataAsset,
	const TArray<FMMDPMXSpaceRuntimeBone>& RuntimeBones,
	const TArray<FMMDPMXSpaceIKChain>& IKChains,
	const TSet<int32>& PMXAppendBonesToEvaluate,
	TArray<TArray<FTransform>>& PMXLocalTransforms,
	int32 NumKeys)
{
	TMap<int32, const FMMDPMXSpaceIKChain*> IKChainByPMXBone;
	for (const FMMDPMXSpaceIKChain& Chain : IKChains)
	{
		if (IsStandardMMDLegIKBone(ModelDataAsset, Chain.PMXBoneIndex))
		{
			IKChainByPMXBone.Add(Chain.PMXBoneIndex, &Chain);
		}
	}

	TArray<FTransform> PMXComponentTransforms;
	for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
	{
		BuildPMXSpaceComponentPoseForFrame(ModelDataAsset, PMXLocalTransforms, FrameIndex, PMXComponentTransforms);
		for (const FMMDPMXSpaceRuntimeBone& RuntimeBone : RuntimeBones)
		{
			if ((RuntimeBone.bInheritRotation || RuntimeBone.bInheritTranslation)
				&& PMXAppendBonesToEvaluate.Contains(RuntimeBone.PMXBoneIndex)
				&& PMXLocalTransforms.IsValidIndex(RuntimeBone.PMXBoneIndex)
				&& PMXLocalTransforms.IsValidIndex(RuntimeBone.InheritParentPMXBoneIndex))
			{
				FTransform& BoneLocal = PMXLocalTransforms[RuntimeBone.PMXBoneIndex][FrameIndex];
				const FVector ParentRestTranslation = GetPMXRestLocalTranslation(ModelDataAsset, RuntimeBone.InheritParentPMXBoneIndex);
				const FTransform& ParentLocal = PMXLocalTransforms[RuntimeBone.InheritParentPMXBoneIndex][FrameIndex];
				if (RuntimeBone.bInheritTranslation)
				{
					BoneLocal.AddToTranslation((ParentLocal.GetTranslation() - ParentRestTranslation) * RuntimeBone.InheritInfluence);
				}
				if (RuntimeBone.bInheritRotation)
				{
					const FQuat WeightedDelta = FQuat::Slerp(FQuat::Identity, ParentLocal.GetRotation(), FMath::Clamp(RuntimeBone.InheritInfluence, 0.0f, 1.0f)).GetNormalized();
					BoneLocal.SetRotation((BoneLocal.GetRotation() * WeightedDelta).GetNormalized());
				}
				BuildPMXSpaceComponentPoseForFrame(ModelDataAsset, PMXLocalTransforms, FrameIndex, PMXComponentTransforms);
			}

			if (const FMMDPMXSpaceIKChain* const* Chain = IKChainByPMXBone.Find(RuntimeBone.PMXBoneIndex))
			{
				SolvePMXSpaceIKChainForFrame(ModelDataAsset, **Chain, PMXLocalTransforms, FrameIndex, PMXComponentTransforms);
			}
		}
	}
}

static void BuildReferenceComponentTransforms(const FReferenceSkeleton& RefSkeleton, const TArray<FTransform>& RefPose, TArray<FTransform>& OutComponentTransforms)
{
	const int32 NumBones = RefSkeleton.GetNum();
	OutComponentTransforms.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FTransform& LocalTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		const int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
		OutComponentTransforms[BoneIndex] = ParentIndex != INDEX_NONE
			? LocalTransform * OutComponentTransforms[ParentIndex]
			: LocalTransform;
	}
}

static FTransform ConvertPMXComponentTransformToUE(const FTransform& PMXComponentTransform, float Scale)
{
	const FVector UETranslation = ConvertMMDPositionToUnreal(PMXComponentTransform.GetTranslation(), Scale);
	const FQuat UERotation = ConvertMMDQuatToUnreal(PMXComponentTransform.GetRotation()).GetNormalized();
	return FTransform(UERotation, UETranslation, FVector::OneVector);
}

static bool EvaluateMMDModelDataPoseToLocalTransforms(
	const VMDData& VmdData,
	const UMMDModelDataAsset* ModelDataAsset,
	const FReferenceSkeleton& RefSkeleton,
	const TArray<FTransform>& RefPose,
	const FMMDAnimationImportSettings& Settings,
	int32 NumKeys,
	TArray<TArray<FTransform>>& OutLocalTransforms,
	TSet<FName>& OutTracksToWrite,
	FMMDIKDebugLog* DebugLog)
{
	if (ModelDataAsset == nullptr || ModelDataAsset->Bones.Num() == 0)
	{
		return false;
	}

	TMap<int32, TArray<FResolvedVMDBoneKey>> PMXTrackMap;
	if (!BuildPMXResolvedBoneKeyMap(VmdData, ModelDataAsset, PMXTrackMap))
	{
		return false;
	}

	OutTracksToWrite.Reset();
	const int32 NumBones = RefSkeleton.GetNum();
	const int32 NumPMXBones = ModelDataAsset->Bones.Num();

	TArray<TArray<FTransform>> PMXLocalTransforms;
	PMXLocalTransforms.SetNum(NumPMXBones);
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < NumPMXBones; ++PMXBoneIndex)
	{
		const FVector RestLocalTranslation = GetPMXRestLocalTranslation(ModelDataAsset, PMXBoneIndex);
		PMXLocalTransforms[PMXBoneIndex].SetNum(NumKeys);
		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			PMXLocalTransforms[PMXBoneIndex][FrameIndex] = FTransform(FQuat::Identity, RestLocalTranslation, FVector::OneVector);
		}
	}

	int32 MappedTrackCount = 0;
	int32 MissingUETrackCount = 0;
	TArray<int32> PMXToSkeleton;
	PMXToSkeleton.SetNum(NumPMXBones);
	for (int32 PMXBoneIndex = 0; PMXBoneIndex < NumPMXBones; ++PMXBoneIndex)
	{
		PMXToSkeleton[PMXBoneIndex] = ResolveModelDataBoneToSkeletonIndex(ModelDataAsset->Bones[PMXBoneIndex], RefSkeleton);
	}

	for (const TPair<int32, TArray<FResolvedVMDBoneKey>>& Pair : PMXTrackMap)
	{
		const int32 PMXBoneIndex = Pair.Key;
		if (!ModelDataAsset->Bones.IsValidIndex(PMXBoneIndex))
		{
			continue;
		}

		const FMMDModelBoneData& BoneData = ModelDataAsset->Bones[PMXBoneIndex];
		const int32 UEBoneIndex = PMXToSkeleton.IsValidIndex(PMXBoneIndex) ? PMXToSkeleton[PMXBoneIndex] : INDEX_NONE;
		if (UEBoneIndex == INDEX_NONE)
		{
			++MissingUETrackCount;
			continue;
		}

		const TArray<FResolvedVMDBoneKey>& Keys = Pair.Value;
		const FVector RestLocalTranslation = GetPMXRestLocalTranslation(ModelDataAsset, PMXBoneIndex);
		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			FVector RawMMDPosition = FVector::ZeroVector;
			FQuat RawMMDRotation = FQuat::Identity;
			SampleResolvedVMDTrackAtFrame(Keys, FrameIndex, RawMMDPosition, RawMMDRotation);
			const FVector PMXTranslation = RestLocalTranslation + ConvertMMDLocalTranslationToPMXSpace(RawMMDPosition, BoneData);
			const FQuat PMXRotation = ConvertMMDLocalRotationToPMXSpace(RawMMDRotation, BoneData);
			PMXLocalTransforms[PMXBoneIndex][FrameIndex].SetTranslation(PMXTranslation);
			PMXLocalTransforms[PMXBoneIndex][FrameIndex].SetRotation(PMXRotation.GetNormalized());
		}
		++MappedTrackCount;
	}

	const TArray<FMMDPMXSpaceRuntimeBone> PMXRuntimeBones = Settings.bBakeMMDIKToFK
		? BuildPMXSpaceRuntimeBones(ModelDataAsset)
		: TArray<FMMDPMXSpaceRuntimeBone>();
	const TArray<FMMDPMXSpaceIKChain> PMXIKChains = Settings.bBakeMMDIKToFK
		? BuildPMXSpaceIKChains(ModelDataAsset)
		: TArray<FMMDPMXSpaceIKChain>();
	TSet<int32> PMXLegIKAffectedBones;
	for (const FMMDPMXSpaceIKChain& Chain : PMXIKChains)
	{
		if (!IsStandardMMDLegIKBone(ModelDataAsset, Chain.PMXBoneIndex))
		{
			continue;
		}

		PMXLegIKAffectedBones.Add(Chain.PMXBoneIndex);
		PMXLegIKAffectedBones.Add(Chain.TargetPMXBoneIndex);
		for (const FMMDPMXSpaceIKChain::FLink& Link : Chain.Links)
		{
			PMXLegIKAffectedBones.Add(Link.PMXBoneIndex);
		}
	}

	TSet<int32> PMXIKAppendBones;
	for (const FMMDPMXSpaceRuntimeBone& RuntimeBone : PMXRuntimeBones)
	{
		if ((RuntimeBone.bInheritRotation || RuntimeBone.bInheritTranslation)
			&& PMXLegIKAffectedBones.Contains(RuntimeBone.InheritParentPMXBoneIndex))
		{
			PMXIKAppendBones.Add(RuntimeBone.PMXBoneIndex);
		}
	}

	if (Settings.bBakeMMDIKToFK)
	{
		ApplyPMXSpaceAppendAndIK(ModelDataAsset, PMXRuntimeBones, PMXIKChains, PMXIKAppendBones, PMXLocalTransforms, NumKeys);
	}

	OutLocalTransforms.SetNum(NumBones);
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		OutLocalTransforms[BoneIndex].SetNum(NumKeys);
		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			OutLocalTransforms[BoneIndex][FrameIndex] = DefaultTransform;
		}
	}

	TArray<FTransform> RefComponentTransforms;
	BuildReferenceComponentTransforms(RefSkeleton, RefPose, RefComponentTransforms);
	TArray<FTransform> PMXComponentTransforms;
	TArray<FTransform> UEComponentTransforms;
	TArray<bool> bHasUEComponent;
	UEComponentTransforms.SetNum(NumBones);
	bHasUEComponent.SetNum(NumBones);

	for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
	{
		BuildPMXSpaceComponentPoseForFrame(ModelDataAsset, PMXLocalTransforms, FrameIndex, PMXComponentTransforms);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			UEComponentTransforms[BoneIndex] = RefComponentTransforms.IsValidIndex(BoneIndex) ? RefComponentTransforms[BoneIndex] : FTransform::Identity;
			bHasUEComponent[BoneIndex] = false;
		}

		for (int32 PMXBoneIndex = 0; PMXBoneIndex < NumPMXBones; ++PMXBoneIndex)
		{
			const int32 UEBoneIndex = PMXToSkeleton.IsValidIndex(PMXBoneIndex) ? PMXToSkeleton[PMXBoneIndex] : INDEX_NONE;
			if (UEBoneIndex == INDEX_NONE || !PMXComponentTransforms.IsValidIndex(PMXBoneIndex))
			{
				continue;
			}

			UEComponentTransforms[UEBoneIndex] = ConvertPMXComponentTransformToUE(PMXComponentTransforms[PMXBoneIndex], Settings.PositionScale);
			bHasUEComponent[UEBoneIndex] = true;
		}

		for (int32 PMXBoneIndex = 0; PMXBoneIndex < NumPMXBones; ++PMXBoneIndex)
		{
			const int32 UEBoneIndex = PMXToSkeleton.IsValidIndex(PMXBoneIndex) ? PMXToSkeleton[PMXBoneIndex] : INDEX_NONE;
			if (UEBoneIndex == INDEX_NONE || !bHasUEComponent.IsValidIndex(UEBoneIndex) || !bHasUEComponent[UEBoneIndex])
			{
				continue;
			}

			const int32 ParentUEBoneIndex = RefSkeleton.GetParentIndex(UEBoneIndex);
			const FTransform& ParentComponent = ParentUEBoneIndex != INDEX_NONE && UEComponentTransforms.IsValidIndex(ParentUEBoneIndex)
				? UEComponentTransforms[ParentUEBoneIndex]
				: FTransform::Identity;
			OutLocalTransforms[UEBoneIndex][FrameIndex] = UEComponentTransforms[UEBoneIndex].GetRelativeTransform(ParentComponent);
		}
	}

	TSet<int32> PMXBonesToWrite;
	for (const TPair<int32, TArray<FResolvedVMDBoneKey>>& Pair : PMXTrackMap)
	{
		PMXBonesToWrite.Add(Pair.Key);
	}

	TSet<int32> PMXIKAffectedBones = PMXLegIKAffectedBones;
	for (const int32 PMXBoneIndex : PMXIKAppendBones)
	{
		PMXIKAffectedBones.Add(PMXBoneIndex);
	}

	for (const int32 PMXBoneIndex : PMXIKAffectedBones)
	{
		PMXBonesToWrite.Add(PMXBoneIndex);
	}

	int32 ArmHandOriginalTrackCount = 0;
	int32 ArmHandExtraWriteCount = 0;
	for (const int32 PMXBoneIndex : PMXBonesToWrite)
	{
		if (!IsMMDArmOrHandBone(ModelDataAsset, PMXBoneIndex))
		{
			continue;
		}
		if (PMXTrackMap.Contains(PMXBoneIndex))
		{
			++ArmHandOriginalTrackCount;
		}
		else
		{
			++ArmHandExtraWriteCount;
		}
	}

	for (const int32 PMXBoneIndex : PMXBonesToWrite)
	{
		const int32 UEBoneIndex = PMXToSkeleton.IsValidIndex(PMXBoneIndex) ? PMXToSkeleton[PMXBoneIndex] : INDEX_NONE;
		if (UEBoneIndex != INDEX_NONE)
		{
			OutTracksToWrite.Add(RefSkeleton.GetBoneName(UEBoneIndex));
		}
	}

	if (DebugLog != nullptr)
	{
		DebugLog->LogAlways(FString::Printf(TEXT("MMD ModelData PMX-space evaluator | PMXBones=%d | PMXTracks=%d | MappedTracks=%d | MissingUETracks=%d | PMXRuntime=%d | PMXIK=%d | WriteSelectedBones=%d | IKAppendBones=%d | ArmHandTracks=%d | ArmHandExtra=%d"),
			ModelDataAsset->Bones.Num(),
			PMXTrackMap.Num(),
			MappedTrackCount,
			MissingUETrackCount,
			PMXRuntimeBones.Num(),
			PMXIKChains.Num(),
			OutTracksToWrite.Num(),
			PMXIKAffectedBones.Num(),
			ArmHandOriginalTrackCount,
			ArmHandExtraWriteCount));
	}

	return OutTracksToWrite.Num() > 0;
}

int32 BuildMorphTargetsFromPMX(USkeletalMesh* SkeletalMesh, const PMXDatas& PMXInfo)
{
#if WITH_EDITOR
	if (SkeletalMesh == nullptr)
	{
		return 0;
	}

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (ImportedModel == nullptr || ImportedModel->LODModels.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PMX Morph: SkeletalMesh has no imported LOD model."));
		return 0;
	}

	const FSkeletalMeshLODModel& BaseLODModel = ImportedModel->LODModels[0];
	const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(0);
	const float PositionThreshold = LODInfo ? LODInfo->BuildSettings.MorphThresholdPosition : UE_THRESH_POINTS_ARE_NEAR;

	TArray<TArray<uint32>> PMXVertexToRenderVertices;
	PMXVertexToRenderVertices.SetNum(PMXInfo.ModelVertices.Num());
	const int32 BaseVertexCount = BaseLODModel.NumVertices;
	int32 MappedRenderVertexCount = 0;

	if (BaseLODModel.MeshToImportVertexMap.Num() == BaseVertexCount)
	{
		for (int32 RenderVertexIndex = 0; RenderVertexIndex < BaseLODModel.MeshToImportVertexMap.Num(); ++RenderVertexIndex)
		{
			const int32 PMXVertexIndex = BaseLODModel.MeshToImportVertexMap[RenderVertexIndex];
			if (PMXVertexToRenderVertices.IsValidIndex(PMXVertexIndex))
			{
				PMXVertexToRenderVertices[PMXVertexIndex].Add(static_cast<uint32>(RenderVertexIndex));
				++MappedRenderVertexCount;
			}
		}
	}
	else if (BaseLODModel.GetRawPointIndices().Num() == BaseVertexCount)
	{
		const TArray<uint32>& RawPointIndices = BaseLODModel.GetRawPointIndices();
		for (int32 RenderVertexIndex = 0; RenderVertexIndex < RawPointIndices.Num(); ++RenderVertexIndex)
		{
			const int32 PMXVertexIndex = static_cast<int32>(RawPointIndices[RenderVertexIndex]);
			if (PMXVertexToRenderVertices.IsValidIndex(PMXVertexIndex))
			{
				PMXVertexToRenderVertices[PMXVertexIndex].Add(static_cast<uint32>(RenderVertexIndex));
				++MappedRenderVertexCount;
			}
		}
	}

	if (MappedRenderVertexCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PMX Morph: no LOD import-vertex map found; falling back to raw PMX vertex indices. Morphs may deform incorrectly."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("PMX Morph: mapped %d render vertices back to %d PMX vertices."), MappedRenderVertexCount, PMXInfo.ModelVertices.Num());
	}

	int32 ImportedMorphCount = 0;
	int32 SkippedMorphCount = 0;
	int32 MissingMappedVertexCount = 0;
	bool bNeedInitMorphTargets = false;
	auto ConvertMorphOffsetToUnreal = [](const FVector& PMXVector) -> FVector3f
	{
		FVector3f TempPos(PMXVector.Z * 8.0f, PMXVector.X * 8.0f, PMXVector.Y * 8.0f);
		return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
	};

	for (const FPMXMorph& Morph : PMXInfo.ModelMorphs)
	{
		if (Morph.MorphType != 1)
		{
			++SkippedMorphCount;
			continue;
		}

		const FString PreferredName = !Morph.NameJP.IsEmpty() ? Morph.NameJP : Morph.NameEN;
		if (PreferredName.IsEmpty())
		{
			++SkippedMorphCount;
			continue;
		}

		TArray<FMorphTargetDelta> Deltas;
		Deltas.Reserve(Morph.Vertices.Num() * 2);
		TSet<uint32> AddedSourceIndices;

		for (const FPMXMorphVertex& MorphVertex : Morph.Vertices)
		{
			if (!PMXInfo.ModelVertices.IsValidIndex(MorphVertex.VertexIndex))
			{
				continue;
			}

			const FVector3f PositionDelta = ConvertMorphOffsetToUnreal(MorphVertex.PositionOffset);
			if (PositionDelta.IsNearlyZero())
			{
				continue;
			}

			TArray<uint32> RenderVertexIndices;
			if (PMXVertexToRenderVertices.IsValidIndex(MorphVertex.VertexIndex))
			{
				RenderVertexIndices = PMXVertexToRenderVertices[MorphVertex.VertexIndex];
			}

			if (RenderVertexIndices.Num() == 0)
			{
				++MissingMappedVertexCount;
				if (MorphVertex.VertexIndex < BaseVertexCount)
				{
					RenderVertexIndices.Add(static_cast<uint32>(MorphVertex.VertexIndex));
				}
			}

			for (const uint32 RenderVertexIndex : RenderVertexIndices)
			{
				if (AddedSourceIndices.Contains(RenderVertexIndex))
				{
					continue;
				}

				FMorphTargetDelta Delta;
				Delta.SourceIdx = RenderVertexIndex;
				Delta.PositionDelta = PositionDelta;
				Delta.TangentZDelta = FVector3f::ZeroVector;
				Deltas.Add(Delta);
				AddedSourceIndices.Add(RenderVertexIndex);
			}
		}

		if (Deltas.Num() == 0)
		{
			++SkippedMorphCount;
			continue;
		}

		const FName MorphTargetName(*PreferredName);
		UMorphTarget* MorphTarget = SkeletalMesh->FindMorphTarget(MorphTargetName);
		if (MorphTarget == nullptr)
		{
			MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, MorphTargetName);
		}

		if (MorphTarget == nullptr)
		{
			++SkippedMorphCount;
			continue;
		}

		MorphTarget->PopulateDeltas(Deltas, 0, BaseLODModel.Sections, false, false, PositionThreshold);
		if (MorphTarget->HasValidData())
		{
			bNeedInitMorphTargets |= SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
			++ImportedMorphCount;
		}
		else
		{
			++SkippedMorphCount;
		}
	}

	if (bNeedInitMorphTargets)
	{
		SkeletalMesh->InitMorphTargetsAndRebuildRenderData();
	}

	UE_LOG(LogTemp, Log, TEXT("PMX Morph: imported %d vertex morph targets, skipped %d non-imported morphs, missing mapped vertices %d."), ImportedMorphCount, SkippedMorphCount, MissingMappedVertexCount);
	return ImportedMorphCount;
#else
	(void)SkeletalMesh;
	(void)PMXInfo;
	return 0;
#endif
}
UTexture2D* CreateTextureFromFile(const FString& TexturePath, const FString& OutPath, const FString& AssetName) {

	if (!FPaths::FileExists(TexturePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Texture file does not exist: %s"), *TexturePath);
		return nullptr;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== CreateTextureFromFile Debug ==="));
	UE_LOG(LogTemp, Warning, TEXT("原始资源名: %s"), *AssetName);

	FString CleanAssetName = FixMMDName(AssetName, TEXT("M_"));


	UE_LOG(LogTemp, Warning, TEXT("清理后的资源名: %s"), *CleanAssetName);

	FString SafeOutPath = FixMMDName(OutPath);
	if (!SafeOutPath.EndsWith(TEXT("/"))) {
		SafeOutPath += TEXT("/");
	}
	FString PackageName = SafeOutPath + CleanAssetName;

	PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));

	UE_LOG(LogTemp, Warning, TEXT("最终包名: %s"), *PackageName);

	if (PackageName.Contains(TEXT(" "))) {
		UE_LOG(LogTemp, Error, TEXT("包名仍然包含空格，这会导致错误: %s"), *PackageName);
		return nullptr;
	}
	UTextureFactory::SuppressImportOverwriteDialog(true);
	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->bCreateMaterial = false;

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *TexturePath)) {
		UE_LOG(LogTemp, Warning, TEXT("Failed to load texture file: %s"), *TexturePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(TexturePath).ToLower();
	if (Extension == TEXT("bmp"))
	{
		TArray<uint8> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		if (DecodeBmpToBGRA8(FileData, DecodedPixels, Width, Height))
		{
			UTexture2D* BmpTexture = CreateTextureFromBGRA8(Package, CleanAssetName, DecodedPixels, Width, Height);
			if (BmpTexture)
			{
				FAssetRegistryModule::AssetCreated(BmpTexture);
				Package->MarkPackageDirty();
				UE_LOG(LogTemp, Log, TEXT("Successfully imported BMP texture: %s (%dx%d)"), *PackageName, Width, Height);
				return BmpTexture;
			}
		}
	}

	const uint8* FileBuffer = FileData.GetData();

	UTexture2D* ImportedTexture = Cast<UTexture2D>(TextureFactory->FactoryCreateBinary(
		UTexture2D::StaticClass(),
		Package,
		FName(*CleanAssetName),  // 使用清理后的名称
		RF_Public | RF_Standalone,
		nullptr,
		*FPaths::GetExtension(TexturePath),
		FileBuffer,
		FileBuffer + FileData.Num(),
		nullptr)
	);

	if (ImportedTexture) {
		FAssetRegistryModule::AssetCreated(ImportedTexture);
		Package->MarkPackageDirty();
		UE_LOG(LogTemp, Log, TEXT("Successfully imported texture: %s"), *PackageName);
	}
	else {
		if (Extension == TEXT("bmp"))
		{
			TArray<uint8> DecodedPixels;
			int32 Width = 0;
			int32 Height = 0;
			if (DecodeBmpToBGRA8(FileData, DecodedPixels, Width, Height))
			{
				ImportedTexture = CreateTextureFromBGRA8(Package, CleanAssetName, DecodedPixels, Width, Height);
				if (ImportedTexture)
				{
					FAssetRegistryModule::AssetCreated(ImportedTexture);
					Package->MarkPackageDirty();
					UE_LOG(LogTemp, Log, TEXT("Successfully imported BMP texture via fallback: %s (%dx%d)"), *PackageName, Width, Height);
				}
			}
		}

		if (!ImportedTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to import texture: %s"), *PackageName);
		}
	}

	return ImportedTexture;
}

static void SetScalarParameterIfPresent(UMaterialInstanceConstant* MaterialInstance, const FName& ParameterName, float Value)
{
	FMaterialParameterInfo ParamInfo(ParameterName);
	MaterialInstance->SetScalarParameterValueEditorOnly(ParamInfo, Value);
}

static void SetVectorParameterIfPresent(UMaterialInstanceConstant* MaterialInstance, const FName& ParameterName, const FLinearColor& Value)
{
	FMaterialParameterInfo ParamInfo(ParameterName);
	MaterialInstance->SetVectorParameterValueEditorOnly(ParamInfo, Value);
}

static UMaterial* LoadMMDBaseMaterial()
{
	static const FString BaseMaterialPath = TEXT("/Ue5MMDTools/Resources/MaterialInstance/Mat_MMD_Base.Mat_MMD_Base");
	UMaterial* BaseMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *BaseMaterialPath));
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load base material from path: %s"), *BaseMaterialPath);
	}
	return BaseMaterial;
}

static bool SetMaterialTextureParameterDefault(UMaterial* Material, const FName& ParameterName, UTexture* Texture)
{
	if (!Material || !Texture)
	{
		return false;
	}

	bool bChanged = false;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		UMaterialExpressionTextureSampleParameter2D* TextureParameter = Cast<UMaterialExpressionTextureSampleParameter2D>(Expression);
		if (TextureParameter && TextureParameter->GetParameterName() == ParameterName)
		{
			TextureParameter->Modify();
			TextureParameter->Texture = Texture;
			bChanged = true;
		}
	}

	return bChanged;
}

FString GetMaterialTexturePath(const FPMXMaterial& Material, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
	if (Material.TextureIndex >= 0 && Material.TextureIndex < PMXInfo.ModelTextureCount) {
		FString PMXDirectory = PMXFilePath;

		FString RelativeTexturePath = PMXInfo.ModelTexturePaths[Material.TextureIndex];
		// 使用 FPaths::Combine 安全拼接路径
		FString FullTexturePath = FPaths::Combine(PMXDirectory, RelativeTexturePath);

		UE_LOG(LogTemp, Warning, TEXT("拼接后的完整路径: '%s'"), *FullTexturePath);
		UE_LOG(LogTemp, Warning, TEXT("文件是否存在: %s"), FPaths::FileExists(FullTexturePath) ? TEXT("是") : TEXT("否"));

		return FullTexturePath;
	}
	else if (Material.TextureIndex == -1) {
		// 没有贴图
		return FString();
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Invalid texture index %d for material %s"), Material.TextureIndex, *Material.NameEN);
		return FString();
	}
}
UMaterialInterface* CreateMaterialFromMMDBase(UTexture2D* Texture2D, const FPMXMaterial& PMXMaterial, const FString& MaterialName, const FString& OutPath) {

	UMaterial* BaseMaterial = LoadMMDBaseMaterial();
	if (!BaseMaterial) {
		return nullptr;
	}

	FString CleanMaterialName = FixMMDName(MaterialName, TEXT("M_"));

	FString SafeOutPath = OutPath;
	if (!SafeOutPath.EndsWith(TEXT("/")))
		SafeOutPath += TEXT("/");
	FString PackageName = SafeOutPath + CleanMaterialName;
	PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));
	PackageName = PackageName.Replace(TEXT(" "), TEXT("_"));

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
		return nullptr;
	}

	Package->FullyLoad();

	UMaterial* Material = FindObject<UMaterial>(Package, *CleanMaterialName);
	if (!Material)
	{
		Material = DuplicateObject<UMaterial>(BaseMaterial, Package, *CleanMaterialName);
	}

	if (!Material)
	{
		UE_LOG(LogTemp, Error, TEXT("Duplicate base material failed: %s"), *PackageName);
		return nullptr;
	}

	Material->SetFlags(RF_Public | RF_Standalone);
	Material->Modify();
	if (Texture2D)
	{
		SetMaterialTextureParameterDefault(Material, TEXT("BaseColorMap"), Texture2D);
		SetMaterialTextureParameterDefault(Material, TEXT("BaseColorTexture"), Texture2D);
		SetMaterialTextureParameterDefault(Material, TEXT("BaseColor"), Texture2D);
	}

	// Keep the graph identical to Mat_MMD_Base, but copy PMX alpha into editable material properties.
	const float MaterialAlpha = FMath::Clamp(PMXMaterial.DiffuseColor.W, 0.0f, 1.0f);
	const bool bNeedsAlphaEvaluation = MaterialAlpha < 0.999f;
	const float AlphaClipValue = bNeedsAlphaEvaluation ? 0.01f : 0.0f;
	// PMX assets often rely on thin shell geometry for hair, face decals and clothing frills.
	// Import as two-sided by default so a winding mismatch on one material slot does not make it invisible.
	Material->TwoSided = true;
	Material->BlendMode = BLEND_Opaque;
	Material->OpacityMaskClipValue = 0.0f;
	Material->bCastDynamicShadowAsMasked = false;
	if (bNeedsAlphaEvaluation)
	{
		Material->BlendMode = BLEND_Translucent;
		Material->OpacityMaskClipValue = AlphaClipValue;
	}
	Material->UpdateCachedExpressionData();

	FAssetRegistryModule::AssetCreated(Material);
	Material->PostEditChange();
	Package->MarkPackageDirty();
	UE_LOG(LogTemp, Log, TEXT("MMD material updated: %s TwoSided=%d BlendMode=%d Alpha=%.3f Texture=%s"),
		*PackageName,
		Material->TwoSided ? 1 : 0,
		static_cast<int32>(Material->BlendMode),
		MaterialAlpha,
		Texture2D ? *Texture2D->GetName() : TEXT("None"));

	return Material;
}

UMaterialInterface* CreateMaterialFromTexture(UTexture2D& Texture2D, const FPMXMaterial& PMXMaterial, const FString& MaterialName, const FString& OutPath) {
	return CreateMaterialFromMMDBase(&Texture2D, PMXMaterial, MaterialName, OutPath);
}

#pragma endregion
#pragma region 顶点
static constexpr float PMX_TO_UE_IMPORT_SCALE = 8.0f;
static constexpr float PMX_IMPORT_OUTLIER_COORD_LIMIT = 10000.0f;

FVector3f ConvertPMXVectorToUnreal(const FVector& PMXVector) {
	return FVector3f(
		PMXVector.X * PMX_TO_UE_IMPORT_SCALE,
		-PMXVector.Z * PMX_TO_UE_IMPORT_SCALE,
		PMXVector.Y * PMX_TO_UE_IMPORT_SCALE);
}
FVector3f ConvertPMXBonePositionToUnreal(const FVector& PMXPosition, float Scale = PMX_TO_UE_IMPORT_SCALE) {
	return FVector3f(
		PMXPosition.X * Scale,
		-PMXPosition.Z * Scale,
		PMXPosition.Y * Scale);
}

static bool IsPMXImportPositionOutlier(const FVector& PMXPosition)
{
	return !FMath::IsFinite(PMXPosition.X)
		|| !FMath::IsFinite(PMXPosition.Y)
		|| !FMath::IsFinite(PMXPosition.Z)
		|| FMath::Abs(PMXPosition.X) > PMX_IMPORT_OUTLIER_COORD_LIMIT
		|| FMath::Abs(PMXPosition.Y) > PMX_IMPORT_OUTLIER_COORD_LIMIT
		|| FMath::Abs(PMXPosition.Z) > PMX_IMPORT_OUTLIER_COORD_LIMIT;
}
static FVector3f ConvertPMXNormalToUnreal(const FVector& PMXNormal)
{
	FVector3f Normal(PMXNormal.X, -PMXNormal.Z, PMXNormal.Y);
	const float LenSq = Normal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector3f(0, 0, 1);
	}
	return Normal * FMath::InvSqrt(LenSq);
}

static FVector3f GetFallbackFaceNormal(const FVector3f& P0, const FVector3f& P1, const FVector3f& P2)
{
	FVector3f FaceNormal = FVector3f::CrossProduct(P2 - P0, P1 - P0);
	const float LenSq = FaceNormal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector3f(0, 0, 1);
	}
	return FaceNormal * FMath::InvSqrt(LenSq);
}

static FVector3f SanitizePMXNormal(const FVector3f& Normal, const FVector3f& FallbackFaceNormal, int32& OutZeroFixedCount, int32& OutFlippedCount)
{
	const float LenSq = Normal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		++OutZeroFixedCount;
		return FallbackFaceNormal;
	}

	const FVector3f UnitNormal = Normal * FMath::InvSqrt(LenSq);
	if (FVector3f::DotProduct(UnitNormal, FallbackFaceNormal) < -0.25f)
	{
		++OutFlippedCount;
		return -UnitNormal;
	}

	return UnitNormal;
}

static FVector3f MakeStableTangentX(const FVector3f& Normal)
{
	const FVector3f Up = FMath::Abs(Normal.Z) < 0.95f ? FVector3f(0.0f, 0.0f, 1.0f) : FVector3f(0.0f, 1.0f, 0.0f);
	FVector3f Tangent = FVector3f::CrossProduct(Up, Normal);
	const float LenSq = Tangent.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector3f(1.0f, 0.0f, 0.0f);
	}
	return Tangent * FMath::InvSqrt(LenSq);
}

static int32 AddPMXWedge(FSkeletalMeshImportData& ImportData, const FPMXVertex& Vertex, int32 VertexIndex)
{
	SkeletalMeshImportData::FVertex Wedge;
	Wedge.VertexIndex = VertexIndex;
	Wedge.UVs[0] = FVector2f(Vertex.UV.X, Vertex.UV.Y);
	for (int32 UVIndex = 0; UVIndex < Vertex.AdditionalUVs.Num() && UVIndex < 7; ++UVIndex)
	{
		if (Vertex.AdditionalUVs.IsValidIndex(UVIndex))
		{
			Wedge.UVs[UVIndex + 1] = FVector2f(Vertex.AdditionalUVs[UVIndex].X, Vertex.AdditionalUVs[UVIndex].Y);
		}
	}
	Wedge.Color = FColor::White;
	return ImportData.Wedges.Add(Wedge);
}
#pragma endregion


void LoadPMXImportData(FSkeletalMeshImportData& PMXImportData, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
	FString PMXPath = FPaths::GetPath(PMXFilePath);
	FString PMXModelName = FPaths::GetBaseFilename(PMXFilePath);
	PMXImportData.bHasNormals = true;
	PMXImportData.bHasTangents = true;
	PMXImportData.bHasVertexColors = false;
	PMXImportData.NumTexCoords = 1 + PMXInfo.PMXGlobals.ExtraUV; // 主UV加上额外的UV
	PMXImportData.MaxMaterialIndex = PMXInfo.ModelMaterialCount;

#pragma region 材质
	PMXImportData.Materials.Reserve(PMXInfo.ModelMaterials.Num());

	// 确保至少有一个默认材质
	if (PMXInfo.ModelMaterials.Num() == 0) {
		SkeletalMeshImportData::FMaterial DefaultMaterial;
		DefaultMaterial.MaterialImportName = TEXT("DefaultMaterial");
		DefaultMaterial.Material = nullptr; // 使用引擎默认材质
		PMXImportData.Materials.Add(DefaultMaterial);
		UE_LOG(LogTemp, Warning, TEXT("没有材质，创建默认材质"));
	}
	else {
		for (int32 MaterialIndex = 0; MaterialIndex < PMXInfo.ModelMaterials.Num(); ++MaterialIndex) {
			const auto& Material = PMXInfo.ModelMaterials[MaterialIndex];
			SkeletalMeshImportData::FMaterial MaterialData;

			// 清理材质名称
			FString CleanMaterialName = Material.NameEN.IsEmpty() ? Material.NameJP : Material.NameEN;
			CleanMaterialName = FixMMDName(CleanMaterialName, TEXT("M_"));

			MaterialData.MaterialImportName = CleanMaterialName;
			MaterialData.Material = nullptr;

			FString TexturePath = GetMaterialTexturePath(Material, PMXInfo, PMXPath);
			UTexture2D* Texture = nullptr;

			// 只有当纹理存在时才创建材质
			if (!TexturePath.IsEmpty() && FPaths::FileExists(TexturePath)) {
				FString CleanFileName = FPaths::GetCleanFilename(TexturePath);
				// 也要清理文件名
				CleanFileName = CleanFileName.Replace(TEXT(" "), TEXT("_"));
				CleanFileName = CleanFileName.Replace(TEXT("-"), TEXT("_"));

				Texture = CreateTextureFromFile(TexturePath,
					FString("/Game/MMDModels/") + PMXModelName + FString("/Textures"),
					CleanFileName);
			}

			MaterialData.Material = CreateMaterialFromMMDBase(Texture,
				Material,
				CleanMaterialName,
				FString("/Game/MMDModels/") + PMXModelName + FString("/Materials"));

			PMXImportData.Materials.Add(MaterialData);
		}
	}

	PMXImportData.MaxMaterialIndex = FMath::Max(0, PMXImportData.Materials.Num() - 1);

	UE_LOG(LogTemp, Warning, TEXT("材质处理完成，共 %d 个材质"), PMXImportData.Materials.Num());
#pragma endregion

#pragma region Wedges

	PMXImportData.Wedges.Reserve(PMXInfo.ModelIndicesCount);
#pragma endregion

#pragma region 顶点Points
	PMXImportData.Points.Reserve(PMXInfo.ModelVertices.Num());

	for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		PMXImportData.Points.Add(ConvertPMXVectorToUnreal(Vertex.Position));
	}
#pragma endregion

#pragma region 面
	PMXImportData.Faces.Reserve(PMXInfo.ModelIndicesCount / 3);
	int32 BaseIndex = 0;
	int32 ZeroNormalCount = 0;
	int32 FlippedNormalCount = 0;
	int32 OutlierTriangleCount = 0;
	TArray<int32> MaterialImportedTriangleCounts;
	TArray<int32> MaterialDegenerateTriangleCounts;
	TArray<int32> MaterialInvalidIndexTriangleCounts;
	TArray<int32> MaterialOutlierTriangleCounts;
	MaterialImportedTriangleCounts.SetNumZeroed(PMXInfo.ModelMaterials.Num());
	MaterialDegenerateTriangleCounts.SetNumZeroed(PMXInfo.ModelMaterials.Num());
	MaterialInvalidIndexTriangleCounts.SetNumZeroed(PMXInfo.ModelMaterials.Num());
	MaterialOutlierTriangleCounts.SetNumZeroed(PMXInfo.ModelMaterials.Num());
	for (int32 MatIndex = 0; MatIndex < PMXInfo.ModelMaterials.Num(); MatIndex++)
	{
		const FPMXMaterial& Material = PMXInfo.ModelMaterials[MatIndex];
		int32 FaceIndexCount = Material.FaceIndexCount;
		int32 TriangleCount = FaceIndexCount / 3;

		for (int32 f = 0; f < TriangleCount; f++)
		{
			int32 w0 = BaseIndex + f * 3 + 0;
			int32 w1 = BaseIndex + f * 3 + 1;
			int32 w2 = BaseIndex + f * 3 + 2;

			int32 vi0 = PMXInfo.ModelIndices[w0];
			int32 vi1 = PMXInfo.ModelIndices[w1];
			int32 vi2 = PMXInfo.ModelIndices[w2];

			// 退化剔除
			if (vi0 == vi1 || vi1 == vi2 || vi0 == vi2)
			{
				++MaterialDegenerateTriangleCounts[MatIndex];
				continue;
			}
			if (vi0 < 0 || vi1 < 0 || vi2 < 0 ||
				vi0 >= PMXInfo.ModelVertices.Num() ||
				vi1 >= PMXInfo.ModelVertices.Num() ||
				vi2 >= PMXInfo.ModelVertices.Num())
			{
				++MaterialInvalidIndexTriangleCounts[MatIndex];
				continue;
			}

			if (IsPMXImportPositionOutlier(PMXInfo.ModelVertices[vi0].Position)
				|| IsPMXImportPositionOutlier(PMXInfo.ModelVertices[vi1].Position)
				|| IsPMXImportPositionOutlier(PMXInfo.ModelVertices[vi2].Position))
			{
				++OutlierTriangleCount;
				++MaterialOutlierTriangleCounts[MatIndex];
				continue;
			}

			SkeletalMeshImportData::FTriangle Tri;
			Tri.WedgeIndex[0] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi0], vi0);
			Tri.WedgeIndex[1] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi2], vi2);
			Tri.WedgeIndex[2] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi1], vi1);
			Tri.MatIndex = MatIndex;
			Tri.AuxMatIndex = 0;
			Tri.SmoothingGroups = 0;

			const FVector& N0 = PMXInfo.ModelVertices[vi0].Normal;
			const FVector& N1 = PMXInfo.ModelVertices[vi1].Normal;
			const FVector& N2 = PMXInfo.ModelVertices[vi2].Normal;
			const FVector3f P0 = PMXImportData.Points[vi0];
			const FVector3f P1 = PMXImportData.Points[vi1];
			const FVector3f P2 = PMXImportData.Points[vi2];
			const FVector3f FaceNormal = GetFallbackFaceNormal(P0, P2, P1);

			Tri.TangentZ[0] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N0), FaceNormal, ZeroNormalCount, FlippedNormalCount);
			Tri.TangentZ[1] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N2), FaceNormal, ZeroNormalCount, FlippedNormalCount);
			Tri.TangentZ[2] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N1), FaceNormal, ZeroNormalCount, FlippedNormalCount);
			Tri.TangentX[0] = MakeStableTangentX(Tri.TangentZ[0]);
			Tri.TangentX[1] = MakeStableTangentX(Tri.TangentZ[1]);
			Tri.TangentX[2] = MakeStableTangentX(Tri.TangentZ[2]);
			Tri.TangentY[0] = FVector3f::CrossProduct(Tri.TangentZ[0], Tri.TangentX[0]).GetSafeNormal();
			Tri.TangentY[1] = FVector3f::CrossProduct(Tri.TangentZ[1], Tri.TangentX[1]).GetSafeNormal();
			Tri.TangentY[2] = FVector3f::CrossProduct(Tri.TangentZ[2], Tri.TangentX[2]).GetSafeNormal();

			PMXImportData.Faces.Add(Tri);
			++MaterialImportedTriangleCounts[MatIndex];
		}
		BaseIndex += FaceIndexCount;
	}

	PMXImportData.ComputeSmoothGroupFromNormals();
	UE_LOG(LogTemp, Log, TEXT("Original PMX normals applied. Triangles=%d Wedges=%d ZeroFixed=%d Flipped=%d OutlierTrianglesSkipped=%d"),
		PMXImportData.Faces.Num(), PMXImportData.Wedges.Num(), ZeroNormalCount, FlippedNormalCount, OutlierTriangleCount);
	for (int32 MatIndex = 0; MatIndex < PMXInfo.ModelMaterials.Num(); ++MatIndex)
	{
		const FPMXMaterial& Material = PMXInfo.ModelMaterials[MatIndex];
		const FString MaterialName = Material.NameJP.IsEmpty() ? Material.NameEN : Material.NameJP;
		UE_LOG(LogTemp, Warning,
			TEXT("MMD_IMPORT_MATERIAL Mat=%d Name='%s' RawTriangles=%d ImportedTriangles=%d DegenerateSkipped=%d InvalidIndexSkipped=%d OutlierSkipped=%d"),
			MatIndex,
			*MaterialName,
			Material.FaceIndexCount / 3,
			MaterialImportedTriangleCounts.IsValidIndex(MatIndex) ? MaterialImportedTriangleCounts[MatIndex] : -1,
			MaterialDegenerateTriangleCounts.IsValidIndex(MatIndex) ? MaterialDegenerateTriangleCounts[MatIndex] : -1,
			MaterialInvalidIndexTriangleCounts.IsValidIndex(MatIndex) ? MaterialInvalidIndexTriangleCounts[MatIndex] : -1,
			MaterialOutlierTriangleCounts.IsValidIndex(MatIndex) ? MaterialOutlierTriangleCounts[MatIndex] : -1);
	}
#pragma endregion

#pragma region 骨骼Bone
	// 建立一个集合防止骨骼重名
	TSet<FString> BoneNameSet;
	{
		SkeletalMeshImportData::FBone Root;
		Root.Name = TEXT("Root");
		Root.ParentIndex = INDEX_NONE;
		Root.NumChildren = 0;
		Root.BonePos.Transform = FTransform3f::Identity;
		Root.BonePos.Length = Root.BonePos.XSize = Root.BonePos.YSize = 1;
		PMXImportData.RefBonesBinary.Add(Root);
		BoneNameSet.Add(Root.Name);
	}

	for (int32 i = 0; i < PMXInfo.ModelBoneCount; ++i) {
		const FPMXBone& Bone = PMXInfo.ModelBones[i];
		SkeletalMeshImportData::FBone NewBone;

		;

		// 确保名字唯一
		int32 Suffix = 1;
		FString UniqueName = Bone.NameJP;
		while (BoneNameSet.Contains(UniqueName)) {
			UniqueName = Bone.NameJP + FString::Printf(TEXT("_%d"), Suffix++);
		}
		BoneNameSet.Add(UniqueName);

		NewBone.Name = UniqueName;
		int32 Parent = Bone.ParentBoneIndex;
		if (Parent >= 0 && Parent < PMXInfo.ModelBoneCount && Parent != i) {
			NewBone.ParentIndex = Parent + 1; // +1 因为Root
		}
		else {
			NewBone.ParentIndex = 0; // 默认挂在Root
		}
		FVector3f BoneGlobalPos = ConvertPMXBonePositionToUnreal(Bone.Position);

		// 计算相对父骨骼的位置（local）
		FVector3f BoneLocalPos = BoneGlobalPos;
		if (Bone.ParentBoneIndex >= 0 && Bone.ParentBoneIndex < PMXInfo.ModelBoneCount) {
			const FPMXBone& ParentPMXBone = PMXInfo.ModelBones[Bone.ParentBoneIndex];
			FVector3f ParentGlobalPos = ConvertPMXBonePositionToUnreal(ParentPMXBone.Position);
			BoneLocalPos = BoneGlobalPos - ParentGlobalPos;
		}
		NewBone.BonePos.Transform = FTransform3f(FQuat4f::Identity, BoneLocalPos);
		NewBone.BonePos.Length = NewBone.BonePos.XSize = NewBone.BonePos.YSize = 1;
		PMXImportData.RefBonesBinary.Add(NewBone);
	}
#pragma endregion

#pragma region 骨骼权重RawBoneInfluence
	TArray<TArray<TPair<int32, float>>> VertexInfluences;
	VertexInfluences.SetNum(PMXInfo.ModelVertexCount);

	for (int32 i = 0; i < PMXInfo.ModelVertexCount; ++i) {
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		const FPMXVertexWeight& Weight = Vertex.Weight;

		switch (Weight.WeightDeformType)
		{
		case 0: // BDEF1
			if (Weight.BoneIndices[0] >= 0) {
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, 1.0f));
			}
			break;
		case 1: // BDEF2
		case 3: // SDEF (basically two bones)
		{
			if (Weight.BoneIndices[0] >= 0) {
				float w0 = Weight.Weights[0];
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, w0));
			}
			if (Weight.BoneIndices[1] >= 0) {
				float w1 = 1.0f - Weight.Weights[0];
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[1] + 1, w1));
			}
		}
		break;
		case 2: // BDEF4
		case 4: // QDEF
			for (int j = 0; j < 4; ++j) {
				if (Weight.BoneIndices[j] >= 0 && Weight.Weights[j] > 0.0f) {
					VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[j] + 1, Weight.Weights[j]));
				}
			}
			break;
		default:
			// 绑定到 Root（0）
			VertexInfluences[i].Add(TPair<int32, float>(0, 1.0f));
			break;
		}
	}

	// 归一化并写入 PMXImportData.Influences
	for (int32 i = 0; i < VertexInfluences.Num(); ++i) {
		float Sum = 0.0f;
		for (auto& P : VertexInfluences[i]) Sum += P.Value;
		if (Sum <= 0.0f) {
			// 保底绑定到 Root
			SkeletalMeshImportData::FRawBoneInfluence Inf;
			Inf.VertexIndex = i;
			Inf.BoneIndex = 0;
			Inf.Weight = 1.0f;
			PMXImportData.Influences.Add(Inf);
			continue;
		}
		// 写入并归一化
		for (auto& P : VertexInfluences[i]) {
			SkeletalMeshImportData::FRawBoneInfluence Inf;
			Inf.VertexIndex = i;              // 顶点索引（对应 PMXImportData.Points 的索引）
			Inf.BoneIndex = P.Key;            // 已经 +1 以对应 Root 在前的 RefBonesBinary
			Inf.Weight = P.Value / Sum;
			if (Inf.Weight > KINDA_SMALL_NUMBER) {
				PMXImportData.Influences.Add(Inf);
			}
		}
	}
#pragma endregion

#pragma region 顶点映射
	PMXImportData.PointToRawMap.Reserve(PMXInfo.ModelVertices.Num());
	for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
		PMXImportData.PointToRawMap.Add(i);
	}
#pragma endregion



#pragma region MeshInfo
	SkeletalMeshImportData::FMeshInfo MeshInfo;
	MeshInfo.Name = FName(*PMXModelName);
	MeshInfo.NumVertices = PMXInfo.ModelVertices.Num();
	MeshInfo.StartImportedVertex = 0;
	PMXImportData.MeshInfos.Add(MeshInfo);
#pragma endregion

	UE_LOG(LogTemp, Warning, TEXT("LoadPMXImportData 完成: 顶点=%d, 面=%d, 骨骼=%d, Influences=%d"),
		PMXImportData.Points.Num(), PMXImportData.Faces.Num(),
		PMXImportData.RefBonesBinary.Num(), PMXImportData.Influences.Num());
}


USkeletalMesh* TMMDMeshBuilder::BuildSkeletalMeshFromPMX(const PMXDatas& PMXInfo, const FString& PackagePath, const FString& AssetName, const FString& PMXFilePath)
{
	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	const FString PreferredAssetName = AssetName.IsEmpty() ? FPaths::GetBaseFilename(PMXFilePath) : AssetName;
	FString CleanAssetName = FixMMDName(PreferredAssetName);

	UE_LOG(LogTemp, Warning, TEXT("=== 开始构建骨骼网格：%s -> %s ==="), *AssetName, *CleanAssetName);

	FString BasePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/");
	FString ModelPath = BasePath + TEXT("Model/");
	FString SkeletonPath = BasePath + TEXT("Skeleton/");

	FString PackageName = ModelPath + CleanAssetName;
	UPackage* Package = CreatePackage(*PackageName);
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, *CleanAssetName, RF_Public | RF_Standalone);

	FSkeletalMeshImportData PMXImportData;
	LoadPMXImportData(PMXImportData, PMXInfo, PMXFilePath);

	if (PMXImportData.Points.Num() == 0 || PMXImportData.Faces.Num() == 0) {
		UE_LOG(LogTemp, Error, TEXT("导入数据无效"));
		return nullptr;
	}

	FString SkeletonName = CleanAssetName + TEXT("_Skeleton");
	FString SkeletonPackageName = SkeletonPath + SkeletonName;
	UPackage* SkeletonPackage = CreatePackage(*SkeletonPackageName);
	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, *SkeletonName, RF_Public | RF_Standalone);

	int32 SkeletalDepth = 0;
	FReferenceSkeleton RefSkeleton;

	SkeletalMeshImportUtils::ProcessImportMeshInfluences(PMXImportData, CleanAssetName);
	SkeletalMeshImportUtils::ProcessImportMeshMaterials(SkeletalMesh->GetMaterials(), PMXImportData);
	SkeletalMeshImportUtils::ProcessImportMeshSkeleton(Skeleton, RefSkeleton, SkeletalDepth, PMXImportData);

	SkeletalMesh->SetRefSkeleton(RefSkeleton);

	if (RefSkeleton.GetNum() > 0) {
		Skeleton->RecreateBoneTree(SkeletalMesh);
	}

	if (SkeletalMesh->GetLODNum() == 0)
	{
		FSkeletalMeshLODInfo LODInfo;
		LODInfo.ScreenSize.Default = 1.0f;
		LODInfo.LODHysteresis = 0.02f;

		LODInfo.BuildSettings.bRecomputeNormals = false;
		LODInfo.BuildSettings.bRecomputeTangents = false;
		LODInfo.BuildSettings.bUseMikkTSpace = false;
		LODInfo.BuildSettings.bComputeWeightedNormals = false;
		LODInfo.BuildSettings.bRemoveDegenerates = true;
		LODInfo.BuildSettings.bUseFullPrecisionUVs = false;
		LODInfo.BuildSettings.bUseHighPrecisionTangentBasis = false;

		LODInfo.bAllowCPUAccess = true;
		LODInfo.bSupportUniformlyDistributedSampling = false;

		SkeletalMesh->AddLODInfo(LODInfo);
		UE_LOG(LogTemp, Warning, TEXT("LODInfo 创建完成"));
	}

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (!ImportedModel) {
		SkeletalMesh->AllocateResourceForRendering();
		ImportedModel = SkeletalMesh->GetImportedModel();
	}

	if (ImportedModel->LODModels.Num() == 0) {
		ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
	}

	FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];

	// 使用 MeshUtilities 构建骨骼网格
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

	IMeshUtilities::MeshBuildOptions BuildOptions;
	BuildOptions.bComputeNormals = false;
	BuildOptions.bComputeTangents = false;
	BuildOptions.bUseMikkTSpace = false;
	BuildOptions.bComputeWeightedNormals = false;
	BuildOptions.bRemoveDegenerateTriangles = true;

	TArray<FVector3f> LODPoints;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
	TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
	TArray<int32> LODPointToRawMap;

	PMXImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

	UE_LOG(LogTemp, Warning, TEXT("LOD数据: 顶点=%d, 楔形点=%d, 面=%d, 影响=%d"),
		LODPoints.Num(), LODWedges.Num(), LODFaces.Num(), LODInfluences.Num());

	bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(
		LODModel,
		CleanAssetName,
		RefSkeleton,
		LODInfluences,
		LODWedges,
		LODFaces,
		LODPoints,
		LODPointToRawMap,
		BuildOptions
	);

	if (!bBuildSuccess) {
		UE_LOG(LogTemp, Error, TEXT("骨骼网格构建失败"));
		return nullptr;
	}

	for (FSkelMeshSection& Section : LODModel.Sections)
	{
		const FString MaterialName = PMXInfo.ModelMaterials.IsValidIndex(Section.MaterialIndex)
			? (PMXInfo.ModelMaterials[Section.MaterialIndex].NameJP.IsEmpty() ? PMXInfo.ModelMaterials[Section.MaterialIndex].NameEN : PMXInfo.ModelMaterials[Section.MaterialIndex].NameJP)
			: FString(TEXT("Invalid"));
		UE_LOG(LogTemp, Warning,
			TEXT("MMD_UE_SECTION SectionMaterialIndex=%d MaterialName='%s' Triangles=%u Vertices=%d BaseIndex=%u BaseVertex=%u"),
			Section.MaterialIndex,
			*MaterialName,
			Section.NumTriangles,
			Section.GetNumVertices(),
			Section.BaseIndex,
			Section.BaseVertexIndex);

		if (PMXInfo.ModelMaterials.IsValidIndex(Section.MaterialIndex))
		{
			const FPMXMaterial& PMXMaterial = PMXInfo.ModelMaterials[Section.MaterialIndex];
			Section.bCastShadow = (PMXMaterial.DrawFlags & PMX_DRAW_CAST_SHADOW) != 0;
		}
	}

	LODModel.NumTexCoords = FMath::Max<uint32>(1, PMXImportData.NumTexCoords);


	SkeletalMesh->SetSkeleton(Skeleton);
	Skeleton->SetPreviewMesh(SkeletalMesh);

	SkeletalMesh->CalculateInvRefMatrices();

	const int32 ImportedMorphTargets = BuildMorphTargetsFromPMX(SkeletalMesh, PMXInfo);
	if (ImportedMorphTargets > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SkeletalMesh morph targets registered: %d"), ImportedMorphTargets);
	}

	UMMDModelDataAsset* ModelDataAsset = CreateMMDModelDataAssetForMesh(SkeletalMesh, PMXInfo, RefSkeleton, BasePath, CleanAssetName, PMXFilePath);
	if (ModelDataAsset != nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("MMDModelData asset created: %s | Bones=%d"), *ModelDataAsset->GetPathName(), ModelDataAsset->Bones.Num());
	}

	const TArray<FMatrix44f>& RefBasesInvMatrix = SkeletalMesh->GetRefBasesInvMatrix();
	UE_LOG(LogTemp, Warning, TEXT("RefBasesInvMatrix 数量: %d"), RefBasesInvMatrix.Num());

	if (RefBasesInvMatrix.Num() == 0) {
		UE_LOG(LogTemp, Error, TEXT("RefBasesInvMatrix 未正确初始化！"));
	}

	if (PMXImportData.Faces.Num() > 0) {
		FBox BoundingBox(ForceInit);
		for (const SkeletalMeshImportData::FTriangle& Face : PMXImportData.Faces) {
			for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
			{
				const int32 WedgeIndex = Face.WedgeIndex[CornerIndex];
				if (!PMXImportData.Wedges.IsValidIndex(WedgeIndex))
				{
					continue;
				}

				const int32 PointIndex = PMXImportData.Wedges[WedgeIndex].VertexIndex;
				if (PMXImportData.Points.IsValidIndex(PointIndex))
				{
					BoundingBox += FVector(PMXImportData.Points[PointIndex]);
				}
			}
		}
		FBoxSphereBounds ActualBounds(BoundingBox);
		SkeletalMesh->SetImportedBounds(ActualBounds);
	}
	else {
		FBoxSphereBounds DefaultBounds(FBox(FVector(-100, -100, -100), FVector(100, 100, 100)));
		SkeletalMesh->SetImportedBounds(DefaultBounds);
	}

	// 完成构建和初始化
	SkeletalMesh->PostEditChange();
	SkeletalMesh->MarkPackageDirty();

	// 注册资源
	FAssetRegistryModule::AssetCreated(SkeletalMesh);
	FAssetRegistryModule::AssetCreated(Skeleton);
	SkeletonPackage->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("骨骼网格创建成功: %s"), *PackageName);

	return SkeletalMesh;
}

UIKRigDefinition* TMMDMeshBuilder::BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
#if WITH_EDITOR
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}
	auto FindMMDBone = [SkeletalMesh](const TArray<FString>& PossibleNames) -> FName
		{
			const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
			for (const FString& BoneName : PossibleNames)
			{
				FName FoundName = FName(*BoneName);
				int32 BoneIndex = RefSkeleton.FindBoneIndex(FoundName);
				if (BoneIndex != INDEX_NONE)
				{
					UE_LOG(LogTemp, Log, TEXT("Found bone: %s"), *BoneName);
					return FoundName;
				}
			}
			return NAME_None;
		};
	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString IKRigPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString IKRigName = PMXModelName + TEXT("_IKRig");

	FString UniquePackageName, UniqueAssetName; {
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(IKRigPath + TEXT("/") + IKRigName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(
		Package,
		UIKRigDefinition::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);
	if (!IKRig)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRigDefinition"));
		return nullptr;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRigController"));
		return nullptr;
	}

	Controller->SetSkeletalMesh(SkeletalMesh);
	UE_LOG(LogTemp, Log, TEXT("Set SkeletalMesh for IKRig: %s"), *SkeletalMesh->GetName());

	FName RetargetRootBone = FindMMDBone({
			TEXT("腰"),      
			TEXT("Waist"),
			TEXT("センター"), 
			TEXT("Center"),
			TEXT("下半身"),    
			TEXT("LowerBody"),
			TEXT("Hips")
		});
	if (RetargetRootBone != NAME_None)
	{
		Controller->SetRetargetRoot(RetargetRootBone);
		UE_LOG(LogTemp, Log, TEXT("✅ Set Retarget Root: %s"), *RetargetRootBone.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find Retarget Root bone"));
	}
	// 添加重定向链
	{
		struct FMMDRetargetChain
		{
			FString ChainName;
			TArray<FString> StartBoneNames;
			TArray<FString> EndBoneNames;
		};
		TArray<FMMDRetargetChain> Chains = {
			{ TEXT("Root"),        { TEXT("Root") }, { TEXT("Root") } },

			{ TEXT("Spine"),       { TEXT("上半身") }, { TEXT("上半身2") } },

			{ TEXT("Neck"),        { TEXT("首") }, { TEXT("首") } },

			{ TEXT("Head"),        { TEXT("頭") }, { TEXT("頭") } },

			{ TEXT("RightArm"),    { TEXT("右腕") }, { TEXT("右手首") } },

			// ✅ 左腕、左手首（日文）- 注意截图是"左肩"开始
			{ TEXT("LeftArm"),     { TEXT("左腕") }, { TEXT("左手首") } },

			// ✅ 右足、右足先（日文）- EX是英文
			{ TEXT("RightLeg"),    { TEXT("右足D") }, { TEXT("右足先EX") } },

			// ✅ 左足、左足先（日文）
			{ TEXT("LeftLeg"),     { TEXT("左足D") }, { TEXT("左足先EX") } },

			// ✅ 手指（日文）- 親指、人指、中指、薬指、小指
			{ TEXT("LeftThumb"),   { TEXT("左親指０") }, { TEXT("左親指２") } },
			{ TEXT("LeftIndex"),   { TEXT("左人指１") }, { TEXT("左人指３") } },
			{ TEXT("LeftMiddle"),  { TEXT("左中指１") }, { TEXT("左中指３") } },
			{ TEXT("LeftRing"),    { TEXT("左薬指１") }, { TEXT("左薬指３") } },
			{ TEXT("LeftPinky"),   { TEXT("左小指１") }, { TEXT("左小指３") } },

			{ TEXT("RightThumb"),  { TEXT("右親指０") }, { TEXT("右親指２") } },
			{ TEXT("RightIndex"),  { TEXT("右人指１") }, { TEXT("右人指３") } },
			{ TEXT("RightMiddle"), { TEXT("右中指１") }, { TEXT("右中指３") } },
			{ TEXT("RightRing"),   { TEXT("右薬指１") }, { TEXT("右薬指３") } },
			{ TEXT("RightPinky"),  { TEXT("右小指１") }, { TEXT("右小指３") } },

			// ✅ 肩（日文）
			{ TEXT("LeftClavicle"), { TEXT("左肩") }, { TEXT("左肩") } },
			{ TEXT("RightClavicle"),{ TEXT("右肩") }, { TEXT("右肩") } }
		};

		int32 ChainCount = 0;
		for (const FMMDRetargetChain& Chain : Chains) {
			FName StartBone = FindMMDBone(Chain.StartBoneNames);
			FName EndBone = FindMMDBone(Chain.EndBoneNames);

			if (StartBone != NAME_None && EndBone != NAME_None)
			{
				FName ChainFName = FName(*Chain.ChainName);

				Controller->AddRetargetChain(ChainFName, StartBone, EndBone, NAME_None);
				ChainCount++;

				UE_LOG(LogTemp, Log, TEXT("✅ Added Retarget Chain: %s (%s -> %s)"),
					*Chain.ChainName, *StartBone.ToString(), *EndBone.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bones for chain: %s"), *Chain.ChainName);
			}
		}
	}
	// 添加IK目标
	{
		struct FMMDIKGoal
		{
			FString GoalName;
			TArray<FString> BoneNames;
		};
		TArray<FMMDIKGoal> IKGoals = {
			// 左脚IK
			{
				TEXT("LeftFoot_IK"),
				{TEXT("左足首"), TEXT("LeftAnkle"), TEXT("LeftFoot")}
			},
			// 右脚IK
			{
				TEXT("RightFoot_IK"),
				{TEXT("右足首"), TEXT("RightAnkle"), TEXT("RightFoot")}
			},
			// 左手IK
			{
				TEXT("LeftHand_IK"),
				{TEXT("左手首"), TEXT("LeftWrist"), TEXT("LeftHand")}
			},
			// 右手IK
			{
				TEXT("RightHand_IK"),
				{TEXT("右手首"), TEXT("RightWrist"), TEXT("RightHand")}
			}
		};
		int32 GoalCount = 0;
		for (const FMMDIKGoal& Goal : IKGoals)
		{
			FName GoalBone = FindMMDBone(Goal.BoneNames);

			if (GoalBone != NAME_None)
			{
				FName GoalName = FName(*Goal.GoalName);

				Controller->AddNewGoal(GoalName, GoalBone);
				GoalCount++;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bone for goal: %s"), *Goal.GoalName);
			}
		}
	}
	// 创建FBIK求解器
	{
		int32 SolverIndex = Controller->AddSolver(UIKRigFBIKSolver::StaticClass());

		if (SolverIndex != INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("✅ Added FBIK Solver at index: %d"), SolverIndex);

			// 连接Goals到求解器
			TArray<FName> GoalNames = {
				TEXT("LeftFoot_IK"),
				TEXT("RightFoot_IK"),
				TEXT("LeftHand_IK"),
				TEXT("RightHand_IK")
			};

			int32 ConnectedCount = 0;
			for (const FName& GoalName : GoalNames)
			{
				bool bConnected = Controller->ConnectGoalToSolver(GoalName, SolverIndex);

				if (bConnected)
				{
					ConnectedCount++;
					UE_LOG(LogTemp, Log, TEXT("✅ Connected Goal to Solver: %s"), *GoalName.ToString());
				}
			}

			UE_LOG(LogTemp, Log, TEXT("📊 Connected %d/%d Goals to Solver"), ConnectedCount, GoalNames.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Failed to add FBIK Solver"));
		}
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(IKRig);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, IKRig, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully saved IKRig: %s"), *FilePath);
		UE_LOG(LogTemp, Log, TEXT("IKRig created with MMD bone mapping"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save IKRig: %s"), *FilePath);
	}

	return IKRig;

#else
	UE_LOG(LogTemp, Error, TEXT("IKRig can only be created in the editor."));
	return nullptr;
#endif
}

UAnimBlueprint* TMMDMeshBuilder::BuildAnimBlueprint(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString AnimBPPackagePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString AnimBPName = PMXModelName + TEXT("_AnimBP");

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton) {
		UE_LOG(LogTemp, Error, TEXT("TargetMesh has no skeleton"));
		return nullptr;
	}

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(AnimBPPackagePath + TEXT("/") + AnimBPName, TEXT(""), UniquePackageName, UniqueAssetName);
	}
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	//Factory->ParentClass = UMMDAnimInstance::StaticClass();
	Factory->PreviewSkeletalMesh = SkeletalMesh;

	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(),
		Package,
		*UniqueAssetName,
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewAnimBP) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create AnimBlueprint"));
		return nullptr;
	}
	//if (NewAnimBP->ParentClass != UMMDAnimInstance::StaticClass()) {
	//	NewAnimBP->ParentClass = UMMDAnimInstance::StaticClass();
	//}
	FKismetEditorUtilities::CompileBlueprint(NewAnimBP);

	// Set PMX source path on AnimInstance CDO for preview auto physics rebuild
    //if (NewAnimBP->GeneratedClass)
    //{
    //    //if (UMMDAnimInstance* AnimCDO = Cast<UMMDAnimInstance>(NewAnimBP->GeneratedClass->GetDefaultObject()))
    //    //{
    //    //    AnimCDO->SetSourcePMXFilePath(PMXFilePath);
    //    //    AnimCDO->Modify();
    //    //    UE_LOG(LogTemp, Verbose, TEXT("[TMMDMeshBuilder] Set SourcePMXFilePath on AnimInstance CDO: %s"), *PMXFilePath);
    //    //}
    //}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAnimBP);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs)) {
		UE_LOG(LogTemp, Log, TEXT("Successfully created and saved AnimBlueprint: %s"), *FilePath);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("AnimBlueprint created but failed to save: %s"), *FilePath);
	}

	return NewAnimBP;
}
#if WITH_EDITOR
UIKRetargeter* TMMDMeshBuilder::BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath)
{
	if (!IKRigTarget) {
		UE_LOG(LogTemp, Error, TEXT("IKRigTarget is null"));
		return nullptr;
	}

	FString MannequinIKRigPath = FString("/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin");

	UIKRigDefinition* IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *MannequinIKRigPath);
	if (!IKRigSource)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load Mannequin IKRig from: %s"), *MannequinIKRigPath);
		UE_LOG(LogTemp, Warning, TEXT("Trying alternative paths..."));

		TArray<FString> AlternativePaths = {
			TEXT("/Game/Characters/Mannequins/Rigs/IK_Mannequin"),
			TEXT("/Engine/Characters/Mannequin/Rigs/IK_Mannequin"),
			TEXT("/Script/Engine.IKRigDefinition'/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin'")
		};

		for (const FString& AltPath : AlternativePaths)
		{
			IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *AltPath);
			if (IKRigSource)
			{
				UE_LOG(LogTemp, Log, TEXT("Loaded Mannequin IKRig from: %s"), *AltPath);
				break;
			}
		}

		if (!IKRigSource)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load Mannequin IKRig. Please check the path."));
			return nullptr;
		}
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString RetargeterPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString RetargeterName = PMXModelName + TEXT("_RTG_FromMannequin");

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(RetargeterPath + TEXT("/") + RetargeterName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if(!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRetargeter* Retargeter = NewObject<UIKRetargeter>(
		Package,
		UIKRetargeter::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);

	if(!Retargeter)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRetargeter"));
		return nullptr;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRetargeterController"));
		return nullptr;
	}

	Controller->SetIKRig(ERetargetSourceOrTarget::Source, IKRigSource);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, IKRigTarget);
	UE_LOG(LogTemp, Log, TEXT("✅ Set Source IKRig: %s"), *IKRigSource->GetName());
	UE_LOG(LogTemp, Log, TEXT("✅ Set Target IKRig: %s"), *IKRigTarget->GetName());

	Retargeter->PostEditChange();

	// 自动映射骨骼链
	Controller->AutoMapChains(EAutoMapChainType::Fuzzy,true);
	UE_LOG(LogTemp, Log, TEXT("✅ Auto-mapped chains"));
	{
		FTargetChainSettings RootSettings = Controller->GetRetargetChainSettings(TEXT("Root"));

		RootSettings.FK.TranslationMode = ERetargetTranslationMode::GloballyScaled;
		RootSettings.FK.RotationMode = ERetargetRotationMode::Interpolated;

		Controller->SetRetargetChainSettings(TEXT("Root"), RootSettings);

		UE_LOG(LogTemp, Log, TEXT("✅ Root chain: TranslationMode=GloballyScaled"));
	}
	Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Retargeter);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, Retargeter, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("✅ Successfully saved IKRetargeter!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("📁 Path: %s"), *UniquePackageName);
		UE_LOG(LogTemp, Log, TEXT("📥 Source: UE5 Mannequin (IK_Mannequin)"));
		UE_LOG(LogTemp, Log, TEXT("📤 Target: %s"), *IKRigTarget->GetName());
		UE_LOG(LogTemp, Log, TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("🎉 You can now retarget Mannequin animations to your MMD model!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ Failed to save IKRetargeter: %s"), *FilePath);
	}

	return Retargeter;
}

bool TMMDMeshBuilder::BuildAnimationImportContext(USkeletalMesh* SkeletalMesh, const PMXDatas* PMXData, const FString& PMXFilePath, const FString& VMDFilePath, FMMDAnimationImportContext& OutContext, FMMDAnimationImportReport* OutReport)
{
	OutContext = FMMDAnimationImportContext{};
	OutContext.SkeletalMesh = SkeletalMesh;
	OutContext.PMXData = PMXData;
	OutContext.ModelDataAsset = FindMMDModelDataAsset(SkeletalMesh);
	OutContext.SourcePMXFilePath = PMXFilePath;
	OutContext.SourceVMDFilePath = VMDFilePath;
	OutContext.Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;

	if (SkeletalMesh == nullptr)
	{
		if (OutReport)
		{
			AppendUniqueMessage(OutReport->Errors, TEXT("Animation import requires a valid SkeletalMesh."));
		}
		return false;
	}

	if (OutContext.Skeleton == nullptr)
	{
		if (OutReport)
		{
			AppendUniqueMessage(OutReport->Errors, TEXT("Target SkeletalMesh does not have a USkeleton."));
		}
		return false;
	}

	return true;
}

bool TMMDMeshBuilder::AnalyzeVMDAnimationImport(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport& OutReport)
{
	OutReport = FMMDAnimationImportReport{};
	OutReport.PackagePath = BuildMMDAnimationFolderPath(Context);
	OutReport.AssetName = FixAnimAssetName(FPaths::GetBaseFilename(Context.SourceVMDFilePath));

	if (Context.SkeletalMesh == nullptr || Context.Skeleton == nullptr)
	{
		AppendUniqueMessage(OutReport.Errors, TEXT("AnalyzeVMDAnimationImport requires a valid SkeletalMesh and USkeleton."));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Context.Skeleton->GetReferenceSkeleton();
	if (RefSkeleton.GetNum() <= 0)
	{
		AppendUniqueMessage(OutReport.Errors, TEXT("Target skeleton has no bones."));
		return false;
	}

	for (const VMDBoneKeyframe& Keyframe : VmdData.BoneKeyframes)
	{
		OutReport.MaxFrame = FMath::Max(OutReport.MaxFrame, static_cast<int32>(Keyframe.FrameNumber));
	}
	for (const VMDMorphKeyframe& Keyframe : VmdData.MorphKeyframes)
	{
		OutReport.MaxFrame = FMath::Max(OutReport.MaxFrame, static_cast<int32>(Keyframe.FrameNumber));
	}

	if (Settings.bImportBoneTracks)
	{
		const TArray<TPair<FString, int32>> BoneCounts = BuildTrackCounts(VmdData.BoneKeyframes, [](const VMDBoneKeyframe& Keyframe)
		{
			return Keyframe.BoneName;
		});

		OutReport.SourceBoneTrackCount = BoneCounts.Num();
		for (const TPair<FString, int32>& Pair : BoneCounts)
		{
			FMMDResolvedBoneTrack Track;
			Track.SourceBoneName = Pair.Key;
			Track.KeyCount = Pair.Value;

			Track.TargetBoneIndex = FindBoneInRefSkeletonSkippingImportRoot(RefSkeleton, Pair.Key);
			if (Track.TargetBoneIndex != INDEX_NONE)
			{
				Track.TargetBoneName = RefSkeleton.GetBoneName(Track.TargetBoneIndex);
				Track.bMatched = true;
				++OutReport.MatchedBoneTrackCount;
			}

			if (!Track.bMatched)
			{
				AppendUniqueMessage(OutReport.Warnings, FString::Printf(TEXT("Unmatched VMD bone track: %s"), *Track.SourceBoneName));
			}
			else if (Track.SourceBoneName == TEXT("Root")
				|| Track.SourceBoneName == TEXT("root")
				|| Track.SourceBoneName == TEXT("\u30bb\u30f3\u30bf\u30fc")
				|| Track.SourceBoneName == TEXT("Center")
				|| Track.SourceBoneName == TEXT("\u4e0a\u534a\u8eab")
				|| Track.SourceBoneName == TEXT("Spine"))
			{
				UE_LOG(LogTemp, Warning, TEXT("[VMD Import] Control bone map: %s -> %s (index %d)"),
					*Track.SourceBoneName,
					*Track.TargetBoneName.ToString(),
					Track.TargetBoneIndex);
			}

			OutReport.BoneTracks.Add(MoveTemp(Track));
		}
	}

	if (Settings.bImportMorphCurves)
	{
		const TArray<TPair<FString, int32>> MorphCounts = BuildTrackCounts(VmdData.MorphKeyframes, [](const VMDMorphKeyframe& Keyframe)
		{
			return Keyframe.MorphName;
		});

		OutReport.SourceMorphTrackCount = MorphCounts.Num();
		for (const TPair<FString, int32>& Pair : MorphCounts)
		{
			FMMDResolvedMorphTrack Track;
			Track.SourceMorphName = Pair.Key;
			Track.KeyCount = Pair.Value;
			const TArray<FMMDMorphTargetContribution> Contributions = ResolveMorphTargetContributions(Context.SkeletalMesh, Context.PMXData, Pair.Key);
			Track.bMatched = Contributions.Num() > 0;
			if (Contributions.Num() == 1)
			{
				Track.TargetMorphName = Contributions[0].TargetMorphName;
			}
			if (Track.bMatched)
			{
				++OutReport.MatchedMorphTrackCount;
			}
			else
			{
				AppendUniqueMessage(OutReport.Warnings, FString::Printf(TEXT("Unmatched VMD morph track: %s"), *Track.SourceMorphName));
			}
			OutReport.MorphTracks.Add(MoveTemp(Track));
		}
	}

	if (Settings.bImportMorphCurves && Context.SkeletalMesh->GetMorphTargets().Num() == 0)
	{
		AppendUniqueMessage(OutReport.Warnings, TEXT("Target SkeletalMesh has no MorphTargets. Facial VMD curves cannot be played until PMX morph import is implemented."));
	}

	return !OutReport.HasErrors();
}

UAnimSequence* TMMDMeshBuilder::BuildVMDAnimation(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport)
{
	FMMDAnimationImportReport LocalReport;
	if (!AnalyzeVMDAnimationImport(VmdData, Context, Settings, LocalReport))
	{
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

#if WITH_EDITOR
	if (!Settings.bImportBoneTracks)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("Current BuildVMDAnimation implementation requires bone track import to be enabled."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	if (LocalReport.MatchedBoneTrackCount <= 0)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("No VMD bone tracks matched the target skeleton."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(LocalReport.PackagePath / LocalReport.AssetName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (Package == nullptr)
	{
		AppendUniqueMessage(LocalReport.Errors, FString::Printf(TEXT("Failed to create animation package: %s"), *UniquePackageName));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	TMap<FName, TArray<FResolvedVMDBoneKey>> BoneTrackMap;
	if (!BuildResolvedBoneKeyMap(VmdData, LocalReport, Settings, BoneTrackMap))
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("Failed to build resolved VMD bone key map."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
	if (AnimSequence == nullptr)
	{
		AppendUniqueMessage(LocalReport.Errors, FString::Printf(TEXT("Failed to create UAnimSequence: %s"), *UniqueAssetName));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	AnimSequence->SetSkeleton(Context.Skeleton);
	AnimSequence->SetPreviewMesh(Context.SkeletalMesh);

	const FReferenceSkeleton& RefSkeleton = Context.Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	const int32 NumFrames = FMath::Max(LocalReport.MaxFrame, 1);
	const int32 NumKeys = NumFrames + 1;
	const int32 NumBones = RefSkeleton.GetNum();

	FMMDIKDebugLog IKDebugLog;
	const TArray<int32> PMXToSkeleton = Settings.bBakeMMDIKToFK || Context.PMXData != nullptr
		? BuildPMXToSkeletonBoneMap(Context.PMXData, RefSkeleton)
		: TArray<int32>();
	const TArray<int32> ModelDataToSkeleton = Settings.bBakeMMDIKToFK || Context.ModelDataAsset != nullptr
		? BuildModelDataToSkeletonBoneMap(Context.ModelDataAsset, RefSkeleton)
		: TArray<int32>();
	const TMap<FName, FMMDBoneSpaceConverter> BoneConverters = BuildVMDToUEBoneConverters(Context.PMXData, RefSkeleton, PMXToSkeleton, Settings.bBakeMMDIKToFK ? &IKDebugLog : nullptr);
	if (Settings.bBakeMMDIKToFK)
	{
		IKDebugLog.LogAlways(FString::Printf(TEXT("Begin | PMXData=%s | ModelData=%s | SourceVMD=%s | MaxFrame=%d | SkeletonBones=%d"),
			Context.PMXData ? TEXT("true") : TEXT("false"),
			Context.ModelDataAsset ? TEXT("true") : TEXT("false"),
			*Context.SourceVMDFilePath,
			LocalReport.MaxFrame,
			NumBones));
		LogVMDControlTrackStats(BoneTrackMap, IKDebugLog);
	}

	TArray<TArray<FTransform>> LocalTransforms;
	TSet<FName> TracksToWrite;
	int32 ConverterFallbackTrackCount = 0;
	const bool bUsedModelDataEvaluator = EvaluateMMDModelDataPoseToLocalTransforms(
		VmdData,
		Context.ModelDataAsset,
		RefSkeleton,
		RefPose,
		Settings,
		NumKeys,
		LocalTransforms,
		TracksToWrite,
		Settings.bBakeMMDIKToFK ? &IKDebugLog : nullptr);
	if (!bUsedModelDataEvaluator)
	{
		LocalTransforms.SetNum(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
			LocalTransforms[BoneIndex].SetNum(NumKeys);
			for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
			{
				LocalTransforms[BoneIndex][FrameIndex] = DefaultTransform;
			}
		}

		for (const TPair<FName, TArray<FResolvedVMDBoneKey>>& Pair : BoneTrackMap)
		{
			const FName BoneName = Pair.Key;
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}

			TracksToWrite.Add(BoneName);
			const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
			const TArray<FResolvedVMDBoneKey>& Keys = Pair.Value;
			const FMMDBoneSpaceConverter* Converter = BoneConverters.Find(BoneName);
			if (Converter == nullptr)
			{
				++ConverterFallbackTrackCount;
			}
			for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
			{
				FVector RawMMDPosition = FVector::ZeroVector;
				FQuat RawMMDRotation = FQuat::Identity;
				SampleResolvedVMDTrackAtFrame(Keys, FrameIndex, RawMMDPosition, RawMMDRotation);
				const FVector ConvertedPosition = Converter != nullptr
					? Converter->ConvertPosition(RawMMDPosition, Settings.PositionScale)
					: ConvertMMDPositionToUnreal(RawMMDPosition, Settings.PositionScale);
				const FQuat ConvertedRotation = Converter != nullptr
					? Converter->ConvertRotation(RawMMDRotation)
					: ConvertMMDQuatToUnreal(RawMMDRotation).GetNormalized();
				const FVector CurrentPos = DefaultTransform.GetTranslation() + ConvertedPosition;
				const FQuat CurrentRot = (DefaultTransform.GetRotation() * ConvertedRotation).GetNormalized();
				LocalTransforms[BoneIndex][FrameIndex].SetTranslation(CurrentPos);
				LocalTransforms[BoneIndex][FrameIndex].SetRotation(CurrentRot);
			}
		}
	}
	const bool bUseModelDataRuntime = Settings.bBakeMMDIKToFK && !bUsedModelDataEvaluator && Context.ModelDataAsset != nullptr && ModelDataToSkeleton.Num() > 0;
	const TArray<FMMDPMXRuntimeBone> RuntimeBones = Settings.bBakeMMDIKToFK && !bUsedModelDataEvaluator
		? (bUseModelDataRuntime
			? BuildMMDModelDataRuntimeBones(Context.ModelDataAsset, RefSkeleton, ModelDataToSkeleton)
			: BuildMMDPMXRuntimeBones(Context.PMXData, RefSkeleton, PMXToSkeleton, &IKDebugLog))
		: TArray<FMMDPMXRuntimeBone>();
	const TArray<FMMDIKBakeChain> IKChains = Settings.bBakeMMDIKToFK && !bUsedModelDataEvaluator
		? (bUseModelDataRuntime
			? BuildMMDModelDataIKBakeChains(Context.ModelDataAsset, RefSkeleton, ModelDataToSkeleton, TracksToWrite, &IKDebugLog)
			: BuildMMDIKBakeChains(Context.PMXData, RefSkeleton, PMXToSkeleton, TracksToWrite, &IKDebugLog))
		: TArray<FMMDIKBakeChain>();
	if (Settings.bBakeMMDIKToFK)
	{
		LogMMDIKSeparationProbe(RefSkeleton, RefPose, LocalTransforms, BoneTrackMap, NumKeys, TEXT("BeforeBake"), IKDebugLog);
		if (!bUsedModelDataEvaluator)
		{
			ApplyMMDAppendAndIKByTransformOrder(RefSkeleton, RefPose, RuntimeBones, IKChains, LocalTransforms, NumKeys);
		}
		LogMMDIKSeparationProbe(RefSkeleton, RefPose, LocalTransforms, BoneTrackMap, NumKeys, TEXT("AfterBake"), IKDebugLog);
		const TCHAR* BakeRuntimeSource = bUsedModelDataEvaluator ? TEXT("ModelDataPMXSpace") : (bUseModelDataRuntime ? TEXT("ModelData") : (Context.PMXData ? TEXT("PMXData") : TEXT("None")));
		IKDebugLog.LogAlways(FString::Printf(TEXT("Bake summary | BakeRuntime=%s | PMXData=%s | ModelData=%s | RuntimeBones=%d | Chains=%d | TracksToWrite=%d"),
			BakeRuntimeSource,
			Context.PMXData ? TEXT("true") : TEXT("false"),
			Context.ModelDataAsset ? TEXT("true") : TEXT("false"),
			RuntimeBones.Num(),
			IKChains.Num(),
			TracksToWrite.Num()));
		IKDebugLog.LogAlways(FString::Printf(TEXT("VMD converter summary | ModelDataEvaluator=%s | Converters=%d | FallbackTracks=%d"),
			bUsedModelDataEvaluator ? TEXT("true") : TEXT("false"),
			BoneConverters.Num(),
			ConverterFallbackTrackCount));
		IKDebugLog.FlushSuppressed();
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Import VMD Animation")));
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(FMath::RoundToInt32(Settings.FrameRate), 1), true);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), true);

	TArray<FVMDWrittenTrackDiagnostic> TrackDiagnostics;
	TrackDiagnostics.Reserve(TracksToWrite.Num());
	int32 WrittenTrackCount = 0;
	int32 StaticTrackCount = 0;
	int32 FailedTrackCount = 0;

	for (const FName BoneName : TracksToWrite)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;
		PosKeys.SetNum(NumKeys);
		RotKeys.SetNum(NumKeys);
		ScaleKeys.SetNum(NumKeys);

		const TArray<FResolvedVMDBoneKey>* SourceKeys = BoneTrackMap.Find(BoneName);
		FVMDWrittenTrackDiagnostic Diagnostic;
		Diagnostic.BoneName = BoneName;
		Diagnostic.SourceKeyCount = SourceKeys ? SourceKeys->Num() : 0;

		for (int32 FrameIndex = 0; FrameIndex < NumKeys; ++FrameIndex)
		{
			const FTransform& LocalTransform = LocalTransforms[BoneIndex][FrameIndex];
			PosKeys[FrameIndex] = FVector3f(LocalTransform.GetTranslation());
			RotKeys[FrameIndex] = FQuat4f(LocalTransform.GetRotation());
			ScaleKeys[FrameIndex] = FVector3f(LocalTransform.GetScale3D());
			Diagnostic.MaxTranslationDelta = FMath::Max(
				Diagnostic.MaxTranslationDelta,
				static_cast<float>(FVector::Dist(LocalTransform.GetTranslation(), DefaultTransform.GetTranslation())));
			Diagnostic.MaxRotationDeltaDegrees = FMath::Max(
				Diagnostic.MaxRotationDeltaDegrees,
				GetQuatDeltaDegrees(LocalTransform.GetRotation(), DefaultTransform.GetRotation()));
		}

		if (!Controller.AddBoneCurve(BoneName, false))
		{
			AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to add VMD bone curve: %s"), *BoneName.ToString()));
			++FailedTrackCount;
			TrackDiagnostics.Add(MoveTemp(Diagnostic));
			continue;
		}

		Diagnostic.bSetKeysSucceeded = Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
		if (!Diagnostic.bSetKeysSucceeded)
		{
			AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to write VMD bone track keys: %s"), *BoneName.ToString()));
			++FailedTrackCount;
		}
		else
		{
			++WrittenTrackCount;
			if (Diagnostic.MaxTranslationDelta <= KINDA_SMALL_NUMBER && Diagnostic.MaxRotationDeltaDegrees <= KINDA_SMALL_NUMBER)
			{
				++StaticTrackCount;
			}
		}
		TrackDiagnostics.Add(MoveTemp(Diagnostic));
	}

	TMap<FString, TArray<FMMDMorphTargetContribution>> ResolvedMorphContributions;
	ResolvedMorphContributions.Reserve(LocalReport.MorphTracks.Num());
	for (const FMMDResolvedMorphTrack& Track : LocalReport.MorphTracks)
	{
		TArray<FMMDMorphTargetContribution> Contributions = ResolveMorphTargetContributions(Context.SkeletalMesh, Context.PMXData, Track.SourceMorphName);
		if (Contributions.Num() > 0)
		{
			ResolvedMorphContributions.Add(Track.SourceMorphName, MoveTemp(Contributions));
		}
	}

	TMap<FName, TArray<FRichCurveKey>> MorphCurveKeys;
	TMap<FName, int32> MorphSourceKeyCounts;
	int32 ExpandedMorphTrackCount = 0;
	if (Settings.bImportMorphCurves && ResolvedMorphContributions.Num() > 0)
	{
		for (const VMDMorphKeyframe& Keyframe : VmdData.MorphKeyframes)
		{
			const TArray<FMMDMorphTargetContribution>* Contributions = ResolvedMorphContributions.Find(Keyframe.MorphName);
			if (Contributions == nullptr || Contributions->Num() == 0)
			{
				continue;
			}

			const float Time = static_cast<float>(Keyframe.FrameNumber) / FMath::Max(Settings.FrameRate, KINDA_SMALL_NUMBER);
			for (const FMMDMorphTargetContribution& Contribution : *Contributions)
			{
				if (Contribution.TargetMorphName == NAME_None)
				{
					continue;
				}

				FRichCurveKey CurveKey(Time, Keyframe.Weight * Contribution.WeightScale);
				CurveKey.InterpMode = RCIM_Linear;
				MorphCurveKeys.FindOrAdd(Contribution.TargetMorphName).Add(CurveKey);
				++MorphSourceKeyCounts.FindOrAdd(Contribution.TargetMorphName);
			}
		}

		for (const TPair<FString, TArray<FMMDMorphTargetContribution>>& Pair : ResolvedMorphContributions)
		{
			if (Pair.Value.Num() > 1 || (Pair.Value.Num() == 1 && Pair.Value[0].TargetMorphName.ToString() != Pair.Key))
			{
				++ExpandedMorphTrackCount;
			}
		}
	}

	TArray<FVMDMorphCurveDiagnostic> MorphCurveDiagnostics;
	MorphCurveDiagnostics.Reserve(MorphCurveKeys.Num());
	int32 WrittenMorphCurveCount = 0;
	int32 FailedMorphCurveCount = 0;
	for (TPair<FName, TArray<FRichCurveKey>>& Pair : MorphCurveKeys)
	{
		const FName MorphName = Pair.Key;
		TArray<FRichCurveKey>& CurveKeys = Pair.Value;
		CurveKeys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B)
		{
			return A.Time < B.Time;
		});

		TArray<FRichCurveKey> UniqueCurveKeys;
		UniqueCurveKeys.Reserve(CurveKeys.Num());
		for (const FRichCurveKey& CurveKey : CurveKeys)
		{
			if (UniqueCurveKeys.Num() > 0 && FMath::IsNearlyEqual(UniqueCurveKeys.Last().Time, CurveKey.Time))
			{
				UniqueCurveKeys.Last() = CurveKey;
			}
			else
			{
				UniqueCurveKeys.Add(CurveKey);
			}
		}

		FVMDMorphCurveDiagnostic Diagnostic;
		Diagnostic.MorphName = MorphName;
		Diagnostic.SourceKeyCount = MorphSourceKeyCounts.FindRef(MorphName);

		const FAnimationCurveIdentifier CurveId(MorphName, ERawCurveTrackTypes::RCT_Float);
		if (AnimSequence->GetDataModel()->FindCurve(CurveId) == nullptr)
		{
			Controller.AddCurve(CurveId, AACF_DefaultCurve | AACF_DriveMorphTarget_DEPRECATED, false);
		}
		Context.Skeleton->AccumulateCurveMetaData(MorphName, false, true);

		Diagnostic.bSetKeysSucceeded = Controller.SetCurveKeys(CurveId, UniqueCurveKeys, false);
		if (Diagnostic.bSetKeysSucceeded)
		{
			++WrittenMorphCurveCount;
		}
		else
		{
			AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to write VMD morph curve keys: %s"), *MorphName.ToString()));
			++FailedMorphCurveCount;
		}
		MorphCurveDiagnostics.Add(MoveTemp(Diagnostic));
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket();
	const IAnimationDataModel* DataModel = AnimSequence->GetDataModel();
	UE_LOG(LogTemp, Warning, TEXT("VMD AnimSequence data model: Tracks=%d/%d Keys=%d/%d Frames=%d Length=%.3f Written=%d Static=%d Failed=%d MorphCurves=%d/%d MorphExpanded=%d MorphFailed=%d"),
		DataModel ? DataModel->GetNumBoneTracks() : 0,
		TracksToWrite.Num(),
		DataModel ? DataModel->GetNumberOfKeys() : 0,
		NumKeys,
		NumFrames,
		DataModel ? DataModel->GetPlayLength() : 0.0f,
		WrittenTrackCount,
		StaticTrackCount,
		FailedTrackCount,
		WrittenMorphCurveCount,
		LocalReport.MatchedMorphTrackCount,
		ExpandedMorphTrackCount,
		FailedMorphCurveCount);

	AnimSequence->PostEditChange();
	AnimSequence->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(AnimSequence);

	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(Package, AnimSequence, *FilePath, SaveArgs))
	{
		AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to save generated animation package: %s"), *FilePath));
	}

	LocalReport.PackagePath = UniquePackageName;
	LocalReport.AssetName = UniqueAssetName;

	if (OutReport != nullptr)
	{
		*OutReport = LocalReport;
	}
	return AnimSequence;
#else
	AppendUniqueMessage(LocalReport.Errors, TEXT("BuildVMDAnimation is editor-only."));
	if (OutReport != nullptr)
	{
		*OutReport = LocalReport;
	}
	return nullptr;
#endif
}

bool TMMDMeshBuilder::AppendVMDMorphCurvesToAnimSequence(UAnimSequence* AnimSequence, const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport)
{
#if WITH_EDITOR
	FMMDAnimationImportSettings MorphSettings = Settings;
	MorphSettings.bImportBoneTracks = false;
	MorphSettings.bImportMorphCurves = true;
	MorphSettings.bBakeMMDIKToFK = false;

	FMMDAnimationImportReport LocalReport;
	if (!AnimSequence)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("Append facial VMD requires a target AnimSequence."));
		if (OutReport)
		{
			*OutReport = LocalReport;
		}
		return false;
	}

	if (!AnalyzeVMDAnimationImport(VmdData, Context, MorphSettings, LocalReport))
	{
		if (OutReport)
		{
			*OutReport = LocalReport;
		}
		return false;
	}

	if (LocalReport.MatchedMorphTrackCount <= 0)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("No VMD morph tracks matched the target SkeletalMesh."));
		if (OutReport)
		{
			*OutReport = LocalReport;
		}
		return false;
	}

	TMap<FString, TArray<FMMDMorphTargetContribution>> ResolvedMorphContributions;
	ResolvedMorphContributions.Reserve(LocalReport.MorphTracks.Num());
	for (const FMMDResolvedMorphTrack& Track : LocalReport.MorphTracks)
	{
		TArray<FMMDMorphTargetContribution> Contributions = ResolveMorphTargetContributions(Context.SkeletalMesh, Context.PMXData, Track.SourceMorphName);
		if (Contributions.Num() > 0)
		{
			ResolvedMorphContributions.Add(Track.SourceMorphName, MoveTemp(Contributions));
		}
	}

	TMap<FName, TArray<FRichCurveKey>> MorphCurveKeys;
	TMap<FName, int32> MorphSourceKeyCounts;
	for (const VMDMorphKeyframe& Keyframe : VmdData.MorphKeyframes)
	{
		const TArray<FMMDMorphTargetContribution>* Contributions = ResolvedMorphContributions.Find(Keyframe.MorphName);
		if (!Contributions || Contributions->Num() == 0)
		{
			continue;
		}

		const float Time = static_cast<float>(Keyframe.FrameNumber) / FMath::Max(MorphSettings.FrameRate, KINDA_SMALL_NUMBER);
		for (const FMMDMorphTargetContribution& Contribution : *Contributions)
		{
			if (Contribution.TargetMorphName == NAME_None)
			{
				continue;
			}

			FRichCurveKey CurveKey(Time, Keyframe.Weight * Contribution.WeightScale);
			CurveKey.InterpMode = RCIM_Linear;
			MorphCurveKeys.FindOrAdd(Contribution.TargetMorphName).Add(CurveKey);
			++MorphSourceKeyCounts.FindOrAdd(Contribution.TargetMorphName);
		}
	}

	if (MorphCurveKeys.Num() == 0)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("No writable morph curves were found in the selected facial VMD."));
		if (OutReport)
		{
			*OutReport = LocalReport;
		}
		return false;
	}

	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Append MMD Facial VMD")), false);

	int32 WrittenMorphCurveCount = 0;
	int32 FailedMorphCurveCount = 0;
	for (TPair<FName, TArray<FRichCurveKey>>& Pair : MorphCurveKeys)
	{
		const FName MorphName = Pair.Key;
		TArray<FRichCurveKey>& CurveKeys = Pair.Value;
		CurveKeys.Sort([](const FRichCurveKey& A, const FRichCurveKey& B)
		{
			return A.Time < B.Time;
		});

		TArray<FRichCurveKey> UniqueCurveKeys;
		UniqueCurveKeys.Reserve(CurveKeys.Num());
		for (const FRichCurveKey& CurveKey : CurveKeys)
		{
			if (UniqueCurveKeys.Num() > 0 && FMath::IsNearlyEqual(UniqueCurveKeys.Last().Time, CurveKey.Time))
			{
				UniqueCurveKeys.Last() = CurveKey;
			}
			else
			{
				UniqueCurveKeys.Add(CurveKey);
			}
		}

		const FAnimationCurveIdentifier CurveId(MorphName, ERawCurveTrackTypes::RCT_Float);
		if (AnimSequence->GetDataModel()->FindCurve(CurveId) == nullptr)
		{
			Controller.AddCurve(CurveId, AACF_DefaultCurve | AACF_DriveMorphTarget_DEPRECATED, false);
		}
		Context.Skeleton->AccumulateCurveMetaData(MorphName, false, true);

		if (Controller.SetCurveKeys(CurveId, UniqueCurveKeys, false))
		{
			++WrittenMorphCurveCount;
		}
		else
		{
			AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to write VMD morph curve keys: %s"), *MorphName.ToString()));
			++FailedMorphCurveCount;
		}
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	AnimSequence->PostEditChange();
	AnimSequence->MarkPackageDirty();

	const FString FilePath = FPackageName::LongPackageNameToFilename(AnimSequence->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(AnimSequence->GetOutermost(), AnimSequence, *FilePath, SaveArgs))
	{
		AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to save updated animation package: %s"), *FilePath));
	}

	UE_LOG(LogTemp, Warning, TEXT("[VMD Facial Append] Updated %s | MorphCurves=%d/%d Failed=%d MaxFrame=%d"),
		*AnimSequence->GetPathName(),
		WrittenMorphCurveCount,
		LocalReport.MatchedMorphTrackCount,
		FailedMorphCurveCount,
		LocalReport.MaxFrame);

	LocalReport.PackagePath = AnimSequence->GetOutermost()->GetName();
	LocalReport.AssetName = AnimSequence->GetName();
	if (OutReport)
	{
		*OutReport = LocalReport;
	}
	return WrittenMorphCurveCount > 0 && FailedMorphCurveCount < MorphCurveKeys.Num();
#else
	if (OutReport != nullptr)
	{
		AppendUniqueMessage(OutReport->Errors, TEXT("AppendVMDMorphCurvesToAnimSequence is editor-only."));
	}
	return false;
#endif
}

UAnimSequence* TMMDMeshBuilder::BuildVMDAnimation(const VMDData& VmdData, const FString& VMDFilePath)
{
	FMMDAnimationImportContext Context;
	Context.SourceVMDFilePath = VMDFilePath;

	FMMDAnimationImportSettings Settings;
	FMMDAnimationImportReport Report;
	AppendUniqueMessage(Report.Errors, TEXT("Legacy BuildVMDAnimation overload no longer has enough context. Pass a SkeletalMesh via FMMDAnimationImportContext."));
	(void)VmdData;
	(void)Settings;
	return nullptr;
}
#endif

