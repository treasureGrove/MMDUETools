#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MMDEndfieldImportSampleCommandlet.generated.h"

/** 只从插件 Resources 导入验证样本，产物保存到 Saved 独立挂载。 */
UCLASS()
class UMMDEndfieldImportSampleCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UMMDEndfieldImportSampleCommandlet();
    virtual int32 Main(const FString& Params) override;
};
