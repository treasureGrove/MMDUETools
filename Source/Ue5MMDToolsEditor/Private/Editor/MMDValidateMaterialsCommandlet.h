#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDValidateMaterialsCommandlet.generated.h"

/** 后台重新编译插件原生材质，并输出机器可读的验证报告。 */
UCLASS()
class UMMDValidateMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
