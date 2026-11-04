#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MMDPreviewFraming.h"
#include "Math/InverseRotationMatrix.h"
#include "Math/PerspectiveMatrix.h"
#include "Math/TranslationMatrix.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPreviewFramingTest, "Ue5MMDTools.UI.PreviewFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FMatrix MakeFramingTestProjection(const FVector& Location, const FRotator& Rotation, double Aspect, double HorizontalFov, double NearClip)
	{
		// 直接使用引擎的相机矩阵投影八角，独立验证距离求解器，而不复写它的距离公式。
		const FMatrix View = FTranslationMatrix(-Location) * FInverseRotationMatrix(Rotation) * FMatrix(
			FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
		const double HalfFov = FMath::DegreesToRadians(HorizontalFov * 0.5);
		return View * FReversedZPerspectiveMatrix(HalfFov, HalfFov, 1.0, Aspect, NearClip, NearClip);
	}
}

bool FMMDPreviewFramingTest::RunTest(const FString& Parameters)
{
	const double Padding = 1.18;
	const double NearClip = 10.0;
	const FBox Samples[] = {
		FBox::BuildAABB(FVector(0, 0, 85), FVector(35, 18, 85)),
		FBox::BuildAABB(FVector(250, -400, 95), FVector(130, 45, 95)),
		FBox::BuildAABB(FVector(10000, -20000, 170), FVector(25, 120, 170))
	};
	const FVector Directions[] = { FVector(0, 1, 0), FVector(1, 0.12, 0), FVector(0, -1, 0), FVector(1, 2, 0.4) };
	int32 SampleIndex = 0;
	for (const FBox& Bounds : Samples)
	{
		for (double Aspect : { 16.0 / 9.0, 9.0 / 16.0, 32.0 / 9.0, 0.25 })
		{
			for (const FVector& Direction : Directions)
			{
				const double Distance = MMDGetBoundsFramingDistance(Bounds, Direction, Aspect, 35.0, Padding, NearClip);
				const FVector Location = Bounds.GetCenter() + Direction.GetSafeNormal() * Distance;
				const FRotator Rotation = (Bounds.GetCenter() - Location).Rotation();
				const FMatrix Projection = MakeFramingTestProjection(Location, Rotation, Aspect, 35.0, NearClip);
				const FString Label = FString::Printf(TEXT("模型 %d，宽高比 %.3f，机位 %s"), SampleIndex, Aspect, *Direction.ToString());
				FVector Corners[8];
				Bounds.GetVertices(Corners);
				double LargestAxis = 0.0;
				for (const FVector& Corner : Corners)
				{
					const FVector4 Clip = Projection.TransformFVector4(FVector4(Corner, 1.0));
					TestTrue(TEXT("全部角点在近裁切面之后：") + Label, Clip.W >= NearClip - 1.e-6);
					const double ProjectedX = FMath::Abs(Clip.X / Clip.W);
					const double ProjectedY = FMath::Abs(Clip.Y / Clip.W);
					TestTrue(TEXT("横向长发不超出安全边界：") + Label, ProjectedX <= 1.0 / Padding + 1.e-6);
					TestTrue(TEXT("头脚不超出安全边界：") + Label, ProjectedY <= 1.0 / Padding + 1.e-6);
					LargestAxis = FMath::Max3(LargestAxis, ProjectedX, ProjectedY);
				}
				TestTrue(TEXT("限制轴占画面 82–86%，不退回过度保守球体构图：") + Label,
					LargestAxis >= 0.82 && LargestAxis <= 0.86);
			}
		}
		++SampleIndex;
	}
	const FBox ReferenceBox = FBox::BuildAABB(FVector::ZeroVector, FVector(30, 20, 90));
	TestEqual(TEXT("90 度已知矩形取景包含前侧深度"),
		MMDGetBoundsFramingDistance(ReferenceBox, FVector(0, 1, 0), 16.0 / 9.0, 90.0, 1.0), 180.0, 1.e-6);
	const FBox TinyBox = FBox::BuildAABB(FVector::ZeroVector, FVector(0.001, 20, 0.001));
	TestTrue(TEXT("近乎零尺寸模型仍不会穿过近裁切面"),
		MMDGetBoundsFramingDistance(TinyBox, FVector(0, 1, 0), 1.0, 35.0, Padding, NearClip) >= 30.0);
	TestEqual(TEXT("无效包围盒无虚构取景距离"),
		MMDGetBoundsFramingDistance(FBox(ForceInit), FVector(0, 1, 0), 1.0, 35.0), 0.0);
	return true;
}
#endif
