#pragma once

#include "CoreMinimal.h"
#include "Math/RotationMatrix.h"

// 目标固定为 Bounds 中心；方向为目标到相机，姿态与 (Target - Camera).Rotation() 一致（无滚转）。
// HorizontalFovDegrees 为实际画幅的水平视角；Padding=1.18 将限制轴占比控制在约 85%。
inline double MMDGetBoundsFramingDistance(const FBox& Bounds, const FVector& TargetToCameraDirection,
	double Aspect, double HorizontalFovDegrees, double Padding = 1.18, double NearClip = 1.0)
{
	if (!Bounds.IsValid) return 0.0;
	const FVector Direction = TargetToCameraDirection.GetSafeNormal(UE_SMALL_NUMBER, FVector(0, 1, 0));
	const FRotationMatrix CameraRotation((-Direction).Rotation());
	const FVector Right = CameraRotation.GetUnitAxis(EAxis::Y);
	const FVector Up = CameraRotation.GetUnitAxis(EAxis::Z);
	const double TanHorizontal = FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(HorizontalFovDegrees, 1.0, 175.0) * 0.5));
	const double TanVertical = TanHorizontal / FMath::Max(Aspect, 0.01);
	const double SafePadding = FMath::Max(Padding, 1.0);
	const double SafeNearClip = FMath::Max(NearClip, 0.01);
	const FVector Center = Bounds.GetCenter();
	FVector Corners[8];
	Bounds.GetVertices(Corners);
	double Distance = SafeNearClip;
	for (const FVector& Corner : Corners)
	{
		const FVector Offset = Corner - Center;
		const double TowardCamera = FVector::DotProduct(Offset, Direction);
		// 每个角自身的透视深度都必须容纳横向/纵向投影，不能只用中心平面的正交尺寸。
		const double RequiredDepth = FMath::Max3(
			FMath::Abs(FVector::DotProduct(Offset, Right)) * SafePadding / TanHorizontal,
			FMath::Abs(FVector::DotProduct(Offset, Up)) * SafePadding / TanVertical, SafeNearClip);
		Distance = FMath::Max(Distance, TowardCamera + RequiredDepth);
	}
	return Distance;
}
