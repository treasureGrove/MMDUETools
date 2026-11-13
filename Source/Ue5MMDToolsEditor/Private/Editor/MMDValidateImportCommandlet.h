#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDValidateImportCommandlet.generated.h"

/** 后台运行同步导入与物理测试，只向 Saved 写入验证报告。 */
UCLASS()
class UMMDValidateImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDValidateImportCommandlet();
	virtual int32 Main(const FString& Params) override;
};
