#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldBakeSdfCommandlet.generated.h"

/**
 * 脸部 SDF 烘焙命令。
 *
 * 把终末地脸部阴影所需的"逐 texel 阴影边界方位角阈值"图从网格自动烘出，
 * 使其成为可再生的派生资产而非手绘配套贴图。
 *
 * 只有显式 -Generate 才写出纹理资产；识别不到任何脸部 texel 时拒绝写出空白图。
 */
UCLASS()
class UMMDEndfieldBakeSdfCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMMDEndfieldBakeSdfCommandlet();
    virtual int32 Main(const FString& Params) override;
};
