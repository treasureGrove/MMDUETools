#include "Rendering/MMDEndfieldNativeProfile.h"

#include "Engine/ToonProfile.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Misc/SecureHash.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "MMDEndfieldNativeRampData.inl"

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

        struct FOriginalRampState
        {
            FRuntimeCurveLinearColor diffuse_ramp;
            FRuntimeCurveLinearColor specular_ramp;
            TStrongObjectPtr<UCurveLinearColor> diffuse_external;
            TStrongObjectPtr<UCurveLinearColor> specular_external;
        };

        struct FClientRampSelection
        {
            const uint8 (*diffuse_pixels)[4] = nullptr;
            const float (*specular_tile)[3] = nullptr;
        };

        FClientRampSelection SelectClientRamps(EMMDMaterialRole role)
        {
            using namespace MMDEndfieldNativeRampData;
            switch (role)
            {
                // Body01 与 Face01 的完整 RD 像素相同；代表皮肤材质没有启用 RS。
                case EMMDMaterialRole::Skin: return {body_rd, nullptr};
                case EMMDMaterialRole::Hair: return {hair_rd, hair_rs_tile};
                case EMMDMaterialRole::Cloth: return {cloth_rd, cloth_rs_tile};
                default: return {};
            }
        }

        template <typename FSample>
        void WriteSampledCurve(FRichCurve& curve, const FSample& sample)
        {
            curve.Reset();
            curve.PreInfinityExtrap = RCCE_Constant;
            curve.PostInfinityExtrap = RCCE_Constant;
            curve.SetDefaultValue(sample(0));
            // 曲线键放在原纹理 texel 中心；atlas 的 64/256 分辨率均按同一 UV 重采样。
            for (int32 x = 0; x < MMDEndfieldNativeRampData::sample_count; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / MMDEndfieldNativeRampData::sample_count;
                curve.SetKeyInterpMode(curve.AddKey(u, sample(x)), RCIM_Linear);
            }
        }

        template <typename FSample>
        void WriteTiledCurve(FRichCurve& curve, const FSample& sample)
        {
            curve.Reset();
            curve.PreInfinityExtrap = RCCE_Constant;
            curve.PostInfinityExtrap = RCCE_Constant;
            curve.SetDefaultValue(sample(0));
            // RGB 是 32x8 数据包，不能让曲线在相邻行的最后/首个 texel 之间插值。
            // atlas 按 texel 中心烘焙；宽度至少 256 时，shader 在 slot 中心选最近 texel Load。
            for (int32 slot = 0; slot < MMDEndfieldNativeRampData::rs_sample_count; ++slot)
            {
                const float u = static_cast<float>(slot) / MMDEndfieldNativeRampData::rs_sample_count;
                curve.SetKeyInterpMode(curve.AddKey(u, sample(slot)), RCIM_Constant);
            }
        }

        void ApplyClientRamps(UToonProfile* profile, const FClientRampSelection& selection)
        {
            if (!selection.diffuse_pixels) return;
            auto& diffuse = profile->Settings.DiffuseRamp;
            auto& specular = profile->Settings.SpecularRamp;
            // 客户端线性数值直接写入嵌入曲线，避免外部曲线的 HSV/alpha 调整改变真实数据。
            diffuse.ExternalCurve = nullptr;
            specular.ExternalCurve = nullptr;
            for (int32 channel = 0; channel < 3; ++channel)
            {
                WriteSampledCurve(diffuse.ColorCurves[channel], [&](int32 x)
                {
                    return static_cast<float>(selection.diffuse_pixels[x][channel]) / 255.0f;
                });
                WriteTiledCurve(specular.ColorCurves[channel], [&](int32 slot)
                {
                    return selection.specular_tile ? selection.specular_tile[slot][channel] : 1.0f;
                });
            }
            // 原生 EvaluateToonProfileSpecularRamp 只读取 RGB；RD alpha 存放此处供插件按 diffuse u 读取。
            WriteSampledCurve(specular.ColorCurves[3], [&](int32 x)
            {
                return static_cast<float>(selection.diffuse_pixels[x][3]) / 255.0f;
            });
        }

        FNativeProfileOptions GetNativeProfileOptions()
        {
            const auto& backend = MMDEndfieldNativeBackend::GetStatus();
            FNativeProfileOptions options;
            options.mode = FMath::Clamp(backend.Mode, 0, 2);
            const IConsoleVariable* atlas_resolution = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate.ToonProfile.Resolution"));
            // 低分辨率不能容纳 256 个独立 RGB 槽；整份副本退回原曲线与模式 0，禁止混用 tile。
            if (options.mode == 2 && (!atlas_resolution || atlas_resolution->GetInt() < MMDEndfieldNativeRampData::sample_count))
            {
                static bool warned_low_resolution = false;
                if (!warned_low_resolution)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[EndfieldNative] Mode 2 requires r.Substrate.ToonProfile.Resolution >= 256; using original Toon curves and Mode 0. Set the resolution before applying the preset."));
                    warned_low_resolution = true;
                }
                options.mode = 0;
            }
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
            using namespace MMDEndfieldNativeRampData;
            hasher.Update(reinterpret_cast<const uint8*>(&algorithm_version), sizeof(algorithm_version));
            hasher.Update(reinterpret_cast<const uint8*>(&sample_count), sizeof(sample_count));
            hasher.Update(reinterpret_cast<const uint8*>(&rs_width), sizeof(rs_width));
            hasher.Update(reinterpret_cast<const uint8*>(&rs_height), sizeof(rs_height));
            hasher.Update(reinterpret_cast<const uint8*>(&rs_sample_count), sizeof(rs_sample_count));
            hasher.Update(reinterpret_cast<const uint8*>(body_rd), sizeof(body_rd));
            hasher.Update(reinterpret_cast<const uint8*>(hair_rd), sizeof(hair_rd));
            hasher.Update(reinterpret_cast<const uint8*>(cloth_rd), sizeof(cloth_rd));
            hasher.Update(reinterpret_cast<const uint8*>(cloth_rs_tile), sizeof(cloth_rs_tile));
            hasher.Update(reinterpret_cast<const uint8*>(hair_rs_tile), sizeof(hair_rs_tile));
            signature = hasher.Finalize();
            return true;
        }
    }

    bool MarkTransient(UToonProfile* profile, EMMDMaterialRole role, bool apply_client_ramps)
    {
        if (!IsValid(profile) || !profile->HasAnyFlags(RF_Transient) || static_cast<uint8>(role) > 5) return false;
        check(IsInGameThread());
        // 弱引用包含 UObject 序列号，地址复用不会误用旧快照；外部源曲线由强引用保护。
        static TMap<TWeakObjectPtr<UToonProfile>, FOriginalRampState> original_ramps;
        for (auto iterator = original_ramps.CreateIterator(); iterator; ++iterator)
        {
            if (!iterator.Key().IsValid()) iterator.RemoveCurrent();
        }
        FOriginalRampState* original = original_ramps.Find(profile);
        if (!original)
        {
            original = &original_ramps.FindOrAdd(profile);
            original->diffuse_ramp = profile->Settings.DiffuseRamp;
            original->specular_ramp = profile->Settings.SpecularRamp;
            original->diffuse_external = TStrongObjectPtr<UCurveLinearColor>(original->diffuse_ramp.ExternalCurve.Get());
            original->specular_external = TStrongObjectPtr<UCurveLinearColor>(original->specular_ramp.ExternalCurve.Get());
        }
        // 先还原两个 ramp，支持同一瞬态副本跨角色、模式 0/1/2 重用。
        profile->Settings.DiffuseRamp = original->diffuse_ramp;
        profile->Settings.SpecularRamp = original->specular_ramp;
        const FNativeProfileOptions options = GetNativeProfileOptions();
        if (apply_client_ramps && options.mode == 2) ApplyClientRamps(profile, SelectClientRamps(role));
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
        // metadata 的键位置和值约定保持不变，客户端 RD alpha 不占用这些位置。
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
