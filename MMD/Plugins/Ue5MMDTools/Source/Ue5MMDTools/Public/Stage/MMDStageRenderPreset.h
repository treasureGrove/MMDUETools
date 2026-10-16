#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Stage/MMDMaterialRole.h"
#include "MMDStageRenderPreset.generated.h"

class UMaterialInterface;
class UToonProfile;

/** 从当前槽底色派生描边；角色细分可继续扩展，未配置时使用通用默认值。 */
USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDStageOutlineRoleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ClampMin = "0", ClampMax = "1"))
	float SourceBrightness = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ClampMin = "0", ClampMax = "2"))
	float SourceSaturation = 1.0f;
};

/** 原生 Toon 材质的部位参数；未指定 Profile 时沿用父材质。 */
USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDStageToonRoleSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon")
	TObjectPtr<UToonProfile> ToonProfile;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0.04", ClampMax = "1"))
	float Roughness = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float Specular = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float Metallic = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "-1", ClampMax = "1"))
	float Anisotropy = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float SphereBlend = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1", ToolTip = "有效自动 SDF 脸面在阴影贴图中的投影抑制；仍接收头发和场景阴影。1 关闭这些脸面的投影，也影响它们对身体和场景的投影。"))
	float FaceShadowSuppress = 0.0f;
};

/** 皮肤表面的线性底色与高光参数；关闭时保留原部位设置，不改变源贴图和透明度。 */
USTRUCT(BlueprintType)
struct UE5MMDTOOLS_API FMMDStageSkinSurfaceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin")
	bool bEnabled = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", HideAlphaChannel))
	FLinearColor Tint = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "1"))
	float TintStrength = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "2"))
	float AlbedoScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "2"))
	float Saturation = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", ClampMin = "0.04", ClampMax = "1"))
	float Roughness = 0.742f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (EditCondition = "bEnabled", ClampMin = "0", ClampMax = "1"))
	float Specular = 0.454f;
};

UENUM(BlueprintType)
enum class EMMDStageTheme : uint8
{
	NeonConcert UMETA(DisplayName = "霓虹演唱会"),
	MoonMirror UMETA(DisplayName = "月光镜庭"),
	SakuraTheater UMETA(DisplayName = "樱庭水镜")
};

