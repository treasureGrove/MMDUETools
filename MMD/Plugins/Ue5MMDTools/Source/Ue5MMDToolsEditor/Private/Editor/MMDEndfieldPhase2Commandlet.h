#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldPhase2Commandlet.generated.h"

/** 独立二期人物预设，只允许写入四个专属资产。 */
UCLASS()
class UMMDEndfieldPhase2Commandlet : public UCommandlet
{

	GENERATED_BODY()

public:
	UMMDEndfieldPhase2Commandlet();
	virtual int32 Main(const FString& Params) override;
};
