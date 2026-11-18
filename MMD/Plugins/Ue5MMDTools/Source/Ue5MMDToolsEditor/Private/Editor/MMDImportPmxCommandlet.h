#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MMDImportPmxCommandlet.generated.h"

/**
 * 命令行 PMX 导入：把 PMX 及其贴图、材质、骨骼、Morph 导入为工程内 /Game 资产。
 * -run=MMDImportPmx -Pmx=<绝对路径> [-Name=<资产名>] [-Package=/Game/MMDModels] [-StandardMaterials] [-Report=<JSON>]
 */
UCLASS()
class UMMDImportPmxCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMMDImportPmxCommandlet();
	virtual int32 Main(const FString& Params) override;
};
