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
	UPROPERTY() TObjectPtr<UTexture2D> Texture;
};

/** 随网格保存和 Cook 的自动脸部数据；运行时只读取，不修改共享 Toon Profile。 */
UCLASS()
class UE5MMDTOOLS_API UMMDEndfieldFaceData : public UAssetUserData
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 3;
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TArray<FMMDGeneratedFaceSdfSlot> Slots;

	UTexture2D* FindTexture(int32 MaterialIndex, FName MaterialSlotName) const;
};
