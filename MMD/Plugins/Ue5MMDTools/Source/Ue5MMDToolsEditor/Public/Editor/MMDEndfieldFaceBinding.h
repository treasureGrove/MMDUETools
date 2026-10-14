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
	};
	struct FResult
	{
		TArray<int32> BoundSlots;
		TArray<FString> TexturePaths;
		TArray<FString> Notes;
		TArray<FString> Errors;
		TArray<FSlotStats> SlotStats;
		int32 FaceTexelCount = 0;
	};

	/** V4 几何方向场；空 Slots 自动筛选皮肤，缺覆盖回退，只保存本工具派生纹理。 */
	UE5MMDTOOLSEDITOR_API bool BakeAndBind(USkeletalMesh* Mesh, const FString& TextureFolder,
		bool bSaveTextures, FResult& Result, const TArray<int32>& Slots = {}, int32 Size = 512);
}
