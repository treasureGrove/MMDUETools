#include "TPMXParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"
bool ReadCharArray(FMemoryReader &Reader, FString &OutString, PMXDatas &PMXInfo);
bool ReadPMXGlobals(FMemoryReader &Reader, TPMXGlobals &OutGlobals);
bool ReadPMXVertex(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXIndices(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXTexturePath(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXMaterial(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXBones(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXMorphs(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXFrames(FMemoryReader& Reader, PMXDatas& PMXInfo);
bool ReadPMXRigid(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXJoint(FMemoryReader &Reader, PMXDatas &PMXInfo);
bool ReadPMXSoftBody(FMemoryReader& Reader, PMXDatas& PMXInfo);
bool TPMXParser::ParsePMXFile(const FString &FilePath)
{
    // 简单重置 - 不使用复杂的赋值操作
    PMXInfo.Version = 0.0f;
    FMemory::Memzero(PMXInfo.Sig, 4);
    
    UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: 开始解析"));
    
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

    if (ReadCharArray(PMXReader, PMXInfo.ModelNameJP, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Name: %s"), *PMXInfo.ModelNameJP);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Name"));
    }

    if (ReadCharArray(PMXReader, PMXInfo.ModelNameEN, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Name EN: %s"), *PMXInfo.ModelNameEN);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Name EN"));
    }

    if (ReadCharArray(PMXReader, PMXInfo.ModelCommentJP, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Model Comment: %s"), *PMXInfo.ModelCommentJP);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read Model Comment"));
    }

    if (ReadCharArray(PMXReader, PMXInfo.ModelCommentEN, PMXInfo))
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
    if (ReadPMXTexturePath(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX texture paths"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX texture paths"));
        // LogTemp: Texture[0]: tex/body00_Miku.png
    }

    if (ReadPMXMaterial(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX materials"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX materials"));
    }
    if (ReadPMXBones(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX bones"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX bones"));
    }
    if (ReadPMXMorphs(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX morphs"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX morphs"));
    }
    if(ReadPMXFrames(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX frames"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX frames"));
    }
    if(ReadPMXRigid(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX rigids"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX rigids"));
    }
    if(ReadPMXJoint(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX joints"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX joints"));
    }
    if (ReadPMXSoftBody(PMXReader, PMXInfo))
    {
        UE_LOG(LogTemp, Log, TEXT("ParsePMXFile: Successfully read PMX soft bodies"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ParsePMXFile: Failed to read PMX soft bodies"));
    }


    UE_LOG(LogTemp, Log, TEXT("=== PMX Parse Summary ==="));
    UE_LOG(LogTemp, Log, TEXT("Vertices: %d"), PMXInfo.ModelVertexCount);
    UE_LOG(LogTemp, Log, TEXT("Indices: %d"), PMXInfo.ModelIndicesCount);
    UE_LOG(LogTemp, Log, TEXT("Textures: %d"), PMXInfo.ModelTextureCount);
    UE_LOG(LogTemp, Log, TEXT("Materials: %d"), PMXInfo.ModelMaterialCount);
    UE_LOG(LogTemp, Log, TEXT("Bones: %d"), PMXInfo.ModelBoneCount);
    UE_LOG(LogTemp, Log, TEXT("Morphs: %d"), PMXInfo.ModelMorphCount);
    UE_LOG(LogTemp, Log, TEXT("Frames: %d"), PMXInfo.ModelFrameCount);
    UE_LOG(LogTemp, Log, TEXT("Rigids: %d"), PMXInfo.ModelRigidCount);
    UE_LOG(LogTemp, Log, TEXT("Joints: %d"), PMXInfo.ModelJointCount);
    UE_LOG(LogTemp, Log, TEXT("Soft Bodies: %d"), PMXInfo.ModelSoftBodyCount);
    return true;
}
bool ReadCharArray(FMemoryReader &Reader, FString &OutString, PMXDatas &PMXInfo)
{
    // 记录当前位置用于调试
    int64 StartPos = Reader.Tell();

    int32 StringLength = 0;
    Reader << StringLength;

    const int64 Remaining = Reader.TotalSize() - Reader.Tell();

    // 详细的边界检查和调试信息
    UE_LOG(LogTemp, Warning, TEXT("ReadCharArray: pos=%lld, length=%d, remaining=%lld, encoding=%d"),
           StartPos, StringLength, Remaining, PMXInfo.PMXGlobals.TextEncoding);

    // 严格的边界检查
    if (StringLength < 0)
    {
        UE_LOG(LogTemp, Error, TEXT("ReadCharArray: negative length %d at position %lld"), StringLength, StartPos);
        OutString = TEXT("");
        return false;
    }

    if (StringLength > Remaining)
    {
        UE_LOG(LogTemp, Error, TEXT("ReadCharArray: length %d exceeds remaining %lld at position %lld"),
               StringLength, Remaining, StartPos);
        OutString = TEXT("");
        return false;
    }

    if (StringLength > 32768) // 合理的最大字符串长度限制
    {
        UE_LOG(LogTemp, Error, TEXT("ReadCharArray: suspiciously large length %d at position %lld"),
               StringLength, StartPos);
        OutString = TEXT("");
        return false;
    }

    if (StringLength == 0)
    {
        OutString = TEXT("");
        return true;
    }

    // 安全的数据读取
    TArray<uint8> RawData;
    RawData.SetNumZeroed(StringLength + 4); // 额外的安全边界
    Reader.Serialize(RawData.GetData(), StringLength);

    // 根据编码类型安全地转换字符串
    if (PMXInfo.PMXGlobals.TextEncoding == 0) // UTF-16LE
    {
        if (StringLength % 2 != 0)
        {
            UE_LOG(LogTemp, Error, TEXT("ReadCharArray: UTF-16 odd length %d at position %lld"),
                   StringLength, StartPos);
            OutString = TEXT("");
            return false;
        }

        int32 CharCount = StringLength / 2;
        if (CharCount == 0)
        {
            OutString = TEXT("");
            return true;
        }

        // 最安全的方法：逐字节构建字符串
        FString Result;
        Result.Reserve(CharCount);

        for (int32 i = 0; i < CharCount; i++)
        {
            // 安全地读取每个UTF-16字符
            if (i * 2 + 1 < StringLength)
            {
                uint16 Char16 = (uint16)RawData[i * 2] | ((uint16)RawData[i * 2 + 1] << 8);
                if (Char16 == 0)
                    break; // 遇到null终止符就停止

                // 直接append单个字符，避免构造函数问题
                TCHAR WideChar = (TCHAR)Char16;
                if (WideChar >= 32 || WideChar == 9 || WideChar == 10 || WideChar == 13) // 可打印字符或基本空白符
                {
                    Result.AppendChar(WideChar);
                }
            }
        }
        OutString = Result;
    }
    else // UTF-8
    {
        // UTF-8处理：使用UE的转换函数
        TArray<char> UTF8Data;
        UTF8Data.SetNumZeroed(StringLength + 1);

        // 安全地复制数据
        for (int32 i = 0; i < StringLength; i++)
        {
            UTF8Data[i] = (char)RawData[i];
        }

        // 使用UE5的安全转换
        FString Result = FString(UTF8_TO_TCHAR(UTF8Data.GetData()));
        OutString = Result;
    }

    UE_LOG(LogTemp, Warning, TEXT("ReadCharArray: successfully read '%s' (length: %d)"),
           *OutString.Left(20), StringLength); // 只显示前20个字符

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
    PMXInfo.ModelVertices.SetNum(PMXInfo.ModelVertexCount);
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
    // 先确保还有 4 字节可读（textureCount）
    const int64 remain0 = (int64)Reader.TotalSize() - (int64)Reader.Tell();
    if (remain0 < (int64)sizeof(int32))
    {
        UE_LOG(LogTemp, Error, TEXT("Textures: not enough bytes for count. remain=%lld"), remain0);
        return false;
    }

    Reader << PMXInfo.ModelTextureCount;
    UE_LOG(LogTemp, Log, TEXT("Textures: count=%d at offset=%lld"), PMXInfo.ModelTextureCount, (int64)Reader.Tell());
    // 合理性校验：允许 0，过大直接判错（防止上游错位导致巨值）
    if (PMXInfo.ModelTextureCount < 0 || PMXInfo.ModelTextureCount > 10000)
    {
        UE_LOG(LogTemp, Error, TEXT("Textures: invalid count=%d (tell=%lld)"), PMXInfo.ModelTextureCount, (int64)Reader.Tell());
        return false;
    }

    PMXInfo.ModelTextureCount = PMXInfo.ModelTextureCount;
    PMXInfo.ModelTexturePaths.SetNum(PMXInfo.ModelTextureCount);

    for (int32 i = 0; i < PMXInfo.ModelTextureCount; ++i)
    {
        const int64 before = (int64)Reader.Tell();
        if (!ReadCharArray(Reader, PMXInfo.ModelTexturePaths[i], PMXInfo))
        {
            const int64 remain = (int64)Reader.TotalSize() - before;
            UE_LOG(LogTemp, Error, TEXT("Textures: read path %d failed at offset=%lld (remain=%lld)"), i, before, remain);
            return false;
        }

        // 可选：统一路径分隔符
        PMXInfo.ModelTexturePaths[i].ReplaceInline(TEXT("\\"), TEXT("/"));

        // 可选：打印少量样本
        if (i < 3)
        {
            UE_LOG(LogTemp, Log, TEXT("Texture[%d]: %s"), i, *PMXInfo.ModelTexturePaths[i]);
        }
    }
    return true;
}
bool ReadPMXMaterial(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    Reader << PMXInfo.ModelMaterialCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXMaterial: MaterialCount=%d at position=%lld"),
           PMXInfo.ModelMaterialCount, Reader.Tell());

    PMXInfo.ModelMaterials.SetNum(PMXInfo.ModelMaterialCount);

    for (int32 i = 0; i < PMXInfo.ModelMaterialCount; i++)
    {
        int64 MaterialStartPos = Reader.Tell();
        UE_LOG(LogTemp, Verbose, TEXT("Reading material %d/%d at position %lld"),
               i + 1, PMXInfo.ModelMaterialCount, MaterialStartPos);

        ReadCharArray(Reader, PMXInfo.ModelMaterials[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelMaterials[i].NameEN, PMXInfo);

        float dr, dg, db, da;
        Reader << dr << dg << db << da;
        PMXInfo.ModelMaterials[i].DiffuseColor = FVector4(dr, dg, db, da);

        float sr, sg, sb;
        Reader << sr << sg << sb;
        PMXInfo.ModelMaterials[i].SpecularColor = FVector(sr, sg, sb);

        Reader << PMXInfo.ModelMaterials[i].SpecularPower;
        float ar, ag, ab;
        Reader << ar << ag << ab;
        PMXInfo.ModelMaterials[i].AmbientColor = FVector(ar, ag, ab);

        Reader << PMXInfo.ModelMaterials[i].DrawFlags;
        float er, eg, eb, ea;
        Reader << er << eg << eb << ea;
        PMXInfo.ModelMaterials[i].EdgeColor = FVector4(er, eg, eb, ea);

        Reader << PMXInfo.ModelMaterials[i].EdgeSize;
        PMXInfo.ModelMaterials[i].TextureIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.TextureIndexSize);
        PMXInfo.ModelMaterials[i].SphereTextureIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.TextureIndexSize);
        Reader << PMXInfo.ModelMaterials[i].SphereMode;

        Reader << PMXInfo.ModelMaterials[i].UseSharedToon;

        // 修复：正确处理 Toon 数据类型
        if (PMXInfo.ModelMaterials[i].UseSharedToon == 0)
        {
            uint8 ToonNumber; // 应该是 uint8，不是 int32
            Reader << ToonNumber;
            PMXInfo.ModelMaterials[i].ToonNumber = ToonNumber;
        }
        else
        {
            PMXInfo.ModelMaterials[i].ToonTextureIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.TextureIndexSize);
        }

        ReadCharArray(Reader, PMXInfo.ModelMaterials[i].Memo, PMXInfo);
        Reader << PMXInfo.ModelMaterials[i].FaceIndexCount;

        // 添加位置检查
        int64 MaterialEndPos = Reader.Tell();
        UE_LOG(LogTemp, Verbose, TEXT("Material %d finished at position %lld (size: %lld bytes)"),
               i, MaterialEndPos, MaterialEndPos - MaterialStartPos);

        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Material[%d]: Name=%s TexIdx=%d SphereIdx=%d SphereMode=%d SharedToon=%d ToonNum=%d FaceCount=%d"),
                   i,
                   *PMXInfo.ModelMaterials[i].NameJP,
                   PMXInfo.ModelMaterials[i].TextureIndex,
                   PMXInfo.ModelMaterials[i].SphereTextureIndex,
                   PMXInfo.ModelMaterials[i].SphereMode,
                   PMXInfo.ModelMaterials[i].UseSharedToon,
                   PMXInfo.ModelMaterials[i].ToonNumber,
                   PMXInfo.ModelMaterials[i].FaceIndexCount);
        }
    }

    UE_LOG(LogTemp, Warning, TEXT("ReadPMXMaterial: Finished at position=%lld"), Reader.Tell());
    return true;
}
bool ReadPMXBones(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    Reader << PMXInfo.ModelBoneCount;
    PMXInfo.ModelBones.SetNum(PMXInfo.ModelBoneCount);

    for (int32 i = 0; i < PMXInfo.ModelBoneCount; i++)
    {
        ReadCharArray(Reader, PMXInfo.ModelBones[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelBones[i].NameEN, PMXInfo);
        float px, py, pz;
        Reader << px << py << pz;
        PMXInfo.ModelBones[i].Position = FVector(px, py, pz);
        PMXInfo.ModelBones[i].ParentBoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);

        Reader << PMXInfo.ModelBones[i].DeformLayer;
        Reader << PMXInfo.ModelBones[i].Flags;
        if (PMXInfo.ModelBones[i].Flags & 0x0001) // ConnectionToChildIsBoneIndex
        {
            PMXInfo.ModelBones[i].TailBoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
        }
        else
        {
            float tx, ty, tz;
            Reader << tx << ty << tz;
            PMXInfo.ModelBones[i].TailOffset = FVector(tx, ty, tz);
        }
        if (PMXInfo.ModelBones[i].Flags & (0x0100 | 0x0200)) // InheritRotate or InheritTranslate
        {
            PMXInfo.ModelBones[i].InheritParentIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            Reader << PMXInfo.ModelBones[i].InheritInfluence;
        }
        if (PMXInfo.ModelBones[i].Flags & 0x0400) // FixedAxis
        {
            float ax, ay, az;
            Reader << ax << ay << az;
            PMXInfo.ModelBones[i].Axis = FVector(ax, ay, az);
        }
        if (PMXInfo.ModelBones[i].Flags & 0x0800) // LocalAxis
        {
            float x1, x2, x3, z1, z2, z3;
            Reader << x1 << x2 << x3 << z1 << z2 << z3;
            PMXInfo.ModelBones[i].LocalAxisX = FVector(x1, x2, x3);
            PMXInfo.ModelBones[i].LocalAxisZ = FVector(z1, z2, z3);
        }
        if (PMXInfo.ModelBones[i].Flags & 0x2000) // ExternalParent
        {
            Reader << PMXInfo.ModelBones[i].ExternalParentKey;
        }
        if (PMXInfo.ModelBones[i].Flags & 0x0020) // IK
        {
            PMXInfo.ModelBones[i].IKTargetBoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            Reader << PMXInfo.ModelBones[i].IKLoopCount;
            Reader << PMXInfo.ModelBones[i].IKLimitAngle;
            Reader << PMXInfo.ModelBones[i].IKLinkCount;

            PMXInfo.ModelBones[i].IKLinks.SetNum(PMXInfo.ModelBones[i].IKLinkCount);

            for (int32 j = 0; j < PMXInfo.ModelBones[i].IKLinkCount; j++)
            {
                PMXInfo.ModelBones[i].IKLinks[j].LinkBoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
                Reader << PMXInfo.ModelBones[i].IKLinks[j].HasLimit;
                if (PMXInfo.ModelBones[i].IKLinks[j].HasLimit)
                {
                    float lx, ly, lz, ux, uy, uz;
                    Reader << lx << ly << lz << ux << uy << uz;
                    PMXInfo.ModelBones[i].IKLinks[j].LowerLimit = FVector(lx, ly, lz);
                    PMXInfo.ModelBones[i].IKLinks[j].UpperLimit = FVector(ux, uy, uz);
                }
            }
        }
        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Bone[%d]: Name=%s Pos=(%.4f,%.4f,%.4f) Parent=%d Layer=%d Flags=0x%04X"),
                   i,
                   *PMXInfo.ModelBones[i].NameJP,
                   PMXInfo.ModelBones[i].Position.X, PMXInfo.ModelBones[i].Position.Y, PMXInfo.ModelBones[i].Position.Z,
                   PMXInfo.ModelBones[i].ParentBoneIndex,
                   PMXInfo.ModelBones[i].DeformLayer,
                   PMXInfo.ModelBones[i].Flags);
        }
    }
    return true;
}

