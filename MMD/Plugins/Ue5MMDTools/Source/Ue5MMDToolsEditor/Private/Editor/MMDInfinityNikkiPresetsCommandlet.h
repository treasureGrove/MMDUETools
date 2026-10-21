#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDInfinityNikkiPresetsCommandlet.generated.h"

/** 复用原生 Substrate 图生成暖暖独立材质和 Profile，旧材质只作兼容回退。 */
UCLASS()
class UMMDInfinityNikkiPresetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDInfinityNikkiPresetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
