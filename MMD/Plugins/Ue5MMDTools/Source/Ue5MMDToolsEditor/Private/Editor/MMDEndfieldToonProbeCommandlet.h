#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldToonProbeCommandlet.generated.h"

/**
 * UE5.8 原生 Substrate Toon BSDF 探针（只读、全 Transient、不写盘、不保存资产）。
 *
 * 验证"角色改用引擎延迟光照 + Toon Profile"是否可行，以及画面是否真的被引擎灯光照亮。
 * 与 -run=MMDStagePresets -EndfieldCapture 相互独立，不进入任何现有预设流程。
 *
 * 用法见 MMDEndfieldToonProbeCommandlet.cpp 文件头。
 */
UCLASS()
class UMMDEndfieldToonProbeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDEndfieldToonProbeCommandlet();
	virtual int32 Main(const FString& Params) override;
};
