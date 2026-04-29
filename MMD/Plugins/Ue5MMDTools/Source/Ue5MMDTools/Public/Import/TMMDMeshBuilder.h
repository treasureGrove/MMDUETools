#pragma once

#include "CoreMinimal.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#if WITH_EDITOR              
#include "RetargetEditor/IKRetargeterController.h"    
#endif
class UIKRigDefinition;
class USkeleton;
class USkeletalMesh;
class UAnimSequence;

struct FMMDResolvedBoneTrack
{
	FString SourceBoneName;
	FName TargetBoneName = NAME_None;
	int32 TargetBoneIndex = INDEX_NONE;
	int32 KeyCount = 0;
	bool bMatched = false;
};

struct FMMDResolvedMorphTrack
{
	FString SourceMorphName;
	FName TargetMorphName = NAME_None;
	int32 KeyCount = 0;
	bool bMatched = false;
};

struct FMMDAnimationImportSettings
{
	float FrameRate = 30.0f;
	float PositionScale = 8.0f;
	bool bImportBoneTracks = true;
	bool bImportMorphCurves = true;
	bool bSkipUnmatchedTracks = true;
};

struct FMMDAnimationImportContext
{
	USkeletalMesh* SkeletalMesh = nullptr;
	USkeleton* Skeleton = nullptr;
	const PMXDatas* PMXData = nullptr;
	FString SourcePMXFilePath;
	FString SourceVMDFilePath;
};

struct FMMDAnimationImportReport
{
	FString PackagePath;
	FString AssetName;
	int32 SourceBoneTrackCount = 0;
	int32 MatchedBoneTrackCount = 0;
	int32 SourceMorphTrackCount = 0;
	int32 MatchedMorphTrackCount = 0;
	int32 MaxFrame = 0;
	TArray<FMMDResolvedBoneTrack> BoneTracks;
	TArray<FMMDResolvedMorphTrack> MorphTracks;
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasErrors() const
	{
		return Errors.Num() > 0;
	}
};

class TMMDMeshBuilder
{
public:
    static USkeletalMesh *BuildSkeletalMeshFromPMX(const PMXDatas &PMXInfo, const FString &PackagePath, const FString &AssetName, const FString& PMXFilePath);
    
	static UIKRigDefinition* BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

    static UAnimBlueprint* BuildAnimBlueprint(USkeletalMesh* SkeletalMesh,const FString& PMXFilePath);

	static UIKRetargeter* BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath);

	static bool BuildAnimationImportContext(USkeletalMesh* SkeletalMesh, const PMXDatas* PMXData, const FString& PMXFilePath, const FString& VMDFilePath, FMMDAnimationImportContext& OutContext, FMMDAnimationImportReport* OutReport = nullptr);

	static bool AnalyzeVMDAnimationImport(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport& OutReport);

	static UAnimSequence* BuildVMDAnimation(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport = nullptr);

	static UAnimSequence* BuildVMDAnimation(const VMDData &VmdData,const FString& VMDFilePath);
}; 

