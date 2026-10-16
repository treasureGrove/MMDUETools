#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

namespace MMDEndfieldFaceBinding
{
	struct FSlotStats
	{
		int32 Slot = INDEX_NONE;
		int32 FaceTexels = 0;
		int32 MirroredTexels = 0;
		int32 ConflictingTexels = 0;
		int32 Triangles = 0;
		int32 FrontDarkTexels = 0;
		int32 NonContinuousTexels = 0;
		int32 ProjectedOccludedTexels = 0;
		int32 ClippedTriangles = 0;
		int32 DepthClampedTexels = 0;
		bool bSkippedTranslucent = false;
	};
	struct FOverlayStats
	{
		int32 Slot = INDEX_NONE;
		int32 HostMaterialIndex = INDEX_NONE;
		FName HostSlotName;
		FString SourceRoleReason;
		bool bAssociated = false;
		int32 ValidatedLODs = 0;
		int32 TestedVertices = 0;
		int32 MatchedVertices = 0;
		int32 TestedTriangles = 0;
		float ValidVertexRatio = 0.0f;
		float ValidAreaRatio = 0.0f;
		float MinimumHeadWeight = 0.0f;
		/** 接受样点与同投影主脸真实几何的最大偏差，单位为头半径；不混入邻 tap 的坡度。 */
		float MaxMatchedDepthError = 0.0f;
		FString Reason;
	};
	struct FResult
	{
		TArray<int32> BoundSlots;
		TArray<FString> TexturePaths;
		TArray<FString> Notes;
		TArray<FString> Errors;
		TArray<FSlotStats> SlotStats;
		TArray<FOverlayStats> OverlayStats;
		TArray<int32> ForwardOverlaySlots;
		int32 FaceTexelCount = 0;
		int32 DataVersion = 0;
		int32 FrameVersion = 0;
		int32 UVChannel = -1;
		FName HeadBoneName;
		/** 拟合中心与轴均为参考组件空间；局部中心用于运行时跟随头骨。 */
		FVector HeadCenter = FVector::ZeroVector;
		FVector HeadCenterHeadLocal = FVector::ZeroVector;
		FVector HeadForward = FVector::ZeroVector;
		FVector HeadRight = FVector::ZeroVector;
		FVector HeadUp = FVector::ZeroVector;
		float HeadRadius = 0.0f;
		float BoneRadius = 0.0f;
		FQuat HeadReferenceRotation = FQuat::Identity;
		FString HeadSkeletonSignature;
		/** 表示计算框架有效，不代表所有候选材质或运行时 shader 已验证。 */
		bool bHeadFrameValid = false;
	};

	/** V5 主脸逐槽烘焙；透明附脸候选保守关联主图，只保存本工具派生纹理。 */
	UE5MMDTOOLSEDITOR_API bool BakeAndBind(USkeletalMesh* Mesh, const FString& TextureFolder,
		bool bSaveTextures, FResult& Result, const TArray<int32>& Slots = {}, int32 Size = 512);
}
