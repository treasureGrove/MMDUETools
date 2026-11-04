#pragma once

#include "CoreMinimal.h"

struct FMMDPreviewMotionPreset
{
	FName Id;
	const TCHAR* Label;
	const TCHAR* RelativePath;
	float Duration;
};

// 使用原作者完整动作，保留肩、肘、腕、手指之间的配合。
inline TConstArrayView<FMMDPreviewMotionPreset> MMDGetPreviewMotionPresets()
{
	static const FMMDPreviewMotionPreset Presets[] = {
		{TEXT("Idle"), TEXT("恬静待机"), TEXT("MMDAgent/mei_wait/mei_wait.vmd"), 4.0f},
		{TEXT("Breath"), TEXT("轻柔呼吸"), TEXT("MMDAgent/mei_breath/mei_breath.vmd"), 2.5f},
		{TEXT("Think"), TEXT("侧头思考"), TEXT("MMDAgent/mei_idle/mei_idle_think.vmd"), 85.0f / 30.0f},
		{TEXT("Clothes"), TEXT("整理衣摆"), TEXT("MMDAgent/mei_idle/mei_idle_touch_clothes.vmd"), 2.0f}
	};
	return Presets;
}

inline const FMMDPreviewMotionPreset* MMDFindPreviewMotionPreset(FName Id)
{
	for (const auto& Preset : MMDGetPreviewMotionPresets()) if (Preset.Id == Id) return &Preset;
	return nullptr;
}
