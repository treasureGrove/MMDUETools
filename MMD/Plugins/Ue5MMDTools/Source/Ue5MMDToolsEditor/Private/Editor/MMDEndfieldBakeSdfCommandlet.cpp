// V5 自动头投影角度阈值场入口，保留旧命令名；不是原作手绘数据或真实遮挡 SDF。
//
// 用法：
//   UnrealEditor-Cmd.exe <项目>.uproject -run=MMDEndfieldBakeSdf \
//     -Mesh=/Game/MMDModels/<模型>/Model/<资产名>.<资产名> \
//     -Sdf=/Game/MMDModels/<模型>/Textures/T_<模型>_FaceSDF \
//     -Report=<JSON路径> [-HeadBone=<真实头骨名>] [-Slots=7] [-Size=512] [-Generate] [-Tga=<调试输出路径>]
//
// 默认自动找头骨，骨尺度与头附近 Skin 拟合均不读全模型 bounds，也不修改网格 UV。
// 本入口只输出单槽贴图；多槽自动生成/绑定请使用 -run=MMDEndfieldBindSdf。
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
    const TCHAR* MMDEndfieldBakeSdfHeadSourceName(MMDEnfieldFaceSdf::EHeadFrameSource Source)
    {
        switch (Source)
        {
        case MMDEnfieldFaceSdf::EHeadFrameSource::HeadBone: return TEXT("head_bone");
        case MMDEnfieldFaceSdf::EHeadFrameSource::BoundsFallback: return TEXT("bounds_fallback");
        default: return TEXT("failed");
        }
    }

    bool MMDEndfieldBakeSdfParseSlots(const FString& Text, TArray<int32>& Slots, FString& Error)
    {
        TArray<FString> Parts;
        Text.ParseIntoArray(Parts, TEXT(","), false);
        if (Text.IsEmpty() || Parts.IsEmpty()) { Error = TEXT("Slots 不能为空"); return false; }
        for (const FString& Part : Parts)
        {
            int64 Value = 0;
            if (Part.IsEmpty()) { Error = TEXT("Slots 不能包含空项"); return false; }
            for (const TCHAR Character : Part)
            {
                if (Character < TEXT('0') || Character > TEXT('9'))
                { Error = TEXT("Slots 只接受逗号分隔的非负整数，材质槽从 0 开始：") + Part; return false; }
                Value = Value * 10 + (Character - TEXT('0'));
                if (Value > MAX_int32) { Error = TEXT("Slots 索引超出 int32 范围：") + Part; return false; }
            }
            if (Slots.Contains(static_cast<int32>(Value)))
            { Error = TEXT("Slots 存在重复索引：") + Part; return false; }
            Slots.Add(static_cast<int32>(Value));
        }
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> MMDEndfieldBakeSdfJsonSlots(const TArray<int32>& Slots)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (const int32 Slot : Slots) Result.Add(MakeShared<FJsonValueNumber>(Slot));
        return Result;
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
    FString HeadBoneName;
    FString SlotsText;
    FString TgaPath;
    int32 TextureSize = 512;
    FParse::Value(*Params, TEXT("Mesh="), MeshPath);
    FParse::Value(*Params, TEXT("Sdf="), SdfPath);
    FParse::Value(*Params, TEXT("Report="), ReportPath);
    FParse::Value(*Params, TEXT("HeadBone="), HeadBoneName);
    FParse::Value(*Params, TEXT("Tga="), TgaPath);
    FParse::Value(*Params, TEXT("Size="), TextureSize);
    const bool bSlotsSpecified = FParse::Value(*Params, TEXT("Slots="), SlotsText) || FParse::Param(*Params, TEXT("Slots"));

    TArray<FString> Errors;
    TArray<TSharedPtr<FJsonValue>> Notes;
    if (MeshPath.IsEmpty()) Errors.Add(TEXT("缺少 -Mesh=<骨骼网格资产路径>"));
    if (SdfPath.IsEmpty()) Errors.Add(TEXT("缺少 -Sdf=<输出 SDF 纹理资产路径>"));
    if (ReportPath.IsEmpty()) Errors.Add(TEXT("缺少 -Report=<JSON 路径>"));
    TArray<int32> Slots;
    FString SlotsError;
    if (bSlotsSpecified && !MMDEndfieldBakeSdfParseSlots(SlotsText, Slots, SlotsError)) Errors.Add(SlotsError);
    if (Slots.Num() > 1)
        Errors.Add(TEXT("本入口只输出单槽贴图，请仅指定 -Slots=7；多槽请使用 -run=MMDEndfieldBindSdf -Slots=0,1"));

    MMDEnfieldFaceSdf::FBakeResult BakeResult;
    MMDEnfieldFaceSdf::FBakeSettings Settings;
    Settings.TextureSize = TextureSize;
    Settings.MaterialIndices = Slots;
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
            FString DeriveMessage;
            const FName PreferredHead = HeadBoneName.IsEmpty() ? NAME_None : FName(*HeadBoneName);
            if (!MMDEnfieldFaceSdf::DeriveHeadFrame(Mesh, PreferredHead, Settings, HeadSource, DeriveMessage))
            {
                Errors.Add(TEXT("头部基推导失败：") + DeriveMessage);
            }
            else
            {
                if (!DeriveMessage.IsEmpty())
                    Notes.Add(MakeShared<FJsonValueString>(DeriveMessage));
                if (!bGenerate)
                {
                    Errors.Add(TEXT("缺少 -Generate：本入口只在校验模式下运行，不写出贴图"));
                }
                else if (!MMDEnfieldFaceSdf::BakeFaceSdf(Mesh, Settings, BakeResult))
                {
                    Errors.Add(TEXT("烘焙失败：") + BakeResult.Error
                        + TEXT("；单槽请指定 -Slots=7，多槽自动绑定使用 -run=MMDEndfieldBindSdf"));
                }
                else if (BakeResult.FaceTexelCount == 0)
                {
                    Errors.Add(TEXT("没有有效的头投影外脸覆盖，拒绝写出空白阈值图"));
                }
                else
                {
                    FString SaveError;
                    const FString Package = SdfPath;
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
    Report->SetBoolField(TEXT("generated"), !WrittenTexture.IsEmpty());
    Report->SetBoolField(TEXT("generate_requested"), bGenerate);
    Report->SetBoolField(TEXT("read_only"), !bGenerate);
    Report->SetStringField(TEXT("target"), TEXT("EndfieldFaceSdf"));
    Report->SetStringField(TEXT("mesh"), MeshPath);
    Report->SetStringField(TEXT("sdf"), SdfPath);
    Report->SetStringField(TEXT("written_texture"), WrittenTexture);
    Report->SetStringField(TEXT("head_frame_source"), MMDEndfieldBakeSdfHeadSourceName(HeadSource));
    Report->SetStringField(TEXT("head_bone_requested"), HeadBoneName.IsEmpty() ? TEXT("auto") : HeadBoneName);
    Report->SetNumberField(TEXT("data_version"), BakeResult.DataVersion);
    Report->SetStringField(TEXT("representation"), TEXT("V5 automatic head-projected angular threshold field"));
    Report->SetStringField(TEXT("encoding"), TEXT("RGBA8: R=positive-yaw-cut/pi; G=negative-yaw-cut/pi; B=clamp(.5+head-forward/(3R),0,1); A=frontmost-coverage"));
    Report->SetStringField(TEXT("projection"), TEXT("uv=(.5+head-right/(3R),.5-head-up/(3R)); no wrap"));
    Report->SetNumberField(TEXT("uv_channel"), Settings.UVChannel);
    Report->SetBoolField(TEXT("mesh_uv_modified"), false);
    Report->SetBoolField(TEXT("true_occlusion_sdf"), false);
    Report->SetBoolField(TEXT("opaque_masked_skin_only"), true);
    Report->SetStringField(TEXT("head_center_reference"), Settings.HeadCenter.ToString());
    Report->SetNumberField(TEXT("head_radius"), Settings.HeadRadius);
    Report->SetArrayField(TEXT("requested_slots"), MMDEndfieldBakeSdfJsonSlots(Slots));
    Report->SetArrayField(TEXT("baked_slots"), MMDEndfieldBakeSdfJsonSlots(BakeResult.BakedMaterialIndices));
    Report->SetArrayField(TEXT("skipped_translucent_slots"), MMDEndfieldBakeSdfJsonSlots(BakeResult.SkippedTranslucentMaterialIndices));
    Report->SetNumberField(TEXT("size"), BakeResult.Size);
    Report->SetNumberField(TEXT("face_texel_count"), BakeResult.FaceTexelCount);
    Report->SetNumberField(TEXT("lit_texel_count"), BakeResult.LitTexelCount);
    Report->SetNumberField(TEXT("considered_vertex_count"), BakeResult.ConsideredVertexCount);
    Report->SetNumberField(TEXT("considered_triangle_count"), BakeResult.ConsideredTriangleCount);
    Report->SetNumberField(TEXT("rejected_triangle_count"), BakeResult.RejectedTriangleCount);
    Report->SetNumberField(TEXT("conflicting_texel_count"), BakeResult.ConflictingTexelCount);
    Report->SetNumberField(TEXT("projected_occluded_texel_count"), BakeResult.ProjectedOccludedTexelCount);
    Report->SetNumberField(TEXT("projected_clipped_triangle_count"), BakeResult.ProjectedClippedTriangleCount);
    Report->SetNumberField(TEXT("depth_clamped_texel_count"), BakeResult.DepthClampedTexelCount);
    Report->SetNumberField(TEXT("padding_texel_count"), BakeResult.PaddingTexelCount);
    Report->SetNumberField(TEXT("front_dark_texel_count"), BakeResult.FrontDarkTexelCount);
    Report->SetNumberField(TEXT("noncontinuous_texel_count"), BakeResult.NonContinuousTexelCount);
    Report->SetNumberField(TEXT("geometry_normal_weight"), BakeResult.GeometryNormalWeight);
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
        BakeResult.FaceTexelCount, BakeResult.LitTexelCount, MMDEndfieldBakeSdfHeadSourceName(HeadSource),
        Errors.Num() == 0 && bSaved);
    return Errors.Num() == 0 && bSaved ? 0 : 1;
}
