#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDStagePresetsCommandlet.generated.h"

/** 只在后台生成插件舞台资产，并用原生 SceneCapture 验证动效与渲染预设。 */
UCLASS()
class UMMDStagePresetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDStagePresetsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
