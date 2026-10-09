#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MMDPhysicsSimulator.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "UObject/StrongObjectPtr.h"

namespace MMDImportTests
{
    void GetCorpusTests(const TCHAR* Extension, TArray<FString>& Names, TArray<FString>& Commands)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
        if (!Plugin.IsValid())
        {
            Names.Add(TEXT("PluginMissing"));
            Commands.Add(FString());
            return;
        }

        const FString ResourcesPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"));
        TArray<FString> Files;
        IFileManager::Get().FindFilesRecursive(Files, *ResourcesPath, Extension, true, false);
        Files.Sort();
        if (Files.IsEmpty())
        {
            Names.Add(TEXT("SamplesMissing"));
            Commands.Add(FString());
            return;
        }

        for (const FString& File : Files)
        {
            FString RelativePath = File;
            FPaths::MakePathRelativeTo(RelativePath, *(ResourcesPath / TEXT("")));
            Names.Add(RelativePath.Replace(TEXT("/"), TEXT(".")));
            Commands.Add(File);
        }
    }

    bool IsFinite(const FVector& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    void AppendCount(TArray<uint8>& Bytes, uint32 Count)
    {
        for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
        {
            Bytes.Add(static_cast<uint8>(Count >> (8 * ByteIndex)));
        }
    }

    TArray<uint8> EmptyVMD(bool Legacy = false)
    {
        TArray<uint8> Bytes;
        Bytes.SetNumZeroed(Legacy ? 40 : 50);
        const ANSICHAR* Magic = Legacy ? "Vocaloid Motion Data file" : "Vocaloid Motion Data 0002";
        FMemory::Memcpy(Bytes.GetData(), Magic, FCStringAnsi::Strlen(Magic));
        AppendCount(Bytes, 0);
        AppendCount(Bytes, 0);
        return Bytes;
    }

    bool WriteFixture(const FString& Name, const TArray<uint8>& Bytes, FString& Path)
    {
        // 损坏输入只写 Saved，不导入或覆盖 Content 资产。
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BackgroundValidation/Fixtures"));
        if (!IFileManager::Get().MakeDirectory(*Directory, true))
        {
            return false;
        }
        Path = FPaths::Combine(Directory, Name);
        return FFileHelper::SaveArrayToFile(Bytes, *Path);
    }

    TArray<uint8> PMXHeader()
    {
        TArray<uint8> Bytes;
        Bytes.SetNumZeroed(17);
        FMemory::Memcpy(Bytes.GetData(), "PMX ", 4);
        const float Version = 2.0f;
        FMemory::Memcpy(Bytes.GetData() + 4, &Version, sizeof(Version));
        Bytes[8] = 8;
        Bytes[9] = 1;
        for (int32 Index = 11; Index < 17; ++Index)
        {
            Bytes[Index] = 1;
        }
        return Bytes;
    }
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMMDPMXCorpusTest, "Ue5MMDTools.Import.PMXCorpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

void FMMDPMXCorpusTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    MMDImportTests::GetCorpusTests(TEXT("*.pmx"), Names, Commands);
}

