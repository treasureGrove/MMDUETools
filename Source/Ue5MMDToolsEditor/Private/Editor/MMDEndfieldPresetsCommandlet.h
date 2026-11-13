#pragma once

#include "Commandlets/Commandlet.h"
#include "MMDEndfieldPresetsCommandlet.generated.h"

/**
 * 终末地角色父材质与预设生成器。
 *
 * 与旧的 MMDEndfieldPhase2（只在 Saved 私有目录实验、机械隔离旧算法）不同，
 * 本生成器把三套父材质与 DA_Render_Endfield 预设**正式写入插件 Content**，
 * 随插件分发、打开即可用；且从零构建 64 输入材质图。
 *
 * 写入范围严格限定在四个白名单包，其余一律拒绝。
 */
UCLASS()
class UMMDEndfieldPresetsCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UMMDEndfieldPresetsCommandlet();
    virtual int32 Main(const FString& Params) override;
};
