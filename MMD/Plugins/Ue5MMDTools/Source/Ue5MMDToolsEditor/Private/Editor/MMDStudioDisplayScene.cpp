#include "Editor/MMDStudioDisplayScene.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "PreviewScene.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace
{
	const FLinearColor DisplaySkyBlue(0.50f, 0.68f, 0.94f);
	const FLinearColor DisplaySkyIvory(0.91f, 0.86f, 0.96f);
	constexpr int32 LightStripStart = 8;
	constexpr int32 RingStart = 10;
	constexpr int32 RingSegments = 24;
	constexpr int32 ComponentCount = RingStart + 2 * RingSegments;

	bool IsMinimalStudio(EMMDStudioDisplayStyle Style)
	{
		return Style == EMMDStudioDisplayStyle::RoseStudio || Style == EMMDStudioDisplayStyle::SlateGallery
			|| Style == EMMDStudioDisplayStyle::PearlAtrium || Style == EMMDStudioDisplayStyle::LilacStudio;
	}

	struct FDisplayPalette
	{
		FLinearColor Sky, Horizon, Floor, Edge, Top, Accent, Secondary;
		float Roughness = 1.0f;
		float Specular = 0.0f;
		float Glow = 0.0f;
	};

	FDisplayPalette GetPalette(EMMDStudioDisplayStyle Style)
	{
		switch (Style)
		{
		case EMMDStudioDisplayStyle::RoseStudio:
			return {FLinearColor(0.65f, 0.30f, 0.45f), FLinearColor(0.96f, 0.68f, 0.77f),
				FLinearColor(0.64f, 0.42f, 0.52f), FLinearColor(0.62f, 0.37f, 0.48f), FLinearColor(0.86f, 0.68f, 0.75f),
				FLinearColor(0.76f, 0.44f, 0.59f), FLinearColor(0.91f, 0.67f, 0.77f), 0.92f, 0.04f, 0.0f};
		case EMMDStudioDisplayStyle::SlateGallery:
			return {FLinearColor(0.035f, 0.055f, 0.09f), FLinearColor(0.14f, 0.17f, 0.22f),
				FLinearColor(0.10f, 0.12f, 0.15f), FLinearColor(0.12f, 0.14f, 0.18f), FLinearColor(0.18f, 0.20f, 0.24f),
				FLinearColor(0.42f, 0.53f, 0.66f), FLinearColor(0.64f, 0.68f, 0.74f), 0.95f, 0.03f, 0.70f};
		case EMMDStudioDisplayStyle::PearlAtrium:
			return {FLinearColor(0.80f, 0.84f, 0.86f), FLinearColor(0.97f, 0.96f, 0.92f),
				FLinearColor(0.73f, 0.75f, 0.76f), FLinearColor(0.66f, 0.68f, 0.69f), FLinearColor(0.87f, 0.87f, 0.84f),
				FLinearColor(0.75f, 0.78f, 0.79f), FLinearColor(0.91f, 0.90f, 0.84f), 0.94f, 0.04f, 0.0f};
		case EMMDStudioDisplayStyle::LilacStudio:
			return {FLinearColor(0.26f, 0.22f, 0.46f), FLinearColor(0.69f, 0.62f, 0.83f),
				FLinearColor(0.40f, 0.34f, 0.56f), FLinearColor(0.36f, 0.29f, 0.49f), FLinearColor(0.67f, 0.58f, 0.80f),
				FLinearColor(0.53f, 0.43f, 0.68f), FLinearColor(0.73f, 0.64f, 0.84f), 0.94f, 0.04f, 0.0f};
		case EMMDStudioDisplayStyle::SkyGlass:
			return {FLinearColor(0.32f, 0.57f, 0.81f), FLinearColor(0.76f, 0.84f, 0.89f),
				FLinearColor(0.47f, 0.54f, 0.59f), FLinearColor(0.38f, 0.49f, 0.56f), FLinearColor(0.68f, 0.74f, 0.77f),
				FLinearColor(0.35f, 0.61f, 0.72f), FLinearColor(0.70f, 0.78f, 0.81f), 0.91f, 0.05f, 0.34f};
		case EMMDStudioDisplayStyle::VioletStage:
			return {FLinearColor(0.055f, 0.055f, 0.14f), FLinearColor(0.30f, 0.24f, 0.39f),
				FLinearColor(0.10f, 0.105f, 0.15f), FLinearColor(0.18f, 0.16f, 0.24f), FLinearColor(0.20f, 0.21f, 0.28f),
				FLinearColor(0.35f, 0.47f, 0.69f), FLinearColor(0.66f, 0.37f, 0.51f), 0.94f, 0.05f, 0.78f};
		case EMMDStudioDisplayStyle::WarmAtelier:
			return {FLinearColor(0.54f, 0.42f, 0.38f), FLinearColor(0.83f, 0.72f, 0.62f),
				FLinearColor(0.43f, 0.38f, 0.33f), FLinearColor(0.46f, 0.38f, 0.29f), FLinearColor(0.71f, 0.64f, 0.54f),
				FLinearColor(0.61f, 0.46f, 0.30f), FLinearColor(0.77f, 0.69f, 0.57f), 0.92f, 0.05f, 0.30f};
		default:
			return {DisplaySkyBlue, DisplaySkyIvory, FLinearColor(0.60f, 0.65f, 0.75f),
				FLinearColor(0.66f, 0.66f, 0.78f), FLinearColor(0.80f, 0.80f, 0.86f), FLinearColor::White, FLinearColor::White};
		}
	}

	struct FDisplayMeshVertex
	{
		FVector3f Position;
		FVector3f Normal;
	};

	struct FDisplayGeometry
	{
		TArray<FDisplayMeshVertex> Vertices;
		TArray<FIntVector> Triangles;

		int32 AddVertex(const FVector3f& Position, const FVector3f& Normal)
		{
			return Vertices.Add({Position, Normal.GetSafeNormal()});
		}

		void AddTriangle(int32 A, int32 B, int32 C)
		{
			// 与 UE MeshDescription 的前面约定一致：几何法线使用 Cross(C-A, B-A)。
			const FVector3f Cross = FVector3f::CrossProduct(Vertices[C].Position - Vertices[A].Position,
				Vertices[B].Position - Vertices[A].Position);
			if (Cross.SizeSquared() < UE_SMALL_NUMBER) return;
			const FVector3f Normal = Vertices[A].Normal + Vertices[B].Normal + Vertices[C].Normal;
			if (FVector3f::DotProduct(Cross, Normal) < 0.0f) Swap(B, C);
			Triangles.Emplace(A, B, C);
		}

		void AddQuad(int32 A, int32 B, int32 C, int32 D)
		{
			AddTriangle(A, B, C);
			AddTriangle(A, C, D);
		}

		UStaticMesh* Build() const
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
			Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("DisplaySurface");
			TArray<FVertexInstanceID> Instances;
			Instances.Reserve(Vertices.Num());
			for (const FDisplayMeshVertex& Point : Vertices)
			{
				if (Point.Position.ContainsNaN() || Point.Normal.ContainsNaN() || !Point.Normal.IsNormalized()) return nullptr;
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = Point.Position;
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = Point.Normal;
				const FVector3f Axis = FMath::Abs(Point.Normal.Z) < 0.95f ? FVector3f(0, 0, 1) : FVector3f(0, 1, 0);
				Tangents[Instance] = FVector3f::CrossProduct(Axis, Point.Normal).GetSafeNormal();
				Signs[Instance] = 1.0f;
				Colors[Instance] = FVector4f(1, 1, 1, 1);
				UVs.Set(Instance, 0, FVector2f(Point.Position.X / 100.0f + 0.5f, Point.Position.Z / 100.0f + 0.5f));
				Instances.Add(Instance);
			}
			for (const FIntVector& Triangle : Triangles)
			{
				const FVertexInstanceID Corners[] = {Instances[Triangle.X], Instances[Triangle.Y], Instances[Triangle.Z]};
				Description.CreateTriangle(Group, MakeArrayView(Corners));
			}
			UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
			Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), TEXT("DisplaySurface")));
			Mesh->SetNumSourceModels(1);
			FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
			Settings.bRecomputeNormals = false;
			Settings.bRecomputeTangents = false;
			Settings.bUseMikkTSpace = false;
			Settings.bGenerateLightmapUVs = false;
			UStaticMesh::FBuildMeshDescriptionsParams Params;
			Params.bFastBuild = true;
			Params.bBuildSimpleCollision = false;
			Params.bMarkPackageDirty = false;
			const TArray<const FMeshDescription*> Descriptions = {&Description};
			return Mesh->BuildFromMeshDescriptions(Descriptions, Params) ? Mesh : nullptr;
		}
	};

	UStaticMesh* GetSmoothDisplayPlatform()
	{
		static TStrongObjectPtr<UStaticMesh> Mesh;
		if (Mesh.IsValid()) return Mesh.Get();
		FDisplayGeometry Geometry;
		constexpr int32 Segments = 128;
		constexpr int32 BevelSegments = 6;
		// 顶底平面与圆润边缘共用连续法线，避免低分段圆柱的折线轮廓。
		for (int32 Half = 0; Half < 2; ++Half)
		{
			for (int32 Row = 0; Row <= BevelSegments; ++Row)
			{
				const float Angle = (Half + float(Row) / BevelSegments) * HALF_PI;
				const float Radius = 47.0f + 3.0f * FMath::Sin(Angle);
				const float Z = (Half == 0 ? 47.0f : -47.0f) + 3.0f * FMath::Cos(Angle);
				for (int32 Segment = 0; Segment < Segments; ++Segment)
				{
					const float Azimuth = 2.0f * PI * Segment / Segments;
					const FVector3f Radial(FMath::Cos(Azimuth), FMath::Sin(Azimuth), 0);
					Geometry.AddVertex(Radial * Radius + FVector3f(0, 0, Z),
						Radial * FMath::Sin(Angle) + FVector3f(0, 0, FMath::Cos(Angle)));
				}
			}
		}
		constexpr int32 Rows = 2 * (BevelSegments + 1);
		for (int32 Row = 0; Row < Rows - 1; ++Row)
			for (int32 Segment = 0; Segment < Segments; ++Segment)
			{
				const int32 Next = (Segment + 1) % Segments;
				Geometry.AddQuad(Row * Segments + Segment, Row * Segments + Next,
					(Row + 1) * Segments + Next, (Row + 1) * Segments + Segment);
			}
		const int32 Top = Geometry.AddVertex(FVector3f(0, 0, 50), FVector3f(0, 0, 1));
		const int32 Bottom = Geometry.AddVertex(FVector3f(0, 0, -50), FVector3f(0, 0, -1));
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const int32 Next = (Segment + 1) % Segments;
			Geometry.AddTriangle(Top, Segment, Next);
			Geometry.AddTriangle(Bottom, (Rows - 1) * Segments + Segment, (Rows - 1) * Segments + Next);
		}
		Mesh.Reset(Geometry.Build());
		return Mesh.Get();
	}

	UStaticMesh* GetRoundedDisplayPanel()
	{
		static TStrongObjectPtr<UStaticMesh> Mesh;
		if (Mesh.IsValid()) return Mesh.Get();
		FDisplayGeometry Geometry;
		TArray<FVector3f> Outline;
		TArray<FVector3f> Outward;
		constexpr int32 CornerSegments = 12;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float CenterX = Corner == 0 || Corner == 3 ? 38.0f : -38.0f;
			const float CenterZ = Corner < 2 ? 38.0f : -38.0f;
			for (int32 Segment = 0; Segment <= CornerSegments; ++Segment)
			{
				const float Angle = (Corner + float(Segment) / CornerSegments) * HALF_PI;
				const FVector3f Normal(FMath::Cos(Angle), 0, FMath::Sin(Angle));
				Outline.Add(FVector3f(CenterX, 0, CenterZ) + Normal * 12.0f);
				Outward.Add(Normal);
			}
		}
		for (int32 Face = 0; Face < 2; ++Face)
		{
			const float Sign = Face == 0 ? -1.0f : 1.0f;
			const FVector3f Normal(0, Sign, 0);
			const int32 Center = Geometry.AddVertex(FVector3f(0, Sign * 50.0f, 0), Normal);
			const int32 Start = Geometry.Vertices.Num();
			for (const FVector3f& Point : Outline) Geometry.AddVertex(Point + FVector3f(0, Sign * 50.0f, 0), Normal);
			for (int32 Index = 0; Index < Outline.Num(); ++Index)
				Geometry.AddTriangle(Center, Start + Index, Start + (Index + 1) % Outline.Num());
		}
		for (int32 Index = 0; Index < Outline.Num(); ++Index)
		{
			const int32 Next = (Index + 1) % Outline.Num();
			const int32 A = Geometry.AddVertex(Outline[Index] + FVector3f(0, -50, 0), Outward[Index]);
			const int32 B = Geometry.AddVertex(Outline[Next] + FVector3f(0, -50, 0), Outward[Next]);
			const int32 C = Geometry.AddVertex(Outline[Next] + FVector3f(0, 50, 0), Outward[Next]);
			const int32 D = Geometry.AddVertex(Outline[Index] + FVector3f(0, 50, 0), Outward[Index]);
			Geometry.AddQuad(A, B, C, D);
		}
		Mesh.Reset(Geometry.Build());
		return Mesh.Get();
	}

	UStaticMesh* GetFlutedDisplayWall()
	{
		static TStrongObjectPtr<UStaticMesh> Mesh;
		if (Mesh.IsValid()) return Mesh.Get();
		FDisplayGeometry Geometry;
		constexpr int32 Segments = 128;
		constexpr float Radius = 61.0f;
		const float HalfAngle = FMath::DegreesToRadians(55.0f);
		auto Position = [Radius](float Angle, float Z, bool bBack)
		{
			const float Groove = 0.8f * FMath::Square(0.5f + 0.5f * FMath::Cos(Angle * 28.0f));
			const float R = Radius + (bBack ? 4.0f : Groove);
			return FVector3f(FMath::Sin(Angle) * R, Radius - FMath::Cos(Angle) * R, Z);
		};
		for (int32 Segment = 0; Segment < Segments; ++Segment)
		{
			const float A = FMath::Lerp(-HalfAngle, HalfAngle, float(Segment) / Segments);
			const float B = FMath::Lerp(-HalfAngle, HalfAngle, float(Segment + 1) / Segments);
			for (int32 Face = 0; Face < 2; ++Face)
			{
				const bool bBack = Face != 0;
				auto Normal = [Position, bBack](float Angle)
				{
					const FVector3f Tangent = Position(Angle + 0.0001f, 0, bBack) - Position(Angle - 0.0001f, 0, bBack);
					return FVector3f(-Tangent.Y, Tangent.X, 0).GetSafeNormal() * (bBack ? -1.0f : 1.0f);
				};
				Geometry.AddQuad(Geometry.AddVertex(Position(A, -50, bBack), Normal(A)),
					Geometry.AddVertex(Position(B, -50, bBack), Normal(B)),
					Geometry.AddVertex(Position(B, 50, bBack), Normal(B)),
					Geometry.AddVertex(Position(A, 50, bBack), Normal(A)));
			}
			for (float Z : {-50.0f, 50.0f})
			{
				const FVector3f Normal(0, 0, Z > 0 ? 1.0f : -1.0f);
				Geometry.AddQuad(Geometry.AddVertex(Position(A, Z, false), Normal),
					Geometry.AddVertex(Position(B, Z, false), Normal),
					Geometry.AddVertex(Position(B, Z, true), Normal),
					Geometry.AddVertex(Position(A, Z, true), Normal));
			}
		}
		for (float Angle : {-HalfAngle, HalfAngle})
		{
			const FVector3f Normal = FVector3f(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Angle < 0 ? -1.0f : 1.0f);
			Geometry.AddQuad(Geometry.AddVertex(Position(Angle, -50, false), Normal),
				Geometry.AddVertex(Position(Angle, -50, true), Normal),
				Geometry.AddVertex(Position(Angle, 50, true), Normal),
				Geometry.AddVertex(Position(Angle, 50, false), Normal));
		}
		Mesh.Reset(Geometry.Build());
		return Mesh.Get();
	}

	UTexture2D* GetDisplaySkyTexture(EMMDStudioDisplayStyle Style)
	{
		const TCHAR* FileName = nullptr;
		switch (Style)
		{
		case EMMDStudioDisplayStyle::SkyGlass: FileName = TEXT("sky_day.png"); break;
		case EMMDStudioDisplayStyle::VioletStage: FileName = TEXT("sky_twilight.png"); break;
		case EMMDStudioDisplayStyle::WarmAtelier: FileName = TEXT("sky_warm.png"); break;
		default: return nullptr;
		}
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		if (!Plugin.IsValid()) return nullptr;
		const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/DisplayScenes"), FileName);
		// 只缓存成功解码的原图；资源尚未到位时继续使用渐变，下次切换可以重试。
		static TMap<FString, TStrongObjectPtr<UTexture2D>> Textures;
		if (const TStrongObjectPtr<UTexture2D>* Texture = Textures.Find(Path)) return Texture->Get();
		if (!FPaths::FileExists(Path)) return nullptr;
		UTexture2D* Texture = FImageUtils::ImportFileAsTexture2D(Path);
		if (!Texture) return nullptr;
		Texture->SetFlags(RF_Transient);
		Texture->SRGB = true;
		Texture->AddressX = TA_Wrap;
		Texture->AddressY = TA_Clamp;
		Texture->Filter = TF_Bilinear;
		Texture->LODGroup = TEXTUREGROUP_Skybox;
		Texture->NeverStream = true;
		Texture->UpdateResource();
		Textures.Emplace(Path, TStrongObjectPtr<UTexture2D>(Texture));
		return Texture;
	}

	template<typename T>
	T* AddDisplayExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	UMaterialExpressionVectorParameter* AddColor(UMaterial* Material, const TCHAR* Name, const FLinearColor& Color)
	{
		auto* Expression = AddDisplayExpression<UMaterialExpressionVectorParameter>(Material);
		Expression->ParameterName = Name;
		Expression->DefaultValue = Color;
		return Expression;
	}

	UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, const TCHAR* Name, float Value)
	{
		auto* Expression = AddDisplayExpression<UMaterialExpressionScalarParameter>(Material);
		Expression->ParameterName = Name;
		Expression->DefaultValue = Value;
		return Expression;
	}

	UMaterialExpression* AddDisplayPanorama(UMaterial* Material, UMaterialExpression* Fallback)
	{
		// 天空球与地面共用世界视线映射，远地面的像素才能与背景连续衔接。
		auto* Direction = AddDisplayExpression<UMaterialExpressionSubtract>(Material);
		Direction->A.Connect(0, AddDisplayExpression<UMaterialExpressionWorldPosition>(Material));
		Direction->B.Connect(0, AddDisplayExpression<UMaterialExpressionCameraPositionWS>(Material));
		auto* UV = AddDisplayExpression<UMaterialExpressionCustom>(Material);
		UV->OutputType = CMOT_Float4;
		UV->Description = TEXT("Display panorama world-ray UV and seam");
		UV->Code = TEXT(R"(
struct TMMDSkyPanoramaUV
{
    float2 GetUV(float3 view_direction, float yaw_degrees, float vertical_offset)
    {
        float3 direction = normalize(view_direction);
        float2 uv = float2(atan2(direction.y, direction.x) * 0.1591549431 + 0.5,
            acos(clamp(direction.z, -1.0, 1.0)) * 0.3183098862);
        return (uv - 0.5) * float2(3.0, 2.0) + 0.5 + float2(yaw_degrees / 360.0, vertical_offset);
    }
    float4 GetSampleData(float2 uv)
    {
        float wrapped_u = frac(uv.x);
        float edge_distance = min(wrapped_u, 1.0 - wrapped_u);
        float seam_blend = 0.5 * (1.0 - smoothstep(0.0, 0.04, edge_distance));
        return float4(uv, 1.0 - wrapped_u, seam_blend);
    }
};
TMMDSkyPanoramaUV sky_uv;
return sky_uv.GetSampleData(sky_uv.GetUV(view_direction, yaw_degrees, vertical_offset));
)");
		UV->Inputs.Reset();
		auto AddInput = [UV](const TCHAR* Name, UMaterialExpression* Expression)
		{
			FCustomInput& Input = UV->Inputs.AddDefaulted_GetRef();
			Input.InputName = Name;
			Input.Input.Connect(0, Expression);
		};
		AddInput(TEXT("view_direction"), Direction);
		AddInput(TEXT("yaw_degrees"), AddScalar(Material, TEXT("SkyYawDegrees"), 0.0f));
		AddInput(TEXT("vertical_offset"), AddScalar(Material, TEXT("SkyVerticalOffset"), 0.0f));
		auto Mask = [Material, UV](bool R, bool G, bool B, bool A)
		{
			auto* Result = AddDisplayExpression<UMaterialExpressionComponentMask>(Material);
			Result->Input.Connect(0, UV);
			Result->R = R; Result->G = G; Result->B = B; Result->A = A;
			return Result;
		};
		auto Sample = [Material](UMaterialExpression* Coordinates)
		{
			auto* Result = AddDisplayExpression<UMaterialExpressionTextureSampleParameter2D>(Material);
			Result->ParameterName = TEXT("SkyPanorama");
			Result->Texture = GEngine->DefaultTexture;
			Result->SamplerType = SAMPLERTYPE_Color;
			Result->Coordinates.Connect(0, Coordinates);
			return Result;
		};
		auto* OppositeUV = AddDisplayExpression<UMaterialExpressionAppendVector>(Material);
		OppositeUV->A.Connect(0, Mask(false, false, true, false));
		OppositeUV->B.Connect(0, Mask(false, true, false, false));
		// 原图左右边缘只在各自 4% 窄带内互相混合，接缝中心取均值，其余采样保持不变。
		auto* Seamless = AddDisplayExpression<UMaterialExpressionLinearInterpolate>(Material);
		Seamless->A.Connect(0, Sample(Mask(true, true, false, false)));
		Seamless->B.Connect(0, Sample(OppositeUV));
		Seamless->Alpha.Connect(0, Mask(false, false, false, true));
		auto* Result = AddDisplayExpression<UMaterialExpressionLinearInterpolate>(Material);
		Result->A.Connect(0, Fallback);
		Result->B.Connect(0, Seamless);
		Result->Alpha.Connect(0, AddScalar(Material, TEXT("UseSkyTexture"), 0.0f));
		return Result;
	}

	UMaterial* GetDisplayBackdropMaterial()
	{
		static TStrongObjectPtr<UMaterial> Material;
		if (Material.IsValid()) return Material.Get();
		Material.Reset(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		Material->bIsSky = true;
		auto* UV = AddDisplayExpression<UMaterialExpressionTextureCoordinate>(Material.Get());
		auto* Vertical = AddDisplayExpression<UMaterialExpressionComponentMask>(Material.Get());
		Vertical->Input.Connect(0, UV);
		Vertical->R = Vertical->B = Vertical->A = false;
		Vertical->G = true;
		auto* Blue = AddColor(Material.Get(), TEXT("SkyColor"), DisplaySkyBlue);
		auto* Ivory = AddColor(Material.Get(), TEXT("HorizonColor"), DisplaySkyIvory);
		auto* GradientRange = AddDisplayExpression<UMaterialExpressionSmoothStep>(Material.Get());
		GradientRange->ConstMin = 0.30f;
		GradientRange->ConstMax = 0.70f;
		GradientRange->Value.Connect(0, Vertical);
		auto* Gradient = AddDisplayExpression<UMaterialExpressionLinearInterpolate>(Material.Get());
		Gradient->A.Connect(0, Blue);
		Gradient->B.Connect(0, Ivory);
		Gradient->Alpha.Connect(0, GradientRange);
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, AddDisplayPanorama(Material.Get(), Gradient));
		Material->PostEditChange();
		Material->EnsureIsComplete();
		return Material.Get();
	}

	UMaterial* GetDisplayFloorMaterial()
	{
		static TStrongObjectPtr<UMaterial> Material;
		if (Material.IsValid()) return Material.Get();
		Material.Reset(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_DefaultLit);
		auto* Position = AddDisplayExpression<UMaterialExpressionWorldPosition>(Material.Get());
		auto* Camera = AddDisplayExpression<UMaterialExpressionCameraPositionWS>(Material.Get());
		auto* PositionXY = AddDisplayExpression<UMaterialExpressionComponentMask>(Material.Get());
		PositionXY->Input.Connect(0, Position);
		PositionXY->R = PositionXY->G = true;
		PositionXY->B = PositionXY->A = false;
		auto* CameraXY = AddDisplayExpression<UMaterialExpressionComponentMask>(Material.Get());
		CameraXY->Input.Connect(0, Camera);
		CameraXY->R = CameraXY->G = true;
		CameraXY->B = CameraXY->A = false;
		auto* ModelXY = AddDisplayExpression<UMaterialExpressionComponentMask>(Material.Get());
		ModelXY->Input.Connect(0, AddColor(Material.Get(), TEXT("FadeCenter"), FLinearColor::Black));
		ModelXY->R = ModelXY->G = true;
		ModelXY->B = ModelXY->A = false;
		auto* FadeCenter = AddDisplayExpression<UMaterialExpressionLinearInterpolate>(Material.Get());
		FadeCenter->A.Connect(0, CameraXY);
		FadeCenter->B.Connect(0, ModelXY);
		FadeCenter->Alpha.Connect(0, AddScalar(Material.Get(), TEXT("UseModelFadeCenter"), 0.0f));
		auto* Distance = AddDisplayExpression<UMaterialExpressionDistance>(Material.Get());
		Distance->A.Connect(0, PositionXY);
		Distance->B.Connect(0, FadeCenter);
		auto* Fade = AddDisplayExpression<UMaterialExpressionSmoothStep>(Material.Get());
		Fade->ConstMin = 600.0f;
		Fade->ConstMax = 1500.0f;
		Fade->Min.Connect(0, AddScalar(Material.Get(), TEXT("FadeStart"), 600.0f));
		Fade->Max.Connect(0, AddScalar(Material.Get(), TEXT("FadeEnd"), 1500.0f));
		Fade->Value.Connect(0, Distance);
		auto* Near = AddDisplayExpression<UMaterialExpressionOneMinus>(Material.Get());
		Near->Input.Connect(0, Fade);
		auto* FloorColor = AddColor(Material.Get(), TEXT("BaseColor"), FLinearColor(0.60f, 0.65f, 0.75f));
		auto* HorizonColor = AddColor(Material.Get(), TEXT("HorizonColor"), (DisplaySkyBlue + DisplaySkyIvory) * 0.5f);
		auto* LitColor = AddDisplayExpression<UMaterialExpressionMultiply>(Material.Get());
		LitColor->A.Connect(0, FloorColor);
		LitColor->B.Connect(0, Near);
		auto* DistantColor = AddDisplayExpression<UMaterialExpressionMultiply>(Material.Get());
		DistantColor->A.Connect(0, AddDisplayPanorama(Material.Get(), HorizonColor));
		DistantColor->B.Connect(0, Fade);
		auto* Zero = AddDisplayExpression<UMaterialExpressionConstant>(Material.Get());
		Zero->R = 0.0f;
		auto* Roughness = AddScalar(Material.Get(), TEXT("Roughness"), 1.0f);
		auto* NearSpecular = AddDisplayExpression<UMaterialExpressionMultiply>(Material.Get());
		NearSpecular->A.Connect(0, AddScalar(Material.Get(), TEXT("Specular"), 0.0f));
		NearSpecular->B.Connect(0, Near);
		auto* FloorRoughness = AddDisplayExpression<UMaterialExpressionLinearInterpolate>(Material.Get());
		FloorRoughness->A.Connect(0, Roughness);
		FloorRoughness->ConstB = 1.0f;
		FloorRoughness->Alpha.Connect(0, Fade);
		auto* Data = Material->GetEditorOnlyData();
		Data->BaseColor.Connect(0, LitColor);
		Data->EmissiveColor.Connect(0, DistantColor);
		Data->Metallic.Connect(0, Zero);
		Data->Specular.Connect(0, NearSpecular);
		Data->Roughness.Connect(0, FloorRoughness);
		Material->PostEditChange();
		Material->EnsureIsComplete();
		return Material.Get();
	}

	UMaterial* GetDisplayAccentMaterial()
	{
		static TStrongObjectPtr<UMaterial> Material;
		if (Material.IsValid()) return Material.Get();
		Material.Reset(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient));
		Material->SetShadingModel(MSM_Unlit);
		auto* Glow = AddDisplayExpression<UMaterialExpressionMultiply>(Material.Get());
		Glow->A.Connect(0, AddColor(Material.Get(), TEXT("BaseColor"), FLinearColor::White));
		Glow->B.Connect(0, AddScalar(Material.Get(), TEXT("Glow"), 1.0f));
		Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Glow);
		Material->PostEditChange();
		Material->EnsureIsComplete();
		return Material.Get();
	}
}

