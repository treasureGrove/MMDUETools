#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Stage/MMDMaterialRole.h"
#include "MMDStageMaterialState.generated.h"

class UMaterialInterface;
class UMMDStageRenderPreset;
class USkeletalMesh;

/** 只挂在组件上的临时恢复记录，不修改 SkeletalMesh 与材质资产。 */
UCLASS(Transient)
class UE5MMDTOOLS_API UMMDStageMaterialState : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> SourceMesh;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SourceOverrides;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> AppliedMaterials;
	UPROPERTY(Transient)
	TObjectPtr<UMMDStageRenderPreset> AppliedPreset;
	UPROPERTY(Transient)
	bool bAppliedWithReplacement = false;
	UPROPERTY(Transient)
	TMap<int32, EMMDMaterialRole> RoleOverrides;
	UPROPERTY(Transient)
	TArray<EMMDMaterialRole> ResolvedRoles;
	UPROPERTY(Transient)
	TArray<FString> RoleReasons;
	UPROPERTY(Transient)
	bool bHasMaterialSnapshot = false;
	UPROPERTY(Transient)
	bool bHasShadowTagSnapshot = false;
	UPROPERTY(Transient)
	bool bHadShadowIncludeTag = false;
	UPROPERTY(Transient)
	bool bHadShadowExcludeTag = false;
	UPROPERTY(Transient)
	bool bAddedShadowExcludeTag = false;
	UPROPERTY(Transient)
	bool bHasContactShadowSnapshot = false;
	UPROPERTY(Transient)
	bool bHadContactShadow = true;
};
