#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/** 参考姿势的共享头部框架；中心为头骨局部坐标，半径与参考轴为网格组件空间。 */
struct UE5MMDTOOLS_API FMMDReferenceHeadFrame
{
	FName BoneName;
	int32 HeadIndex = INDEX_NONE;
	FQuat ReferenceHeadRotation = FQuat::Identity;
	FVector ReferenceHeadScale = FVector::OneVector;
	FVector CenterHeadLocal = FVector::ZeroVector;
	float Radius = 0.0f;
	FVector ForwardRef = FVector::YAxisVector;
	FVector RightRef = FVector::XAxisVector;
	FVector UpRef = FVector::UpVector;
	bool bValid = false;
	bool bFromFaceData = false;
};

namespace MMDEndfieldHeadFrame
{
	inline constexpr int32 CurrentFrameVersion = 1;

	/** 只对头、眼及其参考组件变换生成稳定签名，不依赖模型包围盒或渲染数据。 */
	UE5MMDTOOLS_API FString ComputeSkeletonSignature(const USkeletalMesh* Mesh, FName PreferredHead = NAME_None);

	/** 纯骨骼兜底；中心为头骨原点，尺度优先来自眼距与头眼距离，供编辑器继续拟合。 */
	UE5MMDTOOLS_API bool BuildSkeletonFrame(const USkeletalMesh* Mesh, FName PreferredHead,
		FMMDReferenceHeadFrame& OutFrame, FString& OutMessage);

	/** 优先读取签名匹配的持久化参考框架；缺失或过期时使用纯骨骼兜底。 */
	UE5MMDTOOLS_API bool BuildReferenceFrame(const USkeletalMesh* Mesh, FName PreferredHead,
		FMMDReferenceHeadFrame& OutFrame, FString& OutMessage);
}