FMMDStudioDisplayScene::FMMDStudioDisplayScene(FPreviewScene& InScene, const FBox& ModelBounds, EMMDStudioDisplayStyle InStyle)
	: Scene(&InScene), CurrentBounds(ModelBounds), Style(InStyle)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Surface = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Ue5MMDTools/LookDev/Materials/M_LookDevReference.M_LookDevReference"));
	if (!Cube || !Cylinder || !Sphere || !Surface) return;
	PrimitiveCylinder.Reset(Cylinder);
	Surface->EnsureIsComplete();
	auto AddSurface = [this](UStaticMesh* Mesh, UMaterialInterface* Parent, bool bBackdrop = false)
	{
		TStrongObjectPtr<UStaticMeshComponent> Component(NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetAffectDynamicIndirectLighting(false);
		Component->SetAffectDistanceFieldLighting(false);
		if (bBackdrop)
		{
			Component->bVisibleInReflectionCaptures = false;
			Component->bVisibleInRealTimeSkyCaptures = false;
		}
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Component.Get());
		Component->SetMaterial(0, Material);
		Scene->AddComponent(Component.Get(), FTransform::Identity);
		Components.Add(MoveTemp(Component));
		Materials.Emplace(Material);
		StyleVisibility.Add(false);
	};
	AddSurface(Sphere, GetDisplayBackdropMaterial(), true);
	AddSurface(Cube, GetDisplayFloorMaterial());
	AddSurface(Cylinder, Surface);
	AddSurface(Cylinder, Surface);
	for (int32 Index = 0; Index < 4; ++Index) AddSurface(Cylinder, Surface);
	for (int32 Index = 0; Index < 2; ++Index) AddSurface(Cube, GetDisplayAccentMaterial());
	for (int32 Index = 0; Index < 2 * RingSegments; ++Index) AddSurface(Cylinder, GetDisplayAccentMaterial());
	BackdropMaterials.Emplace(Materials[0].Get());
	for (int32 Index = 1; Index < GetStyles().Num(); ++Index)
		BackdropMaterials.Emplace(UMaterialInstanceDynamic::Create(GetDisplayBackdropMaterial(), Components[0].Get()));
	bInitialized = Components.Num() == ComponentCount;
	SetStyle(Style);
}

