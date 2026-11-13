#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldBindSdfCommandlet.generated.h"

/** 为已有网格生成、绑定并保存自动脸部 SDF；没有 -Generate 时只校验已保存绑定。 */
UCLASS()
class UMMDEndfieldBindSdfCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDEndfieldBindSdfCommandlet();
	virtual int32 Main(const FString& Params) override;
};