bool ReadPMXMorphs(FMemoryReader &Reader, PMXDatas &PMXInfo)
{
    Reader << PMXInfo.ModelMorphCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXMorphs: MorphCount=%d"),
           PMXInfo.ModelMorphCount);
    PMXInfo.ModelMorphs.SetNum(PMXInfo.ModelMorphCount);
    for (int32 i = 0; i < PMXInfo.ModelMorphCount; i++)
    {
        ReadCharArray(Reader, PMXInfo.ModelMorphs[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelMorphs[i].NameEN, PMXInfo);
        Reader << PMXInfo.ModelMorphs[i].Panel;
        Reader << PMXInfo.ModelMorphs[i].MorphType;
        Reader << PMXInfo.ModelMorphs[i].ElementCount;
        if (PMXInfo.ModelMorphs[i].ElementCount < 0 || PMXInfo.ModelMorphs[i].ElementCount > 50000)
        {
            UE_LOG(LogTemp, Error, TEXT("ReadPMXMorphs: Invalid element count %d for morph %d"),
                   PMXInfo.ModelMorphs[i].ElementCount, i);
            return false;
        }
        switch (PMXInfo.ModelMorphs[i].MorphType)
        {
        case 0: // Group
            PMXInfo.ModelMorphs[i].Groups.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {

                PMXInfo.ModelMorphs[i].Groups[j].MorphIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.MorphIndexSize);
                Reader << PMXInfo.ModelMorphs[i].Groups[j].Weight;
            }
            /* code */
            break;
        case 1: // Vertex
            PMXInfo.ModelMorphs[i].Vertices.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].Vertices[j].VertexIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.VertexIndexSize);
                float dx, dy, dz;
                Reader << dx << dy << dz;
                PMXInfo.ModelMorphs[i].Vertices[j].PositionOffset = FVector(dx, dy, dz);
            }
            break;
        case 2: // bone
            PMXInfo.ModelMorphs[i].Bones.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].Bones[j].BoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
                float tr, tg, tb;
                Reader << tr << tg << tb;
                PMXInfo.ModelMorphs[i].Bones[j].Translation = FVector(tr, tg, tb);
                float rx, ry, rz, rw;
                Reader << rx << ry << rz << rw;
                PMXInfo.ModelMorphs[i].Bones[j].RotationQuat = FQuat(rx, ry, rz, rw);
            }
            break;
        case 3: // UV
        case 4: // AdditionalUV1
        case 5: // AdditionalUV2
        case 6: // AdditionalUV3
        case 7: // AdditionalUV4
            PMXInfo.ModelMorphs[i].UVs.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].UVs[j].VertexIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.VertexIndexSize);
                float x, y, z, w;
                Reader << x << y << z << w;
                PMXInfo.ModelMorphs[i].UVs[j].UVOffset = FVector4(x, y, z, w);
            }
            break;
        case 8: // Material
            PMXInfo.ModelMorphs[i].Materials.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].Materials[j].MaterialIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.MaterialIndexSize);
                Reader << PMXInfo.ModelMorphs[i].Materials[j].CalcMode;
                float dr, dg, db, da;
                Reader << dr << dg << db << da;
                PMXInfo.ModelMorphs[i].Materials[j].Diffuse = FVector4(dr, dg, db, da);
                float sr, sg, sb;
                Reader << sr << sg << sb;
                PMXInfo.ModelMorphs[i].Materials[j].Specular = FVector(sr, sg, sb);
                Reader << PMXInfo.ModelMorphs[i].Materials[j].SpecularPower;
                float ar, ag, ab;
                Reader << ar << ag << ab;
                PMXInfo.ModelMorphs[i].Materials[j].Ambient = FVector(ar, ag, ab);
                float er, eg, eb, ea;
                Reader << er << eg << eb << ea;
                PMXInfo.ModelMorphs[i].Materials[j].EdgeColor = FVector4(er, eg, eb, ea);
                Reader << PMXInfo.ModelMorphs[i].Materials[j].EdgeSize;
                float tr, tg, tb, ta;
                Reader << tr << tg << tb << ta;
                PMXInfo.ModelMorphs[i].Materials[j].TextureTint = FVector4(tr, tg, tb, ta);
                float str, stg, stb, sta;
                Reader << str << stg << stb << sta;
                PMXInfo.ModelMorphs[i].Materials[j].SphereTextureTint = FVector4(str, stg, stb, sta);
                float ttr, ttg, ttb, tta;
                Reader << ttr << ttg << ttb << tta;
                PMXInfo.ModelMorphs[i].Materials[j].ToonTextureTint = FVector4(ttr, ttg, ttb, tta);
            }
            break;
        case 9: // Flip
            PMXInfo.ModelMorphs[i].Flips.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].Flips[j].MorphIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.MorphIndexSize);
                Reader << PMXInfo.ModelMorphs[i].Flips[j].Weight;
            }
            break;
        case 10: // Impulse
            PMXInfo.ModelMorphs[i].Impulses.SetNum(PMXInfo.ModelMorphs[i].ElementCount);
            for (int32 j = 0; j < PMXInfo.ModelMorphs[i].ElementCount; j++)
            {
                PMXInfo.ModelMorphs[i].Impulses[j].RigidIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.RigidBodyIndexSize);
                Reader << PMXInfo.ModelMorphs[i].Impulses[j].IsLocal;
                float x, y, z;
                Reader << x << y << z;
                PMXInfo.ModelMorphs[i].Impulses[j].Velocity = FVector(x, y, z);
                float rx, ry, rz;
                Reader << rx << ry << rz;
                PMXInfo.ModelMorphs[i].Impulses[j].Torque = FVector(rx, ry, rz);
            }
            break;
        default:
            // ✅ 添加这个关键的 default 分支！
            UE_LOG(LogTemp, Error, TEXT("ReadPMXMorphs: Unknown morph type %d for morph %d"),
                   PMXInfo.ModelMorphs[i].MorphType, i);
            return false;
        }
        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Morph[%d]: Name=%s Type=%d ElementCount=%d"),
                   i,
                   *PMXInfo.ModelMorphs[i].NameJP,
                   PMXInfo.ModelMorphs[i].MorphType,
                   PMXInfo.ModelMorphs[i].ElementCount);
        }
    }

    return true;
}