FMMDStudioDisplayScene::~FMMDStudioDisplayScene()
{
	if (Scene)
		for (const auto& Component : Components) if (Component.IsValid()) Scene->RemoveComponent(Component.Get());
}

void FMMDStudioDisplayScene::SetStyle(EMMDStudioDisplayStyle InStyle)
{
	if (!IsValid()) return;
	Style = InStyle;
	const int32 BackdropIndex = FMath::Clamp(static_cast<int32>(Style), 0, BackdropMaterials.Num() - 1);
	Materials[0].Reset(BackdropMaterials[BackdropIndex].Get());
	Components[0]->SetMaterial(0, Materials[0].Get());
	for (int32 Index = 2; Index < LightStripStart; ++Index)
	{
		Components[Index]->SetStaticMesh(PrimitiveCylinder.Get());
		Components[Index]->SetMaterial(0, Materials[Index].Get());
	}
	if (IsMinimalStudio(Style))
	{
		if (UStaticMesh* Platform = GetSmoothDisplayPlatform())
		{
			Components[3]->SetStaticMesh(Platform);
			Components[6]->SetStaticMesh(Platform);
		}
		if (Style == EMMDStudioDisplayStyle::SlateGallery || Style == EMMDStudioDisplayStyle::LilacStudio)
		{
			if (UStaticMesh* Panel = GetRoundedDisplayPanel())
			{
				Components[4]->SetStaticMesh(Panel);
				Components[5]->SetStaticMesh(Panel);
			}
		}
		if (Style == EMMDStudioDisplayStyle::SlateGallery)
		{
			Components[4]->SetMaterial(0, Materials[8].Get());
			Components[5]->SetMaterial(0, Materials[9].Get());
		}
		if (Style == EMMDStudioDisplayStyle::PearlAtrium)
			if (UStaticMesh* Wall = GetFlutedDisplayWall()) Components[4]->SetStaticMesh(Wall);
	}
	const FDisplayPalette Palette = GetPalette(Style);
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		UMaterialInstanceDynamic* Material = Materials[Index].Get();
		Material->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
		Material->SetScalarParameterValue(TEXT("Roughness"), Palette.Roughness);
		Material->SetScalarParameterValue(TEXT("Specular"), Palette.Specular);
		Material->SetVectorParameterValue(TEXT("BaseColor"), Index % 2 == 0 ? Palette.Accent : Palette.Secondary);
		if (Index >= LightStripStart) Material->SetScalarParameterValue(TEXT("Glow"), Palette.Glow);
	}
	Materials[0]->SetVectorParameterValue(TEXT("SkyColor"), Palette.Sky);
	Materials[0]->SetVectorParameterValue(TEXT("HorizonColor"), Palette.Horizon);
	UTexture2D* SkyTexture = GetDisplaySkyTexture(Style);
	const float SkyVerticalOffset = Style == EMMDStudioDisplayStyle::SkyGlass ? 0.13f
		: Style == EMMDStudioDisplayStyle::VioletStage ? 0.06f
		: Style == EMMDStudioDisplayStyle::WarmAtelier ? 0.07f : 0.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Materials[Index]->SetTextureParameterValue(TEXT("SkyPanorama"), SkyTexture ? SkyTexture : GEngine->DefaultTexture.Get());
		Materials[Index]->SetScalarParameterValue(TEXT("UseSkyTexture"), SkyTexture ? 1.0f : 0.0f);
		Materials[Index]->SetScalarParameterValue(TEXT("SkyYawDegrees"), 0.0f);
		Materials[Index]->SetScalarParameterValue(TEXT("SkyVerticalOffset"), SkyVerticalOffset);
	}
	Materials[1]->SetVectorParameterValue(TEXT("BaseColor"), Palette.Floor);
	Materials[1]->SetVectorParameterValue(TEXT("HorizonColor"), (Palette.Sky + Palette.Horizon) * 0.5f);
	Materials[2]->SetVectorParameterValue(TEXT("BaseColor"), Palette.Edge);
	Materials[3]->SetVectorParameterValue(TEXT("BaseColor"), Palette.Top);
	Materials[4]->SetVectorParameterValue(TEXT("BaseColor"), FMath::Lerp(Palette.Top, Palette.Floor, 0.28f));
	Materials[5]->SetVectorParameterValue(TEXT("BaseColor"), FMath::Lerp(Palette.Top, Palette.Floor, 0.57f));
	Materials[6]->SetVectorParameterValue(TEXT("BaseColor"), FMath::Lerp(Palette.Top, Palette.Accent, 0.20f));
	Materials[7]->SetVectorParameterValue(TEXT("BaseColor"), Palette.Secondary);
	for (int32 Index = RingStart; Index < ComponentCount; ++Index)
		Materials[Index]->SetVectorParameterValue(TEXT("BaseColor"), Index < RingStart + RingSegments ? Palette.Accent : Palette.Secondary);
	UpdateBounds(CurrentBounds);
}

