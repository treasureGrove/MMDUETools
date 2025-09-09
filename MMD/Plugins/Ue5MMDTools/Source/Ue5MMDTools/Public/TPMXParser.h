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
    uint16 Flags = 0;           

    int32 TailBoneIndex = -1;
    FVector TailOffset = FVector::ZeroVector;

    int32 InheritParentIndex = -1;
    float InheritInfluence = 0.0f;

    FVector Axis = FVector::ZeroVector;

    FVector LocalAxisX = FVector::ZeroVector;
    FVector LocalAxisZ = FVector::ZeroVector;

    int32 ExternalParentKey = -1;

    int32 IKTargetBoneIndex = -1;
    int32 IKLoopCount = 0;
    float IKLimitAngle = 0.0f;
    int32 IKLinkCount = 0;

    struct PMXIKLink
    {
        int32 LinkBoneIndex = -1;
        uint8 HasLimit = 0;
        FVector LowerLimit = FVector::ZeroVector;
        FVector UpperLimit = FVector::ZeroVector;
    };
    TArray<PMXIKLink> IKLinks;
};
// 8) Morphs
// - int32 morphCount
// - repeat morphCount:
// - nameJP (string)
// - nameEN (string)
// - uint8 panel // 0=System/Hidden, 1=Eyebrow, 2=Eye, 3=Mouth, 4=Other
// - uint8 morphType // 0=Group,1=Vertex,2=Bone,3=UV,4=AddUV1,5=AddUV2,6=AddUV3,7=AddUV4,8=Material,9=Flip(2.1),10=Impulse(2.1)
// - int32 elementCount
// - switch(morphType):
// Group:
// - repeat elementCount:
// - morphIndex (morphIndexSize)
// - float weight
// Vertex:
// - repeat:
// - vertexIndex (vertexIndexSize)
// - float3 positionOffset
// Bone:
// - repeat:
// - boneIndex (boneIndexSize)
// - float3 translation
// - float4 rotationQuat // (x,y,z,w)
// UV / AddUV1..4:
// - repeat:
// - vertexIndex (vertexIndexSize)
// - float4 uvOffset
// Material:
// - repeat:
// - materialIndex (materialIndexSize) // -1 means “all materials”
// - uint8 calcMode // 0=Multiply, 1=Add
// - float4 diffuse
// - float3 specular
// - float specularPower
// - float3 ambient
// - float4 edgeColor
// - float edgeSize
// - float4 textureTint
// - float4 sphereTextureTint
// - float4 toonTextureTint
// Flip (2.1):
// - repeat:
// - morphIndex (morphIndexSize)
// - float weight
// Impulse (2.1):
// - repeat:
// - rigidIndex (rigidIndexSize)
// - uint8 isLocal
// - float3 velocity
// - float3 torque
struct PMXMorph
{
    FString NameJP;
    FString NameEN;
    uint8 Panel = 0;        
    uint8 MorphType = 0;    
    int32 ElementCount = 0; 

    struct Group
    {
        int32 MorphIndex = -1; 
        float Weight = 0.0f;   
    };
    struct Vertex
    {
        int32 VertexIndex = -1;                     
        FVector PositionOffset = FVector::ZeroVector; 
    };
    struct Bone
    {
        int32 BoneIndex = -1;                      
        FVector Translation = FVector::ZeroVector; 
        FQuat RotationQuat = FQuat::Identity;     
    };
    struct UV
    {
        int32 VertexIndex = -1;               
        FVector4 UVOffset = FVector4::Zero(); 
    };
    struct Material
    {
        int32 MaterialIndex = -1;                      
        uint8 CalcMode = 0;                            
        FVector4 Diffuse = FVector4::Zero();           
        FVector Specular = FVector::ZeroVector;        
        float SpecularPower = 0.0f;                   
        FVector Ambient = FVector::ZeroVector;        
        FVector4 EdgeColor = FVector4::Zero();         
        float EdgeSize = 0.0f;                         
        FVector4 TextureTint = FVector4::Zero();       
        FVector4 SphereTextureTint = FVector4::Zero(); 
        FVector4 ToonTextureTint = FVector4::Zero();  
    };
    struct Flip
    {
        int32 MorphIndex = -1; 
        float Weight = 0.0f;   
    };
    struct Impulse
    {
        int32 RigidIndex = -1;                  
        uint8 IsLocal = 0;                      
        FVector Velocity = FVector::ZeroVector; 
        FVector Torque = FVector::ZeroVector;   
    };

    TArray<Group> Groups;
    TArray<Vertex> Vertices;
    TArray<Bone> Bones;
    TArray<UV> UVs;
    TArray<Material> Materials;
    TArray<Flip> Flips;
    TArray<Impulse> Impulses;
};
struct PMXDatas
{
    float Version = 0.0f;
    uint8 Sig[4] = {0};

    FString ModelNameJP;
    FString ModelNameEN;
    FString ModelCommentJP;
    FString ModelCommentEN;

    TPMXGlobals PMXGlobals;

    // 顶点数据
    int32 ModelVertexCount = 0;
    TArray<PMXVertex> ModelVertices;
    // 三角面数据
    int32 ModelIndicesCount = 0;
    TArray<int32> ModelIndices;

    int32 ModelTextureCount = 0;
    TArray<FString> ModelTexturePaths;

    int32 ModelMaterialCount = 0;
    TArray<PMXMaterial> ModelMaterials;

    int32 ModelBoneCount = 0;
    TArray<PMXBone> ModelBones;

    int32 ModelMorphCount = 0;
    TArray<PMXMorph> ModelMorphs;
};

class UE5MMDTOOLS_API TPMXParser
{
public:
    bool ParsePMXFile(const FString &FilePath);
    TArray<uint8> PMXData;

    PMXDatas PMXInfo;

private:
};
