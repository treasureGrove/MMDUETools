// 脸部 SDF 烘焙入口。
//
// 用法：
//   UnrealEditor-Cmd.exe <项目>.uproject -run=MMDEndfieldBakeSdf \
//     -Mesh=/Game/MMDModels/<模型>/Model/<资产名>.<资产名> \
//     -Sdf=/Game/MMDModels/<模型>/Textures/T_<模型>_FaceSDF \
//     -Report=<JSON路径> [-HeadBone=Head] [-Size=512] [-Generate] [-Tga=<调试输出路径>]
//
// 头部基自动推导：优先用 HeadBone 的参考姿势变换；骨架缺失或无该骨骼时退回包围盒推理，
// 并在报告里标注来源（head_bone / bounds_fallback），便于人工核对。
// 推导不出有效头部基就报错退出，不写出错误外观的贴图。
#include "MMDEndfieldBakeSdfCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Editor/MMDEndfieldFaceSdfBaker.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    const TCHAR* HeadFrameSourceName(MMDEnfieldFaceSdf::EHeadFrameSource Source)
    {
        switch (Source)
        {
        case MMDEnfieldFaceSdf::EHeadFrameSource::HeadBone: return TEXT("head_bone");
        case MMDEnfieldFaceSdf::EHeadFrameSource::BoundsFallback: return TEXT("bounds_fallback");
        default: return TEXT("failed");
        }
    }
}

UMMDEndfieldBakeSdfCommandlet::UMMDEndfieldBakeSdfCommandlet()
{
    IsClient = true;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMMDEndfieldBakeSdfCommandlet::Main(const FString& Params)
{
    const bool bGenerate = FParse::Param(*Params, TEXT("Generate"));
    FString MeshPath;
    FString SdfPath;
    FString ReportPath;
    FString HeadBoneName = TEXT("Head");
    FString TgaPath;
    int32 TextureSize = 512;
    FParse::Value(*Params, TEXT("Mesh="), MeshPath);
    FParse::Value(*Params, TEXT("Sdf="), SdfPath);
    FParse::Value(*Params, TEXT("Report="), ReportPath);
    FParse::Value(*Params, TEXT("HeadBone="), HeadBoneName);
    FParse::Value(*Params, TEXT("Tga="), TgaPath);
    FParse::Value(*Params, TEXT("Size="), TextureSize);

    TArray<FString> Errors;
    TArray<TSharedPtr<FJsonValue>> Notes;
    if (MeshPath.IsEmpty()) Errors.Add(TEXT("缺少 -Mesh=<骨骼网格资产路径>"));
    if (SdfPath.IsEmpty()) Errors.Add(TEXT("缺少 -Sdf=<输出 SDF 纹理资产路径>"));
    if (ReportPath.IsEmpty()) Errors.Add(TEXT("缺少 -Report=<JSON 路径>"));

    MMDEnfieldFaceSdf::FBakeResult BakeResult;
    MMDEnfieldFaceSdf::EHeadFrameSource HeadSource = MMDEnfieldFaceSdf::EHeadFrameSource::Failed;
    FString WrittenTexture;

    if (Errors.IsEmpty())
    {
        USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
        if (!Mesh)
        {
            Errors.Add(TEXT("无法加载骨骼网格：") + MeshPath);
        }
        else
        {
            MMDEnfieldFaceSdf::FBakeSettings Settings;
            Settings.TextureSize = TextureSize;
            FString DeriveMessage;
            if (!MMDEnfieldFaceSdf::DeriveHeadFrame(Mesh, FName(*HeadBoneName), Settings, HeadSource, DeriveMessage))
            {
                Errors.Add(TEXT("头部基推导失败：") + DeriveMessage);
            }
            else
            {
                if (!DeriveMessage.IsEmpty())
                    Notes.Add(MakeShared<FJsonValueString>(DeriveMessage));
                // 兜底来源不静默通过：明确记录，允许继续但报告标注，由人工决定是否采用。
                if (HeadSource == MMDEnfieldFaceSdf::EHeadFrameSource::BoundsFallback)
                    Notes.Add(MakeShared<FJsonValueString>(
                        TEXT("头部基来自包围盒兜底，SDF 角度可能整体偏移；建议确认骨骼名后重烘")));

                if (!bGenerate)
                {
                    Errors.Add(TEXT("缺少 -Generate：本入口只在校验模式下运行，不写出贴图"));
                }
                else if (!MMDEnfieldFaceSdf::BakeFaceSdf(Mesh, Settings, BakeResult))
                {
                    Errors.Add(TEXT("烘焙失败：") + BakeResult.Error);
                }
                else if (BakeResult.FaceTexelCount == 0)
                {
                    // 没有识别到任何脸部 texel 说明头部基或 UV 判定有问题，拒绝写出空白图。
                    Errors.Add(TEXT("未识别到任何脸部 texel：头部基或 UV 判定可能不正确，拒绝写出空白 SDF"));
                }
                else
                {
                    FString SaveError;
                    const FString Package = FPaths::GetPath(SdfPath);
                    const FString AssetName = FPaths::GetBaseFilename(SdfPath);
                    if (!MMDEnfieldFaceSdf::SaveAsTexture(BakeResult, Package, AssetName, SaveError))
                        Errors.Add(SaveError);
                    else
                        WrittenTexture = SdfPath;
                }
            }
        }
    }

    // 调试用 TGA 在写盘之外单独输出，便于引擎外查看；失败不影响主流程判定。
    if (!TgaPath.IsEmpty() && BakeResult.bSuccess)
    {
        if (!MMDEnfieldFaceSdf::WriteTga(BakeResult, TgaPath))
            Notes.Add(MakeShared<FJsonValueString>(TEXT("TGA 调试图写出失败：") + TgaPath));
    }

    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("success"), Errors.Num() == 0);
    Report->SetBoolField(TEXT("generated"), bGenerate);
    Report->SetStringField(TEXT("target"), TEXT("EndfieldFaceSdf"));
    Report->SetStringField(TEXT("mesh"), MeshPath);
    Report->SetStringField(TEXT("sdf"), SdfPath);
    Report->SetStringField(TEXT("written_texture"), WrittenTexture);
    Report->SetStringField(TEXT("head_frame_source"), HeadFrameSourceName(HeadSource));
    Report->SetNumberField(TEXT("size"), BakeResult.Size);
    Report->SetNumberField(TEXT("face_texel_count"), BakeResult.FaceTexelCount);
    Report->SetNumberField(TEXT("lit_texel_count"), BakeResult.LitTexelCount);
    Report->SetNumberField(TEXT("considered_vertex_count"), BakeResult.ConsideredVertexCount);
    Report->SetArrayField(TEXT("notes"), Notes);
    Report->SetNumberField(TEXT("failed"), Errors.Num());
    TArray<TSharedPtr<FJsonValue>> JsonErrors;
    for (const FString& Failure : Errors)
    {
        JsonErrors.Add(MakeShared<FJsonValueString>(Failure));
        UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldBakeSdf] %s"), *Failure);
    }
    Report->SetArrayField(TEXT("errors"), JsonErrors);

    FString Json;
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
    const bool bSaved = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
        && FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Display, TEXT("[MMDEndfieldBakeSdf] face_texels=%d lit=%d source=%s success=%d"),
        BakeResult.FaceTexelCount, BakeResult.LitTexelCount, HeadFrameSourceName(HeadSource),
        Errors.Num() == 0 && bSaved);
    return Errors.Num() == 0 && bSaved ? 0 : 1;
}
