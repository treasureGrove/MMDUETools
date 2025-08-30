#include "TPMXParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
bool ReadString(FMemoryReader &Reader, FString &OutString, PMXDatas &PMXInfo);
bool ReadPMXGlobals(FMemoryReader &Reader, TPMXGlobals &OutGlobals);
bool ReadPMXVertex(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXIndices(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXTexturePath(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool TPMXParser::ParsePMXFile(const FString &FilePath)
{
    PMXInfo = PMXDatas{};
    // pmx版本号
    if (FilePath.IsEmpty()) // 如果路径为空就返回false
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: FilePath is empty"));
        return false;
    }
    IFileManager &FileManager = IFileManager::Get();
    if (!FileManager.FileExists(*FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: File does not exist: %s"), *FilePath);
        return false;
    }

    if (!FFileHelper::LoadFileToArray(PMXData, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to load file to array: %s"), *FilePath);
        return false;
    }
    UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully loaded file: %s, Size: %d bytes"), *FilePath, PMXData.Num());

    FMemoryReader PMXReader(PMXData, true);
    // 创建一个4字节的数组来存储签名
    PMXReader.Serialize(PMXInfo.Sig, 4);
    FString Signature = FString::Printf(TEXT("%c%c%c%c"), PMXInfo.Sig[0], PMXInfo.Sig[1], PMXInfo.Sig[2], PMXInfo.Sig[3]);
    UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: File Signature: %s"), *Signature);
    // 读version
    PMXReader << PMXInfo.Version;
    UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: File Version: %f"), PMXInfo.Version);

    if (ReadPMXGlobals(PMXReader, PMXInfo.PMXGlobals))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX globals"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX globals"));
    }

    if (ReadString(PMXReader, PMXInfo.ModelNameJP, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Name: %s"), *PMXInfo.ModelNameJP);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Name"));
    }

    if (ReadString(PMXReader, PMXInfo.ModelNameEN, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Name EN: %s"), *PMXInfo.ModelNameEN);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Name EN"));
    }

    if (ReadString(PMXReader, PMXInfo.ModelCommentJP, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Comment: %s"), *PMXInfo.ModelCommentJP);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Comment"));
    }

    if (ReadString(PMXReader, PMXInfo.ModelCommentEN, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Comment EN: %s"), *PMXInfo.ModelCommentEN);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Comment EN"));
    }

    PMXReader << PMXInfo.ModelVertexCount;
    UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Vertex Count: %d"), PMXInfo.ModelVertexCount);
    if (ReadPMXVertex(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX vertex"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX vertex"));
    }
    UE_LOG(LogTemp, Warning, TEXT("ExtraUV count: %d"), PMXInfo.PMXGlobals.ExtraUV);

    if (ReadPMXIndices(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX indices"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX indices"));
    }

    return true;
}
bool ReadString(FMemoryReader &Reader, FString &OutString, PMXDatas &PMXInfo)
{
    int32 StringLength;
    Reader << StringLength;
    if (StringLength < 0 || Reader.TotalSize() - Reader.Tell() < StringLength)
        return false;

    TArray<uint8> StringData;
    StringData.SetNumUninitialized(StringLength);
    Reader.Serialize(StringData.GetData(), StringLength);

    if (PMXInfo.PMXGlobals.TextEncoding == 0) // UTF-16LE
    {
        if (StringLength % 2 != 0)
            UE_LOG(LogTemp, Warning, TEXT("ReadString: UTF-16 string length must be even"));
        TArray<uint16> UTF16Data;
        UTF16Data.SetNumUninitialized(StringLength / 2 + 1);
        FMemory::Memcpy(UTF16Data.GetData(), StringData.GetData(), StringLength);
        UTF16Data[StringLength / 2] = 0;
        OutString = FString(reinterpret_cast<const TCHAR *>(UTF16Data.GetData()));
    }
    else // UTF-8
    {
        OutString = FString(UTF8_TO_TCHAR(StringData.GetData()));
    }
    return true;
}
int32 ReadGlobalIndex(FMemoryReader &Reader, uint8 IndexSize)
{
    switch (IndexSize)
    {
    case 1:
    {
        int8 v;
        Reader << v;
        return v;
    }
    case 2:
    {
        int16 v;
        Reader << v;
        return v;
    }
    case 4:
    {
        int32 v;
        Reader << v;
        return v;
    }
    default:
        return -1; // Invalid index size
    }
}
bool ReadPMXGlobals(FMemoryReader &Reader, TPMXGlobals &OutGlobals)
{
    uint8 GlobalsCount;
    Reader << GlobalsCount;

    UE_LOG(LogTemp, Log, TEXT("ReadPMXGlobals: Count: %d"), GlobalsCount);

    if (GlobalsCount != 8)
    {
        UE_LOG(LogTemp, Error, TEXT("ReadPMXGlobals: Expected 8 globals, got %d"), GlobalsCount);
        return false;
    }

    Reader << OutGlobals.TextEncoding
           << OutGlobals.ExtraUV
           << OutGlobals.VertexIndexSize
           << OutGlobals.TextureIndexSize
           << OutGlobals.MaterialIndexSize
           << OutGlobals.BoneIndexSize
           << OutGlobals.MorphIndexSize
           << OutGlobals.RigidBodyIndexSize;
    return true;
}
bool ReadPMXVertex(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    if (PMXInfo.ModelVertexCount < 10)
    {
        UE_LOG(LogTemp, Warning, TEXT("ReadPMXVertex: ModelVertexCount is suspiciously low: %d"), PMXInfo.ModelVertexCount);
        return false;
    }
    PMXInfo.ModelVertices.SetNumUninitialized(PMXInfo.ModelVertexCount);
    for (int32 i = 0; i < PMXInfo.ModelVertexCount; i++)
    {
        float px, py, pz;
        Reader << px << py << pz;
        PMXInfo.ModelVertices[i].Position = FVector(px, py, pz);
        float nx, ny, nz;
        Reader << nx << ny << nz;
        PMXInfo.ModelVertices[i].Normal = FVector(nx, ny, nz);
        float u, v;
        Reader << u << v;
        PMXInfo.ModelVertices[i].UV = FVector2D(u, v);
        PMXInfo.ModelVertices[i].AdditionalUVs.SetNum(PMXInfo.PMXGlobals.ExtraUV);
        for (int32 uvIndex = 0; uvIndex < PMXInfo.PMXGlobals.ExtraUV; uvIndex++)
        {
            float ux, uy, uz, uw;
            Reader << ux << uy << uz << uw;
            PMXInfo.ModelVertices[i].AdditionalUVs[uvIndex] = FVector4(ux, uy, uz, uw);
        }
        Reader << PMXInfo.ModelVertices[i].Weight.WeightDeformType;

        switch (PMXInfo.ModelVertices[i].Weight.WeightDeformType)
        {
        case 0:
            PMXInfo.ModelVertices[i].Weight.BoneIndices[0] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            PMXInfo.ModelVertices[i].Weight.Weights[0] = 1.0f;
            break;
        case 1:
            PMXInfo.ModelVertices[i].Weight.BoneIndices[0] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            PMXInfo.ModelVertices[i].Weight.BoneIndices[1] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            Reader << PMXInfo.ModelVertices[i].Weight.Weights[0];
            PMXInfo.ModelVertices[i].Weight.Weights[1] = 1.0f - PMXInfo.ModelVertices[i].Weight.Weights[0];
            break;
        case 2:
            for (int j = 0; j < 4; j++)
            {
                PMXInfo.ModelVertices[i].Weight.BoneIndices[j] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            }
            for (int j = 0; j < 4; j++)
            {
                Reader << PMXInfo.ModelVertices[i].Weight.Weights[j];
            }
            break;
        case 3:
            PMXInfo.ModelVertices[i].Weight.BoneIndices[0] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            PMXInfo.ModelVertices[i].Weight.BoneIndices[1] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            Reader << PMXInfo.ModelVertices[i].Weight.Weights[0];
            PMXInfo.ModelVertices[i].Weight.Weights[1] = 1.0f - PMXInfo.ModelVertices[i].Weight.Weights[0];
            float cx, cy, cz;
            Reader << cx << cy << cz;
            PMXInfo.ModelVertices[i].Weight.SDEF_C = FVector(cx, cy, cz);

            float r0x, r0y, r0z;
            Reader << r0x << r0y << r0z;
            PMXInfo.ModelVertices[i].Weight.SDEF_R0 = FVector(r0x, r0y, r0z);

            float r1x, r1y, r1z;
            Reader << r1x << r1y << r1z;
            PMXInfo.ModelVertices[i].Weight.SDEF_R1 = FVector(r1x, r1y, r1z);
            break;
        case 4: // QDEF (PMX 2.1)
            for (int j = 0; j < 4; j++)
            {
                PMXInfo.ModelVertices[i].Weight.BoneIndices[j] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            }
            for (int j = 0; j < 4; j++)
            {
                Reader << PMXInfo.ModelVertices[i].Weight.Weights[j];
            }
            break;
        }
        Reader << PMXInfo.ModelVertices[i].Weight.EdgeScale;
        if (i < 5)
        {
            FString ExtraUVStr;
            for (int32 uvIndex = 0; uvIndex < PMXInfo.ModelVertices[i].AdditionalUVs.Num(); uvIndex++)
            {
                const FVector4 &uv = PMXInfo.ModelVertices[i].AdditionalUVs[uvIndex];
                ExtraUVStr += FString::Printf(TEXT("UV%d=(%.4f,%.4f,%.4f,%.4f) "), uvIndex, uv.X, uv.Y, uv.Z, uv.W);
            }

            FString WeightStr;
            for (int j = 0; j < 4; j++)
            {
                WeightStr += FString::Printf(TEXT("Bone%d=%d W%.4f; "), j, PMXInfo.ModelVertices[i].Weight.BoneIndices[j], PMXInfo.ModelVertices[i].Weight.Weights[j]);
            }

            FString SDEFStr = TEXT("");
            if (PMXInfo.ModelVertices[i].Weight.WeightDeformType == 3)
            {
                SDEFStr = FString::Printf(TEXT("SDEF_C=(%.4f,%.4f,%.4f) SDEF_R0=(%.4f,%.4f,%.4f) SDEF_R1=(%.4f,%.4f,%.4f)"),
                                          PMXInfo.ModelVertices[i].Weight.SDEF_C.X, PMXInfo.ModelVertices[i].Weight.SDEF_C.Y, PMXInfo.ModelVertices[i].Weight.SDEF_C.Z,
                                          PMXInfo.ModelVertices[i].Weight.SDEF_R0.X, PMXInfo.ModelVertices[i].Weight.SDEF_R0.Y, PMXInfo.ModelVertices[i].Weight.SDEF_R0.Z,
                                          PMXInfo.ModelVertices[i].Weight.SDEF_R1.X, PMXInfo.ModelVertices[i].Weight.SDEF_R1.Y, PMXInfo.ModelVertices[i].Weight.SDEF_R1.Z);
            }

            float EdgeScale = PMXInfo.ModelVertices[i].Weight.EdgeScale;

            UE_LOG(LogTemp, Warning, TEXT("Vertex[%d]: Pos=(%.4f,%.4f,%.4f) Normal=(%.4f,%.4f,%.4f) UV=(%.4f,%.4f) %s WeightType=%d %s %s EdgeScale=%.4f"),
                   i,
                   PMXInfo.ModelVertices[i].Position.X, PMXInfo.ModelVertices[i].Position.Y, PMXInfo.ModelVertices[i].Position.Z,
                   PMXInfo.ModelVertices[i].Normal.X, PMXInfo.ModelVertices[i].Normal.Y, PMXInfo.ModelVertices[i].Normal.Z,
                   PMXInfo.ModelVertices[i].UV.X, PMXInfo.ModelVertices[i].UV.Y,
                   *ExtraUVStr,
                   PMXInfo.ModelVertices[i].Weight.WeightDeformType,
                   *WeightStr,
                   *SDEFStr,
                   EdgeScale);
        }
    }

    return true;
}
bool ReadPMXIndices(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    Reader << PMXInfo.ModelIndicesCount;
    UE_LOG(LogTemp, Log, TEXT("ReadPMXIndices: Count: %d"), PMXInfo.ModelIndicesCount);
    PMXInfo.ModelIndices.Empty(PMXInfo.ModelIndicesCount);
    for (int32 i = 0; i < PMXInfo.ModelIndicesCount; i++)
    {
        int32 Index = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.VertexIndexSize);
        PMXInfo.ModelIndices.Add(Index);
        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Index[%d]: %d"), i, Index);
        }
    }

    return true;
}

