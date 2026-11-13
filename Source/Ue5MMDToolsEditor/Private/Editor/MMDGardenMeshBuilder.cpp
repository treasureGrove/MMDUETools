#include "Editor/MMDGardenMeshBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	constexpr const TCHAR* MeshRoot = TEXT("/Ue5MMDTools/Stages/Meshes/");
	constexpr float Tau = 2.0f * PI;

	struct FGardenVertex
	{
		FVector Position;
		FVector Normal;
		FVector Tangent;
		FVector2f UV;
		float BinormalSign;
	};

	struct FGardenGeometry
	{
		TArray<FGardenVertex> Vertices;
		TArray<FIntVector> Triangles;

		int32 AddVertex(const FVector& Position, const FVector& Normal, const FVector& Tangent, const FVector2f& UV, float BinormalSign = 1.0f)
		{
			return Vertices.Add({ Position, Normal, Tangent, UV, BinormalSign });
		}

		void AddTriangle(int32 A, int32 B, int32 C)
		{
			const FVector Cross = FVector::CrossProduct(Vertices[B].Position - Vertices[A].Position,
				Vertices[C].Position - Vertices[A].Position);
			if (Cross.SizeSquared() > 1.e-10)
			{
				Triangles.Add(FIntVector(A, B, C));
			}
		}
	};

	using FSurface = TFunction<FVector(float, float)>;

	void AddSurface(FGardenGeometry& Geometry, int32 USteps, int32 VSteps, const FSurface& Surface,
		bool bReverse = false, bool bWrapU = false, bool bWrapV = false)
	{
		const int32 FirstVertex = Geometry.Vertices.Num();
		for (int32 VIndex = 0; VIndex <= VSteps; ++VIndex)
		{
			const float V = float(VIndex) / VSteps;
			for (int32 UIndex = 0; UIndex <= USteps; ++UIndex)
			{
				const float U = float(UIndex) / USteps;
				constexpr float Epsilon = 0.0002f;
				const float U0 = bWrapU ? U - Epsilon : FMath::Max(0.0f, U - Epsilon);
				const float U1 = bWrapU ? U + Epsilon : FMath::Min(1.0f, U + Epsilon);
				const float V0 = bWrapV ? V - Epsilon : FMath::Max(0.0f, V - Epsilon);
				const float V1 = bWrapV ? V + Epsilon : FMath::Min(1.0f, V + Epsilon);
				FVector DU = Surface(U1, V) - Surface(U0, V);
				FVector DV = Surface(U, V1) - Surface(U, V0);
				if (DU.IsNearlyZero(1.e-7) || DV.IsNearlyZero(1.e-7))
				{
					const float ProbeU = FMath::Clamp(U, 0.005f, 0.995f);
					const float ProbeV = FMath::Clamp(V, 0.005f, 0.995f);
					DU = Surface(U1, ProbeV) - Surface(U0, ProbeV);
					DV = Surface(ProbeU, V1) - Surface(ProbeU, V0);
				}
				// 先归一化导数，避免小花瓣的叉积落入默认长度容差而丢失曲面法线。
				FVector Normal = FVector::CrossProduct(DU.GetSafeNormal(1.e-20), DV.GetSafeNormal(1.e-20)).GetSafeNormal(1.e-20);
				if (Normal.IsNearlyZero()) Normal = FVector::UpVector;
				if (bReverse) Normal *= -1.0;
				FVector Tangent = (DU - Normal * FVector::DotProduct(DU, Normal)).GetSafeNormal(1.e-20);
				if (Tangent.IsNearlyZero())
				{
					const FVector Axis = FMath::Abs(Normal.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
					Tangent = FVector::CrossProduct(Axis, Normal).GetSafeNormal();
				}
				Geometry.AddVertex(Surface(U, V), Normal, Tangent, FVector2f(U, V), bReverse ? -1.0f : 1.0f);
			}
		}
		for (int32 VIndex = 0; VIndex < VSteps; ++VIndex)
		{
			for (int32 UIndex = 0; UIndex < USteps; ++UIndex)
			{
				const int32 A = FirstVertex + VIndex * (USteps + 1) + UIndex;
				const int32 B = A + 1;
				const int32 C = A + USteps + 1;
				const int32 D = C + 1;
				if (bReverse)
				{
					Geometry.AddTriangle(A, C, B);
					Geometry.AddTriangle(B, C, D);
				}
				else
				{
					Geometry.AddTriangle(A, B, C);
					Geometry.AddTriangle(B, D, C);
				}
			}
		}
	}

	void AddCap(FGardenGeometry& Geometry, const FVector& Center, const FVector& Normal,
		int32 Segments, const TFunction<FVector(float)>& Rim)
	{
		const FVector Axis = FMath::Abs(Normal.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector;
		const FVector Tangent = FVector::CrossProduct(Axis, Normal).GetSafeNormal();
		const int32 CenterIndex = Geometry.AddVertex(Center, Normal, Tangent, FVector2f(0.5f, 0.5f));
		TArray<int32> Ring;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const float U = float(Index) / Segments;
			Ring.Add(Geometry.AddVertex(Rim(U), Normal, Tangent,
				FVector2f(0.5f + 0.5f * FMath::Cos(Tau * U), 0.5f + 0.5f * FMath::Sin(Tau * U))));
		}
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const int32 A = Ring[Index];
			const int32 B = Ring[(Index + 1) % Segments];
			const FVector Cross = FVector::CrossProduct(Geometry.Vertices[A].Position - Center,
				Geometry.Vertices[B].Position - Center);
			if (FVector::DotProduct(Cross, Normal) >= 0.0) Geometry.AddTriangle(CenterIndex, A, B);
			else Geometry.AddTriangle(CenterIndex, B, A);
		}
	}

	float RoofHeight(float X, float Y, float HalfX, float HalfY, float RidgeHalf, float Rise, float CornerLift)
	{
		const float T = FMath::Clamp(FMath::Min(1.0f - FMath::Abs(Y) / HalfY,
			(HalfX - FMath::Abs(X)) / (HalfX - RidgeHalf)), 0.0f, 1.0f);
		const float TileEnvelope = FMath::Square(FMath::Sin(PI * T));
		const float Tiles = TileEnvelope * (0.60f * FMath::Square(FMath::Sin(PI * X / 12.0f))
			+ 0.20f * FMath::Square(FMath::Sin(PI * Y / 42.0f)));
		return Rise * (0.70f * T + 0.30f * T * T) + Tiles
			+ CornerLift * FMath::Pow(FMath::Abs(X) / HalfX, 4.0f) * FMath::Pow(FMath::Abs(Y) / HalfY, 4.0f);
	}

	void BuildRoof(FGardenGeometry& Geometry, float HalfX, float HalfY, float RidgeHalf, float Rise,
		float Thickness, float CornerLift)
	{
		// 亭顶：400×300，脊高85；四坡瓦面+18cm角部翘檐，下壳厚6cm。
		for (int32 Side = 0; Side < 4; ++Side)
		{
			const bool bLongSide = Side < 2;
			const float Sign = Side == 0 || Side == 2 ? 1.0f : -1.0f;
			const bool bReverse = Side == 0 || Side == 3;
			const FSurface Top = [=](float U, float V)
			{
				float X;
				float Y;
				if (bLongSide)
				{
					X = (2.0f * U - 1.0f) * FMath::Lerp(HalfX, RidgeHalf, V);
					Y = Sign * HalfY * (1.0f - V);
				}
				else
				{
					X = Sign * FMath::Lerp(HalfX, RidgeHalf, V);
					Y = (2.0f * U - 1.0f) * HalfY * (1.0f - V);
				}
				return FVector(X, Y, RoofHeight(X, Y, HalfX, HalfY, RidgeHalf, Rise, CornerLift));
			};
			const int32 USteps = HalfX > 100.0f ? (bLongSide ? 100 : 76) : 20;
			const int32 VSteps = HalfX > 100.0f ? 24 : 12;
			AddSurface(Geometry, USteps, VSteps, Top, bReverse);
			AddSurface(Geometry, USteps, VSteps, [=](float U, float V)
			{
				return Top(U, V) - FVector(0, 0, Thickness);
			}, !bReverse);
			AddSurface(Geometry, USteps, 1, [=](float U, float V)
			{
				return Top(U, 0) - FVector(0, 0, Thickness * V);
			}, !bReverse);
		}
	}

	void BuildTile(FGardenGeometry& Geometry)
	{
		const FSurface Top = [](float U, float V)
		{
			const float X = 12.0f * (U - 0.5f);
			return FVector(X, 42.0f * (V - 0.5f), 3.2f * FMath::Cos(PI * X / 12.0f) - 1.2f);
		};
		AddSurface(Geometry, 12, 4, Top);
		AddSurface(Geometry, 12, 4, [=](float U, float V) { return Top(U, V) - FVector(0, 0, 0.8f); }, true);
		for (int32 Edge = 0; Edge < 4; ++Edge)
		{
			AddSurface(Geometry, 12, 1, [=](float U, float V)
			{
				const FVector Point = Edge < 2 ? Top(U, float(Edge)) : Top(float(Edge - 2), U);
				return Point - FVector(0, 0, V * 0.8f);
			}, Edge == 0 || Edge == 3);
		}
	}

	void BuildBeam(FGardenGeometry& Geometry)
	{
		const FSurface Surface = [](float U, float V)
		{
			const float Angle = Tau * U;
			const float C = FMath::Cos(Angle);
			const float S = FMath::Sin(Angle);
			const float X = 240.0f * (V - 0.5f);
			return FVector(X, 9.0f * FMath::Sign(C) * FMath::Pow(FMath::Abs(C), 0.40f),
				10.0f * FMath::Sign(S) * FMath::Pow(FMath::Abs(S), 0.40f) + 18.0f * FMath::Pow(X / 120.0f, 4.0f));
		};
		AddSurface(Geometry, 32, 40, Surface, false, true);
		AddCap(Geometry, FVector(-120, 0, 18), -FVector::ForwardVector, 32, [=](float U) { return Surface(U, 0); });
		AddCap(Geometry, FVector(120, 0, 18), FVector::ForwardVector, 32, [=](float U) { return Surface(U, 1); });
	}

	FVector BranchCenter(float T)
	{
		// 分枝锚点固定在轴线上，中段曲率与树皮节保持原创几何。
		if (T <= 0.0f) return FVector::ZeroVector;
		if (T >= 1.0f) return FVector(0, 0, 100);
		const float Bend = FMath::Square(FMath::Sin(PI * T));
		return FVector(12.0f * Bend, 4.0f * Bend * FMath::Sin(Tau * T), 100.0f * T);
	}

	void BuildBranch(FGardenGeometry& Geometry)
	{
		const FSurface Surface = [](float U, float V)
		{
			const float Angle = Tau * U;
			const float Knots = 0.11f * FMath::Exp(-FMath::Square((V - 0.22f) / 0.055f))
				+ 0.16f * FMath::Exp(-FMath::Square((V - 0.57f) / 0.045f))
				+ 0.10f * FMath::Exp(-FMath::Square((V - 0.80f) / 0.050f));
			const float Bark = 0.035f * FMath::Sin(7.0f * Angle + 9.0f * V) * FMath::Sin(PI * V);
			const float Radius = FMath::Lerp(10.0f, 3.0f, V) * (1.0f + Knots * FMath::Square(FMath::Sin(Angle)) + Bark);
			return BranchCenter(V) + Radius * FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
		};
		AddSurface(Geometry, 24, 40, Surface, false, true);
		AddCap(Geometry, FVector::ZeroVector, -FVector::UpVector, 24, [=](float U) { return Surface(U, 0); });
		AddCap(Geometry, FVector(0, 0, 100), FVector::UpVector, 24, [=](float U) { return Surface(U, 1); });
	}

	FVector PetalSurface(float U, float V)
	{
		const float Angle = Tau * U;
		const float S = FMath::Sin(Angle);
		const float OutlineX = 4.0f * S * S * S;
		const float HeartY = 13.0f * FMath::Cos(Angle) - 5.0f * FMath::Cos(2.0f * Angle)
			- 2.0f * FMath::Cos(3.0f * Angle) - FMath::Cos(4.0f * Angle);
		const float OutlineY = (HeartY + 2.54f) * (12.0f / 29.0f);
		return FVector(V * OutlineX, V * OutlineY, -0.45f + 0.85f * V * V + 0.12f * V * FMath::Sin(2.0f * Angle));
	}

	void AddPetal(FGardenGeometry& Geometry, const FTransform& Transform, int32 AngularSteps, int32 RadialSteps)
	{
		const FSurface Top = [=](float U, float V) { return Transform.TransformPosition(PetalSurface(U, V)); };
		AddSurface(Geometry, AngularSteps, RadialSteps, Top, false, true);
		AddSurface(Geometry, AngularSteps, RadialSteps, [=](float U, float V)
		{
			return Transform.TransformPosition(PetalSurface(U, V) - FVector(0, 0, 0.14f));
		}, true, true);
		AddSurface(Geometry, AngularSteps, 1, [=](float U, float V)
		{
			return Transform.TransformPosition(PetalSurface(U, 1) - FVector(0, 0, 0.14f * V));
		}, false, true);
	}

	void BuildBlossoms(FGardenGeometry& Geometry)
	{
		// 18朵独立五瓣樱花；花冠仅由有凹心缺口的薄花瓣组成。
		for (int32 Flower = 0; Flower < 18; ++Flower)
		{
			const float Angle = Flower * 2.3999632f;
			const float Radius = 16.0f + 10.0f * FMath::Sin(Flower * 1.71f) * FMath::Sin(Flower * 1.71f);
			const FVector Center(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle),
				12.5f * FMath::Sin(Flower * 2.17f));
			const FVector Facing = FVector(0.65f * FMath::Cos(Angle), 0.65f * FMath::Sin(Angle),
				0.65f + 0.25f * FMath::Cos(Flower * 1.31f)).GetSafeNormal();
			const FQuat Orientation = FQuat::FindBetweenNormals(FVector::UpVector, Facing);
			const float Size = 0.40f + 0.08f * FMath::Sin(Flower * 1.13f);
			for (int32 Petal = 0; Petal < 5; ++Petal)
			{
				const FQuat Rotation = Orientation * FQuat(FVector::UpVector, Tau * Petal / 5.0f + Angle);
				const FVector Position = Center + Rotation.RotateVector(FVector(0, 5.6f * Size, 0));
				AddPetal(Geometry, FTransform(Rotation, Position, FVector(Size, Size, Size)), 12, 1);
			}
		}
	}

	FVector CubicCurve(const FVector& A, const FVector& B, const FVector& C, const FVector& D, float T)
	{
		const float S = 1.0f - T;
		return A * (S * S * S) + B * (3.0f * S * S * T) + C * (3.0f * S * T * T) + D * (T * T * T);
	}

	FVector TreeStemCenter(float T)
	{
		const float Z = 340.0f * T;
		if (Z <= 245.0f)
		{
			FVector Center = CubicCurve(FVector::ZeroVector, FVector(-10, -6, 0), FVector(10, 5, 0),
				FVector(18, 10, 0), Z / 245.0f);
			Center.Z = Z;
			return Center;
		}
		// 在245cm处保持切线连续，基干没有周期弯曲或拼接后突然增粗的截面。
		FVector Center = CubicCurve(FVector(18, 10, 0), FVector(18 + 8 * 95.0 / 245.0, 10 + 5 * 95.0 / 245.0, 0),
			FVector(25, 9, 0), FVector(24, 8, 0), (Z - 245.0f) / 95.0f);
		Center.Z = Z;
		return Center;
	}

	void AddTreeLimb(FGardenGeometry& Geometry, const TFunction<FVector(float)>& Center,
		float BaseRadius, float TipRadius, int32 Sides, int32 LengthSteps, bool bHorizontalRings)
	{
		const auto Direction = [=](float T)
		{
			return (Center(FMath::Min(1.0f, T + 0.001f)) - Center(FMath::Max(0.0f, T - 0.001f))).GetSafeNormal();
		};
		const FSurface Surface = [=](float U, float V)
		{
			FVector Right = FVector::ForwardVector;
			FVector Up = FVector::RightVector;
			if (!bHorizontalRings)
			{
				const FVector Axis = Direction(V);
				Right = FVector::CrossProduct(FVector::RightVector, Axis).GetSafeNormal();
				Up = FVector::CrossProduct(Axis, Right).GetSafeNormal();
			}
			const float Angle = Tau * U;
			const float Bark = (0.025f * FMath::Cos(3.0f * Angle) + 0.012f * FMath::Sin(5.0f * Angle))
				* 4.0f * V * (1.0f - V);
			const float Radius = FMath::Lerp(BaseRadius, TipRadius, V) * (1.0f + Bark);
			return Center(V) + Radius * (Right * FMath::Cos(Angle) + Up * FMath::Sin(Angle));
		};
		const bool bDescending = bHorizontalRings && Direction(0.5f).Z < 0.0;
		const FVector CapAxis = bDescending ? -FVector::UpVector : FVector::UpVector;
		AddSurface(Geometry, Sides, LengthSteps, Surface, bDescending, true);
		AddCap(Geometry, Center(0), bHorizontalRings ? -CapAxis : -Direction(0), Sides,
			[=](float U) { return Surface(U, 0); });
		AddCap(Geometry, Center(1), bHorizontalRings ? CapAxis : Direction(1), Sides,
			[=](float U) { return Surface(U, 1); });
	}

	void BuildTreeTrunkV2(FGardenGeometry& Geometry)
	{
		AddTreeLimb(Geometry, TreeStemCenter, 20.0f, 2.6f, 20, 40, true);
		const float AnchorHeights[] = { 140, 190, 230 };
		const FVector Ends[] = { FVector(-140, -25, 335), FVector(155, 35, 360), FVector(30, 125, 390) };
		const FVector FirstControls[] = { FVector(-8, -1, 55), FVector(25, 3, 65), FVector(3, 22, 60) };
		const FVector EndDirections[] = { FVector(-25, -4, 35), FVector(40, 8, 30), FVector(8, 35, 28) };
		const float BaseRadii[] = { 10, 8.5f, 7 };
		for (int32 Limb = 0; Limb < 3; ++Limb)
		{
			const FVector Start = TreeStemCenter(AnchorHeights[Limb] / 340.0f);
			const FVector First = Start + FirstControls[Limb];
			const FVector Second = Ends[Limb] - EndDirections[Limb];
			const FVector End = Ends[Limb];
			// 枝根埋入较粗基干内，同一网格一次建出完整树形。
			AddTreeLimb(Geometry, [=](float T) { return CubicCurve(Start, First, Second, End, T); },
				BaseRadii[Limb], 1.4f, 16, 28, false);
		}
		// 四条短支撑根从根颈渐细落地，所有最低顶点仍为Z=0。
		const FVector RootEnds[] = { FVector(-35, -11, 0), FVector(26, -25, 0), FVector(11, 32, 0), FVector(-21, 22, 0) };
		for (const FVector& End : RootEnds)
		{
			const FVector A(0, 0, 34);
			const FVector B(End.X * 0.25, End.Y * 0.25, 18);
			const FVector C(End.X * 0.78, End.Y * 0.78, 4);
			AddTreeLimb(Geometry, [=](float T) { return CubicCurve(A, B, C, End, T); }, 7, 1.2f, 8, 10, true);
		}
	}

	void BuildBranchV2(FGardenGeometry& Geometry)
	{
		AddTreeLimb(Geometry, [](float T)
		{
			const float Bend = T * T * (1.0f - T) * (1.0f - T);
			return FVector(128.0f * Bend, -48.0f * Bend * (1.0f + 0.4f * T), 100.0f * T);
		}, 10, 2, 16, 20, true);
	}

	void BuildBlossomsV2LOD(FGardenGeometry& Geometry, int32 LOD)
	{
		// 远景保留六向边界与九个外侧花心；花面朝向与原花簇一致。
		const int32 OuterFlowers[] = { 1, 3, 5, 6, 8, 10, 12, 14, 17 };
		const int32 AngularSteps = LOD == 0 ? 12 : LOD == 1 ? 8 : 6;
		const int32 FlowerCount = LOD == 2 ? UE_ARRAY_COUNT(OuterFlowers) : 18;
		for (int32 Index = 0; Index < FlowerCount; ++Index)
		{
			const int32 Flower = LOD == 2 ? OuterFlowers[Index] : Index;
			const float Angle = Flower * 2.3999632f;
			const float Radius = 16.0f + 10.0f * FMath::Square(FMath::Sin(Flower * 1.71f));
			const FVector Center(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 12.5f * FMath::Sin(Flower * 2.17f));
			const FVector Facing = FVector(0.65f * FMath::Cos(Angle), 0.65f * FMath::Sin(Angle),
				0.65f + 0.25f * FMath::Cos(Flower * 1.31f)).GetSafeNormal();
			const FQuat Orientation = FQuat::FindBetweenNormals(FVector::UpVector, Facing);
			const float Size = 0.40f + 0.08f * FMath::Sin(Flower * 1.13f);
			for (int32 Petal = 0; Petal < 5; ++Petal)
			{
				const FQuat Rotation = Orientation * FQuat(FVector::UpVector, Tau * Petal / 5.0f + Angle);
				const FTransform Transform(Rotation, Center + Rotation.RotateVector(FVector(0, 5.6f * Size, 0)), FVector(Size));
				// GardenSurface双面显示，单层扇形仍保留凹杯与心形轮廓，省去重复背面和侧壳。
				AddSurface(Geometry, AngularSteps, 1, [=](float U, float V)
				{
					return Transform.TransformPosition(PetalSurface(U, V));
				}, false, true);
			}
		}
	}

	void BuildBlossomsV2(FGardenGeometry& Geometry)
	{
		BuildBlossomsV2LOD(Geometry, 0);
	}

	void BuildReedsV2(FGardenGeometry& Geometry)
	{
		for (int32 Leaf = 0; Leaf < 9; ++Leaf)
		{
			const float Angle = Leaf * 2.3999632f;
			const FVector Across(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			const FVector Lean(-FMath::Sin(Angle), FMath::Cos(Angle), 0);
			const FVector Base = Across * (1.0f + 2.0f * (Leaf % 3) / 2.0f);
			const float Height = 43.0f + 22.0f * Leaf / 8.0f;
			const float Drift = 4.0f + 3.0f * (Leaf % 4) / 3.0f;
			const float HalfWidth = 0.85f + 0.25f * (Leaf % 3) / 2.0f;
			AddSurface(Geometry, 1, 10, [=](float U, float V)
			{
				const float Width = HalfWidth * (0.12f + 0.88f * 4.0f * V * (1.0f - V)) * (1.0f - V);
				return Base + Lean * (Drift * V * V) + Across * (1.5f * V * (1.0f - V) + (2.0f * U - 1.0f) * Width)
					+ FVector(0, 0, Height * V);
			});
		}
	}

	void BuildRock(FGardenGeometry& Geometry)
	{
		const float Heights[] = { 0, 7, 19, 43, 58, 65 };
		const float Radii[] = { 0.68f, 0.92f, 1.0f, 0.91f, 0.69f, 0.42f };
		const FSurface Surface = [=](float U, float V)
		{
			const float Angle = Tau * U;
			const float Level = V * 5.0f;
			const int32 Index = FMath::Min(4, FMath::FloorToInt(Level));
			const float Alpha = Level - Index;
			const float Radius = FMath::Lerp(Radii[Index], Radii[Index + 1], Alpha);
			const float Shape = 0.93f + 0.05f * FMath::Sin(3.0f * Angle + 0.5f) + 0.03f * FMath::Cos(5.0f * Angle);
			const float Z = FMath::Lerp(Heights[Index], Heights[Index + 1], Alpha);
			return FVector(50.0f * Radius * Shape * FMath::Cos(Angle) + 3.0f * FMath::Sin(PI * V),
				42.5f * Radius * Shape * FMath::Sin(Angle), Z + 2.0f * FMath::Sin(2.0f * Angle) * FMath::Sin(PI * V));
		};
		AddSurface(Geometry, 20, 5, Surface, false, true);
		AddCap(Geometry, FVector::ZeroVector, -FVector::UpVector, 20, [=](float U) { return Surface(U, 0); });
		AddCap(Geometry, FVector(0, 0, 65), FVector::UpVector, 20, [=](float U) { return Surface(U, 1); });
	}

	void BuildPedestal(FGardenGeometry& Geometry)
	{
		const float Heights[] = { 0, 5, 9, 15, 21, 28, 57, 66, 71, 80 };
		const float Radii[] = { 26, 26, 23, 23, 17, 12, 12, 15, 18, 18 };
		const FSurface Surface = [=](float U, float V)
		{
			const float Z = 80.0f * V;
			int32 Index = 0;
			while (Index < 8 && Z > Heights[Index + 1]) ++Index;
			float Alpha = (Z - Heights[Index]) / (Heights[Index + 1] - Heights[Index]);
			Alpha = Alpha * Alpha * (3.0f - 2.0f * Alpha);
			const float Radius = FMath::Lerp(Radii[Index], Radii[Index + 1], Alpha);
			return FVector(Radius * FMath::Cos(Tau * U), Radius * FMath::Sin(Tau * U), Z);
		};
		AddSurface(Geometry, 40, 64, Surface, false, true);
		AddCap(Geometry, FVector::ZeroVector, -FVector::UpVector, 40, [=](float U) { return Surface(U, 0); });
		AddCap(Geometry, FVector(0, 0, 80), FVector::UpVector, 40, [=](float U) { return Surface(U, 1); });
	}

	void BuildRipple(FGardenGeometry& Geometry)
	{
		const FSurface Top = [](float U, float V)
		{
			const float Angle = Tau * U;
			const float Radius = 99.3f + 1.4f * V;
			return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle),
				0.10f * FMath::Sin(3.0f * Angle) + 0.04f * FMath::Sin(7.0f * Angle) + 0.06f);
		};
		AddSurface(Geometry, 96, 2, Top, true, true);
		AddSurface(Geometry, 96, 2, [=](float U, float V) { return Top(U, V) - FVector(0, 0, 0.12f); }, false, true);
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			AddSurface(Geometry, 96, 1, [=](float U, float V)
			{
				return Top(U, float(Edge)) - FVector(0, 0, 0.12f * V);
			}, Edge == 0, true);
		}
	}

	struct FGardenAsset
	{
		const TCHAR* Name;
		void (*Generate)(FGardenGeometry&);
		FVector MinimumSize;
		FVector MaximumSize;
		bool bGrounded;
		int32 NumLODs = 1;
		void (*GenerateLOD)(FGardenGeometry&, int32) = nullptr;
		bool bGardenMaterial = false;
		bool bTwoSidedSheet = false;
	};

	bool ValidateGeometry(const FGardenGeometry& Geometry, FString& Error)
	{
		if (Geometry.Vertices.Num() < 3 || Geometry.Triangles.Num() < 1)
		{
			Error = TEXT("庭院网格没有有效三角形");
			return false;
		}
		for (const FGardenVertex& Vertex : Geometry.Vertices)
		{
			if (Vertex.Position.ContainsNaN() || Vertex.Normal.ContainsNaN() || Vertex.Tangent.ContainsNaN()
				|| !FMath::IsFinite(Vertex.UV.X) || !FMath::IsFinite(Vertex.UV.Y)
				|| !Vertex.Normal.IsNormalized() || !Vertex.Tangent.IsNormalized()
				|| FMath::Abs(FVector::DotProduct(Vertex.Normal, Vertex.Tangent)) > 0.01)
			{
				Error = TEXT("庭院网格的位置、UV或切线基无效");
				return false;
			}
		}
		return true;
	}

	FMeshDescription MakeDescription(const FGardenGeometry& Geometry)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		auto Positions = Attributes.GetVertexPositions();
		auto Normals = Attributes.GetVertexInstanceNormals();
		auto Tangents = Attributes.GetVertexInstanceTangents();
		auto Signs = Attributes.GetVertexInstanceBinormalSigns();
		auto Colors = Attributes.GetVertexInstanceColors();
		auto UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		const FPolygonGroupID Group = Description.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("GardenSurface");
		TArray<FVertexInstanceID> Instances;
		Instances.Reserve(Geometry.Vertices.Num());
		for (const FGardenVertex& Point : Geometry.Vertices)
		{
			const FVertexID Vertex = Description.CreateVertex();
			Positions[Vertex] = FVector3f(Point.Position);
			const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
			Normals[Instance] = FVector3f(Point.Normal);
			Tangents[Instance] = FVector3f(Point.Tangent);
			Signs[Instance] = Point.BinormalSign;
			Colors[Instance] = FVector4f(1, 1, 1, 1);
			UVs.Set(Instance, 0, Point.UV);
			Instances.Add(Instance);
		}
		for (const FIntVector& Triangle : Geometry.Triangles)
		{
			const FVertexInstanceID Corners[] = { Instances[Triangle.X], Instances[Triangle.Y], Instances[Triangle.Z] };
			Description.CreateTriangle(Group, MakeArrayView(Corners));
		}
		return Description;
	}

	bool ValidateMesh(UStaticMesh* Mesh, const FGardenAsset& Definition, FString& Error)
	{
		if (!Mesh || Mesh->GetNumSourceModels() != Definition.NumLODs
			|| Mesh->GetStaticMaterials().Num() != 1 || !Mesh->GetStaticMaterials()[0].MaterialInterface
			|| Mesh->GetStaticMaterials()[0].MaterialSlotName != TEXT("GardenSurface")
			|| (Definition.bTwoSidedSheet && !Mesh->GetStaticMaterials()[0].MaterialInterface->IsTwoSided()))
		{
			Error = FString::Printf(TEXT("庭院网格的LOD或材质合同无效：%s"), Definition.Name);
			return false;
		}
		for (int32 LOD = 0; LOD < Definition.NumLODs; ++LOD)
		{
			const FMeshDescription* Description = Mesh->GetMeshDescription(LOD);
			if (!Description || Description->Triangles().Num() < 1 || Description->PolygonGroups().Num() != 1
				|| Mesh->GetStaticMaterials().Num() != 1 || !Mesh->GetStaticMaterials()[0].MaterialInterface)
			{
				Error = FString::Printf(TEXT("庭院网格源数据或单材质合同无效：%s"), Definition.Name);
				return false;
			}
			const FStaticMeshConstAttributes Attributes(*Description);
			const auto Positions = Attributes.GetVertexPositions();
			const auto Normals = Attributes.GetVertexInstanceNormals();
			const auto Tangents = Attributes.GetVertexInstanceTangents();
			const auto Signs = Attributes.GetVertexInstanceBinormalSigns();
			const auto UVs = Attributes.GetVertexInstanceUVs();
			if (UVs.GetNumChannels() < 1)
			{
				Error = FString::Printf(TEXT("庭院网格缺少UV0：%s"), Definition.Name);
				return false;
			}
			FBox Bounds(ForceInit);
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				const FVector Position(Positions[Vertex]);
				if (Position.ContainsNaN()) { Error = TEXT("庭院网格包含非有限顶点"); return false; }
				Bounds += Position;
			}
			for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
			{
				const FVector Normal(Normals[Instance]);
				const FVector Tangent(Tangents[Instance]);
				const FVector2f UV = UVs.Get(Instance, 0);
				if (Normal.ContainsNaN() || Tangent.ContainsNaN() || !Normal.IsNormalized() || !Tangent.IsNormalized()
					|| FMath::Abs(FVector::DotProduct(Normal, Tangent)) > 0.01
					|| !FMath::IsFinite(Signs[Instance]) || FMath::Abs(Signs[Instance]) < 0.99f
					|| !FMath::IsFinite(UV.X) || !FMath::IsFinite(UV.Y))
				{
					Error = FString::Printf(TEXT("庭院网格的UV或切线基无效：%s"), Definition.Name);
					return false;
				}
			}
			const FVector Size = Bounds.GetSize();
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (Size[Axis] < Definition.MinimumSize[Axis] || Size[Axis] > Definition.MaximumSize[Axis])
				{
					Error = FString::Printf(TEXT("庭院网格尺寸超出合同：%s (%s)"), Definition.Name, *Size.ToString());
					return false;
				}
			}
			if (Definition.bGrounded && FMath::Abs(Bounds.Min.Z) > 0.01)
			{
				Error = FString::Printf(TEXT("庭院网格底部不在Z=0：%s"), Definition.Name);
				return false;
			}
			if (Definition.GenerateLOD == BuildBlossomsV2LOD)
			{
				const int32 ExpectedTriangles[] = { 1080, 720, 270 };
				const float ScreenSizes[] = { 1.0f, 0.22f, 0.055f };
				const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
				if (Description->Triangles().Num() != ExpectedTriangles[LOD]
					|| Mesh->GetAutoComputeLODScreenSize()
					|| !FMath::IsNearlyEqual(Mesh->GetSourceModel(LOD).ScreenSize.Default, ScreenSizes[LOD])
					|| !RenderData || RenderData->LODResources.Num() != 3
					|| RenderData->LODResources[LOD].GetNumTriangles() != ExpectedTriangles[LOD]
					|| !FMath::IsNearlyEqual(RenderData->ScreenSize[LOD].Default, ScreenSizes[LOD]))
				{
					Error = FString::Printf(TEXT("花簇手工LOD的三角形数或屏幕尺寸无效：%s LOD%d"), Definition.Name, LOD);
					return false;
				}
			}
		}
		return true;
	}

	bool SaveMesh(UStaticMesh* Mesh, FString& Error)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(Mesh->GetOutermost()->GetName(),
			FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_None;
		Args.Error = GError;
		if (!UPackage::SavePackage(Mesh->GetOutermost(), Mesh, *Filename, Args))
		{
			Error = FString::Printf(TEXT("无法保存庭院网格：%s"), *Filename);
			return false;
		}
		return true;
	}
}

