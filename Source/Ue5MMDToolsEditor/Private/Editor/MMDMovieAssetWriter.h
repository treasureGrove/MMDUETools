#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimSequence;

namespace MMDMovieAssetWriter
{
	struct FResult
	{
		FString AssetsDirectory;
		FString MountRoot;
		FString SequenceObjectPath;
		FString BakedAnimationObjectPath;
	};

	// 每条骨骼轨道与镜头数组都按输出帧排列，骨骼顺序与网格 RefSkeleton 一致。
	bool Write(const FString& OutputDir, USkeletalMesh* Mesh, UAnimSequence* SourceAnimation,
		const TArray<TArray<FTransform>>& BoneLocalTracks, const TArray<FTransform>& CameraTransforms,
		const TArray<float>& HorizontalFovs, const TArray<bool>& CameraCuts, int32 Fps,
		FResult& OutResult, FString& OutError, float PlaybackRate = 1.0f);
}
