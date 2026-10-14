#pragma once

#include "CoreMinimal.h"
#include "Stage/MMDMaterialRole.h"

class UToonProfile;

namespace MMDEndfieldNativeProfile
{
    // 只修改瞬态副本；模式 2 的 Skin/Hair/Cloth 使用客户端 RD 与固定 V 的 RS，模式 0/1 恢复原曲线。
    // Diffuse alpha 的 t=.25/.40625/.5625 仍保存 mode/漫反射GI倍率/镜面GI倍率；RD alpha 放在 Specular alpha。
    // Cook 夹具用 false 保留源曲线载体，运行时 GetOrCreate 才按请求模式替换曲线。
    UE5MMDTOOLS_API bool MarkTransient(UToonProfile* profile, EMMDMaterialRole role, bool apply_client_ramps = true);
    UE5MMDTOOLS_API UToonProfile* GetOrCreate(UToonProfile* source, EMMDMaterialRole role);
}
