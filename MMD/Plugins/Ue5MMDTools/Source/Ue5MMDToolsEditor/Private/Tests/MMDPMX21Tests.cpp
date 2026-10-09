#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "TPMXParser.h"

namespace MMDPMX21Tests
{
    template <typename T>
    void Write(FMemoryWriter& Writer, T Value)
    {
        Writer << Value;
    }

    void WriteText(FMemoryWriter& Writer, const TCHAR* Text)
    {
        FTCHARToUTF8 UTF8(Text);
        Write(Writer, int32(UTF8.Length()));
        Writer.Serialize(const_cast<ANSICHAR*>(UTF8.Get()), UTF8.Length());
    }

    void WriteVector(FMemoryWriter& Writer, float X, float Y, float Z)
    {
        Write(Writer, X); Write(Writer, Y); Write(Writer, Z);
    }

    void WriteIndex(FMemoryWriter& Writer, int32 Value, int32 Width)
    {
        if (Width == 1) Write(Writer, int8(Value));
        else if (Width == 2) Write(Writer, int16(Value));
        else Write(Writer, Value);
    }

    struct FFixture
    {
        TArray<uint8> Bytes;
        int32 SoftBodyCountOffset = 0;
        int32 AnchorCountOffset = 0;
        int32 PinVertexCountOffset = 0;
    };

    FFixture MakeFixture(int32 MaterialIndexWidth)
    {
        FFixture Fixture;
        FMemoryWriter Writer(Fixture.Bytes);
        char Signature[] = "PMX ";
        Writer.Serialize(Signature, 4);
        Write(Writer, 2.1f);
        Write(Writer, uint8(8)); Write(Writer, uint8(1)); Write(Writer, uint8(0));
        Write(Writer, uint8(1)); Write(Writer, uint8(1)); Write(Writer, uint8(MaterialIndexWidth));
        Write(Writer, uint8(1)); Write(Writer, uint8(1)); Write(Writer, uint8(1));
        WriteText(Writer, TEXT("SoftBodyFixture")); WriteText(Writer, TEXT(""));
        WriteText(Writer, TEXT("")); WriteText(Writer, TEXT(""));

        // 一个顶点、材质、骨骼与刚体，锚点和 pin 均引用有效记录。
        Write(Writer, int32(1));
        WriteVector(Writer, 0, 0, 0); WriteVector(Writer, 0, 1, 0);
        Write(Writer, 0.0f); Write(Writer, 0.0f);
        Write(Writer, uint8(0)); WriteIndex(Writer, 0, 1); Write(Writer, 1.0f);
        Write(Writer, int32(0)); Write(Writer, int32(0));
        Write(Writer, int32(1));
        WriteText(Writer, TEXT("Material")); WriteText(Writer, TEXT(""));
        WriteVector(Writer, 1, 1, 1); Write(Writer, 1.0f);
        WriteVector(Writer, 0, 0, 0); Write(Writer, 0.0f); WriteVector(Writer, 0, 0, 0);
        Write(Writer, uint8(0)); WriteVector(Writer, 0, 0, 0); Write(Writer, 1.0f); Write(Writer, 1.0f);
        WriteIndex(Writer, -1, 1); WriteIndex(Writer, -1, 1);
        Write(Writer, uint8(0)); Write(Writer, uint8(1)); Write(Writer, uint8(0));
        WriteText(Writer, TEXT("")); Write(Writer, int32(0));
        Write(Writer, int32(1));
        WriteText(Writer, TEXT("Bone")); WriteText(Writer, TEXT(""));
        WriteVector(Writer, 0, 0, 0); WriteIndex(Writer, -1, 1);
        Write(Writer, int32(0)); Write(Writer, uint16(0)); WriteVector(Writer, 0, 1, 0);
        Write(Writer, int32(0)); Write(Writer, int32(0));
        Write(Writer, int32(1));
        WriteText(Writer, TEXT("Rigid")); WriteText(Writer, TEXT(""));
        WriteIndex(Writer, 0, 1); Write(Writer, uint8(0)); Write(Writer, uint16(0xffff)); Write(Writer, uint8(0));
        WriteVector(Writer, 1, 1, 1); WriteVector(Writer, 0, 0, 0); WriteVector(Writer, 0, 0, 0);
        Write(Writer, 1.0f);
        for (int32 Index = 0; Index < 4; ++Index) Write(Writer, 0.0f);
        Write(Writer, uint8(0)); Write(Writer, int32(0));

        Fixture.SoftBodyCountOffset = int32(Writer.Tell());
        Write(Writer, int32(1));
        WriteText(Writer, TEXT("Soft")); WriteText(Writer, TEXT(""));
        Write(Writer, uint8(0)); WriteIndex(Writer, 0, MaterialIndexWidth);
        Write(Writer, uint8(5)); Write(Writer, uint16(0x5a3c)); Write(Writer, uint8(7));
        Write(Writer, int32(123456)); Write(Writer, int32(9));
        Write(Writer, 1.75f); Write(Writer, 0.25f); Write(Writer, int32(3));
        for (int32 Index = 0; Index < 12; ++Index) Write(Writer, float(Index + 1) / 16.0f);
        for (int32 Index = 0; Index < 6; ++Index) Write(Writer, float(Index + 1) / 8.0f);
        for (int32 Index = 0; Index < 4; ++Index) Write(Writer, int32(Index + 2));
        Write(Writer, 0.125f); Write(Writer, 0.375f); Write(Writer, 0.625f);
        Fixture.AnchorCountOffset = int32(Writer.Tell());
        Write(Writer, int32(1)); WriteIndex(Writer, 0, 1); WriteIndex(Writer, 0, 1); Write(Writer, uint8(1));
        Fixture.PinVertexCountOffset = int32(Writer.Tell());
        Write(Writer, int32(1)); WriteIndex(Writer, 0, 1);
        return Fixture;
    }

