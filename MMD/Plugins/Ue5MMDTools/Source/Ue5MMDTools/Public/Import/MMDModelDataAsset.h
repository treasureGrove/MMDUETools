#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Engine/DataAsset.h"
#include "MMDModelDataAsset.generated.h"

USTRUCT(BlueprintType)
struct FMMDModelIKLinkData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 LinkBoneIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	bool bHasLimit = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector LowerLimit = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector UpperLimit = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FMMDModelBoneData
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FString NameJP;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FString NameEN;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FName UEBoneName = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 UEBoneIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 ParentBoneIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 DeformLayer = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 Flags = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 InheritParentIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	float InheritInfluence = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector FixedAxis = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector LocalAxisX = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FVector LocalAxisZ = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 IKTargetBoneIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	int32 IKLoopCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	float IKLimitAngle = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	TArray<FMMDModelIKLinkData> IKLinks;
};

UCLASS(BlueprintType)
class UE5MMDTOOLS_API UMMDModelDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FString ModelId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FString ModelNameJP;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	FString ModelNameEN;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD")
	TArray<FMMDModelBoneData> Bones;
};

UCLASS()
class UE5MMDTOOLS_API UMMDSkeletalMeshUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "MMD")
	TObjectPtr<UMMDModelDataAsset> ModelDataAsset;
};
