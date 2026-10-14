#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "MMDEndfieldFaceData.generated.h"

class UTexture2D;

USTRUCT()
struct FMMDGeneratedFaceSdfSlot
{
	GENERATED_BODY()

	UPROPERTY() int32 MaterialIndex = INDEX_NONE;
	UPROPERTY() FName MaterialSlotName;
	/** 仅旧 V3 方位角阈值图使用，不存放 V4 方向场。 */
	UPROPERTY() TObjectPtr<UTexture2D> Texture;
	/** V4：RG=头空间单位漫反射方向的 oct 编码，B=镜像类别，A=有效覆盖。 */
	UPROPERTY() TObjectPtr<UTexture2D> DirectionTexture;
	UPROPERTY() int32 DirectionVersion = 0;
	UPROPERTY() int32 UVChannel = 0;
};

/** 随网格保存和 Cook 的自动脸部数据；运行时只读取，不修改共享 Toon Profile。 */
UCLASS()
class UE5MMDTOOLS_API UMMDEndfieldFaceData : public UAssetUserData
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 4;
	static constexpr int32 LegacyThresholdVersion = 3;
	static constexpr const TCHAR* DirectionTextureParameter = TEXT("ToonFaceDirectionTexture");
	static constexpr const TCHAR* DirectionEnabledParameter = TEXT("ToonFaceDirectionEnabled");
	static constexpr const TCHAR* DirectionVersionParameter = TEXT("ToonFaceDirectionVersion");
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TArray<FMMDGeneratedFaceSdfSlot> Slots;

	UTexture2D* FindTexture(int32 MaterialIndex, FName MaterialSlotName) const;
	UTexture2D* FindDirectionTexture(int32 MaterialIndex, FName MaterialSlotName) const;
};
