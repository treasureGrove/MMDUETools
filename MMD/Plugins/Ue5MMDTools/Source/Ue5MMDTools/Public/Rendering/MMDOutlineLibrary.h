#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMDOutlineLibrary.generated.h"

class USkeletalMeshComponent;
class UMMDStageRenderPreset;

UCLASS()
class UE5MMDTOOLS_API UMMDOutlineLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 按导入的 PMX 材质槽应用几何描边；没有描边元数据时保持现有 Overlay。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Rendering")
	static bool ApplyMMDOutline(USkeletalMeshComponent* Component, bool bEnabled = true);

	/** 可恢复的预设细描边，继续遵守 PMX 各材质槽的边线开关。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Rendering")
	static bool ApplyMMDOutlinePreset(USkeletalMeshComponent* Component, const UMMDStageRenderPreset* Preset);

	UFUNCTION(BlueprintCallable, Category = "MMD|Rendering")
	static bool RestoreMMDOutline(USkeletalMeshComponent* Component);

	static void RefreshMMDOutlinePreset(USkeletalMeshComponent* Component);
};
