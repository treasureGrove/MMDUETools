#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MMDImportVmdCommandlet.generated.h"

/**
 * 命令行 VMD 导入：把 VMD 骨骼／表情动画导入为 AnimSequence。
 * -run=MMDImportVmd -Vmd=<绝对路径> -Mesh=/Game/... [-Pmx=<原模型路径>] [-Name=<动画名>] [-Force] [-Report=<JSON>]
 */
UCLASS()
class UMMDImportVmdCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UMMDImportVmdCommandlet();
	virtual int32 Main(const FString& Params) override;
};
