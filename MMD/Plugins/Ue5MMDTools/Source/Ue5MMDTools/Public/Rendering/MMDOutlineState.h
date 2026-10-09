#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "MMDOutlineState.generated.h"

class UMaterialInterface;
class USkeletalMesh;
class UMMDStageRenderPreset;

/** 组件临时描边记录；不改导入材质或网格资产。 */
UCLASS(Transient)
class UE5MMDTOOLS_API UMMDOutlineState : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> SourceMesh;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SourceGlobalOverlay;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SourceSlotOverlays;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> AppliedSlotOverlays;
	UPROPERTY(Transient)
	TObjectPtr<UMMDStageRenderPreset> Preset;
	UPROPERTY(Transient)
	float SourceBoundsScale = 1.0f;
	UPROPERTY(Transient)
	float AppliedBoundsScale = 1.0f;
};
