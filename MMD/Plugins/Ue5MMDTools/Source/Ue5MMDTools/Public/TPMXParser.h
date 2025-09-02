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
// - repeat materialCount:
// - nameJP (string)
// - nameEN (string)
// - float4 diffuse // RGBA
// - float3 specular // RGB
// - float specularPower
// - float3 ambient
// - uint8 drawFlags // bitfield: 0x01 DoubleSided, 0x02 GroundShadow, 0x04 CastShadow,
// // 0x08 ReceiveShadow, 0x10 Edge, 0x20 VertexColor, 0x40 PointDraw, 0x80 LineDraw
// - float4 edgeColor // RGBA
// - float edgeSize
// - textureIndex (textureIndexSize) // main texture, -1 if none
// - sphereTextureIndex (textureIndexSize) // -1 if none
// - uint8 sphereMode // 0=Off, 1=Mul, 2=Add, 3=SubTex
// - uint8 useSharedToon // 0=use common toonXX, 1=use texture index
// if useSharedToon==0:
// - uint8 toonNumber // 0..9 -> toon01..toon10 (engine-side mapping)
// else:
// - toonTextureIndex (textureIndexSize)
// - string memo // material note
// - int32 faceIndexCount // number of indices this material covers (submesh size)
struct PMXMaterial
{
    FString NameJP;
    FString NameEN;
    FVector4 DiffuseColor;
    FVector4 SpecularColor;
    float SpecularPower;
    FVector AmbientColor;
    uint8 DrawFlags;
    FVector4 EdgeColor;
    float EdgeSize;
    int32 TextureIndex;
    int32 SphereTextureIndex;
    uint8 SphereMode;
    uint8 UseSharedToon;
    uint8 ToonNumber;
    int32 ToonTextureIndex;
    FString Memo;
    int32 FaceIndexCount;
};
// 7) Bones
// - int32 boneCount
// - repeat boneCount:
// - nameJP (string)
// - nameEN (string)
// - float3 position
// - parentBoneIndex (boneIndexSize) // -1 if no parent
// - int32 deformLayer // display/transform order layer
// - uint16 flags // bitfield:
// // 0x0001 ConnectionToChildIsBoneIndex (tail by index)
// // 0x0002 CanRotate
// // 0x0004 CanTranslate
// // 0x0008 Visible
// // 0x0010 Enabled
// // 0x0020 IK
// // 0x0100 InheritRotate
// // 0x0200 InheritTranslate
// // 0x0400 FixedAxis
// // 0x0800 LocalAxis
// // 0x2000 ExternalParent
// - if (flags & 0x0001) then:
// - tailBoneIndex (boneIndexSize)
// else:
// - float3 tailOffset
// - if (flags & (0x0100 | 0x0200)) then: // inherit rot/pos
// - inheritParentIndex (boneIndexSize)
// - float inheritInfluence // 0..1
// - if (flags & 0x0400) then: // fixed axis
// - float3 axis
// - if (flags & 0x0800) then: // local axis
// - float3 localAxisX
// - float3 localAxisZ
// - if (flags & 0x2000) then: // external parent
// - int32 externalParentKey
// - if (flags & 0x0020) then: // IK block
// - ikTargetBoneIndex (boneIndexSize)
// - int32 ikLoopCount
// - float ikLimitAngle // radians
// - int32 ikLinkCount
// - repeat ikLinkCount:
// - linkBoneIndex (boneIndexSize)
// - uint8 hasLimit
// - if hasLimit != 0:
// - float3 lowerLimit // per-axis radians
// - float3 upperLimit
struct PMXBone
{
    FString NameJP;
    FString NameEN;
    FVector Position = FVector::ZeroVector;
    int32 ParentBoneIndex = -1;
    int32 DeformLayer = 0;
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
    TArray<PMXMaterial> ModelMaterials;

    int32 ModelBoneCount;
    TArray<PMXBone> ModelBones;
};

class UE5MMDTOOLS_API TPMXParser
{
public:
    bool ParsePMXFile(const FString &FilePath);
    TArray<uint8> PMXData;

    PMXDatas PMXInfo;

private:
};
