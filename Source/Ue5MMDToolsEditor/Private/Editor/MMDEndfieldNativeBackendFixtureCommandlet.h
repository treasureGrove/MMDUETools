#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldNativeBackendFixtureCommandlet.generated.h"

/** 只在专用验证目录生成原生后端 Cook 夹具，不修改项目默认地图。 */
UCLASS()
class UMMDEndfieldNativeBackendFixtureCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDEndfieldNativeBackendFixtureCommandlet();
	virtual int32 Main(const FString& Params) override;
};