bool ReadPMXTexturePath(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    return true;
}
// =============================================
// PMX文件解析格式
// =============================================
// PMX file layout (v2.0/v2.1) — read order

// 1) Header
// - char[4] magic = "PMX "
// - float version // 2.0 or 2.1
// - uint8 headerSize // usually 8
// - uint8[headerSize] headerFields, expanded as:
// [0] uint8 encoding // 0=UTF16LE, 1=UTF8 (for all following text fields)
// [1] uint8 addUVCount // additional UV count per vertex: 0..4
// [2] uint8 vertexIndexSize // 1/2/4 bytes (signed)
// [3] uint8 textureIndexSize // 1/2/4 bytes (signed)
// [4] uint8 materialIndexSize // 1/2/4 bytes (signed)
// [5] uint8 boneIndexSize // 1/2/4 bytes (signed)
// [6] uint8 morphIndexSize // 1/2/4 bytes (signed)
// [7] uint8 rigidIndexSize // 1/2/4 bytes (signed)

// 2) Model Info (text fields, use encoding above; format = int32 byteLength + bytes)
// - nameJP
// - nameEN
// - commentJP
// - commentEN

// 3) Vertices
// - int32 vertexCount
// - repeat vertexCount times:
// - float3 position
// - float3 normal
// - float2 uv
// - float4 addUV[addUVCount] // each additional UV is vec4
// - uint8 weightDeformType // 0=BDEF1, 1=BDEF2, 2=BDEF4, 3=SDEF, 4=QDEF(2.1)
// - switch(weightDeformType):
// BDEF1:
// - boneIndex(boneIndexSize)
// BDEF2:
// - boneIndex[2](boneIndexSize each)
// - float weight0 // weight1 = 1 - weight0
// BDEF4:
// - boneIndex4
// - float weight[4]
// SDEF:
// - boneIndex2
// - float weight0
// - float3 C, float3 R0, float3 R1
// QDEF (2.1):
// - boneIndex4
// - float weight[4]
// - float edgeScale // 边缘放大系数（轮廓描边用）

// 4) Indices (triangle list)
// - int32 indexCount // number of vertex indices (multiple of 3)
// - vertexIndex[indexCount] // each uses vertexIndexSize (signed)

// 5) Textures (paths, relative; string format same as above)
// - int32 textureCount
// - repeat textureCount: string texturePath

// 6) Materials
// - int32 materialCount
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