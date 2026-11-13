#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldToonPresetsCommandlet.generated.h"

/** 独立生成或只读校验终末地 V2 原生 Substrate Toon 的十个白名单资产。 */
UCLASS()
class UMMDEndfieldToonPresetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDEndfieldToonPresetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
