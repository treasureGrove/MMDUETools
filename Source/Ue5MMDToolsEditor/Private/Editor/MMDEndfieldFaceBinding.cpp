#include "Editor/MMDEndfieldFaceBinding.h"

#include "Components/SkeletalMeshComponent.h"
#include "Containers/StaticArray.h"
#include "Editor/MMDEndfieldFaceSdfBaker.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "Rendering/MMDEndfieldHeadFrame.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "SkinnedAssetCompiler.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Stage/MMDStageLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace MMDEndfieldOverlay5
{
	using MMDEndfieldFaceBinding::FOverlayStats;
	constexpr double MinimumValidRatio = 0.98;
	constexpr double MinimumHeadWeight = 0.90;
	constexpr double MaximumDepthError = 0.03;
	constexpr double DepthQuantizationError = 1.5 / 255.0;
	constexpr double MinimumNormalDot = 0.50;
	constexpr int32 ProjectionGridSize = 64;

	struct FCpuLod
	{
		const FSkeletalMeshLODRenderData* LOD = nullptr;
		const FPositionVertexBuffer* Positions = nullptr;
		const FRawStaticIndexBuffer16or32Interface* Indices = nullptr;
		const FSkinWeightVertexBuffer* Weights = nullptr;
		int32 VertexCount = 0;
	};

	struct FHostTriangle
	{
		FVector Local[3];
		FVector Normal = FVector::ZeroVector;
		FVector2D UV[3];
		double Determinant = 0.0;
	};

	struct FHost
	{
		int32 Slot = INDEX_NONE;
		MMDEnfieldFaceSdf::FBakeResult Bake;
		TArray<FHostTriangle> Triangles;
		TArray<TArray<int32>> Bins;
		TBitArray<> AtlasDepthValid;
		UTexture* BaseTexture = nullptr;
	};

	static int32 SectionMaterial(const USkeletalMesh* Mesh, int32 LODIndex, int32 SectionIndex,
		const FSkelMeshRenderSection& Section)
	{
		const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(LODIndex);
		return Info && Info->LODMaterialMap.IsValidIndex(SectionIndex) && Info->LODMaterialMap[SectionIndex] != INDEX_NONE
			? Info->LODMaterialMap[SectionIndex] : static_cast<int32>(Section.MaterialIndex);
	}

	static UTexture* FindBaseTexture(UMaterialInterface* Material)
	{
		if (!Material) return nullptr;
		for (const TCHAR* Name : { TEXT("base_color_tex"), TEXT("BaseColorMap"), TEXT("BaseColorTexture"),
			TEXT("BaseColor"), TEXT("DiffuseTexture"), TEXT("DiffuseTex") })
		{
			UTexture* Texture = nullptr;
			if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Texture) && IsValid(Texture)) return Texture;
		}
		return nullptr;
	}

	static bool ReadLod(const USkeletalMesh* Mesh, int32 LODIndex, FCpuLod& Out, FString& Error)
	{
		const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
		if (!Render || !Render->LODRenderData.IsValidIndex(LODIndex))
		{ Error = TEXT("缺少参考 LOD"); return false; }
		Out.LOD = &Render->LODRenderData[LODIndex];
		Out.Positions = &Out.LOD->StaticVertexBuffers.PositionVertexBuffer;
		Out.VertexCount = static_cast<int32>(Out.Positions->GetNumVertices());
		if (Out.VertexCount <= 0 || !Out.Positions->GetVertexData() || !Out.LOD->MultiSizeIndexContainer.IsIndexBufferValid())
		{ Error = TEXT("LOD 缺少 CPU 位置或索引"); return false; }
		Out.Indices = Out.LOD->MultiSizeIndexContainer.GetIndexBuffer();
		if (!Out.Indices || !Out.Indices->Num() || !Out.Indices->GetResourceDataSize())
		{ Error = TEXT("LOD 索引没有 CPU 副本"); return false; }
		Out.Weights = Out.LOD->GetSkinWeightVertexBuffer();
		if (!Out.Weights || Out.Weights->GetNumVertices() != static_cast<uint32>(Out.VertexCount)
			|| !Out.Weights->GetDataVertexBuffer()->GetWeightData()
			|| (Out.Weights->GetVariableBonesPerVertex() && !Out.Weights->GetLookupVertexBuffer()->GetLookupData()))
		{ Error = TEXT("LOD 蒙皮权重没有 CPU 副本，保守跳过透明关联"); return false; }
		return true;
	}

	static FVector LocalPosition(const FVector& Position, const MMDEnfieldFaceSdf::FBakeSettings& Settings)
	{
		const FVector Offset = (Position - Settings.HeadCenter) / Settings.HeadRadius;
		return FVector(FVector::DotProduct(Offset, Settings.HeadRight), FVector::DotProduct(Offset, Settings.HeadForward),
			FVector::DotProduct(Offset, Settings.HeadUp));
	}

	static bool InHeadRegion(const FVector& Local)
	{
		return !Local.ContainsNaN() && Local.Y > 0.0 && FMath::Abs(Local.X) < 1.5
			&& FMath::Abs(Local.Z) < 1.5 && Local.SizeSquared() < 6.25;
	}

	static bool ReadHeadWeight(const FCpuLod& Geometry, const FSkelMeshRenderSection& Section,
		uint32 Vertex, const TBitArray<>& HeadBones, double& OutWeight)
	{
		if (Vertex < Section.BaseVertexIndex || Vertex >= static_cast<uint64>(Section.BaseVertexIndex) + Section.NumVertices)
			return false;
		const TConstArrayView<FBoneIndexType> BoneMap = Section.HasUnifiedBoneMap()
			? Geometry.LOD->GetUnifiedBoneMap() : TConstArrayView<FBoneIndexType>(Section.BoneMap);
		uint32 Offset = 0, Count = 0;
		Geometry.Weights->GetVertexInfluenceOffsetCount(Vertex, Offset, Count);
		if (!Count || Count > Geometry.Weights->GetMaxBoneInfluences()) return false;
		uint32 Total = 0, Head = 0;
		for (uint32 Influence = 0; Influence < Count; ++Influence)
		{
			// GetBoneWeight 已统一返回 16bit 原始权重；按总和归一，不假定输入为 8bit。
			const uint16 Weight = Geometry.Weights->GetBoneWeight(Vertex, Influence);
			if (!Weight) continue;
			const uint32 LocalBone = Geometry.Weights->GetBoneIndex(Vertex, Influence);
			if (!BoneMap.IsValidIndex(LocalBone) || !HeadBones.IsValidIndex(BoneMap[LocalBone])) return false;
			Total += Weight;
			if (HeadBones[BoneMap[LocalBone]]) Head += Weight;
		}
		if (!Total) return false;
		OutWeight = static_cast<double>(Head) / Total;
		return true;
	}

	static bool BuildHostProjection(const USkeletalMesh* Mesh, int32 LODIndex, const FCpuLod& Geometry,
		const MMDEnfieldFaceSdf::FBakeSettings& Settings, FHost& Host)
	{
		Host.Bins.SetNum(ProjectionGridSize * ProjectionGridSize);
		for (int32 SectionIndex = 0; SectionIndex < Geometry.LOD->RenderSections.Num(); ++SectionIndex)
		{
			const FSkelMeshRenderSection& Section = Geometry.LOD->RenderSections[SectionIndex];
			if (Section.bDisabled || SectionMaterial(Mesh, LODIndex, SectionIndex, Section) != Host.Slot) continue;
			if (static_cast<uint64>(Section.BaseIndex) + static_cast<uint64>(Section.NumTriangles) * 3 > static_cast<uint64>(Geometry.Indices->Num())) return false;
			for (uint32 Index = 0; Index < Section.NumTriangles; ++Index)
			{
				FHostTriangle Triangle;
				FVector Position[3];
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const uint32 Vertex = Geometry.Indices->Get(Section.BaseIndex + Index * 3 + Corner);
					if (Vertex >= static_cast<uint32>(Geometry.VertexCount)) return false;
					Position[Corner] = FVector(Geometry.Positions->VertexPosition(Vertex));
					Triangle.Local[Corner] = LocalPosition(Position[Corner], Settings);
					Triangle.UV[Corner] = FVector2D(0.5 + Triangle.Local[Corner].X / 3.0, 0.5 - Triangle.Local[Corner].Z / 3.0);
				}
				// RenderData 已由导入器交换绕序；不能再次使用原始 PMX 的叉乘顺序。
				Triangle.Normal = FVector::CrossProduct(Position[2] - Position[0], Position[1] - Position[0]).GetSafeNormal();
				const FVector2D A = Triangle.UV[1] - Triangle.UV[0], B = Triangle.UV[2] - Triangle.UV[0];
				Triangle.Determinant = A.X * B.Y - A.Y * B.X;
				if (Triangle.Normal.IsNearlyZero() || !FMath::IsFinite(Triangle.Determinant) || FMath::Abs(Triangle.Determinant) < 1.e-12) continue;
				const double MinU = FMath::Min3(Triangle.UV[0].X, Triangle.UV[1].X, Triangle.UV[2].X);
				const double MaxU = FMath::Max3(Triangle.UV[0].X, Triangle.UV[1].X, Triangle.UV[2].X);
				const double MinV = FMath::Min3(Triangle.UV[0].Y, Triangle.UV[1].Y, Triangle.UV[2].Y);
				const double MaxV = FMath::Max3(Triangle.UV[0].Y, Triangle.UV[1].Y, Triangle.UV[2].Y);
				if (MaxU < 0 || MinU > 1 || MaxV < 0 || MinV > 1) continue;
				const int32 StoredIndex = Host.Triangles.Add(Triangle);
				const int32 X0 = FMath::Clamp(FMath::FloorToInt(MinU * ProjectionGridSize), 0, ProjectionGridSize - 1);
				const int32 X1 = FMath::Clamp(FMath::FloorToInt(MaxU * ProjectionGridSize), 0, ProjectionGridSize - 1);
				const int32 Y0 = FMath::Clamp(FMath::FloorToInt(MinV * ProjectionGridSize), 0, ProjectionGridSize - 1);
				const int32 Y1 = FMath::Clamp(FMath::FloorToInt(MaxV * ProjectionGridSize), 0, ProjectionGridSize - 1);
				for (int32 Y = Y0; Y <= Y1; ++Y)
					for (int32 X = X0; X <= X1; ++X) Host.Bins[Y * ProjectionGridSize + X].Add(StoredIndex);
			}
		}
		return !Host.Triangles.IsEmpty();
	}

	static bool QueryHostSurface(const FHost& Host, const FVector2D& UV, double& OutDepth, FVector& OutNormal)
	{
		if (Host.Triangles.IsEmpty() || UV.X < 0 || UV.X > 1 || UV.Y < 0 || UV.Y > 1) return false;
		const int32 BinX = FMath::Clamp(FMath::FloorToInt(UV.X * ProjectionGridSize), 0, ProjectionGridSize - 1);
		const int32 BinY = FMath::Clamp(FMath::FloorToInt(UV.Y * ProjectionGridSize), 0, ProjectionGridSize - 1);
		double FrontDepth = -TNumericLimits<double>::Max();
		FVector HostNormal = FVector::ZeroVector;
		for (const int32 Index : Host.Bins[BinY * ProjectionGridSize + BinX])
		{
			const FHostTriangle& Triangle = Host.Triangles[Index];
			const FVector2D D = UV - Triangle.UV[0], A = Triangle.UV[1] - Triangle.UV[0], B = Triangle.UV[2] - Triangle.UV[0];
			const double W1 = (D.X * B.Y - D.Y * B.X) / Triangle.Determinant;
			const double W2 = (A.X * D.Y - A.Y * D.X) / Triangle.Determinant, W0 = 1.0 - W1 - W2;
			if (W0 < -1.e-5 || W1 < -1.e-5 || W2 < -1.e-5) continue;
			const double Depth = Triangle.Local[0].Y * W0 + Triangle.Local[1].Y * W1 + Triangle.Local[2].Y * W2;
			if (Depth > FrontDepth) { FrontDepth = Depth; HostNormal = Triangle.Normal; }
		}
		if (HostNormal.IsNearlyZero()) return false;
		OutDepth = FrontDepth;
		OutNormal = HostNormal;
		return true;
	}

	static void ValidateAtlasDepth(FHost& Host)
	{
		Host.AtlasDepthValid.Init(false, Host.Bake.Size * Host.Bake.Size);
		for (int32 Y = 0; Y < Host.Bake.Size; ++Y)
		for (int32 X = 0; X < Host.Bake.Size; ++X)
		{
			const int32 Pixel = Y * Host.Bake.Size + X, Byte = Pixel * 4;
			if (Host.Bake.Pixels[Byte + 3] != 255) continue;
			double Depth = 0.0;
			FVector Normal;
			if (!QueryHostSurface(Host, FVector2D((X + 0.5) / Host.Bake.Size, (Y + 0.5) / Host.Bake.Size), Depth, Normal)) continue;
			const double Decoded = (Host.Bake.Pixels[Byte + 2] / 255.0 - 0.5) * 3.0;
			// B 必须与其自身 texel 中心的真实几何吻合；浮点栅格容差不扩大 .03R 贴合窗。
			Host.AtlasDepthValid[Pixel] = FMath::Abs(Decoded - Depth) <= DepthQuantizationError + 1.e-4;
		}
	}

	static bool SampleHost(const FHost& Host, const FHost& GeometryHost, const FVector& Local,
		const FVector& Normal, double& OutError)
	{
		const FVector2D UV(0.5 + Local.X / 3.0, 0.5 - Local.Z / 3.0);
		if (!InHeadRegion(Local)) return false;
		double CurrentDepth = 0.0, ReferenceDepth = 0.0;
		FVector CurrentNormal, ReferenceNormal;
		if (!QueryHostSurface(GeometryHost, UV, CurrentDepth, CurrentNormal)
			|| !QueryHostSurface(Host, UV, ReferenceDepth, ReferenceNormal)) return false;
		const double GeometryError = FMath::Max(FMath::Abs(Local.Y - CurrentDepth), FMath::Abs(Local.Y - ReferenceDepth));
		if (GeometryError > MaximumDepthError || FVector::DotProduct(Normal, CurrentNormal) < MinimumNormalDot) return false;
		const double SampleX = UV.X * Host.Bake.Size - 0.5, SampleY = UV.Y * Host.Bake.Size - 0.5;
		const int32 X0 = FMath::FloorToInt(SampleX), Y0 = FMath::FloorToInt(SampleY);
		const double WX = SampleX - X0, WY = SampleY - Y0;
		double Coverage = 0.0;
		for (int32 Y = 0; Y < 2; ++Y)
		for (int32 X = 0; X < 2; ++X)
		{
			const double Weight = (X ? WX : 1.0 - WX) * (Y ? WY : 1.0 - WY);
			if (Weight < 1.e-6) continue;
			const int32 Pixel = FMath::Clamp(Y0 + Y, 0, Host.Bake.Size - 1) * Host.Bake.Size
				+ FMath::Clamp(X0 + X, 0, Host.Bake.Size - 1), Byte = Pixel * 4;
			if (!Host.AtlasDepthValid.IsValidIndex(Pixel) || !Host.AtlasDepthValid[Pixel]) continue;
			const double Depth = (Host.Bake.Pixels[Byte + 2] / 255.0 - 0.5) * 3.0;
			// receiver 与邻 tap 的深度还包含表面坡度；只用于验证 runtime .08R 完整窗口。
			if (FMath::Abs(Local.Y - Depth) <= 0.08) Coverage += Weight;
		}
		if (Coverage < MinimumValidRatio) return false;
		OutError = GeometryError;
		return true;
	}

	struct FPolygonPoint
	{
		FVector Local = FVector::ZeroVector;
		double HeadWeight = 0.0;
	};

	static void ClipPlane(TArray<FPolygonPoint>& Polygon, int32 Axis, double Boundary, bool KeepGreater)
	{
		if (Polygon.IsEmpty()) return;
		TArray<FPolygonPoint> Clipped;
		FPolygonPoint Previous = Polygon.Last();
		bool PreviousInside = KeepGreater ? Previous.Local[Axis] >= Boundary : Previous.Local[Axis] <= Boundary;
		for (const FPolygonPoint& Point : Polygon)
		{
			const bool Inside = KeepGreater ? Point.Local[Axis] >= Boundary : Point.Local[Axis] <= Boundary;
			if (Inside != PreviousInside)
			{
				const double Alpha = (Boundary - Previous.Local[Axis]) / (Point.Local[Axis] - Previous.Local[Axis]);
				FPolygonPoint Intersection;
				Intersection.Local = FMath::Lerp(Previous.Local, Point.Local, Alpha);
				Intersection.HeadWeight = FMath::Lerp(Previous.HeadWeight, Point.HeadWeight, Alpha);
				Clipped.Add(Intersection);
			}
			if (Inside) Clipped.Add(Point);
			Previous = Point;
			PreviousInside = Inside;
		}
		Polygon = MoveTemp(Clipped);
	}

	static double PolygonArea(const TArray<FPolygonPoint>& Polygon)
	{
		double Area = 0.0;
		for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
			Area += FVector::CrossProduct(Polygon[Index].Local - Polygon[0].Local,
				Polygon[Index + 1].Local - Polygon[0].Local).Size() * 0.5;
		return Area;
	}

	static double ValidTriangleArea(const FHost& Host, const FHost& GeometryHost,
		const TStaticArray<FPolygonPoint, 3>& Triangle, const FVector& Normal, double& OutMaximumError)
	{
		const double MinX = FMath::Min3(Triangle[0].Local.X, Triangle[1].Local.X, Triangle[2].Local.X);
		const double MaxX = FMath::Max3(Triangle[0].Local.X, Triangle[1].Local.X, Triangle[2].Local.X);
		const double MinZ = FMath::Min3(Triangle[0].Local.Z, Triangle[1].Local.Z, Triangle[2].Local.Z);
		const double MaxZ = FMath::Max3(Triangle[0].Local.Z, Triangle[1].Local.Z, Triangle[2].Local.Z);
		const int32 X0 = FMath::Clamp(FMath::FloorToInt((0.5 + MinX / 3.0) * Host.Bake.Size - 0.5), -1, Host.Bake.Size - 1);
		const int32 X1 = FMath::Clamp(FMath::FloorToInt((0.5 + MaxX / 3.0) * Host.Bake.Size - 0.5), -1, Host.Bake.Size - 1);
		const int32 Y0 = FMath::Clamp(FMath::FloorToInt((0.5 - MaxZ / 3.0) * Host.Bake.Size - 0.5), -1, Host.Bake.Size - 1);
		const int32 Y1 = FMath::Clamp(FMath::FloorToInt((0.5 - MinZ / 3.0) * Host.Bake.Size - 0.5), -1, Host.Bake.Size - 1);
		double ValidArea = 0.0;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		for (int32 X = X0; X <= X1; ++X)
		{
			TArray<FPolygonPoint> Polygon;
			for (const FPolygonPoint& Point : Triangle) Polygon.Add(Point);
			// 按双线性采样的单元精确裁剪；单元内固定四 tap，孔洞不能被重心采样漏过。
			ClipPlane(Polygon, 0, 3.0 * ((X + 0.5) / Host.Bake.Size - 0.5), true);
			ClipPlane(Polygon, 0, 3.0 * ((X + 1.5) / Host.Bake.Size - 0.5), false);
			ClipPlane(Polygon, 2, 3.0 * (0.5 - (Y + 1.5) / Host.Bake.Size), true);
			ClipPlane(Polygon, 2, 3.0 * (0.5 - (Y + 0.5) / Host.Bake.Size), false);
			if (Polygon.Num() < 3) continue;
			const double Area = PolygonArea(Polygon);
			if (Area <= 1.e-14) continue;
			bool Valid = true;
			for (int32 DY = 0; DY < 2 && Valid; ++DY)
			for (int32 DX = 0; DX < 2; ++DX)
			{
				const int32 Byte = (FMath::Clamp(Y + DY, 0, Host.Bake.Size - 1) * Host.Bake.Size
					+ FMath::Clamp(X + DX, 0, Host.Bake.Size - 1)) * 4;
				if (!Host.AtlasDepthValid[Byte / 4]) { Valid = false; break; }
			}
			if (!Valid) continue;
			FPolygonPoint Center;
			for (const FPolygonPoint& Point : Polygon)
			{
				Center.Local += Point.Local / Polygon.Num();
				Center.HeadWeight += Point.HeadWeight / Polygon.Num();
			}
			Polygon.Add(Center);
			for (const FPolygonPoint& Point : Polygon)
			{
				double Error = 0.0;
				if (Point.HeadWeight < MinimumHeadWeight || !SampleHost(Host, GeometryHost, Point.Local, Normal, Error))
				{ Valid = false; break; }
				for (int32 DY = 0; DY < 2 && Valid; ++DY)
				for (int32 DX = 0; DX < 2; ++DX)
				{
					const int32 Byte = (FMath::Clamp(Y + DY, 0, Host.Bake.Size - 1) * Host.Bake.Size
						+ FMath::Clamp(X + DX, 0, Host.Bake.Size - 1)) * 4;
					const double Depth = (Host.Bake.Pixels[Byte + 2] / 255.0 - 0.5) * 3.0;
					if (FMath::Abs(Point.Local.Y - Depth) > 0.08) { Valid = false; break; }
				}
				if (!Valid) break;
				OutMaximumError = FMath::Max(OutMaximumError, Error);
			}
			if (Valid) ValidArea += Area;
		}
		return ValidArea;
	}

	static bool ValidateLod(const USkeletalMesh* Mesh, int32 LODIndex, int32 Slot, const FCpuLod& Geometry,
		const MMDEnfieldFaceSdf::FBakeSettings& Settings, const FHost& Host, const FHost& GeometryHost, const TBitArray<>& HeadBones,
		MMDEndfieldFaceBinding::FOverlayStats& Stats, bool& bPresent)
	{
		TMap<uint32, FVector> VertexNormals;
		TMap<uint32, double> HeadWeights;
		TArray<TStaticArray<uint32, 3>> Triangles;
		TArray<FVector> TriangleNormals;
		TArray<double> TriangleAreas;
		bPresent = false;
		for (int32 SectionIndex = 0; SectionIndex < Geometry.LOD->RenderSections.Num(); ++SectionIndex)
		{
			const FSkelMeshRenderSection& Section = Geometry.LOD->RenderSections[SectionIndex];
			if (Section.bDisabled || SectionMaterial(Mesh, LODIndex, SectionIndex, Section) != Slot) continue;
			bPresent = true;
			if (Section.HasClothingData()) { Stats.Reason = TEXT("附脸层包含布料变形 section"); return false; }
			if (static_cast<uint64>(Section.BaseIndex) + static_cast<uint64>(Section.NumTriangles) * 3 > static_cast<uint64>(Geometry.Indices->Num()))
			{ Stats.Reason = TEXT("附脸层 section 索引越界"); return false; }
			for (uint32 Index = 0; Index < Section.NumTriangles; ++Index)
			{
				TStaticArray<uint32, 3> Indices;
				FVector Positions[3];
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const uint32 Vertex = Geometry.Indices->Get(Section.BaseIndex + Index * 3 + Corner);
					if (Vertex >= static_cast<uint32>(Geometry.VertexCount)) { Stats.Reason = TEXT("附脸层顶点越界"); return false; }
					Indices[Corner] = Vertex;
					Positions[Corner] = FVector(Geometry.Positions->VertexPosition(Vertex));
					// 头窗是全顶点硬约束；不能先过滤再计算 98% 的分母。
					if (!InHeadRegion(LocalPosition(Positions[Corner], Settings)))
					{ Stats.Reason = TEXT("附脸层存在头部窗口外顶点"); return false; }
					if (!HeadWeights.Contains(Vertex))
					{
						double Weight = 0.0;
						if (!ReadHeadWeight(Geometry, Section, Vertex, HeadBones, Weight))
						{ Stats.Reason = TEXT("附脸层骨映射或权重无效"); return false; }
						HeadWeights.Add(Vertex, Weight);
					}
				}
				const FVector AreaNormal = FVector::CrossProduct(Positions[2] - Positions[0], Positions[1] - Positions[0]);
				const double TwiceArea = AreaNormal.Size();
				for (const uint32 Vertex : Indices) VertexNormals.FindOrAdd(Vertex, FVector::ZeroVector) += AreaNormal;
				Triangles.Add(Indices);
				TriangleNormals.Add(AreaNormal.GetSafeNormal());
				TriangleAreas.Add(TwiceArea * 0.5);
			}
		}
		if (!bPresent) return true;
		if (GeometryHost.Triangles.IsEmpty()) { Stats.Reason = TEXT("当前 LOD 没有启用的同身份主脸 section"); return false; }
		if (VertexNormals.IsEmpty() || Triangles.IsEmpty()) { Stats.Reason = TEXT("附脸层没有可验证三角形"); return false; }
		int32 ValidVertices = 0;
		double MinHeadWeight = 1.0, MaxError = 0.0, TotalArea = 0.0, ValidArea = 0.0;
		for (const TPair<uint32, FVector>& Pair : VertexNormals)
		{
			const double Weight = HeadWeights.FindChecked(Pair.Key);
			MinHeadWeight = FMath::Min(MinHeadWeight, Weight);
			double Error = 0.0;
			if (Weight >= MinimumHeadWeight && SampleHost(Host, GeometryHost,
				LocalPosition(FVector(Geometry.Positions->VertexPosition(Pair.Key)), Settings), Pair.Value.GetSafeNormal(), Error))
			{ ++ValidVertices; MaxError = FMath::Max(MaxError, Error); }
		}
		for (int32 Index = 0; Index < Triangles.Num(); ++Index)
		{
			// 使用归一化头部坐标面积，比例与参考组件空间一致。
			TotalArea += TriangleAreas[Index] / FMath::Square(Settings.HeadRadius);
			TStaticArray<FPolygonPoint, 3> Polygon;
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				const uint32 Vertex = Triangles[Index][Corner];
				Polygon[Corner].Local = LocalPosition(FVector(Geometry.Positions->VertexPosition(Vertex)), Settings);
				Polygon[Corner].HeadWeight = HeadWeights.FindChecked(Vertex);
			}
			ValidArea += ValidTriangleArea(Host, GeometryHost, Polygon, TriangleNormals[Index], MaxError);
		}
		const double VertexRatio = static_cast<double>(ValidVertices) / VertexNormals.Num();
		const double AreaRatio = TotalArea > UE_SMALL_NUMBER ? ValidArea / TotalArea : 0.0;
		Stats.TestedVertices += VertexNormals.Num();
		Stats.MatchedVertices += ValidVertices;
		Stats.TestedTriangles += Triangles.Num();
		Stats.ValidVertexRatio = FMath::Min(Stats.ValidVertexRatio, static_cast<float>(VertexRatio));
		Stats.ValidAreaRatio = FMath::Min(Stats.ValidAreaRatio, static_cast<float>(AreaRatio));
		Stats.MinimumHeadWeight = FMath::Min(Stats.MinimumHeadWeight, static_cast<float>(MinHeadWeight));
		Stats.MaxMatchedDepthError = FMath::Max(Stats.MaxMatchedDepthError, static_cast<float>(MaxError));
		if (VertexRatio < MinimumValidRatio || AreaRatio < MinimumValidRatio)
		{ Stats.Reason = FString::Printf(TEXT("LOD%d 顶点/面积贴合率 %.4f/%.4f 未同时达到 .98"), LODIndex, VertexRatio, AreaRatio); return false; }
		++Stats.ValidatedLODs;
		return true;
	}

	static void AssociateOverlays(USkeletalMesh* Mesh, UMMDEndfieldFaceData* Data,
		const MMDEnfieldFaceSdf::FBakeSettings& Settings, const FMMDReferenceHeadFrame& Frame,
		TArray<FHost>& Hosts, const TArray<int32>& AllowedSlots, MMDEndfieldFaceBinding::FResult& Result)
	{
		if (!Data || Data->FrameVersion != MMDEndfieldHeadFrame::CurrentFrameVersion || Hosts.IsEmpty()) return;
		const FSkeletalMeshRenderData* Render = Mesh->GetResourceForRendering();
		if (!Render || !Render->LODRenderData.Num()) return;
		TArray<FCpuLod> Lods;
		for (int32 LODIndex = 0; LODIndex < Render->LODRenderData.Num(); ++LODIndex)
		{
			FCpuLod& Geometry = Lods.AddDefaulted_GetRef();
			FString Error;
			if (!ReadLod(Mesh, LODIndex, Geometry, Error))
			{
				Result.Notes.Add(FString::Printf(TEXT("透明关联保守跳过 LOD%d：%s；主脸已烘结果保留"), LODIndex, *Error));
				return;
			}
		}
		for (FHost& Host : Hosts)
		{
			if (!BuildHostProjection(Mesh, 0, Lods[0], Settings, Host)) Host.Triangles.Reset();
			else ValidateAtlasDepth(Host);
			Host.BaseTexture = FindBaseTexture(Mesh->GetMaterials()[Host.Slot].MaterialInterface);
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		TBitArray<> HeadBones(false, Skeleton.GetNum());
		for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
		{
			for (int32 Parent = Bone; Parent != INDEX_NONE; Parent = Skeleton.GetParentIndex(Parent))
				if (Parent == Frame.HeadIndex) { HeadBones[Bone] = true; break; }
		}
		TStrongObjectPtr<USkeletalMeshComponent> Classifier(NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Classifier->SetSkeletalMesh(Mesh);
		for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot)
		{
			if (!AllowedSlots.IsEmpty() && !AllowedSlots.Contains(Slot)) continue;
			UMaterialInterface* Material = Mesh->GetMaterials()[Slot].MaterialInterface;
			// 仅普通连续透明覆盖；加法/预乘等其它混合语义不在本批自动转换范围。
			if (!Material || Material->GetBlendMode() != BLEND_Translucent) continue;
			const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Classifier.Get(), Slot);
			const FString SourceReason = UMMDStageLibrary::GetResolvedCharacterMaterialRoleReason(Classifier.Get(), Slot);
			const bool GenericCandidate = Role == EMMDMaterialRole::Generic && SourceReason == TEXT("generic:unrecognized");
			if (Role != EMMDMaterialRole::Skin && !GenericCandidate) continue;
			UTexture* BaseTexture = FindBaseTexture(Material);
			FOverlayStats Best;
			Best.Slot = Slot;
			Best.SourceRoleReason = SourceReason;
			Best.Reason = TEXT("没有满足角色/底图限制的有效主脸");
			int32 AcceptedHosts = 0;
			for (const FHost& Host : Hosts)
			{
				if (Host.Triangles.IsEmpty() || (GenericCandidate && (!BaseTexture || BaseTexture != Host.BaseTexture))) continue;
				FOverlayStats Attempt;
				Attempt.Slot = Slot;
				Attempt.SourceRoleReason = SourceReason;
				Attempt.HostMaterialIndex = Host.Slot;
				Attempt.HostSlotName = Mesh->GetMaterials()[Host.Slot].MaterialSlotName;
				Attempt.ValidVertexRatio = Attempt.ValidAreaRatio = Attempt.MinimumHeadWeight = 1.0f;
				bool Valid = true;
				for (int32 LODIndex = 0; LODIndex < Lods.Num(); ++LODIndex)
				{
					FHost CurrentLODHost;
					const FHost* GeometryHost = &Host;
					if (LODIndex > 0)
					{
						CurrentLODHost.Slot = Host.Slot;
						if (!BuildHostProjection(Mesh, LODIndex, Lods[LODIndex], Settings, CurrentLODHost)) CurrentLODHost.Triangles.Reset();
						GeometryHost = &CurrentLODHost;
					}
					bool Present = false;
					if (!ValidateLod(Mesh, LODIndex, Slot, Lods[LODIndex], Settings, Host, *GeometryHost, HeadBones, Attempt, Present))
					{ Valid = false; break; }
				}
				Attempt.bAssociated = Valid && Attempt.ValidatedLODs > 0;
				if (Attempt.bAssociated)
				{
					++AcceptedHosts;
					Attempt.Reason = TEXT("所有可见参考 LOD 同时通过头权重、几何朝向、覆盖和 .03R 深度检查");
					Best = Attempt;
				}
				else if (!AcceptedHosts && (Best.HostMaterialIndex == INDEX_NONE
					|| FMath::Min(Attempt.ValidVertexRatio, Attempt.ValidAreaRatio) > FMath::Min(Best.ValidVertexRatio, Best.ValidAreaRatio))) Best = Attempt;
			}
			if (AcceptedHosts > 1)
			{
				Best.bAssociated = false;
				Best.Reason = TEXT("多个主脸同时满足关联条件，无法唯一判定，保持原材质路径");
			}
			if (Best.bAssociated)
			{
				// 新 Data 尚未挂网格；不可调用 FindDirectionSlot 读取网格上的旧缓存。
				const FMMDGeneratedFaceSdfSlot* HostBinding = Data->Slots.FindByPredicate([&Best](const FMMDGeneratedFaceSdfSlot& Host)
				{ return Host.MaterialIndex == Best.HostMaterialIndex && !Host.bForwardOverlay; });
				if (HostBinding && IsValid(HostBinding->DirectionTexture))
				{
					const TObjectPtr<UTexture2D> HostTexture = HostBinding->DirectionTexture;
					FMMDGeneratedFaceSdfSlot& Binding = Data->Slots.AddDefaulted_GetRef();
					Binding.MaterialIndex = Slot;
					Binding.MaterialSlotName = Mesh->GetMaterials()[Slot].MaterialSlotName;
					Binding.DirectionTexture = HostTexture;
					Binding.DirectionVersion = UMMDEndfieldFaceData::CurrentVersion;
					Binding.UVChannel = -1;
					Binding.HostMaterialIndex = Best.HostMaterialIndex;
					Binding.HostSlotName = Best.HostSlotName;
					Binding.bForwardOverlay = true;
					Binding.OverlayValidatedLODs = Best.ValidatedLODs;
					Binding.OverlayTestedVertices = Best.TestedVertices;
					Binding.OverlayValidVertexRatio = Best.ValidVertexRatio;
					Binding.OverlayValidAreaRatio = Best.ValidAreaRatio;
					Binding.OverlayMinimumHeadWeight = Best.MinimumHeadWeight;
					Binding.OverlayMaxMatchedDepthError = Best.MaxMatchedDepthError;
					Result.BoundSlots.AddUnique(Slot);
					Result.ForwardOverlaySlots.Add(Slot);
				}
				else { Best.bAssociated = false; Best.Reason = TEXT("新生成主槽缺少有效图，未关联透明层"); }
			}
			Result.OverlayStats.Add(Best);
			Result.Notes.Add(FString::Printf(TEXT("透明槽%d → 主槽%d：%s；保留原连续 alpha，参考顶点/面积贴合率 %.4f/%.4f"),
				Slot, Best.HostMaterialIndex, *Best.Reason, Best.ValidVertexRatio, Best.ValidAreaRatio));
		}
		if (!Result.ForwardOverlaySlots.IsEmpty())
			Result.Notes.Add(TEXT("透明附脸关联仅验证参考姿态和现有 LOD；不保证独立 Morph、布料或其它非刚性变形保持贴合，不改变源 RGB/Opacity/UV"));
	}
}

