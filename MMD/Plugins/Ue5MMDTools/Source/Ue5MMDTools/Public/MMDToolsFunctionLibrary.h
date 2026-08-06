#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMDToolsFunctionLibrary.generated.h"

class UMeshComponent;

/**
 * 编辑器调试工具：高亮某个 PMX 材质对应的子网格（mesh 材质槽）。
 * 通过临时把高亮材质覆盖到指定槽位实现，可随时恢复原材质。
 */
UCLASS()
class UE5MMDTOOLS_API UMMDToolsFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 高亮指定材质槽位（覆盖一个 Unlit 发光材质，其余槽位不变） */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Debug")
	static void HighlightMaterialSlot(UMeshComponent* Mesh, int32 SlotIndex);

	/** 恢复被高亮的材质槽位到原材质 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Debug")
	static void ClearMaterialHighlight(UMeshComponent* Mesh, int32 SlotIndex);

	/** 清除该组件所有槽位的高亮 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Debug")
	static void ClearAllMaterialHighlights(UMeshComponent* Mesh);
};