void FMMDStudioDisplayScene::UpdateBounds(const FBox& ModelBounds)
{
	CurrentBounds = ModelBounds;
	if (!IsValid()) return;
	const FVector Center = ModelBounds.IsValid ? ModelBounds.GetCenter() : FVector(0, 0, 85);
	const float Floor = ModelBounds.IsValid ? ModelBounds.Min.Z - 0.3f : 0.0f;
	const float Height = ModelBounds.IsValid ? FMath::Max(float(ModelBounds.GetSize().Z), 30.0f) : 170.0f;
	const float Scale = FMath::Clamp(Height / 170.0f, 0.2f, 10.0f);
	const float HalfWidth = ModelBounds.IsValid ? float(ModelBounds.GetExtent().X) : 55.0f;
	const float HalfDepth = ModelBounds.IsValid ? float(ModelBounds.GetExtent().Y) : 35.0f;
	const bool bStudio = Style == EMMDStudioDisplayStyle::Studio;
	const bool bMinimalStudio = IsMinimalStudio(Style);
	const float Radius = bStudio ? (ModelBounds.IsValid ? FMath::Clamp(HalfWidth * 1.05f, 125.0f, 245.0f) : 140.0f)
		: FMath::Max(HalfWidth * 1.15f, Height * 0.76f);
	for (int32 Index = 0; Index < StyleVisibility.Num(); ++Index) StyleVisibility[Index] = Index < 4;
	Components[0]->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, 100), FVector(400)));
	// 新场景只用厘米级高差和宽踏面建立层次，角色附近没有竖向遮挡。
	const float GroundDrop = bStudio ? 0.35f : 2.4f * Scale;
	Components[1]->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Floor - 5.0f - GroundDrop), FVector(300, 300, 0.1)));
	const float EdgeWidth = bStudio ? 0.75f : 0.75f * Scale;
	Components[2]->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Floor - 0.65f), FVector((Radius + EdgeWidth) / 50.0f, (Radius + EdgeWidth) / 50.0f, 0.01f)));
	Components[3]->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Floor - 0.5f), FVector(Radius / 50.0f, Radius / 50.0f, 0.01f)));
	if (bMinimalStudio)
	{
		// 四套摄影棚只保留一层圆润台面，边缘倒角直接来自网格而非叠圆饼。
		StyleVisibility[2] = false;
		Components[3]->SetWorldTransform(FTransform(FRotator::ZeroRotator, FVector(Center.X, Center.Y, Floor - GroundDrop * 0.5f),
			FVector(Radius / 50.0f, Radius / 50.0f, GroundDrop / 100.0f)));
	}
	Materials[1]->SetScalarParameterValue(TEXT("FadeStart"), bStudio ? 600.0f : 500.0f * Scale);
	Materials[1]->SetScalarParameterValue(TEXT("FadeEnd"), bStudio ? 1500.0f : 1200.0f * Scale);
	Materials[1]->SetScalarParameterValue(TEXT("UseModelFadeCenter"), bStudio ? 0.0f : 1.0f);
	Materials[1]->SetVectorParameterValue(TEXT("FadeCenter"), FLinearColor(Center.X, Center.Y, 0.0f));

	auto Place = [this, Center, Floor](int32 Index, const FVector& Location, const FVector& Dimensions, float Yaw = 0.0f)
	{
		StyleVisibility[Index] = true;
		Components[Index]->SetWorldTransform(FTransform(FRotator(0, Yaw, 0),
			FVector(Center.X, Center.Y, Floor) + Location, Dimensions / 100.0f));
	};
	auto PlaceRing = [this, Center, Floor](int32 Ring, float RingRadius, float Elevation, float Thickness)
	{
		const FVector RingCenter(Center.X, Center.Y, Floor + Elevation);
		for (int32 Segment = 0; Segment < RingSegments; ++Segment)
		{
			const float A = 2.0f * PI * Segment / RingSegments;
			const float B = 2.0f * PI * (Segment + 1) / RingSegments;
			const FVector Start(FMath::Cos(A) * RingRadius, FMath::Sin(A) * RingRadius, 0);
			const FVector End(FMath::Cos(B) * RingRadius, FMath::Sin(B) * RingRadius, 0);
			const FVector Delta = End - Start;
			const int32 Index = RingStart + Ring * RingSegments + Segment;
			StyleVisibility[Index] = true;
			Components[Index]->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Delta).ToQuat(),
				RingCenter + (Start + End) * 0.5f, FVector(Thickness / 50.0f, Thickness / 50.0f, Delta.Size() / 100.0f)));
		}
	};
	if (!bStudio && !bMinimalStudio)
	{
		const bool bTerrace = Style == EMMDStudioDisplayStyle::SkyGlass;
		const float WidthScale = bTerrace ? 1.48f : 1.30f;
		const float DepthScale = bTerrace ? 1.26f : 1.30f;
		Place(4, FVector(0, 0, -1.25f * Scale), FVector(Radius * 2.0f * WidthScale, Radius * 2.0f * DepthScale, 0.9f * Scale));
		Place(5, FVector(0, 0, -2.05f * Scale), FVector(Radius * 2.0f * (WidthScale + 0.32f), Radius * 2.0f * (DepthScale + 0.32f), 0.9f * Scale));
	}
	const float Back = FMath::Max(HalfDepth + Height * 1.05f, Radius * 1.50f);
	const float Side = FMath::Max(HalfWidth + Height * 0.85f, Radius * 1.55f);
	if (Style == EMMDStudioDisplayStyle::VioletStage)
	{
		PlaceRing(0, Radius * 0.93f, 0.12f * Scale, 0.22f * Scale);
		PlaceRing(1, Radius * 1.21f, -0.68f * Scale, 0.18f * Scale);
		// 灯带贴近远处地面，亮度仅用于分层，不照亮人物或形成头部光环。
		Place(8, FVector(-Side * 0.75f, -Back * 1.35f, -1.75f * Scale), FVector(Height * 1.05f, 0.65f * Scale, 0.45f * Scale), -8.0f);
		Place(9, FVector(Side * 0.78f, -Back * 1.48f, -1.75f * Scale), FVector(Height * 0.86f, 0.65f * Scale, 0.45f * Scale), 10.0f);
	}
	else if (Style == EMMDStudioDisplayStyle::WarmAtelier)
	{
		Place(6, FVector(-Side, -Back * 0.80f, Height * 0.045f - GroundDrop), FVector(Height * 0.83f, Height * 0.29f, Height * 0.09f), -20.0f);
		Place(7, FVector(Side * 1.02f, -Back * 0.98f, Height * 0.032f - GroundDrop), FVector(Height * 0.72f, Height * 0.27f, Height * 0.064f), 18.0f);
	}
	else if (Style == EMMDStudioDisplayStyle::SlateGallery)
	{
		Place(4, FVector(-Side * 1.10f, -Back * 1.12f, Height * 0.325f - GroundDrop),
			FVector(Height * 0.72f, Height * 0.025f, Height * 0.65f), -14.0f);
		Place(5, FVector(Side * 1.16f, -Back * 1.28f, Height * 0.45f - GroundDrop),
			FVector(Height * 0.62f, Height * 0.025f, Height * 0.90f), 12.0f);
	}
	else if (Style == EMMDStudioDisplayStyle::PearlAtrium)
	{
		Place(4, FVector(-Side * 1.12f, -Back * 1.10f, Height * 0.525f - GroundDrop),
			FVector(Height * 0.72f, Height * 0.54f, Height * 1.05f), -12.0f);
	}
	else if (Style == EMMDStudioDisplayStyle::LilacStudio)
	{
		Place(4, FVector(Side * 1.12f, -Back * 1.16f, Height * 0.475f - GroundDrop),
			FVector(Height * 1.35f, Height * 0.035f, Height * 0.95f), 0.0f);
		Place(6, FVector(-Side * 0.92f, -Back * 0.45f, Height * 0.075f - GroundDrop),
			FVector(Height * 0.52f, Height * 0.38f, Height * 0.15f), -8.0f);
	}
	SetVisible(bSceneVisible);
}