bool MMDEndfieldFaceBinding::BakeAndBind(USkeletalMesh* Mesh, const FString& TextureFolder,
	bool bSaveTextures, FResult& Result, const TArray<int32>& Slots, int32 Size)
{
	Result = FResult();
	if (!IsValid(Mesh))
	{
		Result.Errors.Add(TEXT("自动脸部烘焙缺少骨骼网格"));
		return false;
	}
	TArray<USkinnedAsset*> Pending = { Mesh };
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Pending);
	MMDEnfieldFaceSdf::FBakeSettings Settings;
	Settings.TextureSize = Size;
	Result.DataVersion = UMMDEndfieldFaceData::CurrentVersion;
	Result.UVChannel = Settings.UVChannel;
	MMDEnfieldFaceSdf::EHeadFrameSource Source;
	FString FrameMessage;
	if (!MMDEnfieldFaceSdf::DeriveHeadFrame(Mesh, NAME_None, Settings, Source, FrameMessage)
		|| Source != MMDEnfieldFaceSdf::EHeadFrameSource::HeadBone)
	{
		Result.Notes.Add(TEXT("没有可靠头骨，未绑定脸部方向场，保留通用 Toon：") + FrameMessage);
		Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
		if (bSaveTextures) Mesh->MarkPackageDirty();
		return true;
	}
	if (!FrameMessage.IsEmpty()) Result.Notes.Add(FrameMessage);
	Result.HeadCenter = Settings.HeadCenter;
	Result.HeadRadius = Settings.HeadRadius;
	Result.HeadForward = Settings.HeadForward;
	Result.HeadRight = Settings.HeadRight;
	Result.HeadUp = Settings.HeadUp;
	TArray<int32> Candidates = Slots;
	if (Candidates.IsEmpty())
	{
		TStrongObjectPtr<USkeletalMeshComponent> Probe(NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Probe->SetSkeletalMesh(Mesh);
		for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot)
			if (UMMDStageLibrary::GetResolvedCharacterMaterialRole(Probe.Get(), Slot) == EMMDMaterialRole::Skin)
				Candidates.Add(Slot);
	}
	TStrongObjectPtr<UMMDEndfieldFaceData> Data(NewObject<UMMDEndfieldFaceData>(Mesh, NAME_None,
		bSaveTextures ? RF_NoFlags : RF_Transient));
	Data->Version = UMMDEndfieldFaceData::CurrentVersion;
	FMMDReferenceHeadFrame Frame;
	FString FrameError;
	if (MMDEndfieldHeadFrame::BuildSkeletonFrame(Mesh, NAME_None, Frame, FrameError))
	{
		TArray<FTransform> ReferenceTransforms = Mesh->GetRefSkeleton().GetRefBonePose();
		for (int32 Bone = 0; Bone < ReferenceTransforms.Num(); ++Bone)
		{
			const int32 Parent = Mesh->GetRefSkeleton().GetParentIndex(Bone);
			if (Parent != INDEX_NONE) ReferenceTransforms[Bone] *= ReferenceTransforms[Parent];
		}
		Data->FrameVersion = MMDEndfieldHeadFrame::CurrentFrameVersion;
		Data->HeadBoneName = Frame.BoneName;
		Data->HeadCenterHeadLocal = ReferenceTransforms[Frame.HeadIndex].InverseTransformPosition(Settings.HeadCenter);
		Data->HeadRadius = Settings.HeadRadius;
		Data->HeadReferenceRotation = Frame.ReferenceHeadRotation;
		Data->HeadSkeletonSignature = MMDEndfieldHeadFrame::ComputeSkeletonSignature(Mesh, Frame.BoneName);
		Result.FrameVersion = Data->FrameVersion;
		Result.HeadBoneName = Data->HeadBoneName;
		Result.HeadCenterHeadLocal = Data->HeadCenterHeadLocal;
		Result.BoneRadius = Frame.Radius;
		Result.HeadReferenceRotation = Data->HeadReferenceRotation;
		Result.HeadSkeletonSignature = Data->HeadSkeletonSignature;
		Result.bHeadFrameValid = !Data->HeadSkeletonSignature.IsEmpty()
			&& !Data->HeadCenterHeadLocal.ContainsNaN() && FMath::IsFinite(Data->HeadRadius) && Data->HeadRadius > UE_SMALL_NUMBER;
	}
	Result.Notes.Add(TEXT("V5 是自动头部投影角阈值图，不是实际遮挡 SDF；保留全部网格 UV，原生头发投影独立处理"));
	TArray<MMDEndfieldOverlay5::FHost> OverlayHosts;
	for (const int32 Slot : Candidates)
	{
		if (!Mesh->GetMaterials().IsValidIndex(Slot))
		{
			Result.Errors.Add(FString::Printf(TEXT("脸部材质槽越界：%d"), Slot));
			continue;
		}
		Settings.MaterialIndices = { Slot };
		MMDEnfieldFaceSdf::FBakeResult Bake;
		const bool bBaked = MMDEnfieldFaceSdf::BakeFaceSdf(Mesh, Settings, Bake);
		FSlotStats& Stats = Result.SlotStats.AddDefaulted_GetRef();
		Stats.Slot = Slot;
		Stats.FaceTexels = Bake.FaceTexelCount;
		Stats.MirroredTexels = Bake.MirroredTexelCount;
		Stats.ConflictingTexels = Bake.ConflictingTexelCount;
		Stats.Triangles = Bake.ConsideredTriangleCount;
		Stats.FrontDarkTexels = Bake.FrontDarkTexelCount;
		Stats.NonContinuousTexels = Bake.NonContinuousTexelCount;
		Stats.ProjectedOccludedTexels = Bake.ProjectedOccludedTexelCount;
		Stats.ClippedTriangles = Bake.ProjectedClippedTriangleCount;
		Stats.DepthClampedTexels = Bake.DepthClampedTexelCount;
		Stats.bSkippedTranslucent = Bake.SkippedTranslucentMaterialIndices.Contains(Slot);
		if (!bBaked)
		{
			Result.Notes.Add(FString::Printf(TEXT("槽 %d 无可靠脸部覆盖，保持回退：%s"), Slot, *Bake.Error));
			continue;
		}
		FString Name = FString::Printf(TEXT("T_%s_FaceDirection_%d_%d"), *Mesh->GetName(), UMMDEndfieldFaceData::CurrentVersion, Slot);
		FString Package = TextureFolder / Name;
		if (bSaveTextures && FPackageName::DoesPackageExist(Package))
		{
			UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name));
			const FString Version = Existing ? Existing->GetOutermost()->GetMetaData().GetValue(Existing, TEXT("MMDGeneratedFaceDirectionVersion")) : FString();
			if (!Existing || Version != FString::FromInt(UMMDEndfieldFaceData::CurrentVersion))
			{
				// 同名用户资产不属于派生缓存，保留原件并选择新的资产路径。
				const FString OriginalPackage = Package;
				FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(OriginalPackage, TEXT(""), Package, Name);
				Result.Notes.Add(TEXT("保留同名现有资产，改用派生纹理路径：") + Package);
			}
		}
		FString Error;
		UTexture2D* Texture = nullptr;
		if (bSaveTextures)
		{
			if (!FPackageName::IsValidLongPackageName(TextureFolder))
				Error = TEXT("派生纹理目录不是有效资产路径：") + TextureFolder;
			else if (MMDEnfieldFaceSdf::SaveAsTexture(Bake, Package, Name, Error))
				Texture = LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name));
		}
		else
			Texture = MMDEnfieldFaceSdf::CreateTexture(Bake, Mesh, FName(Name), RF_Transient, Error);
		if (!Texture)
		{
			Result.Errors.Add(Error.IsEmpty() ? TEXT("烘焙纹理创建失败：") + Name : Error);
			continue;
		}
		FMMDGeneratedFaceSdfSlot& Binding = Data->Slots.AddDefaulted_GetRef();
		Binding.MaterialIndex = Slot;
		Binding.MaterialSlotName = Mesh->GetMaterials()[Slot].MaterialSlotName;
		Binding.DirectionTexture = Texture;
		Binding.DirectionVersion = UMMDEndfieldFaceData::CurrentVersion;
		Binding.UVChannel = Settings.UVChannel;
		Result.BoundSlots.Add(Slot);
		Result.TexturePaths.Add(Texture->GetPathName());
		Result.FaceTexelCount += Bake.FaceTexelCount;
		MMDEndfieldOverlay5::FHost& Host = OverlayHosts.AddDefaulted_GetRef();
		Host.Slot = Slot;
		Host.Bake = MoveTemp(Bake);
	}
	if (Result.Errors.IsEmpty())
	{
		MMDEndfieldOverlay5::AssociateOverlays(Mesh, Data.Get(), Settings, Frame, OverlayHosts, Slots, Result);
		// 重烘无覆盖也清掉旧缓存，避免重导后的新 UV 继续消费旧纹理。
		Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
		if (!Data->Slots.IsEmpty()) Mesh->AddAssetUserData(Data.Get());
		if (bSaveTextures) Mesh->MarkPackageDirty();
	}
	return Result.Errors.IsEmpty();
}
