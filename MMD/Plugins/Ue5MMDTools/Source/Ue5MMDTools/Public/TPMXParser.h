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
// 9) Display Frames (for UI panels)
// - int32 frameCount
// - repeat frameCount:
// - nameJP (string)
// - nameEN (string)
// - uint8 isSpecial // 0=normal, 1=special (root frame)
// - int32 elementCount
// - repeat elementCount:
// - uint8 elemType // 0=Bone, 1=Morph
// - elemIndex // sized by (boneIndexSize or morphIndexSize)
struct PMXFrame
{
    FString NameJP;
    FString NameEN;
    uint8 IsSpecial = 0;
    int32 ElementCount = 0;

    struct Element
    {
        uint8 ElemType;
        int32 ElemIndex;
    };
    TArray<Element> Elements;
};
// 10) Rigid Bodies (physics)
// - int32 rigidCount
// - repeat rigidCount:
// - nameJP (string)
// - nameEN (string)
// - relatedBoneIndex (boneIndexSize) // -1 if unbound
// - uint8 group // 0..15
// - uint16 collisionMask // bitmask
// - uint8 shapeType // 0=Sphere, 1=Box, 2=Capsule
// - float3 size // sphere:r= x; box: x,y,z; capsule: r=x, h=y
// - float3 position
// - float3 rotation // radians
// - float mass
// - float linearDamping
// - float angularDamping
// - float restitution
// - float friction
// - uint8 physicsMode // 0=Static, 1=Dynamic, 2=BoneTracked
struct PMXRigid
{
    FString NameJP;
    FString NameEN;
    int32 RelatedBoneIndex = -1;
    uint8 Group = 0;
    uint16 CollisionMask = 0;
    uint8 ShapeType = 0;
    FVector Size = FVector::ZeroVector;
    FVector Position = FVector::ZeroVector;
    FVector Rotation = FVector::ZeroVector;
    float Mass = 0.0f;
    float LinearDamping = 0.0f;
    float AngularDamping = 0.0f;
    float Restitution = 0.0f;
    float Friction = 0.0f;
    uint8 PhysicsMode = 0;
};
// 11) Joints (constraints)
// - int32 jointCount
// - repeat jointCount:
// - nameJP (string)
// - nameEN (string)
// - uint8 jointType // 0=Spring6DOF (common). 2.1 may define more but layout same below.
// - rigidA (rigidIndexSize)
// - rigidB (rigidIndexSize)
// - float3 position
// - float3 rotation
// - float3 limitPosLower
// - float3 limitPosUpper
// - float3 limitRotLower
// - float3 limitRotUpper
// - float3 springPos
// - float3 springRot
struct PMXJoint
{
    FString NameJP;
    FString NameEN;
    uint8 JointType = 0;
    int32 RigidA = -1;
    int32 RigidB = -1;
    FVector Position = FVector::ZeroVector;
    FVector Rotation = FVector::ZeroVector;
    FVector LimitPosLower = FVector::ZeroVector;
    FVector LimitPosUpper = FVector::ZeroVector;
    FVector LimitRotLower = FVector::ZeroVector;
    FVector LimitRotUpper = FVector::ZeroVector;
    FVector SpringPos = FVector::ZeroVector;
    FVector SpringRot = FVector::ZeroVector;
};
// 12) Soft Bodies (PMX 2.1 only; optional, often 0)
// - int32 softBodyCount
// - repeat softBodyCount:
// - nameJP (string)
// - nameEN (string)
// - uint8 shapeType // 0=TriMesh, 1=Rope, etc.
// - int32 materialIndex // target material
// - uint8 group
// - uint16 collisionMask
// - uint8 flags // soft body flags
// - int32 bLinkDistance, clusterCount, totalMass, margin, aeroModel, etc. // 多个 sim 参数（按规范顺序读取）
// - int32 anchorCount
// - repeat anchorCount:
// - rigidIndex (rigidIndexSize)
// - vertexIndex (vertexIndexSize)
// - uint8 nearMode
// - int32 pinVertexCount
// - repeat pinVertexCount:
// - vertexIndex (vertexIndexSize)
struct PMXSoftBody
{
    FString NameJP;
    FString NameEN;
    uint8 ShapeType = 0;
    int32 MaterialIndex = -1;
    uint8 Group = 0;
    uint16 CollisionMask = 0;
    uint8 Flags = 0;
    // 多个 sim 参数（按规范顺序读取）
    int32 BLinkDistance = 0;
    int32 ClusterCount;
    int32 TotalMass=0;
    int32 Margin=0;
    int32 AeroModel=0;

