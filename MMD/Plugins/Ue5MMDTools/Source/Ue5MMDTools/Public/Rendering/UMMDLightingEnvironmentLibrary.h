#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UMMDLightingEnvironmentLibrary.generated.h"

class UWorld;

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
	static FString CreateEnvironmentLevelAsset(EMMDLightingEnvironment Environment, const FString& FolderPath = TEXT("/Game/MMDLighting"));

	/** 在编辑器里打开该环境的关卡资产（不存在则先自动生成）。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static bool OpenEnvironmentLevel(EMMDLightingEnvironment Environment, const FString& FolderPath = TEXT("/Game/MMDLighting"));

	/** 批量生成所有（8 个）光照环境关卡资产，返回成功数量。 */
	UFUNCTION(BlueprintCallable, Category = "MMD Tools|Lighting")
	static int32 CreateAllEnvironmentLevelAssets(const FString& FolderPath = TEXT("/Game/MMDLighting"));
};
