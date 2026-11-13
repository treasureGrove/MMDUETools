#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDCreateMovieCommandlet.generated.h"

/** 在独立预览世界连续运行动作和 Bullet，输出 Saved 中的真实引擎图像序列。 */
UCLASS()
class UMMDCreateMovieCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UMMDCreateMovieCommandlet();
	virtual int32 Main(const FString& Params) override;
};
