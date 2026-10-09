#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Stage/MMDMaterialRole.h"
#include "MMDStageLibrary.generated.h"

class UMMDStageRenderPreset;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class UE5MMDTOOLS_API UMMDStageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 显式应用角色预设；保留原材质和导入贴图，可通过 RestoreCharacterMaterials 恢复。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Stage")
	static bool ApplyCharacterRenderPreset(USkeletalMeshComponent* Component, UMMDStageRenderPreset* Preset,
		bool bReplaceMaterials = true);

	UFUNCTION(BlueprintCallable, Category = "MMD|Stage")
	static bool RestoreCharacterMaterials(USkeletalMeshComponent* Component);

	/** 仅按当前网格的槽名和原材质名分类；未识别的部位回退通用。 */
	UFUNCTION(BlueprintPure, Category = "MMD|Materials")
	static EMMDMaterialRole GetResolvedCharacterMaterialRole(USkeletalMeshComponent* Component, int32 MaterialIndex);

	/** 覆盖只属于当前组件和网格；已有预设只更新仍由它持有的 MID。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Materials")
	static bool SetCharacterMaterialRoleOverride(USkeletalMeshComponent* Component, int32 MaterialIndex, EMMDMaterialRole Role);

	UFUNCTION(BlueprintCallable, Category = "MMD|Materials")
	static bool ClearCharacterMaterialRoleOverride(USkeletalMeshComponent* Component, int32 MaterialIndex);

	static void ApplyPresetParameters(UMaterialInstanceDynamic* Material, const UMMDStageRenderPreset* Preset);
};