bool FMMDPMXCorpusTest::RunTest(const FString& Parameters)
{
    if (!TestFalse(TEXT("PMX 样本路径非空"), Parameters.IsEmpty()))
    {
        return false;
    }

    TPMXParser Parser;
    if (!TestTrue(TEXT("真实 PMX 文件解析成功"), Parser.ParsePMXFile(Parameters)))
    {
        return false;
    }

    const PMXDatas& Data = Parser.PMXInfo;
    TestTrue(TEXT("PMX 签名"), FMemory::Memcmp(Data.Sig, "PMX ", 4) == 0);
    TestTrue(TEXT("PMX 版本为 2.0 或 2.1"), FMath::IsNearlyEqual(Data.Version, 2.0f) || FMath::IsNearlyEqual(Data.Version, 2.1f));
    TestTrue(TEXT("模型含顶点"), Data.ModelVertexCount > 0);
    TestEqual(TEXT("顶点计数一致"), Data.ModelVertices.Num(), Data.ModelVertexCount);
    TestEqual(TEXT("索引计数一致"), Data.ModelIndices.Num(), Data.ModelIndicesCount);
    TestEqual(TEXT("材质计数一致"), Data.ModelMaterials.Num(), Data.ModelMaterialCount);
    TestEqual(TEXT("骨骼计数一致"), Data.ModelBones.Num(), Data.ModelBoneCount);
    TestEqual(TEXT("Morph 计数一致"), Data.ModelMorphs.Num(), Data.ModelMorphCount);
    TestEqual(TEXT("刚体计数一致"), Data.ModelRigids.Num(), Data.ModelRigidCount);
    TestEqual(TEXT("关节计数一致"), Data.ModelJoints.Num(), Data.ModelJointCount);
    TestEqual(TEXT("三角面索引数是 3 的倍数"), Data.ModelIndicesCount % 3, 0);

    for (const int32 Index : Data.ModelIndices)
    {
        if (Index < 0 || Index >= Data.ModelVertices.Num())
        {
            AddError(FString::Printf(TEXT("顶点索引越界: %d / %d"), Index, Data.ModelVertices.Num()));
            return false;
        }
    }

    int64 MaterialIndexCount = 0;
    for (const FPMXMaterial& Material : Data.ModelMaterials)
    {
        if (Material.FaceIndexCount < 0 || Material.FaceIndexCount % 3 != 0)
        {
            AddError(FString::Printf(TEXT("材质面索引数无效: %s (%d)"), *Material.NameJP, Material.FaceIndexCount));
            return false;
        }
        MaterialIndexCount += Material.FaceIndexCount;
    }
    TestEqual(TEXT("材质段覆盖全部索引"), MaterialIndexCount, static_cast<int64>(Data.ModelIndices.Num()));

    for (int32 VertexIndex = 0; VertexIndex < Data.ModelVertices.Num(); ++VertexIndex)
    {
        const FPMXVertex& Vertex = Data.ModelVertices[VertexIndex];
        if (!MMDImportTests::IsFinite(Vertex.Position) || !MMDImportTests::IsFinite(Vertex.Normal))
        {
            AddError(FString::Printf(TEXT("顶点位置或法线含非有限值: %d"), VertexIndex));
            return false;
        }
    }
    for (const FPMXBone& Bone : Data.ModelBones)
    {
        if (Bone.ParentBoneIndex < -1 || Bone.ParentBoneIndex >= Data.ModelBones.Num())
        {
            AddError(FString::Printf(TEXT("骨骼父索引越界: %s (%d)"), *Bone.NameJP, Bone.ParentBoneIndex));
            return false;
        }
    }
    return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMMDVMDCorpusTest, "Ue5MMDTools.Import.VMDCorpus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

void FMMDVMDCorpusTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
    MMDImportTests::GetCorpusTests(TEXT("*.vmd"), Names, Commands);
}