void FMMDStudioDisplayScene::SetVisible(bool bVisible)
{
	bSceneVisible = bVisible;
	for (int32 Index = 0; Index < Components.Num(); ++Index)
	{
		const bool bShow = bVisible && StyleVisibility[Index];
		Components[Index]->SetVisibility(bShow);
		Components[Index]->SetHiddenInGame(!bShow);
	}
}

TConstArrayView<EMMDStudioDisplayStyle> FMMDStudioDisplayScene::GetStyles()
{
	static constexpr EMMDStudioDisplayStyle Styles[] = {EMMDStudioDisplayStyle::Studio,
		EMMDStudioDisplayStyle::RoseStudio, EMMDStudioDisplayStyle::SlateGallery,
		EMMDStudioDisplayStyle::PearlAtrium, EMMDStudioDisplayStyle::LilacStudio,
		EMMDStudioDisplayStyle::SkyGlass, EMMDStudioDisplayStyle::VioletStage, EMMDStudioDisplayStyle::WarmAtelier};
	return MakeArrayView(Styles);
}

EMMDLightingEnvironment FMMDStudioDisplayScene::GetLightingEnvironment(EMMDStudioDisplayStyle InStyle)
{
	switch (InStyle)
	{
	case EMMDStudioDisplayStyle::SkyGlass: return EMMDLightingEnvironment::DisplaySkyGlass;
	case EMMDStudioDisplayStyle::VioletStage: return EMMDLightingEnvironment::DisplayVioletStage;
	case EMMDStudioDisplayStyle::WarmAtelier: return EMMDLightingEnvironment::DisplayWarmAtelier;
	case EMMDStudioDisplayStyle::RoseStudio: return EMMDLightingEnvironment::DisplayRoseStudio;
	case EMMDStudioDisplayStyle::SlateGallery: return EMMDLightingEnvironment::DisplaySlateGallery;
	case EMMDStudioDisplayStyle::PearlAtrium: return EMMDLightingEnvironment::DisplayPearlAtrium;
	case EMMDStudioDisplayStyle::LilacStudio: return EMMDLightingEnvironment::DisplayLilacStudio;
	default: return EMMDLightingEnvironment::Studio3Point;
	}
}

