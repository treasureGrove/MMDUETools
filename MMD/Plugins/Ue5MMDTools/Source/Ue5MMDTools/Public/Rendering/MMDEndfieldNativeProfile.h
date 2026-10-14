#pragma once

#include "CoreMinimal.h"
#include "Stage/MMDMaterialRole.h"

class UToonProfile;

namespace MMDEndfieldNativeProfile
{
    // 只标记瞬态副本，原始 RGB 曲线与资产不变；alpha 在 t=.25/.40625/.5625 保存 mode/漫反射GI倍率/镜面GI倍率。
    UE5MMDTOOLS_API bool MarkTransient(UToonProfile* profile, EMMDMaterialRole role);
    UE5MMDTOOLS_API UToonProfile* GetOrCreate(UToonProfile* source, EMMDMaterialRole role);
}
