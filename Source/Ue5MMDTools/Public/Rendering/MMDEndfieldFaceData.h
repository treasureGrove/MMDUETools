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
	/** V5：独立头部投影的左右角阈值、前层深度、覆盖率；V4 旧方向图需重烘。 */
	UPROPERTY() TObjectPtr<UTexture2D> DirectionTexture;
	UPROPERTY() int32 DirectionVersion = 0;
	/** -1 表示过程化头部坐标，不占用网格 UV 通道。 */
	UPROPERTY() int32 UVChannel = -1;
	/** 透明附脸层复用已验证的非透明主脸图，不改变源颜色、法线或连续 alpha。 */
	UPROPERTY() int32 HostMaterialIndex = INDEX_NONE;
	UPROPERTY() FName HostSlotName;
	UPROPERTY() bool bForwardOverlay = false;
	/** 只描述导入参考姿态的关联证据，不代表独立 Morph 或任意变形已验证。 */
	UPROPERTY() int32 OverlayValidatedLODs = 0;
	UPROPERTY() int32 OverlayTestedVertices = 0;
	UPROPERTY() float OverlayValidVertexRatio = 0.0f;
	UPROPERTY() float OverlayValidAreaRatio = 0.0f;
	UPROPERTY() float OverlayMinimumHeadWeight = 0.0f;
	UPROPERTY() float OverlayMaxMatchedDepthError = 0.0f;
};

/** 随网格保存和 Cook 的自动脸部数据；运行时只读取，不修改共享 Toon Profile。 */
UCLASS()
class UE5MMDTOOLS_API UMMDEndfieldFaceData : public UAssetUserData
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 5;
	static constexpr int32 LegacyThresholdVersion = 3;
	static constexpr const TCHAR* DirectionTextureParameter = TEXT("ToonFaceDirectionTexture");
	static constexpr const TCHAR* DirectionEnabledParameter = TEXT("ToonFaceDirectionEnabled");
	static constexpr const TCHAR* DirectionVersionParameter = TEXT("ToonFaceDirectionVersion");
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TArray<FMMDGeneratedFaceSdfSlot> Slots;

	/** 烘焙与运行时共享同一参考头框架；旧资产默认为0，必须重新标定。 */
	UPROPERTY() int32 FrameVersion = 0;
	UPROPERTY() FName HeadBoneName;
	UPROPERTY() FVector HeadCenterHeadLocal = FVector::ZeroVector;
	UPROPERTY() float HeadRadius = 0.0f;
	UPROPERTY() FQuat HeadReferenceRotation = FQuat::Identity;
	UPROPERTY() FString HeadSkeletonSignature;

	UTexture2D* FindTexture(int32 MaterialIndex, FName MaterialSlotName) const;
	UTexture2D* FindDirectionTexture(int32 MaterialIndex, FName MaterialSlotName) const;
	/** 同时验证参考头框架及透明层的主槽身份，禁止自身引用和关联链。 */
	const FMMDGeneratedFaceSdfSlot* FindDirectionSlot(int32 MaterialIndex, FName MaterialSlotName) const;
	/** 仅用于运行时 MID 缓存失效；不是资产完整性或密码学证明。 */
	uint32 ComputeBindingSignature() const;
};
