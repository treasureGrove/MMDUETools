#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UMMDLightingEnvironmentLibrary.generated.h"

class UWorld;
class FPreviewScene;

/**
 * MMD 光照环境库：一键在编辑器/关卡里搭好一套预设灯光，用于验证 toon 材质在各种
 * 光照下的表现（LightDataRT 系统会实时收集这些 Directional/Point/Spot/Rect 灯）。
 *
 * 标准环境（真实感参考）：
 *   Studio3Point  三点布光（主光+辅光+轮廓光+天光）
 *   Daylight      户外日光（强平行光+天光+天空补光）
 *   OvercastSoft  阴天柔光（顶部柔光+高天光）
 *   IndoorWarm    室内暖光（暖色点光+面光补光）
 *
 * 特殊环境（风格化验证）：
 *   NeonNight     赛博霓虹（品红/青点光+紫聚光，无主光）
 *   RimSilhouette 轮廓剪影（强背光，正面极暗）
 *   GoldenHour    黄金时刻（低角度暖橙平行光）
 *   HorrorGreen   恐怖绿光（底部绿光+冷色顶光）
 */
UENUM(BlueprintType)
enum class EMMDLightingEnvironment : uint8
{
	None            = 0 UMETA(DisplayName = "无（清除环境光）"),
	Studio3Point    = 1 UMETA(DisplayName = "三点布光（标准）"),
	Daylight        = 2 UMETA(DisplayName = "户外日光（标准）"),
	OvercastSoft    = 3 UMETA(DisplayName = "阴天柔光（标准）"),
	IndoorWarm      = 4 UMETA(DisplayName = "室内暖光（标准）"),
	NeonNight       = 5 UMETA(DisplayName = "赛博霓虹（特殊）"),
	RimSilhouette   = 6 UMETA(DisplayName = "轮廓剪影（特殊）"),
	GoldenHour      = 7 UMETA(DisplayName = "黄金时刻（特殊）"),
	HorrorGreen     = 8 UMETA(DisplayName = "恐怖绿光（特殊）"),
};

UCLASS()
class UE5MMDTOOLS_API UMMDLightingEnvironmentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 应用一套预设光照环境（先清除上一次应用的环境光，再按预设生成灯光），返回生成的灯数。World 为空时回退到编辑器世界。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static int32 ApplyLightingEnvironment(UWorld* World, EMMDLightingEnvironment Environment);

	/** 清除本库生成的（特定世界里的）环境光。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static void ClearLightingEnvironment(UWorld* World);

	/** 把光照环境作为灯光组件加到预览场景（FPreviewScene/FAdvancedPreviewScene），只影响预览窗口、不污染关卡。返回生成的灯数。 */
	static int32 ApplyLightingEnvironmentToPreview(FPreviewScene* PreviewScene, EMMDLightingEnvironment Environment);

	/** 同上，但灯光按模型实际大小摆放：Center=模型中心，Scale=模型半径/标准半径(100cm)。
	 *  灯位 = Center + Spec.Location*Scale，半径 = Spec.Radius*Scale，保证灯光打到模型身上。 */
	static int32 ApplyLightingEnvironmentToPreviewScaled(FPreviewScene* PreviewScene, EMMDLightingEnvironment Environment,
		const FVector& Center, float Scale);

	/** 清除之前通过 ApplyLightingEnvironmentToPreview 加到该预览场景的环境光组件。 */
	static void ClearLightingEnvironmentFromPreview(FPreviewScene* PreviewScene);

	/** 把光照环境打包成 LightDataRT 第 0 行布局的灯光数据（MaxLights*4 个 float4，16 盏灯）。
	 *  预览场景的灯是孤儿组件，GWorld 扫描收集不到，需用本函数打包后经
	 *  UMMDAnimeLightDataSubsystem::SetPreviewLightOverride 推给子系统覆盖。 */
	static TArray<FVector4f> PackEnvironmentLightData(EMMDLightingEnvironment Environment);

	/** 同上，但灯光数据按模型实际大小缩放（与 ApplyLightingEnvironmentToPreviewScaled 一致）。 */
	static TArray<FVector4f> PackEnvironmentLightDataScaled(EMMDLightingEnvironment Environment,
		const FVector& Center, float Scale);

	/** 预设的中文显示名（用于 UI 下拉/按钮）。 */
	UFUNCTION(BlueprintPure, Category = "MMD Tools|Lighting")
	static FString GetEnvironmentDisplayName(EMMDLightingEnvironment Environment);

	/** 是否为“特殊”风格化环境（区别于标准参考环境）。 */
	UFUNCTION(BlueprintPure, Category = "MMD Tools|Lighting")
	static bool IsSpecialEnvironment(EMMDLightingEnvironment Environment);

	/** 预设的英文资产名（用于关卡/资产命名，如 LE_Studio3Point）。 */
	UFUNCTION(BlueprintPure, Category = "MMD Tools|Lighting")
	static FString GetEnvironmentAssetName(EMMDLightingEnvironment Environment);

	/** 生成该环境的可编辑关卡资产（.umap，含灯光 actor，可在编辑器里打开调整）。返回资产路径（失败返回空）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static FString CreateEnvironmentLevelAsset(EMMDLightingEnvironment Environment, const FString& FolderPath = TEXT("/Ue5MMDTools/Maps"));

	/** 在编辑器里打开该环境的关卡资产（不存在则先自动生成）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static bool OpenEnvironmentLevel(EMMDLightingEnvironment Environment, const FString& FolderPath = TEXT("/Ue5MMDTools/Maps"));

	/** 批量生成所有（8 个）光照环境关卡资产，返回成功数量。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static int32 CreateAllEnvironmentLevelAssets(const FString& FolderPath = TEXT("/Ue5MMDTools/Maps"));

	/** 把编辑器主透视视口相机归位到看向舞台中央（模型原点）的标准机位。 */
	static void ResetLevelViewportCamera();

	/** 把编辑器主透视视口相机同步到当前关卡里的 MMDStageCamera（场景配套机位）。
	 *  找到并同步成功返回 true；关卡里没有该相机返回 false。 */
	static bool SyncToStageCamera();
};
