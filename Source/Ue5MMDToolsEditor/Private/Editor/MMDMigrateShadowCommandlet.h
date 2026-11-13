#pragma once
#include "Commandlets/Commandlet.h"
#include "MMDMigrateShadowCommandlet.generated.h"

/**
 * 一次性迁移：给使用 MMDBaseToon.usf 的插件示例材质加场景阴影输入
 * （MMDShadowMap / MMDShadowBias）。
 *
 * 运行：UnrealEditor-Cmd <project> -run=MMDMigrateShadow
 */
UCLASS()
class UMMDMigrateShadowCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