bool ReadPMXFrames(FMemoryReader& Reader, PMXDatas& PMXInfo){
    Reader<< PMXInfo.ModelFrameCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXFrames: FrameCount=%d"),
           PMXInfo.ModelFrameCount);
    PMXInfo.ModelFrames.SetNum(PMXInfo.ModelFrameCount);
    for (int32 i = 0; i < PMXInfo.ModelFrameCount; i++){
        ReadCharArray(Reader, PMXInfo.ModelFrames[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelFrames[i].NameEN, PMXInfo);
        Reader<<PMXInfo.ModelFrames[i].IsSpecial;
        Reader << PMXInfo.ModelFrames[i].ElementCount;
        if (PMXInfo.ModelFrames[i].ElementCount < 0 || PMXInfo.ModelFrames[i].ElementCount > 9999999)
        {
            UE_LOG(LogTemp, Error, TEXT("ReadPMXFrames: Invalid element count %d for frame %d"),
                   PMXInfo.ModelFrames[i].ElementCount, i);
            return false;
        }
        PMXInfo.ModelFrames[i].Elements.SetNum(PMXInfo.ModelFrames[i].ElementCount);
        for (int32 j = 0; j < PMXInfo.ModelFrames[i].ElementCount; j++){
            Reader<<PMXInfo.ModelFrames[i].Elements[j].ElemType;
            if(PMXInfo.ModelFrames[i].Elements[j].ElemType==0){
                PMXInfo.ModelFrames[i].Elements[j].ElemIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
            }else if(PMXInfo.ModelFrames[i].Elements[j].ElemType==1){
                PMXInfo.ModelFrames[i].Elements[j].ElemIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.MorphIndexSize);
            }else{
                UE_LOG(LogTemp,Error,TEXT("PMX Frame ElemType Error"));
            }
        }
        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Frame[%d]: Name=%s ElementCount=%d"),
                   i,
                   *PMXInfo.ModelFrames[i].NameJP,
                   PMXInfo.ModelFrames[i].ElementCount);
        }
    }
    return true;
}
bool ReadPMXRigid(FMemoryReader& Reader, PMXDatas& PMXInfo){
    Reader<<PMXInfo.ModelRigidCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXRigid: RigidBodiesCount=%d"),
           PMXInfo.ModelRigidCount);
    PMXInfo.ModelRigids.SetNum(PMXInfo.ModelRigidCount);
    for (int32 i = 0; i < PMXInfo.ModelRigidCount; i++){
        ReadCharArray(Reader, PMXInfo.ModelRigids[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelRigids[i].NameEN, PMXInfo);
        PMXInfo.ModelRigids[i].RelatedBoneIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.BoneIndexSize);
        Reader<<PMXInfo.ModelRigids[i].Group;
        Reader<<PMXInfo.ModelRigids[i].CollisionMask;
        Reader<<PMXInfo.ModelRigids[i].ShapeType;
        float sx,sy,sz;
        Reader<<sx<<sy<<sz;
        PMXInfo.ModelRigids[i].Size=FVector(sx,sy,sz);
        float px,py,pz;
        Reader<<px<<py<<pz;
        PMXInfo.ModelRigids[i].Position=FVector(px,py,pz);
        float rx,ry,rz;
        Reader<<rx<<ry<<rz;
        PMXInfo.ModelRigids[i].Rotation=FVector(rx,ry,rz);
        Reader<<PMXInfo.ModelRigids[i].Mass;
        Reader<<PMXInfo.ModelRigids[i].LinearDamping;
        Reader<<PMXInfo.ModelRigids[i].AngularDamping;
        Reader<<PMXInfo.ModelRigids[i].Restitution;
        Reader<<PMXInfo.ModelRigids[i].Friction;
        Reader<<PMXInfo.ModelRigids[i].PhysicsMode;

        if (i < 5)
        {
            UE_LOG(LogTemp, Warning, TEXT("Rigid[%d]: Name=%s BoneIndex=%d ShapeType=%d Size=(%.4f,%.4f,%.4f) Pos=(%.4f,%.4f,%.4f) Rot=(%.4f,%.4f,%.4f) Mass=%.4f"),
                   i,
                   *PMXInfo.ModelRigids[i].NameJP,
                   PMXInfo.ModelRigids[i].RelatedBoneIndex,
                   PMXInfo.ModelRigids[i].ShapeType,
                   PMXInfo.ModelRigids[i].Size.X,
                   PMXInfo.ModelRigids[i].Size.Y,
                   PMXInfo.ModelRigids[i].Size.Z,
                   PMXInfo.ModelRigids[i].Position.X,
                   PMXInfo.ModelRigids[i].Position.Y,
                   PMXInfo.ModelRigids[i].Position.Z,
                   PMXInfo.ModelRigids[i].Rotation.X,
                   PMXInfo.ModelRigids[i].Rotation.Y,
                   PMXInfo.ModelRigids[i].Rotation.Z,
                   PMXInfo.ModelRigids[i].Mass);
        }
    }
    return true;
}
bool ReadPMXJoint(FMemoryReader& Reader, PMXDatas& PMXInfo){
    Reader<<PMXInfo.ModelJointCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXJoint: JointsCount=%d"),
           PMXInfo.ModelJointCount);
    PMXInfo.ModelJoints.SetNum(PMXInfo.ModelJointCount);

    for (int32 i = 0; i < PMXInfo.ModelJointCount; i++){
        ReadCharArray(Reader, PMXInfo.ModelJoints[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelJoints[i].NameEN, PMXInfo);
        Reader<<PMXInfo.ModelJoints[i].JointType;
        PMXInfo.ModelJoints[i].RigidA = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.RigidBodyIndexSize);
        PMXInfo.ModelJoints[i].RigidB = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.RigidBodyIndexSize);
        float px,py,pz;
        Reader<<px<<py<<pz;
        PMXInfo.ModelJoints[i].Position=FVector(px,py,pz);
        float rx,ry,rz;
        Reader<<rx<<ry<<rz;
        PMXInfo.ModelJoints[i].Rotation=FVector(rx,ry,rz);
        float llx,lly,llz;
        Reader<<llx<<lly<<llz;
        PMXInfo.ModelJoints[i].LimitPosLower=FVector(llx,lly,llz);
        float ulx,uly,ulz;
        Reader<<ulx<<uly<<ulz;
        PMXInfo.ModelJoints[i].LimitPosUpper=FVector(ulx,uly,ulz);
        float lrx,lry,lrz;
        Reader<<lrx<<lry<<lrz;
        PMXInfo.ModelJoints[i].LimitRotLower=FVector(lrx,lry,lrz);
        float urx,ury,urz;
        Reader<<urx<<ury<<urz;
        PMXInfo.ModelJoints[i].LimitRotUpper=FVector(urx,ury,urz);
        float spx,spy,spz;
        Reader<<spx<<spy<<spz;
        PMXInfo.ModelJoints[i].SpringPos=FVector(spx,spy,spz);
        float srz,sry,srx;
        Reader<<srx<<sry<<srz;
        PMXInfo.ModelJoints[i].SpringRot=FVector(srx,sry,srz);
    }
    return true;
}
bool ReadPMXSoftBody(FMemoryReader& Reader, PMXDatas& PMXInfo){
    Reader<<PMXInfo.ModelSoftBodyCount;
    UE_LOG(LogTemp, Warning, TEXT("ReadPMXSoftBody: SoftBodiesCount=%d"),
           PMXInfo.ModelSoftBodyCount);
    PMXInfo.ModelSoftBodies.SetNum(PMXInfo.ModelSoftBodyCount);
    for (int32 i = 0; i < PMXInfo.ModelSoftBodyCount; i++){
        ReadCharArray(Reader, PMXInfo.ModelSoftBodies[i].NameJP, PMXInfo);
        ReadCharArray(Reader, PMXInfo.ModelSoftBodies[i].NameEN, PMXInfo);
        Reader<<PMXInfo.ModelSoftBodies[i].ShapeType;
        Reader<<PMXInfo.ModelSoftBodies[i].MaterialIndex;
        Reader<<PMXInfo.ModelSoftBodies[i].Group;
        Reader<<PMXInfo.ModelSoftBodies[i].CollisionMask;
        Reader<<PMXInfo.ModelSoftBodies[i].Flags;
        Reader<<PMXInfo.ModelSoftBodies[i].BLinkDistance;
        Reader<<PMXInfo.ModelSoftBodies[i].ClusterCount;
        Reader<<PMXInfo.ModelSoftBodies[i].TotalMass;
        Reader<<PMXInfo.ModelSoftBodies[i].Margin;
        Reader<<PMXInfo.ModelSoftBodies[i].AeroModel;

        Reader<<PMXInfo.ModelSoftBodies[i].VCF;
        Reader<<PMXInfo.ModelSoftBodies[i].DP;
        Reader<<PMXInfo.ModelSoftBodies[i].DG;
        Reader<<PMXInfo.ModelSoftBodies[i].LF;
        Reader<<PMXInfo.ModelSoftBodies[i].PR;
        Reader<<PMXInfo.ModelSoftBodies[i].VC;
        Reader<<PMXInfo.ModelSoftBodies[i].DF;
        Reader<<PMXInfo.ModelSoftBodies[i].MT;
        Reader<<PMXInfo.ModelSoftBodies[i].CHR;
        Reader<<PMXInfo.ModelSoftBodies[i].KHR;
        Reader<<PMXInfo.ModelSoftBodies[i].SHR;
        Reader<<PMXInfo.ModelSoftBodies[i].AHR;

        Reader<<PMXInfo.ModelSoftBodies[i].SRHR_CL;
        Reader<<PMXInfo.ModelSoftBodies[i].SKHR_CL;
        Reader<<PMXInfo.ModelSoftBodies[i].SSHR_CL;
        Reader<<PMXInfo.ModelSoftBodies[i].SR_SPLT_CL;
        Reader<<PMXInfo.ModelSoftBodies[i].SK_SPLT_CL;
        Reader<<PMXInfo.ModelSoftBodies[i].SS_SPLT_CL;

        Reader<<PMXInfo.ModelSoftBodies[i].V_IT;
        Reader<<PMXInfo.ModelSoftBodies[i].P_IT;
        Reader<<PMXInfo.ModelSoftBodies[i].D_IT;
        Reader<<PMXInfo.ModelSoftBodies[i].LST;
        Reader<<PMXInfo.ModelSoftBodies[i].AST;
        Reader<<PMXInfo.ModelSoftBodies[i].VST;

        Reader<<PMXInfo.ModelSoftBodies[i].AnchorCount;
        PMXInfo.ModelSoftBodies[i].Anchors.SetNum(PMXInfo.ModelSoftBodies[i].AnchorCount);
        for (int32 j = 0; j < PMXInfo.ModelSoftBodies[i].AnchorCount; j++){
            PMXInfo.ModelSoftBodies[i].Anchors[j].RigidIndex = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.RigidBodyIndexSize);
        }
        Reader<<PMXInfo.ModelSoftBodies[i].PinVertexCount;
        PMXInfo.ModelSoftBodies[i].PinVertices.SetNum(PMXInfo.ModelSoftBodies[i].PinVertexCount);
        for (int32 j = 0; j < PMXInfo.ModelSoftBodies[i].PinVertexCount; j++){
            PMXInfo.ModelSoftBodies[i].PinVertices[j] = ReadGlobalIndex(Reader, PMXInfo.PMXGlobals.VertexIndexSize);
        }

    }
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