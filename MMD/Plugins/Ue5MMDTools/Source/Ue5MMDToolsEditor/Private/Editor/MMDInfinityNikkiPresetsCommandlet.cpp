#include "MMDInfinityNikkiPresetsCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/ToonProfile.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubstrate.h"
#include "MaterialShared.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "RenderingThread.h"
#include "RHIStrings.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageRenderPreset.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    constexpr const TCHAR* owner_key = TEXT("MMDInfinityNikkiGenerator");
    constexpr const TCHAR* owner = TEXT("InfinityNikkiPresetV2Native");
    constexpr const TCHAR* signature_key = TEXT("MMDInfinityNikkiSignature");
    constexpr const TCHAR* preset_package = TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki");
    constexpr const TCHAR* native_root = TEXT("/Ue5MMDTools/Stages/Materials/InfinityNikki/");
    const TCHAR* blends[] = {TEXT("Opaque"), TEXT("Masked"), TEXT("Translucent")};
    const TCHAR* roles[] = {TEXT("Generic"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"), TEXT("Eye")};
    const EBlendMode blend_modes[] = {BLEND_Opaque, BLEND_Masked, BLEND_Translucent};

    FString ObjectPath(const FString& package)
    {
        return package + TEXT(".") + FPackageName::GetLongPackageAssetName(package);
    }

    FString MaterialPackage(int32 index)
    {
        return FString(native_root) + TEXT("M_MMD_InfinityNikki_")
            + (index >= 3 ? TEXT("Face_") : TEXT("")) + blends[index % 3];
    }

    FString ProfilePackage(int32 index)
    {
        return FString(native_root) + TEXT("Profiles/TP_InfinityNikki_") + roles[index];
    }

    bool IsOwned(UObject* asset, bool allow_legacy_preset = false)
    {
        if (!asset) return false;
        const FString tag = asset->GetOutermost()->GetMetaData().GetValue(asset, owner_key);
        return tag == owner || (allow_legacy_preset && tag == TEXT("InfinityNikkiPresetV1"));
    }

    bool Save(UObject* asset, const FString& signature, const TSet<FString>& allowed,
        TArray<TSharedPtr<FJsonValue>>& written, FString& error)
    {
        const FString package = asset->GetOutermost()->GetName();
        if (!allowed.Contains(package)) { error = TEXT("拒绝写入白名单外包：") + package; return false; }
        asset->ClearFlags(RF_Transient);
        asset->SetFlags(RF_Public | RF_Standalone);
        asset->GetOutermost()->GetMetaData().SetValue(asset, owner_key, owner);
        asset->GetOutermost()->GetMetaData().SetValue(asset, signature_key, *signature);
        asset->MarkPackageDirty();
        const FString filename = FPackageName::LongPackageNameToFilename(package, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(filename), true);
        FSavePackageArgs args;
        args.TopLevelFlags = RF_Public | RF_Standalone;
        args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(asset->GetOutermost(), asset, *filename, args))
        { error = TEXT("保存失败：") + package; return false; }
        written.Add(MakeShared<FJsonValueString>(package));
        return true;
    }

    bool BuildSignature(FString& signature, FString& error)
    {
        const TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
        if (!plugin) return false;
        FString bundle(owner);
        for (const TCHAR* relative : {
            TEXT("Shaders/Core/TMMDInfinityNikkiNative.ush"), TEXT("Shaders/Core/TMMDInfinityNikkiResponse.ush"),
            TEXT("Shaders/Core/TMMDInfinityNikkiGeometry.ush"),
            TEXT("Shaders/Core/TMMDEndfieldNativeBSDF.ush"), TEXT("Shaders/Core/TMMDEndfieldNative.ush"),
            TEXT("Source/Ue5MMDToolsEditor/Private/Editor/MMDInfinityNikkiPresetsCommandlet.cpp")})
        {
            FString source;
            if (!FFileHelper::LoadFileToString(source, *FPaths::Combine(plugin->GetBaseDir(), relative)))
            { error = TEXT("缺少依赖：") + FString(relative); return false; }
            bundle += FString(relative) + TEXT("\n") + source;
        }
        FTCHARToUTF8 utf8(*bundle);
        signature = FMD5::HashBytes(reinterpret_cast<const uint8*>(utf8.Get()), utf8.Length());
        return true;
    }

    UMaterialExpressionSubstrateToonBSDF* FindToon(UMaterial* material)
    {
        UMaterialExpressionSubstrateToonBSDF* found = nullptr;
        if (!material) return nullptr;
        for (UMaterialExpression* expression : material->GetExpressionCollection().Expressions)
            if (auto* toon = Cast<UMaterialExpressionSubstrateToonBSDF>(expression))
            {
                if (found) return nullptr;
                found = toon;
            }
        return found;
    }

    bool CheckMaterial(UMaterial* material, EBlendMode blend)
    {
        const UMaterialEditorOnlyData* data = material ? material->GetEditorOnlyData() : nullptr;
        auto* toon = FindToon(material);
        if (!data || !toon || material->MaterialDomain != MD_Surface || material->BlendMode != blend
            || !material->TwoSided || !material->GetUsageByFlag(MATUSAGE_SkeletalMesh)
            || !material->GetUsageByFlag(MATUSAGE_MorphTargets) || !data->FrontMaterial.Expression
            || data->EmissiveColor.Expression || !toon->BaseColor.Expression || !toon->Normal.Expression)
            return false;
        bool payload = false;
        bool geometry = !material->GetPathName().StartsWith(native_root);
        for (UMaterialExpression* expression : material->GetExpressionCollection().Expressions)
        {
            if (expression && expression->GetTypedOuter<UMaterial>() != material) return false;
            if (expression && expression->GetClass()->GetName() == TEXT("MaterialExpressionMMDEndfieldNativePayload")) payload = true;
            if (auto* custom = Cast<UMaterialExpressionCustom>(expression))
                geometry |= custom->Code.Contains(TEXT("TMMDInfinityNikkiGeometry geometry;"));
        }
        return payload && geometry;
    }

    void ConfigureProfile(UToonProfile* profile)
    {
        profile->Settings = GetDefault<UToonProfile>()->Settings;
        auto& settings = profile->Settings;
        settings.DiffuseRamp.ExternalCurve = nullptr;
        settings.SpecularRamp.ExternalCurve = nullptr;
        for (int32 channel = 0; channel < 4; ++channel)
        {
            FRichCurve& diffuse = settings.DiffuseRamp.ColorCurves[channel];
            FRichCurve& specular = settings.SpecularRamp.ColorCurves[channel];
            diffuse.Reset(); specular.Reset();
            for (const auto& point : {TPair<float,float>(0.f,0.f), {0.5f,0.f}, {1.f,1.f}})
                diffuse.SetKeyInterpMode(diffuse.AddKey(point.Key, channel == 3 ? 1.f : point.Value), RCIM_Linear);
            specular.AddKey(0.f,1.f); specular.AddKey(1.f,1.f);
        }
        settings.DiffuseRampOffsetTexture = nullptr;
        settings.DiffuseRampOffsetStrength = 0.f;
        settings.SpecularRampOffsetTexture = nullptr;
        settings.SpecularRampOffsetStrength = 0.f;
        settings.ShadowHatchingPatternTexture = nullptr;
        settings.ShadowHatchingPatternStrength = 0.f;
        settings.DiffuseIndirectScale = 1.f;
        settings.SpecularIndirectScale = 1.f;
        settings.bDiffuseRampIncludeShadow = 0;
    }

    void ConfigurePreset(UMMDStageRenderPreset* preset, const TArray<TStrongObjectPtr<UMaterial>>& parents,
        const TArray<TStrongObjectPtr<UToonProfile>>& profiles)
    {
        preset->DisplayName = FText::FromString(TEXT("无限暖暖 · 原生柔光衣装"));
        preset->bOverrideStageTheme = false;
        preset->Theme = EMMDStageTheme::SakuraTheater;
        preset->CharacterMaterialOpaque = LoadObject<UMaterial>(nullptr, TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_InfinityNikki_Opaque.M_MMD_InfinityNikki_Opaque"));
        preset->CharacterMaterialMasked = LoadObject<UMaterial>(nullptr, TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_InfinityNikki_Masked.M_MMD_InfinityNikki_Masked"));
        preset->CharacterMaterialTranslucent = LoadObject<UMaterial>(nullptr, TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_InfinityNikki_Translucent.M_MMD_InfinityNikki_Translucent"));
        preset->CharacterToonMaterialOpaque = parents[0].Get();
        preset->CharacterToonMaterialMasked = parents[1].Get();
        preset->CharacterToonMaterialTranslucent = parents[2].Get();
        preset->CharacterToonFaceMaterialOpaque = parents[3].Get();
        preset->CharacterToonFaceMaterialMasked = parents[4].Get();
        preset->CharacterToonFaceMaterialTranslucent = parents[5].Get();
        preset->CharacterToonRoles.Reset();
        const float roughness[] = {.58f,.40f,.30f,.64f,.18f,.17f};
        const float specular[] = {.42f,.42f,.55f,.38f,.70f,.55f};
        for (int32 index = 0; index < 6; ++index)
        {
            FMMDStageToonRoleSettings settings;
            settings.ToonProfile = profiles[index].Get();
            settings.Roughness = roughness[index];
            settings.Specular = specular[index];
            settings.Metallic = index == 4 ? .85f : 0.f;
            settings.Anisotropy = 0.f;
            settings.SphereBlend = index == 1 ? 1.f : 0.f;
            settings.FaceShadowSuppress = index == 1 ? 1.f : 0.f;
            settings.ShadowCastStrength = index == 2 ? 0.f : 1.f;
            preset->CharacterToonRoles.Add(static_cast<EMMDMaterialRole>(index), settings);
        }
        preset->CharacterSkinLighting = FMMDStageSkinLightingSettings();
        preset->CharacterSkinSurface = FMMDStageSkinSurfaceSettings();
        preset->CharacterFaceSkinSurface = FMMDStageSkinSurfaceSettings();
        preset->CharacterSkinSurface.bEnabled = true;
        preset->CharacterSkinSurface.Tint = FLinearColor(1.f,.94f,.90f);
        preset->CharacterSkinSurface.TintStrength = .12f;
        preset->CharacterSkinSurface.AlbedoScale = 1.04f;
        preset->CharacterSkinSurface.Saturation = 1.03f;
        preset->CharacterSkinSurface.Roughness = .40f;
        preset->CharacterSkinSurface.Specular = .42f;
        preset->CharacterFaceSkinSurface = preset->CharacterSkinSurface;
        preset->bCharacterOutline = false;
        preset->bUseMaskedHair = true;
        preset->bCharacterSelfShadow = true;
        preset->bCharacterContactShadow = false;
        preset->KeyLightColor = FLinearColor(1.f,.93f,.84f);
        preset->KeyLightRotation = FRotator(-28.f,32.f,0.f);
        preset->KeyLightIntensity = 5.8f;
        preset->FillLightColor = FLinearColor(.83f,.90f,1.f);
        preset->FillLightRotation = FRotator(-18.f,145.f,0.f);
        preset->FillLightIntensity = .75f;
        preset->RimLightColor = FLinearColor(.90f,.95f,1.f);
        preset->RimLightRotation = FRotator(-15.f,-100.f,0.f);
        preset->RimLightIntensity = 1.25f;
        preset->bOverrideLightShape = true;
        preset->KeyLightSourceAngle = 1.5f;
        preset->KeyLightSpecularScale = 1.f;
        preset->FillLightSpecularScale = .35f;
        preset->RimLightSpecularScale = .75f;
        preset->bOverrideSkyLight = true;
        preset->SkyLightColor = FLinearColor(.96f,.97f,1.f);
        preset->SkyLightIntensity = .32f;
        preset->SkyLightLowerHemisphereColor = FLinearColor(.07f,.05f,.04f);
        preset->ExposureBias = .25f;
        preset->BloomIntensity = .12f;
        preset->FilmToe = .18f;
        preset->ColorGradingSaturation = 1.04f;
        preset->ColorGradingContrast = 1.04f;
        preset->ColorGradingGamma = 1.f;
    }

    bool PresetMatches(UMMDStageRenderPreset* actual, UMMDStageRenderPreset* expected)
    {
        for (TFieldIterator<FProperty> it(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); it; ++it)
            if (!it->Identical_InContainer(actual, expected)) return false;
        return true;
    }
}

UMMDInfinityNikkiPresetsCommandlet::UMMDInfinityNikkiPresetsCommandlet()
{
    IsClient = true; IsEditor = true; IsServer = false; LogToConsole = true;
}

int32 UMMDInfinityNikkiPresetsCommandlet::Main(const FString& params)
{
    const bool generate = FParse::Param(*params, TEXT("Generate"));
    FString report_path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("InfinityNikkiValidation/native-presets.json"));
    FParse::Value(*params, TEXT("Report="), report_path);
    TArray<FString> errors;
    TArray<TSharedPtr<FJsonValue>> written, material_reports;
    TArray<TStrongObjectPtr<UMaterial>> parents;
    TArray<TStrongObjectPtr<UToonProfile>> profiles;
    TSet<FString> allowed;
    allowed.Add(preset_package);
    for (int32 index = 0; index < 6; ++index) { allowed.Add(MaterialPackage(index)); allowed.Add(ProfilePackage(index)); }
    FString signature, error;
    if (!BuildSignature(signature,error)) errors.Add(error);
    if (!MMDEndfieldNativeBackend::GetStatus().bProfileMarkerAllowed)
        errors.Add(TEXT("必须启用 Substrate 与原生后端，且协议校验通过"));
    // 写入之前统一检查归属，只有旧自有预设可原位迁移。
    for (const FString& package : allowed)
    {
        if (!FPackageName::DoesPackageExist(package)) continue;
        UObject* existing = LoadObject<UObject>(nullptr, *ObjectPath(package));
        if (!IsOwned(existing, package == preset_package)) errors.Add(TEXT("拒绝覆盖未知资产：") + package);
    }
    auto current = [&signature](UObject* asset)
    {
        return asset && FString(asset->GetOutermost()->GetMetaData().GetValue(asset, signature_key)) == signature;
    };
    for (int32 index = 0; index < 6 && errors.IsEmpty(); ++index)
    {
        const FString package = ProfilePackage(index);
        UToonProfile* profile = LoadObject<UToonProfile>(nullptr, *ObjectPath(package));
        if (!current(profile))
        {
            if (!generate) { errors.Add(TEXT("Profile 缺失或签名过期：") + package); break; }
            if (!profile) { profile = NewObject<UToonProfile>(CreatePackage(*package), *FPackageName::GetLongPackageAssetName(package), RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(profile); }
            ConfigureProfile(profile);
            profile->Guid = FGuid::NewGuid();
            profile->PostEditChange();
            if (!Save(profile,signature,allowed,written,error)) { errors.Add(error); break; }
        }
        profiles.Emplace(profile);
    }
    for (int32 index = 0; index < 6 && errors.IsEmpty(); ++index)
    {
        const FString package = MaterialPackage(index);
        UMaterial* material = LoadObject<UMaterial>(nullptr, *ObjectPath(package));
        if (!current(material))
        {
            if (!generate) { errors.Add(TEXT("原生父材质缺失或签名过期：") + package); break; }
            if (!material)
            {
                const FString source_package = TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_")
                    + FString(index >= 3 ? TEXT("Face_") : TEXT("")) + blends[index%3];
                UMaterial* source = LoadObject<UMaterial>(nullptr, *ObjectPath(source_package));
                if (!CheckMaterial(source,blend_modes[index%3])) { errors.Add(TEXT("原生只读模板不符合合同：")+source_package); break; }
                material = DuplicateObject<UMaterial>(source, CreatePackage(*package), *FPackageName::GetLongPackageAssetName(package));
                FAssetRegistryModule::AssetCreated(material);
            }
            FindToon(material)->ToonProfile = profiles[0].Get();
            for (UMaterialExpression* expression : material->GetExpressionCollection().Expressions)
            {
                if (auto* custom = Cast<UMaterialExpressionCustom>(expression))
                {
                    if (custom->Code.Contains(TEXT("geometry.PrepareSurface(")))
                    {
                        custom->Code.ReplaceInline(TEXT("/Core/TMMDEndfieldNative.ush"), TEXT("/Core/TMMDInfinityNikkiGeometry.ush"));
                        custom->Code.ReplaceInline(TEXT("TMMDEndfieldNative geometry;"), TEXT("TMMDInfinityNikkiGeometry geometry;"));
                        custom->Description = TEXT("暖暖连续几何柔肤法线与独立投射覆盖");
                    }
                }
                if (auto* scalar = Cast<UMaterialExpressionScalarParameter>(expression))
                    if (scalar->ParameterName == TEXT("ToonSkinEnabled") || scalar->ParameterName == TEXT("ToonFaceSkinEnabled"))
                        scalar->DefaultValue = 0.f;
            }
            material->StateId = FGuid::NewGuid();
            material->PostEditChange();
            if (!Save(material,signature,allowed,written,error)) { errors.Add(error); break; }
        }
        if (!CheckMaterial(material,blend_modes[index%3])) { errors.Add(TEXT("原生材质图无效：")+package); break; }
        parents.Emplace(material);
    }
    TStrongObjectPtr<UMMDStageRenderPreset> preset;
    if (errors.IsEmpty())
    {
        UMMDStageRenderPreset* source = LoadObject<UMMDStageRenderPreset>(nullptr, TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater.DA_Render_SakuraTheater"));
        preset.Reset(LoadObject<UMMDStageRenderPreset>(nullptr, *ObjectPath(preset_package)));
        TStrongObjectPtr<UMMDStageRenderPreset> expected(source ? DuplicateObject<UMMDStageRenderPreset>(source,GetTransientPackage()) : nullptr);
        if (!expected.IsValid()) errors.Add(TEXT("只读舞台模板缺失"));
        else
        {
            ConfigurePreset(expected.Get(),parents,profiles);
            if (!current(preset.Get()) || !PresetMatches(preset.Get(),expected.Get()))
            {
                if (!generate) errors.Add(TEXT("原生暖暖预设缺失或过期"));
                else
                {
                    if (!preset.IsValid()) { preset.Reset(NewObject<UMMDStageRenderPreset>(CreatePackage(preset_package),TEXT("DA_Render_InfinityNikki"),RF_Public|RF_Standalone)); FAssetRegistryModule::AssetCreated(preset.Get()); }
                    for (TFieldIterator<FProperty> it(UMMDStageRenderPreset::StaticClass(),EFieldIterationFlags::None);it;++it)
                        it->CopyCompleteValue_InContainer(preset.Get(),expected.Get());
                    if (!Save(preset.Get(),signature,allowed,written,error)) errors.Add(error);
                }
            }
        }
    }
    if (errors.IsEmpty() && GShaderCompilingManager)
    {
        for (auto& parent : parents) parent->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
        FAssetCompilingManager::Get().FinishAllCompilation();
        GShaderCompilingManager->FinishAllCompilation();
        FlushRenderingCommands();
        for (int32 index=0;index<parents.Num();++index)
        {
            auto* parent=parents[index].Get();
            const FMaterialResource* resource=parent->GetMaterialResource(GMaxRHIShaderPlatform);
            const FMaterialShaderMap* map=resource ? resource->GetGameThreadShaderMap() : nullptr;
            const bool ready=resource && resource->IsCompilationFinished() && resource->GetCompileErrors().IsEmpty()
                && map && map->CompiledSuccessfully() && map->IsCompilationFinalized() && map->IsValidForRendering(false) && map->GetShaderNum()>0;
            auto item=MakeShared<FJsonObject>();
            item->SetStringField(TEXT("asset"),parent->GetPathName());
            item->SetBoolField(TEXT("passed"),ready);
            item->SetNumberField(TEXT("shader_count"),map ? map->GetShaderNum() : 0);
            item->SetStringField(TEXT("path"),TEXT("Substrate FrontMaterial; shared native per-light backend"));
            material_reports.Add(MakeShared<FJsonValueObject>(item));
            if (!ready) { errors.Add(TEXT("SM6 编译失败：")+parent->GetPathName()); if(resource) errors.Append(resource->GetCompileErrors()); }
        }
    }
    else if(errors.IsEmpty()) errors.Add(TEXT("需要真实 SM6 RHI"));
    auto report=MakeShared<FJsonObject>();
    report->SetBoolField(TEXT("success"),errors.IsEmpty());
    report->SetBoolField(TEXT("generated"),generate);
    report->SetNumberField(TEXT("written_count"),written.Num());
    report->SetNumberField(TEXT("profile_count"),profiles.Num());
    report->SetStringField(TEXT("source_signature"),signature);
    report->SetStringField(TEXT("preset"),ObjectPath(preset_package));
    report->SetStringField(TEXT("render_path"),TEXT("native_substrate"));
    report->SetNumberField(TEXT("backend_protocol"),MMDEndfieldNativeBackend::CookedGlobalShaderProtocolVersion);
    report->SetArrayField(TEXT("materials"),material_reports);
    report->SetArrayField(TEXT("written"),written);
    TArray<TSharedPtr<FJsonValue>> json_errors;
    for(const auto& failure:errors) { json_errors.Add(MakeShared<FJsonValueString>(failure)); UE_LOG(LogTemp,Error,TEXT("[InfinityNikkiNative] %s"),*failure); }
    report->SetArrayField(TEXT("errors"),json_errors);
    FString json; FJsonSerializer::Serialize(report,TJsonWriterFactory<>::Create(&json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(report_path),true);
    if(!FFileHelper::SaveStringToFile(json,*report_path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return 1;
    return errors.IsEmpty() ? 0 : 1;
}