bool FMMDGardenMeshBuilder::BuildAssets(bool bSave, FString& Error)
{
	Error.Reset();
	if (!IsInGameThread())
	{
		Error = TEXT("庭院网格必须在游戏线程生成");
		return false;
	}
	const FGardenAsset Assets[] =
	{
		{ TEXT("SM_GardenRoofHip"), [](FGardenGeometry& G) { BuildRoof(G, 200, 150, 80, 85, 6, 18); }, FVector(399, 299, 90), FVector(401, 301, 92), false },
		{ TEXT("SM_GardenRoofTile"), BuildTile, FVector(11.9, 41.9, 3.9), FVector(12.1, 42.1, 4.1), false },
		{ TEXT("SM_GardenCurvedBeam"), BuildBeam, FVector(239, 17.9, 37), FVector(241, 18.1, 39), false },
		{ TEXT("SM_GardenBranch"), BuildBranch, FVector(22, 17, 99.9), FVector(35, 25, 100.1), true },
		{ TEXT("SM_GardenBlossomCluster"), BuildBlossoms, FVector(38, 38, 24), FVector(60, 60, 40), false },
		{ TEXT("SM_GardenPetal"), [](FGardenGeometry& G) { AddPetal(G, FTransform::Identity, 40, 5); }, FVector(7.9, 11.6, 0.9), FVector(8.1, 12.2, 1.4), false },
		{ TEXT("SM_GardenRock"), BuildRock, FVector(85, 72, 64.9), FVector(105, 90, 65.1), true },
		{ TEXT("SM_GardenLanternCap"), [](FGardenGeometry& G) { BuildRoof(G, 42.5f, 42.5f, 0, 25, 4, 4); }, FVector(84.9, 84.9, 28.9), FVector(85.1, 85.1, 30.5), false },
		{ TEXT("SM_GardenLanternPedestal"), BuildPedestal, FVector(51.9, 51.9, 79.9), FVector(52.1, 52.1, 80.1), true },
		{ TEXT("SM_GardenWaterRipple"), BuildRipple, FVector(201, 201, 0.30), FVector(202, 202, 0.45), false },
		{ TEXT("SM_GardenTreeTrunkV2"), BuildTreeTrunkV2, FVector(290, 140, 389), FVector(305, 180, 393), true, 1, nullptr, true, false },
		{ TEXT("SM_GardenBranchV2"), BuildBranchV2, FVector(20, 19, 99.9), FVector(29, 23, 100.1), true, 1, nullptr, true, false },
		{ TEXT("SM_GardenBlossomClusterV2"), BuildBlossomsV2, FVector(38, 38, 24), FVector(60, 60, 40), false, 3, BuildBlossomsV2LOD, true, true },
		{ TEXT("SM_GardenReedClumpV2"), BuildReedsV2, FVector(8, 8, 64.9), FVector(25, 25, 65.1), true, 1, nullptr, true, true }
	};
	UMaterialInterface* DefaultMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StagePanel.M_MMD_StagePanel"));
	if (!DefaultMaterial)
	{
		DefaultMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_StageFloor.M_MMD_StageFloor"));
	}
	if (!DefaultMaterial)
	{
		Error = TEXT("请先生成StagePanel或StageFloor默认材质");
		return false;
	}
	UMaterialInterface* GardenMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Ue5MMDTools/Stages/Materials/M_MMD_GardenSurface.M_MMD_GardenSurface"));
	if (!GardenMaterial || !GardenMaterial->IsTwoSided())
	{
		Error = TEXT("请先生成双面的GardenSurface材质，再生成V2网格");
		return false;
	}
	for (const FGardenAsset& Definition : Assets)
	{
		const FString PackageName = FString(MeshRoot) + Definition.Name;
		const FString ObjectPath = PackageName + TEXT(".") + Definition.Name;
		const bool bOnDisk = FPackageName::DoesPackageExist(PackageName);
		UObject* Existing = bOnDisk ? LoadObject<UObject>(nullptr, *ObjectPath) : FindObject<UObject>(nullptr, *ObjectPath);
		UStaticMesh* Mesh = Cast<UStaticMesh>(Existing);
		if (Existing || bOnDisk)
		{
			if (!ValidateMesh(Mesh, Definition, Error)) return false;
			if (bSave && !bOnDisk && !SaveMesh(Mesh, Error)) return false;
			continue;
		}
		TArray<FMeshDescription> Descriptions;
		Descriptions.Reserve(Definition.NumLODs);
		for (int32 LOD = 0; LOD < Definition.NumLODs; ++LOD)
		{
			FGardenGeometry Geometry;
			if (Definition.GenerateLOD) Definition.GenerateLOD(Geometry, LOD);
			else Definition.Generate(Geometry);
			if (!ValidateGeometry(Geometry, Error)) return false;
			Descriptions.Add(MakeDescription(Geometry));
		}
		UPackage* Package = CreatePackage(*PackageName);
		Mesh = NewObject<UStaticMesh>(Package, Definition.Name, RF_Public | RF_Standalone);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(Definition.bGardenMaterial ? GardenMaterial : DefaultMaterial,
			TEXT("GardenSurface"), TEXT("GardenSurface")));
		Mesh->SetNumSourceModels(Definition.NumLODs);
		Mesh->SetAutoComputeLODScreenSize(Definition.NumLODs == 1);
		const float ScreenSizes[] = { 1.0f, 0.22f, 0.055f };
		TArray<const FMeshDescription*> DescriptionPointers;
		for (int32 LOD = 0; LOD < Definition.NumLODs; ++LOD)
		{
			FStaticMeshSourceModel& Source = Mesh->GetSourceModel(LOD);
			FMeshBuildSettings& Settings = Source.BuildSettings;
			Settings.bRecomputeNormals = false;
			Settings.bRecomputeTangents = false;
			Settings.bUseMikkTSpace = false;
			Settings.bGenerateLightmapUVs = false;
			Settings.bUseFullPrecisionUVs = true;
			Source.ScreenSize.Default = ScreenSizes[LOD];
			// 每级都有原创源几何，禁止再做自动减面。
			Source.ReductionSettings.PercentTriangles = 1.0f;
			Source.ReductionSettings.PercentVertices = 1.0f;
			Source.ReductionSettings.MaxDeviation = 0.0f;
			Source.ReductionSettings.BaseLODModel = LOD;
			DescriptionPointers.Add(&Descriptions[LOD]);
		}
		Mesh->SetLightMapCoordinateIndex(0);
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bBuildSimpleCollision = false;
		Params.bFastBuild = false;
		Params.bUseHashAsGuid = true;
		if (!Mesh->BuildFromMeshDescriptions(DescriptionPointers, Params))
		{
			Error = FString::Printf(TEXT("无法构建庭院网格：%s"), Definition.Name);
			return false;
		}
		UStaticMesh* FinishedMeshes[] = { Mesh };
		FStaticMeshCompilingManager::Get().FinishCompilation(MakeArrayView(FinishedMeshes));
		if (!ValidateMesh(Mesh, Definition, Error)) return false;
		FAssetRegistryModule::AssetCreated(Mesh);
		Mesh->MarkPackageDirty();
		if (bSave && !SaveMesh(Mesh, Error)) return false;
	}
	return true;
}
