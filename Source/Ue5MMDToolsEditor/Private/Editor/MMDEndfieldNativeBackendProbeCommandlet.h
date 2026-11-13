#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldNativeBackendProbeCommandlet.generated.h"

/** 复用 ToonProbe 场景，验证插件原生后端的启动映射、实际 shader 与引擎源文件完整性。 */
UCLASS()
class UMMDEndfieldNativeBackendProbeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDEndfieldNativeBackendProbeCommandlet();
	virtual int32 Main(const FString& Params) override;
};
