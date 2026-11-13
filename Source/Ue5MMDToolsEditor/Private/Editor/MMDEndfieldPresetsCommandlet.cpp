// 终末地角色父材质与预设生成器。
//
// 与旧的 MMDEndfieldPhase2（只在 Saved 私有目录、机械隔离旧算法）不同，本生成器：
//   1. 从零构建材质图，接线 TMMDEndfieldCharacter.usf 的全部 64 个输入；
//   2. 把三套父材质与预设**正式写入插件 Content**，随插件分发，打开即可用；
//   3. 可选贴图（SDF / Ramp / 场景深度）默认走程序化兜底，连上资源即启用真实做法；
//   4. 只有显式 -Generate 才写盘，且写入范围严格限定在下方的白名单内。
//
// 用法：
//   编译 Editor 模块后执行
//   UnrealEditor-Cmd.exe <项目>.uproject -run=MMDEndfieldPresets -Generate -Report=<JSON路径>
//   省略 -Generate 时只做校验（不写任何资产），用于 CI/预检。
#include "MMDEndfieldPresetsCommandlet.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialShared.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RHI.h"
#include "RHIShaderPlatform.h"
#include "RHIStrings.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompiler.h"
#include "Stage/MMDStageRenderPreset.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
    // 严格写入白名单：只有这四个包允许被本 commandlet 创建或覆盖。
    const TCHAR* EndfieldAllowedPackages[] = {
        TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Opaque"),
        TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Masked"),
        TEXT("/Ue5MMDTools/Stages/Materials/Endfield/M_MMD_Endfield_Translucent"),
        TEXT("/Ue5MMDTools/Stages/Presets/Endfield/DA_Render_Endfield") };

    const TCHAR* EndfieldCore = TEXT("/Plugin/Ue5MMDTools/Core/TMMDEndfieldLighting.ush");
    const TCHAR* EndfieldCharacter = TEXT("/Plugin/Ue5MMDTools/TMMDShader/TMMDEndfieldCharacter.usf");

    // 主着色节点的输入顺序必须与 TMMDEndfieldCharacter.usf 的 OutputColor 形参一致。
    const TCHAR* MainInputNames[] = {
        // 通用 toon 参数（沿用既有约定，保证预设可复用）
        TEXT("base_color"), TEXT("light_data_tex"), TEXT("mmd_shadow_map"), TEXT("mat_cap"),
        TEXT("shadow_threshold"), TEXT("shadow_softness"), TEXT("shadow_tint"), TEXT("ambient_strength"),
        TEXT("light_scale"), TEXT("specular_strength"), TEXT("specular_power"), TEXT("rim_strength"), TEXT("rim_power"),
        TEXT("toon_amount"), TEXT("diffuse_wrap"), TEXT("color_saturation"), TEXT("shadow_saturation"),
        TEXT("roughness"), TEXT("specular_tint"), TEXT("sphere_mode"), TEXT("sphere_strength"),
        TEXT("black_lift"), TEXT("style_family"), TEXT("material_role"),
        TEXT("environment_reflection"), TEXT("environment_specular_strength"),
        // 终末地专属：头部基、阴影硬化、分路强度
        TEXT("head_forward_ws"), TEXT("head_right_ws"), TEXT("head_position_radius"),
        TEXT("scene_shadow_sharpness"), TEXT("half_lambert_shadow_sharpness"),
        TEXT("direct_lighting_intensity"), TEXT("gi_intensity"),
        TEXT("rim_intensity"), TEXT("rim_width"), TEXT("rim_color"),
        TEXT("flip_sdf_threshold"), TEXT("sdf_shadow_center"), TEXT("sdf_sharpness"),
        // 各向异性（头发主导特征）与皮肤透散
        TEXT("anisotropy_value"), TEXT("anisotropy_intensity"), TEXT("anisotropy_direction"),
        TEXT("anisotropic_hl_power"), TEXT("anisotropic_hl_threshold"),
        TEXT("anisotropic_hl_intensity"), TEXT("anisotropic_hl_color"),
        TEXT("sss_radius"), TEXT("sss_value"), TEXT("sss_color"),
        // 可选贴图开关与遮罩
        TEXT("use_sdf_map"), TEXT("use_ramp_map"), TEXT("use_scene_depth_rim"),
        TEXT("character_rim_mask"), TEXT("face_occlusion"),
        TEXT("diffuse_ramp_map"), TEXT("sdf_map"), TEXT("scene_depth_tex") };

    constexpr int32 MainInputCount = UE_ARRAY_COUNT(MainInputNames);

    FString EndfieldObjectPath(const FString& Package)
    {
        return Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    }

    template<typename T>
    T* EndfieldAddExpression(UMaterial* Material)
    {
        T* Expression = NewObject<T>(Material);
        Expression->Material = Material;
        Material->GetExpressionCollection().Expressions.Add(Expression);
        return Expression;
    }

    UMaterialExpressionScalarParameter* EndfieldAddScalar(UMaterial* Material, const TCHAR* Name, float Value,
                                                  const TCHAR* Group = TEXT("Endfield character"),
                                                  float Min = 0.0f, float Max = 0.0f)
    {
        UMaterialExpressionScalarParameter* Parameter = EndfieldAddExpression<UMaterialExpressionScalarParameter>(Material);
        Parameter->ParameterName = Name;
        Parameter->ExpressionGUID = FGuid::NewGuid();
        Parameter->Group = Group;
        Parameter->DefaultValue = Value;
        if (Max > Min)
        {
            Parameter->SliderMin = Min;
            Parameter->SliderMax = Max;
        }
        return Parameter;
    }

    UMaterialExpressionVectorParameter* EndfieldAddVector(UMaterial* Material, const TCHAR* Name, const FLinearColor& Value,
                                                  const TCHAR* Group = TEXT("Endfield character"))
    {
        UMaterialExpressionVectorParameter* Parameter = EndfieldAddExpression<UMaterialExpressionVectorParameter>(Material);
        Parameter->ParameterName = Name;
        Parameter->ExpressionGUID = FGuid::NewGuid();
        Parameter->Group = Group;
        Parameter->DefaultValue = Value;
        return Parameter;
    }

    // UMaterialExpressionTextureObjectParameter 的 Texture 成员是 UTexture*，
    // 因此接受 UTexture*（灯光 RT 与阴影 RT 都是 UTextureRenderTarget2D，不是 UTexture2D）。
    UMaterialExpressionTextureObjectParameter* EndfieldAddTexture(UMaterial* Material, const TCHAR* Name,
                                                          UTexture* DefaultTexture,
                                                          EMaterialSamplerType SamplerType = SAMPLERTYPE_Color)
    {
        UMaterialExpressionTextureObjectParameter* Texture = EndfieldAddExpression<UMaterialExpressionTextureObjectParameter>(Material);
        Texture->ParameterName = Name;
        Texture->ExpressionGUID = FGuid::NewGuid();
        Texture->Group = TEXT("Optional Endfield inputs");
        Texture->Texture = DefaultTexture;
        Texture->SamplerType = SamplerType;
        return Texture;
    }

    void EndfieldAddCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* Name,
                        UMaterialExpression* Expression, int32 OutputIndex = 0)
    {
        if (!Expression)
            return;
        FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
        Input.InputName = Name;
        Input.Input.Connect(OutputIndex, Expression);
    }

    /**
     * 构建/刷新一个终末地父材质。
     *
     * @param Package         目标包（必须在白名单内）。
     * @param Name            资产名。
     * @param Blend           混合模式。
     * @param WhiteTexture    base_color 默认白图。
     * @param BlackTexture    mat_cap / 可选贴图默认黑图。
     * @param LightData       灯光数据 RT。
     * @param ShadowMap       插件主光阴影 RT。
     * @param SceneDepth      场景深度（可选；缺失走几何菲涅耳轮廓）。
     * @param bGenerate       false 时只校验不写盘。
     * @param Error           失败原因。
     * @return 成功返回材质，失败返回 nullptr。
     */
    UMaterial* BuildEndfieldMaterial(const FString& Package, const FString& Name, EBlendMode Blend,
                                     UTexture2D* WhiteTexture, UTexture2D* BlackTexture,
                                     UTextureRenderTarget2D* LightData, UTextureRenderTarget2D* ShadowMap,
                                     UTexture* SceneDepth, const FString& Signature,
                                     bool bGenerate, FString& Error)
    {
        const bool bExists = FPackageName::DoesPackageExist(Package);
        UMaterial* Material = bExists ? LoadObject<UMaterial>(nullptr, *EndfieldObjectPath(Package)) : nullptr;
        if (bExists && !Material)
        {
            Error = FString::Printf(TEXT("无法加载已存在的父材质：%s"), *Package);
            return nullptr;
        }
        // 已存在的资产签名一致时无需重建，避免无意义重写与 shader 重编。
        if (Material)
        {
            UMaterialExpressionCustom* Existing = Cast<UMaterialExpressionCustom>(
                Material->GetEditorOnlyData()->EmissiveColor.Expression);
            if (Existing && Existing->Code.StartsWith(Signature + TEXT("\n"))
                && Existing->Inputs.Num() == MainInputCount)
                return Material;
        }
        if (!bGenerate)
        {
            Error = FString::Printf(TEXT("父材质缺失或签名过期（需 -Generate）：%s"), *Package);
            return nullptr;
        }
        if (Material)
        {
            // 复用现有包，清空旧表达式与注释后重建，保证图结构完全由本函数决定。
            Material->GetExpressionCollection().Empty();
        }
        else
        {
            Material = NewObject<UMaterial>(CreatePackage(*Package), *Name, RF_Public | RF_Standalone);
            if (!Material)
            {
                Error = FString::Printf(TEXT("无法创建父材质：%s"), *Package);
                return nullptr;
            }
            FArchiveUObject InitialArchive;
            Material->Serialize(InitialArchive);
            FAssetRegistryModule::AssetCreated(Material);
        }

        Material->MaterialDomain = MD_Surface;
        Material->BlendMode = Blend;
        // 完全在 Custom 节点内自算光照，因此使用 Unlit，不依赖引擎延迟光照。
        Material->SetShadingModel(MSM_Unlit);
        Material->TwoSided = true;
        Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
        Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
        // 0.1 比阶段的 0.01 更稳健：避免 MMD 半透明边缘在 Masked 下出现锯齿孔洞。
        Material->OpacityMaskClipValue = 0.1f;
        Material->bCastDynamicShadowAsMasked = Blend == BLEND_Translucent;

        // base_color：贴图 × 实例色，与导入材质管线兼容。
        // 必须用 TextureSampleParameter2D（采样出颜色）；TextureObjectParameter 是纹理对象，
        // 参与 Multiply 会报 "arithmetic on non-primitive types: texture2D"。
        UMaterialExpressionTextureSampleParameter2D* BaseTexture = EndfieldAddExpression<UMaterialExpressionTextureSampleParameter2D>(Material);
        BaseTexture->ParameterName = TEXT("base_color_tex");
        BaseTexture->ExpressionGUID = FGuid::NewGuid();
        BaseTexture->Group = TEXT("Imported material");
        BaseTexture->Texture = WhiteTexture;
        BaseTexture->SamplerType = SAMPLERTYPE_Color;
        UMaterialExpressionVectorParameter* BaseColor = EndfieldAddVector(Material, TEXT("BaseColor"), FLinearColor::White,
                                                                 TEXT("Imported material"));
        UMaterialExpressionMultiply* TintedBase = EndfieldAddExpression<UMaterialExpressionMultiply>(Material);
        TintedBase->A.Connect(0, BaseTexture);
        TintedBase->B.Connect(0, BaseColor);

        UMaterialExpressionScalarParameter* Opacity = EndfieldAddScalar(Material, TEXT("Opacity"), 1.0f, TEXT("Imported material"));
        UMaterialExpressionMultiply* FinalOpacity = EndfieldAddExpression<UMaterialExpressionMultiply>(Material);
        // 输出索引 4 = 采样节点的 A（alpha 通道）。
        FinalOpacity->A.Connect(4, BaseTexture);
        FinalOpacity->B.Connect(0, Opacity);

        UMaterialExpressionCustom* Shader = EndfieldAddExpression<UMaterialExpressionCustom>(Material);
        Shader->OutputType = CMOT_Float3;
        Shader->Description = TEXT("Endfield 角色着色");
        Shader->Code = Signature + TEXT("\n#include \"") + EndfieldCharacter + TEXT("\"\nreturn 0;");
        Shader->Inputs.Reset();

        // ---- 按 MainInputNames 顺序接线；每个输入都有默认值兜底 ----
        EndfieldAddCustomInput(Shader, TEXT("base_color"), TintedBase);
        EndfieldAddCustomInput(Shader, TEXT("light_data_tex"), EndfieldAddTexture(Material, TEXT("light_data_tex"), LightData));
        EndfieldAddCustomInput(Shader, TEXT("mmd_shadow_map"), EndfieldAddTexture(Material, TEXT("mmd_shadow_map"), ShadowMap));
        EndfieldAddCustomInput(Shader, TEXT("mat_cap"), EndfieldAddTexture(Material, TEXT("mat_cap"), BlackTexture));
        EndfieldAddCustomInput(Shader, TEXT("shadow_threshold"), EndfieldAddScalar(Material, TEXT("ShadowThreshold"), 0.43f, TEXT("Toon"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("shadow_softness"), EndfieldAddScalar(Material, TEXT("ShadowSoftness"), 0.10f, TEXT("Toon"), 0.001f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("shadow_tint"), EndfieldAddVector(Material, TEXT("ShadowTint"), FLinearColor(0.67f, 0.73f, 0.84f), TEXT("Toon")));
        EndfieldAddCustomInput(Shader, TEXT("ambient_strength"), EndfieldAddScalar(Material, TEXT("AmbientStrength"), 0.12f, TEXT("Toon"), 0.0f, 2.0f));
        EndfieldAddCustomInput(Shader, TEXT("light_scale"), EndfieldAddScalar(Material, TEXT("LightScale"), 1.0f, TEXT("Toon"), 0.0f, 4.0f));
        EndfieldAddCustomInput(Shader, TEXT("specular_strength"), EndfieldAddScalar(Material, TEXT("SpecularStrength"), 0.40f, TEXT("Toon"), 0.0f, 1.25f));
        EndfieldAddCustomInput(Shader, TEXT("specular_power"), EndfieldAddScalar(Material, TEXT("SpecularPower"), 64.0f, TEXT("Toon"), 1.0f, 512.0f));
        EndfieldAddCustomInput(Shader, TEXT("rim_strength"), EndfieldAddScalar(Material, TEXT("RimStrength"), 0.14f, TEXT("Toon"), 0.0f, 0.5f));
        EndfieldAddCustomInput(Shader, TEXT("rim_power"), EndfieldAddScalar(Material, TEXT("RimPower"), 3.2f, TEXT("Toon"), 0.1f, 16.0f));
        EndfieldAddCustomInput(Shader, TEXT("toon_amount"), EndfieldAddScalar(Material, TEXT("ToonAmount"), 1.0f, TEXT("Toon"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("diffuse_wrap"), EndfieldAddScalar(Material, TEXT("DiffuseWrap"), 0.10f, TEXT("Toon"), 0.0f, 0.5f));
        EndfieldAddCustomInput(Shader, TEXT("color_saturation"), EndfieldAddScalar(Material, TEXT("ColorSaturation"), 1.02f, TEXT("Toon"), 0.0f, 2.0f));
        EndfieldAddCustomInput(Shader, TEXT("shadow_saturation"), EndfieldAddScalar(Material, TEXT("ShadowSaturation"), 0.90f, TEXT("Toon"), 0.0f, 2.0f));
        EndfieldAddCustomInput(Shader, TEXT("roughness"), EndfieldAddScalar(Material, TEXT("Roughness"), 0.55f, TEXT("Toon"), 0.04f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("specular_tint"), EndfieldAddVector(Material, TEXT("SpecularTint"), FLinearColor(0.82f, 0.92f, 1.10f), TEXT("Toon")));
        EndfieldAddCustomInput(Shader, TEXT("sphere_mode"), EndfieldAddScalar(Material, TEXT("sphere_mode"), 0.0f, TEXT("Imported material"), 0.0f, 3.0f));
        EndfieldAddCustomInput(Shader, TEXT("sphere_strength"), EndfieldAddScalar(Material, TEXT("SphereStrength"), 0.10f, TEXT("Imported material"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("black_lift"), EndfieldAddScalar(Material, TEXT("BlackLift"), 0.035f, TEXT("Toon"), 0.0f, 0.035f));
        // style_family = 2 即 Endfield 风格族（与导入管线约定一致）。
        EndfieldAddCustomInput(Shader, TEXT("style_family"), EndfieldAddScalar(Material, TEXT("style_family"), 2.0f, TEXT("Toon"), 0.0f, 3.0f));
        EndfieldAddCustomInput(Shader, TEXT("material_role"), EndfieldAddScalar(Material, TEXT("MaterialRole"), 0.0f, TEXT("Material role"), 0.0f, 5.0f));
        // environment_reflection 由 SkyLightEnvMapSample 提供时画面更准；默认常量保证可编译。
        UMaterialExpressionConstant* NoReflection = EndfieldAddExpression<UMaterialExpressionConstant>(Material);
        NoReflection->R = 0.0f;
        EndfieldAddCustomInput(Shader, TEXT("environment_reflection"), NoReflection);
        EndfieldAddCustomInput(Shader, TEXT("environment_specular_strength"), EndfieldAddScalar(Material, TEXT("EnvironmentSpecularStrength"), 0.35f, TEXT("Toon"), 0.0f, 2.0f));

        // 头部几何基：由 MMDHeadBoneSubsystem 每帧写入；默认零向量会让脸部效果自动退化。
        const TCHAR* HeadParameters[] = { TEXT("MMDHeadForward"), TEXT("MMDHeadRight"), TEXT("MMDHeadPositionRadius") };
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(HeadParameters); ++Index)
        {
            UMaterialExpressionVectorParameter* Head = EndfieldAddVector(Material, HeadParameters[Index],
                                                                FLinearColor(0, 0, 0, 0), TEXT("Geometric head frame"));
            // VectorParameter 的 RGBA 输出索引为 5，必须保留 W（半径），不能用 RGB。
            EndfieldAddCustomInput(Shader, MainInputNames[26 + Index], Head, 5);
        }

        EndfieldAddCustomInput(Shader, TEXT("scene_shadow_sharpness"), EndfieldAddScalar(Material, TEXT("SceneShadowSharpness"), 0.10f, TEXT("Endfield shadow"), 0.05f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("half_lambert_shadow_sharpness"), EndfieldAddScalar(Material, TEXT("HalfLambertShadowSharpness"), 0.10f, TEXT("Endfield shadow"), 0.05f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("direct_lighting_intensity"), EndfieldAddScalar(Material, TEXT("DirectLightingIntensity"), 1.0f, TEXT("Endfield shadow"), 0.0f, 5.0f));
        EndfieldAddCustomInput(Shader, TEXT("gi_intensity"), EndfieldAddScalar(Material, TEXT("GIIntensity"), 0.85f, TEXT("Endfield shadow"), 0.0f, 5.0f));
        EndfieldAddCustomInput(Shader, TEXT("rim_intensity"), EndfieldAddScalar(Material, TEXT("RimIntensity"), 1.0f, TEXT("Endfield rim"), 0.1f, 10.0f));
        EndfieldAddCustomInput(Shader, TEXT("rim_width"), EndfieldAddScalar(Material, TEXT("RimWidth"), 1.0f, TEXT("Endfield rim"), 0.1f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("rim_color"), EndfieldAddVector(Material, TEXT("RimColor"), FLinearColor::White, TEXT("Endfield rim")));

        // 脸部 SDF：默认关闭（走半兰伯特），连上烘焙好的 SDF 后把开关设为 1 即启用。
        EndfieldAddCustomInput(Shader, TEXT("flip_sdf_threshold"), EndfieldAddScalar(Material, TEXT("FlipSDFThreshold"), 0.0f, TEXT("Endfield face SDF"), -1.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("sdf_shadow_center"), EndfieldAddScalar(Material, TEXT("SDFShadowCenter"), 1.0f, TEXT("Endfield face SDF"), -1.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("sdf_sharpness"), EndfieldAddScalar(Material, TEXT("SDFSharpness"), 0.10f, TEXT("Endfield face SDF"), 0.05f, 1.0f));

        EndfieldAddCustomInput(Shader, TEXT("anisotropy_value"), EndfieldAddScalar(Material, TEXT("AnisotropyValue"), 0.35f, TEXT("Endfield anisotropy"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropy_intensity"), EndfieldAddScalar(Material, TEXT("AnisotropyIntensity"), 1.0f, TEXT("Endfield anisotropy"), 0.0f, 3.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropy_direction"), EndfieldAddScalar(Material, TEXT("AnisotropyDirection"), 0.0f, TEXT("Endfield anisotropy"), -1.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropic_hl_power"), EndfieldAddScalar(Material, TEXT("AnisotropicHLPower"), 5.0f, TEXT("Endfield anisotropy"), 1.0f, 32.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropic_hl_threshold"), EndfieldAddScalar(Material, TEXT("AnisotropicHLThreshold"), 0.995f, TEXT("Endfield anisotropy"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropic_hl_intensity"), EndfieldAddScalar(Material, TEXT("AnisotropicHLIntensity"), 1.0f, TEXT("Endfield anisotropy"), 0.0f, 4.0f));
        EndfieldAddCustomInput(Shader, TEXT("anisotropic_hl_color"), EndfieldAddVector(Material, TEXT("AnisotropicHLColor"), FLinearColor::White, TEXT("Endfield anisotropy")));

        // 皮肤透散默认关闭（与真实默认一致），需要时提高 SSSValue。
        EndfieldAddCustomInput(Shader, TEXT("sss_radius"), EndfieldAddScalar(Material, TEXT("SSSRadius"), 1.0f, TEXT("Endfield skin"), 0.0f, 10.0f));
        EndfieldAddCustomInput(Shader, TEXT("sss_value"), EndfieldAddScalar(Material, TEXT("SSSValue"), 0.0f, TEXT("Endfield skin"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("sss_color"), EndfieldAddVector(Material, TEXT("SSSColor"), FLinearColor(1.0f, 0.76f, 0.63f), TEXT("Endfield skin")));

        // 可选贴图开关：默认全 0，只连 base_color 也能完整着色。
        EndfieldAddCustomInput(Shader, TEXT("use_sdf_map"), EndfieldAddScalar(Material, TEXT("UseSdfMap"), 0.0f, TEXT("Optional Endfield inputs"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("use_ramp_map"), EndfieldAddScalar(Material, TEXT("UseRampMap"), 0.0f, TEXT("Optional Endfield inputs"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("use_scene_depth_rim"), EndfieldAddScalar(Material, TEXT("UseSceneDepthRim"), 0.0f, TEXT("Optional Endfield inputs"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("character_rim_mask"), EndfieldAddScalar(Material, TEXT("CharacterRimMask"), 1.0f, TEXT("Optional Endfield inputs"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("face_occlusion"), EndfieldAddScalar(Material, TEXT("FaceOcclusion"), 0.0f, TEXT("Optional Endfield inputs"), 0.0f, 1.0f));
        EndfieldAddCustomInput(Shader, TEXT("diffuse_ramp_map"), EndfieldAddTexture(Material, TEXT("DiffuseRampMap"), BlackTexture));
        EndfieldAddCustomInput(Shader, TEXT("sdf_map"), EndfieldAddTexture(Material, TEXT("SDFMap"), BlackTexture));
        // 纹理参数必须绑定一张有效贴图：空纹理会让引擎报 "Missing texture" 并导致整个材质编译失败。
        // 因此场景深度默认绑黑图，配合 UseSceneDepthRim = 0 关闭采样。
        EndfieldAddCustomInput(Shader, TEXT("scene_depth_tex"),
                       EndfieldAddTexture(Material, TEXT("SceneDepthTexture"),
                                  SceneDepth ? SceneDepth : BlackTexture, SAMPLERTYPE_LinearGrayscale));

        if (Shader->Inputs.Num() != MainInputCount)
        {
            Error = FString::Printf(TEXT("自定义节点输入数量不符：%d != %d"), Shader->Inputs.Num(), MainInputCount);
            return nullptr;
        }

        UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
        Data->EmissiveColor.Connect(0, Shader);
        Data->Opacity.Connect(0, FinalOpacity);
        Data->OpacityMask.Connect(0, FinalOpacity);
        Material->StateId = FGuid::NewGuid();
        Material->PostEditChange();
        return Material;
    }

    bool EndfieldSaveAsset(UObject* Asset, const FString& Package, FString& Error)
    {
        bool bAllowed = false;
        for (const TCHAR* Allowed : EndfieldAllowedPackages) bAllowed |= Package == Allowed;
        if (!bAllowed)
        {
            Error = TEXT("拒绝写白名单之外的包：") + Package;
            return false;
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        Asset->SetFlags(RF_Public | RF_Standalone);
        Asset->ClearFlags(RF_Transient);
        Asset->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args))
        {
            Error = TEXT("无法保存资产：") + Package;
            return false;
        }
        return true;
    }

    /** 汇总专用 shader 源码签名；源码改变时必须重写材质。 */
    bool EndfieldBuildSignature(FString& OutSignature, FString& Error)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
        if (!Plugin)
        {
            Error = TEXT("找不到 Ue5MMDTools 插件");
            return false;
        }
        FString Bundle;
        // 材质图结构版本：本函数里的接线方式一旦改变（节点类型、输出索引、输入顺序），
        // 必须同时提升此版本号，否则签名不变会被判定为"无需重建"而复用旧图。
        Bundle += TEXT("EndfieldMaterialGraphVersion=2\n");
        for (const TCHAR* Relative : { TEXT("Shaders/TMMDShader/TMMDEndfieldCharacter.usf"),
            TEXT("Shaders/Core/MMDToonLighting.ush"), TEXT("Shaders/Core/TMMDEndfieldLighting.ush") })
        {
            FString Source;
            if (!FFileHelper::LoadFileToString(Source, *FPaths::Combine(Plugin->GetBaseDir(), Relative)) || Source.IsEmpty())
            {
                Error = FString::Printf(TEXT("无法读取专用 shader：%s"), Relative);
                return false;
            }
            Bundle += FString(Relative) + TEXT("\n") + Source + TEXT("\n");
        }
        FTCHARToUTF8 Utf8(*Bundle);
        OutSignature = TEXT("// 终末地源码签名：") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        return true;
    }

    void EndfieldConfigurePreset(UMMDStageRenderPreset* Preset, const TArray<TStrongObjectPtr<UMaterial>>& Parents)
    {
        Preset->DisplayName = FText::FromString(TEXT("终末地"));
        // 保留樱庭水镜的舞台资源引用，但不切换主题，避免替换用户当前舞台。
        Preset->Theme = EMMDStageTheme::SakuraTheater;
        Preset->bOverrideStageTheme = false;
        Preset->CharacterMaterialOpaque = Parents[0].Get();
        Preset->CharacterMaterialMasked = Parents[1].Get();
        Preset->CharacterMaterialTranslucent = Parents[2].Get();
        Preset->bUseMaskedHair = true;
        Preset->bCharacterSelfShadow = true;
        Preset->bCharacterOutline = true;
        // 终末地：主光偏冷白、补光冷色、轮廓光强且靠背，配合低 GI 的分离感。
        Preset->KeyLightColor = FLinearColor(1.00f, 0.96f, 0.92f);
        Preset->KeyLightRotation = FRotator(-32.0f, 48.0f, 0.0f);
        Preset->KeyLightIntensity = 3.60f;
        Preset->FillLightColor = FLinearColor(0.62f, 0.72f, 0.92f);
        Preset->FillLightRotation = FRotator(-8.0f, 135.0f, 0.0f);
        Preset->FillLightIntensity = 0.30f;
        Preset->RimLightColor = FLinearColor(0.42f, 0.78f, 1.00f);
        Preset->RimLightRotation = FRotator(-9.0f, -100.0f, 0.0f);
        Preset->RimLightIntensity = 1.05f;
        Preset->AccentLightIntensity = 0.0f;
        Preset->ExposureBias = 0.10f;
        Preset->BloomIntensity = 0.22f;
        Preset->ColorGradingContrast = 1.04f;
        Preset->ColorGradingSaturation = 1.06f;
        Preset->ColorGradingGamma = 1.0f;
        Preset->ColorGradingGain = FLinearColor::White;
        Preset->WhiteTemperature = 6800.0f;
        Preset->FilmToe = 0.30f;
        // 终末地核心：低环境、硬转折、极窄半影。
        Preset->AmbientStrength = 0.10f;
        Preset->LightScale = 1.0f;
        Preset->ShadowThreshold = 0.43f;
        Preset->ShadowSoftness = 0.10f;
        Preset->ShadowTint = FLinearColor(0.67f, 0.73f, 0.84f);
        Preset->DiffuseWrap = 0.10f;
        Preset->ToonAmount = 1.0f;
        Preset->SpecularStrength = 0.40f;
        Preset->SpecularPower = 64.0f;
        Preset->Roughness = 0.55f;
        Preset->SpecularTint = FLinearColor(0.82f, 0.92f, 1.10f);
        Preset->RimStrength = 0.14f;
        Preset->RimPower = 3.20f;
        Preset->BlackLift = 0.035f;
        Preset->ColorSaturation = 1.02f;
        Preset->ShadowSaturation = 0.90f;
        Preset->SoftSpecularStrength = 0.10f;
        Preset->OutlinePixelWidth = 0.90f;
        Preset->OutlineColor = FLinearColor(0.014f, 0.016f, 0.024f);
        Preset->SkinOutlineColor = FLinearColor(0.060f, 0.022f, 0.016f);
        Preset->HairOutlineColor = FLinearColor(0.010f, 0.028f, 0.032f);
        Preset->ClothOutlineColor = FLinearColor(0.012f, 0.016f, 0.028f);
    }
}

UMMDEndfieldPresetsCommandlet::UMMDEndfieldPresetsCommandlet()
{
    IsClient = true;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMMDEndfieldPresetsCommandlet::Main(const FString& Params)
{
    const bool bGenerate = FParse::Param(*Params, TEXT("Generate"));
    FString ReportPath;
    if (!FParse::Value(*Params, TEXT("Report="), ReportPath) || ReportPath.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldPresets] 必须提供 Report=<JSON路径>"));
        return 1;
    }

    TArray<FString> Errors;
    TArray<TSharedPtr<FJsonValue>> Written;
    TArray<TSharedPtr<FJsonValue>> MaterialReports;
    TArray<TSharedPtr<FJsonValue>> AssetReports;
    TArray<TStrongObjectPtr<UMaterial>> Parents;
    FString Signature;
    FString Error;

    if (!EndfieldBuildSignature(Signature, Error))
        Errors.Add(Error);

    // 需要的共享资源；缺失即报错，不静默生成错误外观的材质。
    UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
    UTexture2D* BlackTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/Black.Black"));
    UTextureRenderTarget2D* LightData = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT"));
    UTextureRenderTarget2D* ShadowMap = LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT"));
    if (!WhiteTexture) Errors.Add(TEXT("缺少引擎默认白图"));
    if (!BlackTexture) Errors.Add(TEXT("缺少引擎默认黑图"));
    if (!LightData) Errors.Add(TEXT("缺少灯光数据 RT：/Ue5MMDTools/Rendering/LightDataRT"));
    if (!ShadowMap) Errors.Add(TEXT("缺少阴影 RT：/Ue5MMDTools/Rendering/MMDShadowMapRT"));

    const TCHAR* AssetNames[] = { TEXT("M_MMD_Endfield_Opaque"), TEXT("M_MMD_Endfield_Masked"),
                                 TEXT("M_MMD_Endfield_Translucent") };
    const EBlendMode BlendModes[] = { BLEND_Opaque, BLEND_Masked, BLEND_Translucent };

    if (Errors.IsEmpty())
    {
        for (int32 Index = 0; Index < 3; ++Index)
        {
            UMaterial* Material = BuildEndfieldMaterial(EndfieldAllowedPackages[Index], AssetNames[Index], BlendModes[Index],
                                                        WhiteTexture, BlackTexture, LightData, ShadowMap,
                                                        nullptr, Signature, bGenerate, Error);
            if (!Material)
            {
                Errors.Add(Error);
                break;
            }
            Parents.Emplace(Material);
            if (bGenerate)
            {
                if (!EndfieldSaveAsset(Material, EndfieldAllowedPackages[Index], Error))
                {
                    Errors.Add(Error);
                    break;
                }
                Written.Add(MakeShared<FJsonValueString>(EndfieldAllowedPackages[Index]));
            }
        }
    }

    // ---- 预设 ----
    TStrongObjectPtr<UMMDStageRenderPreset> Preset;
    if (Errors.IsEmpty() && Parents.Num() == 3)
    {
        const FString Package = EndfieldAllowedPackages[3];
        const bool bExists = FPackageName::DoesPackageExist(Package);
        if (bExists)
            Preset.Reset(LoadObject<UMMDStageRenderPreset>(nullptr, *EndfieldObjectPath(Package)));
        TStrongObjectPtr<UMMDStageRenderPreset> Expected(NewObject<UMMDStageRenderPreset>(GetTransientPackage()));
        if (!Expected.IsValid())
            Errors.Add(TEXT("无法构造预设配置"));
        else
        {
            // 从只读的樱庭模板取舞台资源引用，再覆盖终末地参数，避免引用丢失。
            UMMDStageRenderPreset* Template = LoadObject<UMMDStageRenderPreset>(nullptr,
                TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_SakuraTheater.DA_Render_SakuraTheater"));
            if (Template)
                for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
                    It->CopyCompleteValue_InContainer(Expected.Get(), Template);
            EndfieldConfigurePreset(Expected.Get(), Parents);

            bool bNeedsWrite = !Preset.IsValid();
            if (Preset.IsValid())
            {
                for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
                    if (!It->Identical_InContainer(Preset.Get(), Expected.Get())) { bNeedsWrite = true; break; }
            }
            if (bNeedsWrite)
            {
                if (!bGenerate)
                    Errors.Add(TEXT("预设缺失或配置过期（需 -Generate）：") + Package);
                else
                {
                    if (!Preset.IsValid())
                    {
                        Preset.Reset(NewObject<UMMDStageRenderPreset>(CreatePackage(*Package), TEXT("DA_Render_Endfield"),
                                                                     RF_Public | RF_Standalone));
                        if (Preset.IsValid()) FAssetRegistryModule::AssetCreated(Preset.Get());
                    }
                    if (!Preset.IsValid())
                        Errors.Add(TEXT("无法创建预设：") + Package);
                    else
                    {
                        for (TFieldIterator<FProperty> It(UMMDStageRenderPreset::StaticClass(), EFieldIterationFlags::None); It; ++It)
                            It->CopyCompleteValue_InContainer(Preset.Get(), Expected.Get());
                        if (!EndfieldSaveAsset(Preset.Get(), Package, Error))
                            Errors.Add(Error);
                        else
                            Written.Add(MakeShared<FJsonValueString>(Package));
                    }
                }
            }
        }
    }

    // ---- 编译校验：必须真实编译出 SM6 shader map，否则不算通过 ----
    if (Errors.IsEmpty() && GShaderCompilingManager && FApp::CanEverRender() && !GUsingNullRHI)
    {
        for (const TStrongObjectPtr<UMaterial>& Parent : Parents)
            Parent->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
        FAssetCompilingManager::Get().FinishAllCompilation();
        GShaderCompilingManager->FinishAllCompilation();
        FlushRenderingCommands();
        for (const TStrongObjectPtr<UMaterial>& Parent : Parents)
        {
            TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> CompileErrors;
            const FMaterialResource* Resource = Parent->GetMaterialResource(GMaxRHIShaderPlatform);
            const FMaterialShaderMap* Map = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
            if (Resource)
                for (const FString& Failure : Resource->GetCompileErrors())
                    CompileErrors.Add(MakeShared<FJsonValueString>(Failure));
            if (!Resource || !Resource->IsCompilationFinished() || !Map || !Map->CompiledSuccessfully()
                || !Map->IsCompilationFinalized() || !Map->IsValidForRendering(false))
                CompileErrors.Add(MakeShared<FJsonValueString>(TEXT("材质资源未就绪或编译失败")));
            Item->SetStringField(TEXT("asset"), Parent->GetPathName());
            Item->SetStringField(TEXT("source_signature"), Signature);
            Item->SetNumberField(TEXT("main_inputs"), MainInputCount);
            Item->SetBoolField(TEXT("passed"), CompileErrors.IsEmpty());
            Item->SetArrayField(TEXT("errors"), CompileErrors);
            MaterialReports.Add(MakeShared<FJsonValueObject>(Item));
            if (!CompileErrors.IsEmpty())
                Errors.Add(TEXT("SM6 编译失败：") + Parent->GetPathName());
        }
    }
    else if (Errors.IsEmpty())
    {
        Errors.Add(TEXT("校验需要可渲染 RHI 与 shader 编译管理器"));
    }

    for (const TStrongObjectPtr<UMaterial>& Parent : Parents)
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("asset"), Parent->GetPathName());
        Item->SetStringField(TEXT("type"), TEXT("Material"));
        Item->SetBoolField(TEXT("passed"), Errors.IsEmpty()
            && FPackageName::DoesPackageExist(Parent->GetOutermost()->GetName()));
        AssetReports.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (Preset.IsValid())
    {
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("asset"), Preset->GetPathName());
        Item->SetStringField(TEXT("type"), TEXT("RenderPreset"));
        Item->SetBoolField(TEXT("passed"), Errors.IsEmpty()
            && FPackageName::DoesPackageExist(Preset->GetOutermost()->GetName()));
        AssetReports.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    const bool bSuccess = Errors.IsEmpty() && Parents.Num() == 3 && Preset.IsValid();
    Report->SetBoolField(TEXT("success"), bSuccess);
    Report->SetBoolField(TEXT("generated"), bGenerate);
    Report->SetStringField(TEXT("target"), TEXT("Endfield"));
    Report->SetStringField(TEXT("shader_platform"), LexToString(GMaxRHIShaderPlatform));
    Report->SetStringField(TEXT("preset"), Preset.IsValid() ? Preset->GetPathName() : TEXT("missing"));
    Report->SetStringField(TEXT("source_signature"), Signature);
    Report->SetNumberField(TEXT("main_input_count"), MainInputCount);
    Report->SetArrayField(TEXT("written_packages"), Written);
    Report->SetArrayField(TEXT("materials"), MaterialReports);
    Report->SetArrayField(TEXT("assets"), AssetReports);
    Report->SetNumberField(TEXT("failed"), Errors.Num());
    TArray<TSharedPtr<FJsonValue>> JsonErrors;
    for (const FString& Failure : Errors)
    {
        JsonErrors.Add(MakeShared<FJsonValueString>(Failure));
        UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldPresets] %s"), *Failure);
    }
    Report->SetArrayField(TEXT("errors"), JsonErrors);

    FString Json;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
    const bool bSaved = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        && FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Display, TEXT("[MMDEndfieldPresets] materials=%d writes=%d success=%d"),
        MaterialReports.Num(), Written.Num(), bSuccess && bSaved);
    return bSuccess && bSaved ? 0 : 1;
}
