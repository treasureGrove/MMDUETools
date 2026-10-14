#include "Rendering/MMDEndfieldNativeProfile.h"

#include "Engine/ToonProfile.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Misc/SecureHash.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/UObjectGlobals.h"

namespace MMDEndfieldNativeProfile
{
    namespace
    {
        struct FNativeProfileOptions
        {
            int32 mode = 2;
            float diffuse_scale = 1.0f;
            float specular_scale = 1.0f;
        };

        FNativeProfileOptions GetNativeProfileOptions()
        {
            const auto& backend = MMDEndfieldNativeBackend::GetStatus();
            FNativeProfileOptions options;
            options.mode = FMath::Clamp(backend.Mode, 0, 2);
            options.diffuse_scale = FMath::IsFinite(backend.IndirectDiffuseScale) ? FMath::Clamp(backend.IndirectDiffuseScale, 0.0f, 8.0f) : 1.0f;
            options.specular_scale = FMath::IsFinite(backend.IndirectSpecularScale) ? FMath::Clamp(backend.IndirectSpecularScale, 0.0f, 8.0f) : 1.0f;
            return options;
        }

        bool ComputeSourceSignature(UToonProfile* source, FSHAHash& signature)
        {
            // 对象引用本身只保存指针；关联曲线内容需分别纳入本进程缓存签名。
            UObject* objects[] = {
                source,
                source->Settings.DiffuseRamp.ExternalCurve.Get(),
                source->Settings.SpecularRamp.ExternalCurve.Get(),
                source->Settings.ShadowHatchingPatternDistributionRamp.ExternalCurve.Get(),
                source->Settings.SpecularIndirectRamp.ExternalCurve.Get()
            };
            FSHA1 hasher;
            for (UObject* object : objects)
            {
                TArray<uint8> bytes;
                if (IsValid(object))
                {
                    // 全量序列化，并跳过本地化命名空间初始化，避免读取签名时修改源资产。
                    FObjectWriter object_writer(object, bytes, true, true, false, PPF_DuplicateVerbatim);
                    if (object_writer.IsError()) return false;
                }
                const int32 byte_count = bytes.Num();
                hasher.Update(reinterpret_cast<const uint8*>(&byte_count), sizeof(byte_count));
                if (byte_count > 0) hasher.Update(bytes.GetData(), static_cast<uint64>(byte_count));
            }
            const FNativeProfileOptions options = GetNativeProfileOptions();
            hasher.Update(reinterpret_cast<const uint8*>(&options.mode), sizeof(options.mode));
            hasher.Update(reinterpret_cast<const uint8*>(&options.diffuse_scale), sizeof(options.diffuse_scale));
            hasher.Update(reinterpret_cast<const uint8*>(&options.specular_scale), sizeof(options.specular_scale));
            signature = hasher.Finalize();
            return true;
        }
    }

    bool MarkTransient(UToonProfile* profile, EMMDMaterialRole role)
    {
        if (!IsValid(profile) || !profile->HasAnyFlags(RF_Transient) || static_cast<uint8>(role) > 5) return false;
        check(IsInGameThread());
        FRichCurve* alpha_curve = &profile->Settings.DiffuseRamp.ColorCurves[3];
        if (UCurveLinearColor* source_curve = profile->Settings.DiffuseRamp.ExternalCurve)
        {
            UCurveLinearColor* curve = DuplicateObject<UCurveLinearColor>(source_curve, GetTransientPackage());
            if (!curve) return false;
            curve->SetFlags(RF_Transient);
            curve->ClearFlags(RF_Public | RF_Standalone);
            curve->AdjustMinAlpha = 0.0f;
            curve->AdjustMaxAlpha = 1.0f;
            profile->Settings.DiffuseRamp.ExternalCurve = curve;
            alpha_curve = &curve->FloatCurves[3];
        }
        FRichCurve& alpha = *alpha_curve;
        alpha.Reset();
        const float first = -32.0f - static_cast<float>(role);
        const float last = -64.0f - static_cast<float>(role);
        const FNativeProfileOptions options = GetNativeProfileOptions();
        // 各平台保持平坦，平台间线性过渡；RGB 与外部源曲线始终不变。
        for (const TPair<float, float>& key : {
            TPair<float, float>(0.0f, first), {0.125f, first},
            {0.1875f, static_cast<float>(options.mode)}, {0.3125f, static_cast<float>(options.mode)},
            {0.375f, options.diffuse_scale}, {0.4375f, options.diffuse_scale},
            {0.5f, options.specular_scale}, {0.625f, options.specular_scale},
            {0.875f, last}, {1.0f, last}
        })
        {
            alpha.SetKeyInterpMode(alpha.AddKey(key.Key, key.Value), RCIM_Linear);
        }
        return true;
    }

    UToonProfile* GetOrCreate(UToonProfile* source, EMMDMaterialRole role)
    {
        if (!IsValid(source) || static_cast<uint8>(role) > 5) return source;
        check(IsInGameThread());
        using FRoleCache = TMap<uint8, TWeakObjectPtr<UToonProfile>>;
        struct FSourceCacheEntry
        {
            FSHAHash source_signature{};
            FRoleCache roles;
        };
        static TMap<TWeakObjectPtr<UToonProfile>, FSourceCacheEntry> cache;
        for (auto iterator = cache.CreateIterator(); iterator; ++iterator)
        {
            if (!iterator.Key().IsValid()) iterator.RemoveCurrent();
        }
        FSHAHash source_signature;
        if (!ComputeSourceSignature(source, source_signature)) return source;
        FSourceCacheEntry& source_entry = cache.FindOrAdd(source);
        if (source_entry.source_signature != source_signature)
        {
            // 源曲线或后端参数变化后按角色重建，旧副本由原 MID 持有至替换。
            source_entry.roles.Reset();
            source_entry.source_signature = source_signature;
        }
        FRoleCache& roles = source_entry.roles;
        const uint8 role_index = static_cast<uint8>(role);
        if (const TWeakObjectPtr<UToonProfile>* existing = roles.Find(role_index))
        {
            if (existing->IsValid()) return existing->Get();
        }
        UToonProfile* profile = DuplicateObject<UToonProfile>(source, GetTransientPackage());
        if (!profile) return source;
        profile->SetFlags(RF_Transient);
        profile->ClearFlags(RF_Public | RF_Standalone);
        profile->Guid = FGuid::NewGuid();
        if (!MarkTransient(profile, role)) return source;
        roles.Add(role_index, profile);
        // MID 的 ToonProfileOverride 强引用负责副本生命周期，缓存只持弱引用。
        return profile;
    }
}