/** 舞台、灯光和角色渲染共用的可分发预设。 */
UCLASS(BlueprintType)
class UE5MMDTOOLS_API UMMDStageRenderPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Stage")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Stage")
	EMMDStageTheme Theme = EMMDStageTheme::NeonConcert;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Stage")
	bool bOverrideStageTheme = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> CharacterMaterialOpaque;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> CharacterMaterialMasked;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> CharacterMaterialTranslucent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "启用 Substrate Toon Profile 时优先使用此父材质；未配置时沿用原不透明父材质。"))
	TObjectPtr<UMaterialInterface> CharacterToonMaterialOpaque;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "启用 Substrate Toon Profile 时优先使用此父材质；未配置时沿用原遮罩父材质。"))
	TObjectPtr<UMaterialInterface> CharacterToonMaterialMasked;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "启用 Substrate Toon Profile 时优先使用此父材质；未配置时沿用原半透明父材质。"))
	TObjectPtr<UMaterialInterface> CharacterToonMaterialTranslucent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials|Face", meta = (ToolTip = "有效脸部绑定使用的专用不透明 Toon 父材质；为空或混合模式不符时沿用普通父材质。"))
	TObjectPtr<UMaterialInterface> CharacterToonFaceMaterialOpaque;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials|Face", meta = (ToolTip = "有效脸部绑定使用的专用遮罩 Toon 父材质；保留原裁剪与显式阴影投射设置。"))
	TObjectPtr<UMaterialInterface> CharacterToonFaceMaterialMasked;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials|Face", meta = (ToolTip = "已验证透明附脸层的专用 Toon 父材质；必须保留原连续 alpha，不自动改为遮罩。"))
	TObjectPtr<UMaterialInterface> CharacterToonFaceMaterialTranslucent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ToolTip = "按材质部位选择原生 Toon Profile 和参数；缺少部位时使用 Generic，缺少 Generic 时使用安全默认值。"))
	TMap<EMMDMaterialRole, FMMDStageToonRoleSettings> CharacterToonRoles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Skin", meta = (ToolTip = "身体皮肤表面设置；脸部遮罩外继续使用这些参数，关闭时沿用原 Skin 部位参数。"))
	FMMDStageSkinSurfaceSettings CharacterSkinSurface;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon|Face", meta = (ToolTip = "仅在有效脸部投影遮罩内覆盖皮肤表面参数；暖暗部从实际底色派生，不伪称 SSS。"))
	FMMDStageSkinSurfaceSettings CharacterFaceSkinSurface;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "将整体 Opacity 为 1 的半透明头发改用 Masked 父材质，优先保证深度与排序稳定；会舍弃贴图的柔和透明过渡。需要保留渐隐时可关闭；头发阴影层和整体淡出不受此选项影响。"))
	bool bUseMaskedHair = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "让使用本预设的角色参与插件主光阴影，形成刘海、衣领和肢体间的真实遮挡；显式 MMDShadowExclude 标签仍优先。恢复材质时恢复原阴影标签。"))
	bool bCharacterSelfShadow = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials", meta = (ToolTip = "角色投射屏幕空间接触阴影；关闭可避免鼻尖和眼窝的短距离伪影，普通阴影贴图继续工作。"))
	bool bCharacterContactShadow = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	bool bCharacterOutline = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ToolTip = "可选专用描边父材质；为空时完整沿用导入的 PMX 描边材质。"))
	TObjectPtr<UMaterialInterface> CharacterOutlineMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ClampMin = "0", ClampMax = "1", ToolTip = "专用父材质的底色染色权重；0 使用原描边配色，1 从当前槽底色派生。"))
	float OutlineSourceColorBlend = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	TMap<EMMDMaterialRole, FMMDStageOutlineRoleSettings> CharacterOutlineRoles;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ClampMin = "0", ClampMax = "4", ToolTip = "1080p 参考像素宽度，随透视镜头距离和 FOV 换算；继续乘 PMX EdgeSize 与顶点 EdgeScale。为隔离正交 CSM 深度，正交视角不绘制此细描边。"))
	float OutlinePixelWidth = 1.20f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline", meta = (ClampMin = "0", ClampMax = "1", ToolTip = "0 保留 PMX 边线色，1 使用部位配色；始终保留 PMX 边线透明度。"))
	float OutlinePaletteBlend = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	FLinearColor OutlineColor = FLinearColor(0.012f, 0.014f, 0.021f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	FLinearColor SkinOutlineColor = FLinearColor(0.055f, 0.019f, 0.014f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	FLinearColor HairOutlineColor = FLinearColor(0.008f, 0.025f, 0.028f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Outline")
	FLinearColor ClothOutlineColor = FLinearColor(0.010f, 0.014f, 0.025f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> PanelMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> BeamMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Materials")
	TObjectPtr<UMaterialInterface> BackdropMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Palette")
	FLinearColor BaseTint = FLinearColor(0.014f, 0.020f, 0.042f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Palette")
	FLinearColor AccentTint = FLinearColor(0.045f, 0.82f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Palette")
	FLinearColor SecondaryTint = FLinearColor(0.70f, 0.10f, 0.46f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FLinearColor KeyLightColor = FLinearColor(1.0f, 0.92f, 0.87f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FLinearColor FillLightColor = FLinearColor(0.69f, 0.84f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FLinearColor RimLightColor = FLinearColor(0.47f, 0.77f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FRotator KeyLightRotation = FRotator(-38.0f, 40.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FRotator FillLightRotation = FRotator(-20.0f, 140.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting")
	FRotator RimLightRotation = FRotator(-12.0f, -90.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting", meta = (ClampMin = "0", ClampMax = "10"))
	float KeyLightIntensity = 3.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting", meta = (ClampMin = "0", ClampMax = "10"))
	float FillLightIntensity = 0.80f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting", meta = (ClampMin = "0", ClampMax = "10"))
	float RimLightIntensity = 1.80f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Lighting", meta = (ClampMin = "0", ClampMax = "10"))
	float AccentLightIntensity = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "-4", ClampMax = "4"))
	float ExposureBias = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "0", ClampMax = "2"))
	float BloomIntensity = 0.16f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "0", ClampMax = "2"))
	float ColorGradingSaturation = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	float ColorGradingContrast = 1.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "0.5", ClampMax = "1.5"))
	float ColorGradingGamma = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess")
	FLinearColor ColorGradingGain = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "1500", ClampMax = "15000"))
	float WhiteTemperature = 6500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|PostProcess", meta = (ClampMin = "0", ClampMax = "1"))
	float FilmToe = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float ShadowThreshold = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0.001", ClampMax = "1"))
	float ShadowSoftness = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon")
	FLinearColor ShadowTint = FLinearColor(0.58f, 0.62f, 0.74f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "2"))
	float AmbientStrength = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "4"))
	float LightScale = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "2"))
	float SpecularStrength = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "1", ClampMax = "256"))
	float SpecularPower = 48.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "2"))
	float RimStrength = 0.10f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0.1", ClampMax = "16"))
	float RimPower = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float ToonAmount = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float DiffuseWrap = 0.18f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "2"))
	float ColorSaturation = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "2"))
	float ShadowSaturation = 0.90f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0.04", ClampMax = "1"))
	float Roughness = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon")
	FLinearColor SpecularTint = FLinearColor(0.85f, 0.90f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "0.1"))
	float BlackLift = 0.018f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Toon", meta = (ClampMin = "0", ClampMax = "1"))
	float SoftSpecularStrength = 0.16f;
};