bool FMMDVMDCorpusTest::RunTest(const FString& Parameters)
{
    if (!TestFalse(TEXT("VMD 样本路径非空"), Parameters.IsEmpty()))
    {
        return false;
    }

    TVMDParser Parser;
    if (!TestTrue(TEXT("真实 VMD 文件解析成功"), Parser.ParseVMDFile(Parameters)))
    {
        return false;
    }
    const VMDData& Data = Parser.VMDInfo;
    TestTrue(TEXT("VMD 头有效"), Data.Header.StartsWith(TEXT("Vocaloid Motion Data")));
    TestTrue(TEXT("动作包含骨骼、表情、相机或灯光轨道"),
        Data.BoneKeyframes.Num() + Data.MorphKeyframes.Num() + Data.CameraKeyframes.Num() + Data.LightKeyframes.Num() > 0);
    for (const VMDBoneKeyframe& Key : Data.BoneKeyframes)
    {
        if (Key.BoneName.IsEmpty() || !MMDImportTests::IsFinite(Key.Position) || Key.Rotation.ContainsNaN())
        {
            AddError(FString::Printf(TEXT("骨骼轨道包含无效名称或变换: %s"), *Key.BoneName));
            return false;
        }
    }
    for (const VMDMorphKeyframe& Key : Data.MorphKeyframes)
    {
        if (Key.MorphName.IsEmpty() || !FMath::IsFinite(Key.Weight))
        {
            AddError(FString::Printf(TEXT("表情轨道包含无效名称或权重: %s"), *Key.MorphName));
            return false;
        }
    }
    for (const VMDCameraKeyframe& Key : Data.CameraKeyframes)
    {
        if (!MMDImportTests::IsFinite(Key.Interest) || !MMDImportTests::IsFinite(Key.Rotation)
            || !FMath::IsFinite(Key.Distance) || !FMath::IsFinite(Key.ViewAngle))
        {
            AddError(TEXT("相机轨道包含非有限值"));
            return false;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDVMDOptionalEOFTest, "Ue5MMDTools.Import.VMDOptionalEOF",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDVMDOptionalEOFTest::RunTest(const FString& Parameters)
{
    for (int32 Legacy = 0; Legacy < 2; ++Legacy)
    {
        TArray<uint8> Bytes = MMDImportTests::EmptyVMD(Legacy != 0);
        for (int32 Boundary = 0; Boundary <= 4; ++Boundary)
        {
            FString Path;
            if (!TestTrue(TEXT("写入可选段边界样本"), MMDImportTests::WriteFixture(
                FString::Printf(TEXT("OptionalEOF_%d_%d.vmd"), Legacy, Boundary), Bytes, Path)))
            {
                return false;
            }
            TVMDParser Parser;
            TestTrue(FString::Printf(TEXT("旧版=%d 边界=%d 允许正常 EOF"), Legacy, Boundary), Parser.ParseVMDFile(Path));
            MMDImportTests::AppendCount(Bytes, 0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDVMDCorruptInputTest, "Ue5MMDTools.Import.VMDCorruptInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDVMDCorruptInputTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        FString Name;
        TArray<uint8> Bytes;
        FString ExpectedError;
    };
    TArray<FCase> Cases;
    Cases.Add({TEXT("TruncatedHeader.vmd"), {0, 1}, TEXT("VMD: Not enough bytes for HeaderMagic")});

    TArray<uint8> InvalidMagic;
    InvalidMagic.SetNumZeroed(58);
    FMemory::Memcpy(InvalidMagic.GetData(), "Invalid Motion Data", 19);
    Cases.Add({TEXT("InvalidMagic.vmd"), MoveTemp(InvalidMagic), TEXT("VMD: Unsupported header")});

    TArray<uint8> ExcessiveCount = MMDImportTests::EmptyVMD();
    FMemory::Memset(ExcessiveCount.GetData() + 50, 0xff, 4);
    Cases.Add({TEXT("ExcessiveBoneCount.vmd"), MoveTemp(ExcessiveCount), TEXT("VMD: BoneKeyframes count is unreasonably large")});

    TArray<uint8> TruncatedBone = MMDImportTests::EmptyVMD();
    TruncatedBone[50] = 1;
    Cases.Add({TEXT("TruncatedBone.vmd"), MoveTemp(TruncatedBone), TEXT("VMD: BoneKeyframes count 1 exceeds remaining bytes")});

    const TCHAR* SectionNames[] = {TEXT("CameraKeyframes"), TEXT("LightKeyframes"), TEXT("ShadowKeyframes"), TEXT("IKKeyframes")};
    for (int32 Section = 0; Section < UE_ARRAY_COUNT(SectionNames); ++Section)
    {
        TArray<uint8> Bytes = MMDImportTests::EmptyVMD();
        for (int32 Previous = 0; Previous < Section; ++Previous)
        {
            MMDImportTests::AppendCount(Bytes, 0);
        }
        MMDImportTests::AppendCount(Bytes, 1);
        Cases.Add({FString::Printf(TEXT("Truncated_%s.vmd"), SectionNames[Section]), MoveTemp(Bytes),
            FString::Printf(TEXT("VMD: %s count 1 exceeds remaining bytes"), SectionNames[Section])});
    }

    TArray<uint8> ShortOptionalHeader = MMDImportTests::EmptyVMD();
    ShortOptionalHeader.Add(0);
    Cases.Add({TEXT("ShortOptionalHeader.vmd"), MoveTemp(ShortOptionalHeader), TEXT("VMD: Trailing bytes too short for CameraKeyframes count")});

    for (const FCase& Case : Cases)
    {
        FString Path;
        if (!TestTrue(TEXT("写入损坏 VMD 样本"), MMDImportTests::WriteFixture(Case.Name, Case.Bytes, Path)))
        {
            return false;
        }
        AddExpectedMessagePlain(Case.ExpectedError, ELogVerbosity::Error);
        TVMDParser Parser;
        TestFalse(Case.Name, Parser.ParseVMDFile(Path));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDImportMissingInputTest, "Ue5MMDTools.Import.MissingInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDImportMissingInputTest::RunTest(const FString& Parameters)
{
    TPMXParser PMX;
    TVMDParser VMD;
    AddExpectedMessagePlain(TEXT("ParsePMXFile: FilePath is empty"), ELogVerbosity::Error);
    TestFalse(TEXT("空 PMX 路径被拒绝"), PMX.ParsePMXFile(FString()));
    AddExpectedMessagePlain(TEXT("VMD: File path is empty"), ELogVerbosity::Error);
    TestFalse(TEXT("空 VMD 路径被拒绝"), VMD.ParseVMDFile(FString()));
    const FString MissingPath = FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("BackgroundValidation/Fixtures"), FGuid::NewGuid().ToString() + TEXT(".missing"));
    AddExpectedMessagePlain(TEXT("ParsePMXFile: File does not exist"), ELogVerbosity::Error);
    TestFalse(TEXT("不存在的 PMX 被拒绝"), PMX.ParsePMXFile(MissingPath));
    AddExpectedMessagePlain(TEXT("VMD: File does not exist"), ELogVerbosity::Error);
    TestFalse(TEXT("不存在的 VMD 被拒绝"), VMD.ParseVMDFile(MissingPath));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPMXCorruptInputTest, "Ue5MMDTools.Import.PMXCorruptInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDPMXCorruptInputTest::RunTest(const FString& Parameters)
{
    struct FCase
    {
        FString Name;
        TArray<uint8> Bytes;
        TArray<FString> ExpectedErrors;
    };
    TArray<FCase> Cases;
    Cases.Add({TEXT("TruncatedHeader.pmx"), {0, 1}, {TEXT("ParsePMXFile: truncated header")}});

    TArray<uint8> InvalidMagic = MMDImportTests::PMXHeader();
    InvalidMagic[0] = 'X';
    Cases.Add({TEXT("InvalidMagic.pmx"), MoveTemp(InvalidMagic), {TEXT("ParsePMXFile: invalid signature or unsupported version")}});

    TArray<uint8> InvalidVersion = MMDImportTests::PMXHeader();
    const float Version = 3.0f;
    FMemory::Memcpy(InvalidVersion.GetData() + 4, &Version, sizeof(Version));
    Cases.Add({TEXT("UnsupportedVersion.pmx"), MoveTemp(InvalidVersion), {TEXT("ParsePMXFile: invalid signature or unsupported version")}});

    for (const int32 GlobalByte : {9, 10, 11})
    {
        TArray<uint8> InvalidGlobals = MMDImportTests::PMXHeader();
        InvalidGlobals[GlobalByte] = GlobalByte == 9 ? 2 : GlobalByte == 10 ? 5 : 3;
        Cases.Add({FString::Printf(TEXT("InvalidGlobals_%d.pmx"), GlobalByte), MoveTemp(InvalidGlobals),
            {TEXT("ReadPMXGlobals: invalid encoding, extra UV or index size"), TEXT("ParsePMXFile: Failed to read PMX globals")}});
    }

    TArray<uint8> NegativeString = MMDImportTests::PMXHeader();
    MMDImportTests::AppendCount(NegativeString, MAX_uint32);
    Cases.Add({TEXT("NegativeString.pmx"), MoveTemp(NegativeString),
        {TEXT("ReadCharArray: negative length"), TEXT("ParsePMXFile: Failed to read Model Name")}});

    TArray<uint8> TruncatedString = MMDImportTests::PMXHeader();
    MMDImportTests::AppendCount(TruncatedString, 4);
    Cases.Add({TEXT("TruncatedString.pmx"), MoveTemp(TruncatedString),
        {TEXT("ReadCharArray: length 4 exceeds remaining"), TEXT("ParsePMXFile: Failed to read Model Name")}});

    for (const uint32 VertexCount : {MAX_uint32, static_cast<uint32>(MAX_int32), 1u})
    {
        TArray<uint8> InvalidVertices = MMDImportTests::PMXHeader();
        for (int32 StringIndex = 0; StringIndex < 4; ++StringIndex)
        {
            MMDImportTests::AppendCount(InvalidVertices, 0);
        }
        MMDImportTests::AppendCount(InvalidVertices, VertexCount);
        Cases.Add({FString::Printf(TEXT("InvalidVertexCount_%u.pmx"), VertexCount), MoveTemp(InvalidVertices),
            {TEXT("PMX: invalid ModelVertexCount count")}});
    }

    TMap<FString, int32> ExpectedCounts;
    for (const FCase& Case : Cases)
    {
        for (const FString& Error : Case.ExpectedErrors)
        {
            ++ExpectedCounts.FindOrAdd(Error);
        }
    }
    for (const TPair<FString, int32>& Error : ExpectedCounts)
    {
        AddExpectedMessagePlain(Error.Key, ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, Error.Value);
    }
    for (const FCase& Case : Cases)
    {
        FString Path;
        if (!TestTrue(TEXT("写入损坏 PMX 样本"), MMDImportTests::WriteFixture(Case.Name, Case.Bytes, Path)))
        {
            return false;
        }
        TPMXParser Parser;
        TestFalse(Case.Name, Parser.ParsePMXFile(Path));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPMXParserReuseTest, "Ue5MMDTools.Import.PMXParserReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDPMXParserReuseTest::RunTest(const FString& Parameters)
{
    TArray<FString> Names;
    TArray<FString> Commands;
    MMDImportTests::GetCorpusTests(TEXT("*.pmx"), Names, Commands);
    if (Commands.IsEmpty() || Commands[0].IsEmpty())
    {
        AddError(TEXT("没有可重复解析的 PMX 样本"));
        return false;
    }

    TPMXParser Parser;
    if (!TestTrue(TEXT("首次真实 PMX 解析"), Parser.ParsePMXFile(Commands[0])))
    {
        return false;
    }
    const int32 FirstVertexCount = Parser.PMXInfo.ModelVertexCount;
    const int32 FirstBoneCount = Parser.PMXInfo.ModelBoneCount;
    AddExpectedMessagePlain(TEXT("ParsePMXFile: FilePath is empty"), ELogVerbosity::Error);
    TestFalse(TEXT("重复使用时拒绝空路径"), Parser.ParsePMXFile(FString()));
    TestEqual(TEXT("失败后不保留旧顶点"), Parser.PMXInfo.ModelVertices.Num(), 0);
    TestEqual(TEXT("失败后不保留旧骨骼"), Parser.PMXInfo.ModelBones.Num(), 0);
    TestEqual(TEXT("失败后不保留旧原始字节"), Parser.PMXData.Num(), 0);
    TestTrue(TEXT("失败后仍能重读真实 PMX"), Parser.ParsePMXFile(Commands[0]));
    TestEqual(TEXT("重复解析顶点计数稳定"), Parser.PMXInfo.ModelVertexCount, FirstVertexCount);
    TestEqual(TEXT("重复解析骨骼计数稳定"), Parser.PMXInfo.ModelBoneCount, FirstBoneCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPhysicsOptionalDataTest, "Ue5MMDTools.Physics.OptionalData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FMMDPhysicsOptionalDataTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
    TArray<FMMDPhysicsRigidBodyData> Rigids;
    TArray<FMMDPhysicsJointData> Joints;
    FMMDPhysicsSimulator Simulator;
    TestTrue(TEXT("无刚体模型允许初始化"), Simulator.InitializeFromPMX(Rigids, Joints, Component.Get()));
    TestTrue(TEXT("无刚体初始化状态正常"), Simulator.IsInitialized());
    TArray<FTransform> Transforms;
    Simulator.TickMMDPhysicsOnComponentTransforms(Transforms, FTransform::Identity, 1.0f / 60.0f);
    Simulator.Shutdown();
    TestFalse(TEXT("无刚体模型退出清理"), Simulator.IsInitialized());

    FMMDPhysicsRigidBodyData& Rigid = Rigids.AddDefaulted_GetRef();
    TestEqual(TEXT("默认刚体没有绑定骨骼"), Rigid.RelatedBoneIndex, -1);
    TestEqual(TEXT("默认刚体位置归零"), Rigid.ShapePosition, FVector::ZeroVector);
    Rigid.ShapeType = 0;
    Rigid.ShapeSize = FVector(1.0f);
    Rigid.PhysicsMode = 0;
    TestTrue(TEXT("独立刚体无需关节"), Simulator.InitializeFromPMX(Rigids, Joints, Component.Get()));
    Simulator.TickMMDPhysicsOnComponentTransforms(Transforms, FTransform::Identity, 1.0f / 60.0f);
    TestTrue(TEXT("独立刚体模拟状态正常"), Simulator.IsInitialized());
    Simulator.Shutdown();
    TestFalse(TEXT("独立刚体退出清理"), Simulator.IsInitialized());

    Rigid.PhysicsMode = 99;
    AddExpectedMessagePlain(TEXT("InitializeFromPMX failed: invalid PhysicsMode 99"), ELogVerbosity::Error);
    TestFalse(TEXT("非法物理模式被拒绝"), Simulator.InitializeFromPMX(Rigids, Joints, Component.Get()));
    TestFalse(TEXT("拒绝后未留下模拟世界"), Simulator.IsInitialized());
    return true;
}

#endif