FString FMMDStudioDisplayScene::GetDisplayName(EMMDStudioDisplayStyle InStyle)
{
	switch (InStyle)
	{
	case EMMDStudioDisplayStyle::SkyGlass: return TEXT("晴空露台");
	case EMMDStudioDisplayStyle::VioletStage: return TEXT("暮色星台");
	case EMMDStudioDisplayStyle::WarmAtelier: return TEXT("暖光画廊");
	case EMMDStudioDisplayStyle::RoseStudio: return TEXT("樱雾柔光");
	case EMMDStudioDisplayStyle::SlateGallery: return TEXT("深蓝展厅");
	case EMMDStudioDisplayStyle::PearlAtrium: return TEXT("珍珠回廊");
	case EMMDStudioDisplayStyle::LilacStudio: return TEXT("薰紫摄影棚");
	default: return TEXT("纯白影棚");
	}
}

FString FMMDStudioDisplayScene::GetDescription(EMMDStudioDisplayStyle InStyle)
{
	switch (InStyle)
	{
	case EMMDStudioDisplayStyle::SkyGlass: return TEXT("开阔云空 · 浅石宽阶 · 清透日光");
	case EMMDStudioDisplayStyle::VioletStage: return TEXT("暮色星空 · 地面细环 · 柔亮面光");
	case EMMDStudioDisplayStyle::WarmAtelier: return TEXT("暖云天幕 · 低矮圆景 · 奶油柔光");
	case EMMDStudioDisplayStyle::RoseStudio: return TEXT("柔粉无缝背景 · 圆润细边台 · 中性柔光");
	case EMMDStudioDisplayStyle::SlateGallery: return TEXT("低饱和深蓝 · 哑光展台 · 两侧柔光板");
	case EMMDStudioDisplayStyle::PearlAtrium: return TEXT("珍珠白空间 · 偏侧弧墙 · 细腻竖槽");
	case EMMDStudioDisplayStyle::LilacStudio: return TEXT("浅紫渐层 · 简洁宽背板 · 低矮侧台");
	default: return TEXT("浅色渐层 · 简洁圆台 · 校准三点光");
	}
}

FString FMMDStudioDisplayScene::GetId(EMMDStudioDisplayStyle InStyle)
{
	switch (InStyle)
	{
	case EMMDStudioDisplayStyle::SkyGlass: return TEXT("SkyGlass");
	case EMMDStudioDisplayStyle::VioletStage: return TEXT("VioletStage");
	case EMMDStudioDisplayStyle::WarmAtelier: return TEXT("WarmAtelier");
	case EMMDStudioDisplayStyle::RoseStudio: return TEXT("RoseStudio");
	case EMMDStudioDisplayStyle::SlateGallery: return TEXT("SlateGallery");
	case EMMDStudioDisplayStyle::PearlAtrium: return TEXT("PearlAtrium");
	case EMMDStudioDisplayStyle::LilacStudio: return TEXT("LilacStudio");
	default: return TEXT("Studio");
	}
}

bool FMMDStudioDisplayScene::TryParse(const FString& Id, EMMDStudioDisplayStyle& OutStyle)
{
	const FString Value = Id.TrimStartAndEnd();
	for (EMMDStudioDisplayStyle Candidate : GetStyles())
	{
		if (Value.Equals(GetId(Candidate), ESearchCase::IgnoreCase)) { OutStyle = Candidate; return true; }
	}
	return false;
}
