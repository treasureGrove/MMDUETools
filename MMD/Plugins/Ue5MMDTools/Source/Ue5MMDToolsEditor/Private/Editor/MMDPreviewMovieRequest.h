#pragma once

#include "CoreMinimal.h"

struct FMMDPreviewMovieRequest
{
	FString ModelAsset, AnimationAsset, Motion, Preset, Stage;
	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FIntPoint Size = FIntPoint::ZeroValue;
	double HorizontalFOV = 0.0;
	float PlaybackRate = 1.0f;
	bool bPhysics = false;
	FString BuildArguments(const FString& Script, const FString& EngineRoot, const FString& Project, const FString& Output) const;
};
