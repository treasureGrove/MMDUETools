#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MMDStageTextureGenerator.generated.h"

class UTexture2D;

/** 可重复生成的通用舞台贴图，不绑定模型 UV。 */
UCLASS()
class UE5MMDTOOLSEDITOR_API UMMDStageTextureGenerator : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** 只写入插件 Stages/Textures 下的四张生成贴图；默认保留已有资产。 */
	UFUNCTION(BlueprintCallable, Category="MMD|Stage|Editor", meta=(AdvancedDisplay="Resolution,Seed,bOverwrite"))
	static bool GenerateGenericStageTextures(TArray<UTexture2D*>& Textures, FString& Error,
		int32 Resolution = 256, int32 Seed = 2026, bool bOverwrite = false);
};
