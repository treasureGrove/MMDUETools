#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"

inline float MMDGetAdjacentPreviewFrameTime(float Time, float Duration, FFrameRate Rate, int32 Direction)
{
	if (!FMath::IsFinite(Time) || !FMath::IsFinite(Duration) || Duration <= 0.0f
		|| Rate.Numerator <= 0 || Rate.Denominator <= 0 || Direction == 0) return Time;
	Time = FMath::Clamp(Time, 0.0f, Duration);
	const auto FrameToTime = [Rate](int64 Frame)
	{
		return static_cast<float>(static_cast<double>(Frame) * Rate.Denominator / Rate.Numerator);
	};
	double Frame = static_cast<double>(Time) * Rate.Numerator / Rate.Denominator;
	const int64 LastFrame = FMath::RoundToInt64(static_cast<double>(Duration) * Rate.Numerator / Rate.Denominator);
	const int64 NearestFrame = FMath::RoundToInt64(Frame);
	// 仅在采样点和当前时间落入同一个 float 表示时吸附，不吞掉其前后的可表示时间。
	if (Time == Duration) Frame = static_cast<double>(LastFrame);
	else if (FrameToTime(NearestFrame) == Time) Frame = static_cast<double>(NearestFrame);
	const int64 NextFrame = Direction > 0 ? FMath::FloorToInt64(Frame) + 1 : FMath::CeilToInt64(Frame) - 1;
	const int64 TargetFrame = FMath::Clamp<int64>(NextFrame, 0, LastFrame);
	return TargetFrame == LastFrame ? Duration : FMath::Clamp(FrameToTime(TargetFrame), 0.0f, Duration);
}
