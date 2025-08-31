#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"

class FMemoryReader;
class USkeletalMesh;

struct TPMXGlobals
{
    uint8 TextEncoding = 0;
    uint8 ExtraUV = 0;
    uint8 VertexIndexSize = 4;
    uint8 TextureIndexSize = 4;
    uint8 MaterialIndexSize = 4;
    uint8 BoneIndexSize = 4;
    uint8 MorphIndexSize = 4;
    uint8 RigidBodyIndexSize = 4;
};
struct PMXVertexWeight
{
    uint8 WeightDeformType = 0; // 0=BDEF1, 1=BDEF2, 2=BDEF4, 3=SDEF, 4=QDEF(2.1)
    int32 BoneIndices[4] = {-1, -1, -1, -1};
    float Weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // SDEF specific
    FVector SDEF_C = FVector::ZeroVector;
    FVector SDEF_R0 = FVector::ZeroVector;
    FVector SDEF_R1 = FVector::ZeroVector;
    // QDEF specific
    float EdgeScale = 0.0f; // 边缘放大系数（轮廓描边用）
};
struct PMXVertex
{
    FVector Position = FVector::ZeroVector;
    FVector Normal = FVector::ZeroVector;
    FVector2D UV = FVector2D::ZeroVector;
    TArray<FVector4> AdditionalUVs;
    PMXVertexWeight Weight;
};
struct PMXMaterial
{
    FString Name;
    FString TexturePath;
    FLinearColor DiffuseColor;
    FLinearColor SpecularColor;
    FLinearColor AmbientColor;
    float Shininess;
    float Opacity;
    bool bTwoSided;
};
struct PMXDatas
{
    float Version;
    uint8 Sig[4];

    FString ModelNameJP;
    FString ModelNameEN;
    FString ModelCommentJP;
    FString ModelCommentEN;

    TPMXGlobals PMXGlobals;

    // 顶点数据
    int32 ModelVertexCount;
    TArray<PMXVertex> ModelVertices;
    // 三角面数据
    int32 ModelIndicesCount;
    TArray<int32> ModelIndices;

    int32 ModelTextureCount;
    TArray<FString> ModelTexturePaths;

    int32 ModelMaterialCount;
};

class UE5MMDTOOLS_API TPMXParser
{
public:
    bool ParsePMXFile(const FString &FilePath);
    TArray<uint8> PMXData;

    PMXDatas PMXInfo;

private:
};