    int32 VCF=0;//Velocities Correction Factor - 速度修正因子
    int32 DP=0;//Damping Parameter - 阻尼参数
    int32 DG=0;//Drag Coefficient - 拖曳系数
    int32 LF=0;//Lift Force - 升力
    int32 PR=0;//Pressure - 压力
    int32 VC=0;//Volume Conservation - 体积守恒
    int32 DF=0;//Dynamic Friction - 动态摩擦
    int32 MT=0;//Pose Matching - 姿态匹配
    int32 CHR=0;//Rigid Contact Hardness - 与刚体碰撞硬度
    int32 KHR=0;//Kinetic Contact Hardness - 动态碰撞硬度
    int32 SHR=0;//Soft Contact Hardness - 与柔性体碰撞硬度
    int32 AHR=0;//Anchor Hardness - 与锚点碰撞硬度

    int32 SRHR_CL=0;//Soft vs Rigid Hardness (Cluster) - 柔性体与刚体碰撞硬度（Cluster）
    int32 SKHR_CL=0;//Soft vs Kinetic Hardness (Cluster) - 柔性体与动态碰撞硬度（Cluster）
    int32 SSHR_CL=0;//Soft vs Soft Hardness (Cluster) - 柔性体与柔性体碰撞硬度（Cluster）
    int32 SR_SPLT_CL=0;//Soft vs Rigid Impulse Splitting (Cluster) - 柔性体与刚体冲量分割（Cluster）
    int32 SK_SPLT_CL=0;//Soft vs Kinetic Impulse Splitting (Cluster) - 柔性体与动态冲量分割（Cluster）
    int32 SS_SPLT_CL=0;//Soft vs Soft Impulse Splitting (Cluster) - 柔性体与柔性体冲量分割（Cluster）

    int32 V_IT=0;//Velocities Iterations - 速度迭代
    int32 P_IT=0;//Positions Iterations - 位置迭代
    int32 D_IT=0;//Densities Iterations - 密度迭代
    int32 C_IT=0;//Cluster Iterations - 簇迭代

    float LST=0.0f;//Lift Coefficient - 升力系数
    float AST=0.0f;//Aero Surface Tension - 空气表面
    float VST=0.0f;//Volume Surface Tension - 体积表面

    int32 AnchorCount = 0;
    struct Anchor
    {
        int32 RigidIndex = -1;
        int32 VertexIndex = -1;
        uint8 NearMode = 0;
    };
    TArray<Anchor> Anchors;
    int32 PinVertexCount = 0;
    TArray<int32> PinVertices;
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

    int32 ModelFrameCount = 0;
    TArray<PMXFrame> ModelFrames;

    int32 ModelRigidCount = 0;
    TArray<PMXRigid> ModelRigids;

    int32 ModelJointCount = 0;
    TArray<PMXJoint> ModelJoints;

    int32 ModelSoftBodyCount = 0;
    TArray<PMXSoftBody> ModelSoftBodies;
};

class UE5MMDTOOLS_API TPMXParser
{
public:
    bool ParsePMXFile(const FString &FilePath);
    TArray<uint8> PMXData;

    PMXDatas PMXInfo;

private:
};