    bool SaveFixture(const FString& Name, const TArray<uint8>& Bytes, FString& OutPath)
    {
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/Fixtures"));
        OutPath = Directory / Name;
        return IFileManager::Get().MakeDirectory(*Directory, true) && FFileHelper::SaveArrayToFile(Bytes, *OutPath);
    }
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMMDPMX21SoftBodyTest, "Ue5MMDTools.Import.PMX21SoftBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

void FMMDPMX21SoftBodyTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    for (const int32 Width : {1, 2, 4})
    {
        Names.Add(FString::Printf(TEXT("MaterialIndex%dByte"), Width));
        Commands.Add(FString::FromInt(Width));
    }
}

bool FMMDPMX21SoftBodyTest::RunTest(const FString& Parameters)
{
    const int32 Width = FCString::Atoi(*Parameters);
    const MMDPMX21Tests::FFixture Fixture = MMDPMX21Tests::MakeFixture(Width);
    FString Path;
    if (!TestTrue(TEXT("写入 PMX2.1 fixture"), MMDPMX21Tests::SaveFixture(TEXT("SoftBody") + Parameters + TEXT(".pmx"), Fixture.Bytes, Path))) return false;
    TPMXParser Parser;
    if (!TestTrue(TEXT("完整 PMX2.1 解析"), Parser.ParsePMXFile(Path))
        || !TestEqual(TEXT("SoftBody 数量"), Parser.PMXInfo.ModelSoftBodies.Num(), 1)) return false;
    const FPMXSoftBody& Body = Parser.PMXInfo.ModelSoftBodies[0];
    TestEqual(TEXT("材质索引按 header 宽度读取"), Body.MaterialIndex, 0);
    TestEqual(TEXT("组字节保持对齐"), Body.Group, uint8(5));
    TestEqual(TEXT("碰撞 mask 保持对齐"), Body.CollisionMask, uint16(0x5a3c));
    TestEqual(TEXT("B-link 保持对齐"), Body.BLinkDistance, 123456);
    TestEqual(TEXT("簇数量"), Body.ClusterCount, 9);
    TestEqual(TEXT("浮点总质量"), Body.TotalMass, 1.75f);
    TestEqual(TEXT("浮点 Margin"), Body.Margin, 0.25f);
    TestEqual(TEXT("Config 首参数"), Body.VCF, 0.0625f);
    TestEqual(TEXT("Config 尾参数"), Body.AHR, 0.75f);
    TestEqual(TEXT("Cluster 首参数"), Body.SRHR_CL, 0.125f);
    TestEqual(TEXT("Cluster 尾参数"), Body.SS_SPLT_CL, 0.75f);
    TestEqual(TEXT("完整迭代参数"), Body.C_IT, 5);
    TestEqual(TEXT("材质刚度"), Body.VST, 0.625f);
    if (!TestEqual(TEXT("锚点数"), Body.Anchors.Num(), 1) || !TestEqual(TEXT("Pin 数"), Body.PinVertices.Num(), 1)) return false;
    TestEqual(TEXT("锚点刚体"), Body.Anchors[0].RigidIndex, 0);
    TestEqual(TEXT("锚点顶点"), Body.Anchors[0].VertexIndex, 0);
    TestEqual(TEXT("锚点 near"), Body.Anchors[0].NearMode, uint8(1));
    TestEqual(TEXT("Pin 顶点"), Body.PinVertices[0], 0);

    // 损坏计数只触发已限制数量的读取路径，不能进入巨额分配。
    AddExpectedError(TEXT("PMX: invalid ModelSoftBodyCount count"), EAutomationExpectedErrorFlags::Contains, 1, false);
    AddExpectedError(TEXT("PMX: invalid ModelSoftBodies[i].AnchorCount count"), EAutomationExpectedErrorFlags::Contains, 1, false);
    AddExpectedError(TEXT("PMX: invalid ModelSoftBodies[i].PinVertexCount count"), EAutomationExpectedErrorFlags::Contains, 2, false);
    AddExpectedError(TEXT("ParsePMXFile: Failed to read PMX soft bodies"), EAutomationExpectedErrorFlags::Contains, 4, false);
    for (const int32 Offset : {Fixture.SoftBodyCountOffset, Fixture.AnchorCountOffset, Fixture.PinVertexCountOffset})
    {
        TArray<uint8> Corrupt = Fixture.Bytes;
        const int32 Negative = -1;
        FMemory::Memcpy(Corrupt.GetData() + Offset, &Negative, sizeof(Negative));
        TestTrue(TEXT("写入损坏 SoftBody fixture"), MMDPMX21Tests::SaveFixture(TEXT("SoftBodyNegative") + FString::FromInt(Offset) + TEXT(".pmx"), Corrupt, Path));
        TestFalse(TEXT("负 SoftBody 子计数拒绝"), Parser.ParsePMXFile(Path));
    }
    TArray<uint8> Truncated = Fixture.Bytes;
    Truncated.Pop();
    TestTrue(TEXT("写入截断 Pin fixture"), MMDPMX21Tests::SaveFixture(TEXT("SoftBodyTruncated") + Parameters + TEXT(".pmx"), Truncated, Path));
    TestFalse(TEXT("截断 Pin 拒绝"), Parser.ParsePMXFile(Path));
    return true;
}

#endif
