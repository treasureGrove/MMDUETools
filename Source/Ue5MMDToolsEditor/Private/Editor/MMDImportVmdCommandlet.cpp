// -run=MMDImportVmd -Vmd=<绝对路径> -Mesh=/Game/... [-Pmx=<原模型路径>] [-Name=<动画名>] [-Force] [-Report=<JSON>]
#include "MMDImportVmdCommandlet.h"

#include "Animation/AnimSequence.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Import/TMMDMeshBuilder.h"
#include "Import/TVMDParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool MMDVmdWriteJson(const TSharedRef<FJsonObject>& Report, const FString& Path)
	{
		FString Json;
		return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
			&& FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString MMDVmdSanitize(const FString& Input)
	{
		FString Result;
		for (const TCHAR Character : Input)
		{
			Result.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('_') ? Character : TEXT('_'));
		}
		if (Result.IsEmpty()) Result = TEXT("MMDAnimation");
		if (Result.Len() > 40) Result.LeftInline(40);
		return Result;
	}

	FString MMDVmdAbsolutePath(const FString& Input)
	{
		FString Result = FPaths::ConvertRelativePathToFull(Input);
		FPaths::NormalizeFilename(Result);
		return FPaths::CollapseRelativeDirectories(Result) ? Result : FString();
	}
}

UMMDImportVmdCommandlet::UMMDImportVmdCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = false;
}

int32 UMMDImportVmdCommandlet::Main(const FString& Params)
{
	const double StartSeconds = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	TArray<FString> Warnings;

	FString ReportPath;
	if (!FParse::Value(*Params, TEXT("Report="), ReportPath) || ReportPath.IsEmpty())
	{
		ReportPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MMDImportVmd.json"));
	}
	else
	{
		ReportPath = MMDVmdAbsolutePath(ReportPath);
	}

	ON_SCOPE_EXIT
	{
		Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
		TArray<TSharedPtr<FJsonValue>> JsonErrors;
		for (const FString& Error : Errors)
		{
			JsonErrors.Add(MakeShared<FJsonValueString>(Error));
			UE_LOG(LogTemp, Error, TEXT("[MMDImportVmd] %s"), *Error);
		}
		TArray<TSharedPtr<FJsonValue>> JsonWarnings;
		for (const FString& Warning : Warnings)
		{
			JsonWarnings.Add(MakeShared<FJsonValueString>(Warning));
		}
		Report->SetArrayField(TEXT("errors"), JsonErrors);
		Report->SetArrayField(TEXT("warnings"), JsonWarnings);
		Report->SetStringField(TEXT("report"), ReportPath);
		Report->SetNumberField(TEXT("elapsed_seconds"), FPlatformTime::Seconds() - StartSeconds);
		if (!MMDVmdWriteJson(Report, ReportPath))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMDImportVmd] 无法写报告：%s"), *ReportPath);
		}
	};

	FString VmdInput, MeshPath, PmxInput, AssetName;
	if (!FParse::Value(*Params, TEXT("Vmd="), VmdInput) || VmdInput.IsEmpty())
	{
		Errors.Add(TEXT("必须提供 -Vmd=<动作绝对路径>"));
		return 1;
	}
	if (!FParse::Value(*Params, TEXT("Mesh="), MeshPath) || MeshPath.IsEmpty())
	{
		Errors.Add(TEXT("必须提供 -Mesh=</Game/...> 目标骨骼网格"));
		return 1;
	}
	const FString VmdPath = MMDVmdAbsolutePath(VmdInput);
	Report->SetStringField(TEXT("source_vmd"), VmdPath);
	Report->SetStringField(TEXT("target_mesh"), MeshPath);
	if (VmdPath.IsEmpty() || !IFileManager::Get().FileExists(*VmdPath)
		|| !FPaths::GetExtension(VmdPath).Equals(TEXT("vmd"), ESearchCase::IgnoreCase))
	{
		Errors.Add(TEXT("Vmd 必须是已存在的 .vmd 文件"));
		return 1;
	}

	FString MeshObjectPath = MeshPath;
	MeshObjectPath.RemoveFromEnd(TEXT(".") + FPaths::GetCleanFilename(MeshObjectPath));
	TStrongObjectPtr<USkeletalMesh> Mesh(LoadObject<USkeletalMesh>(nullptr, *MeshObjectPath));
	if (!Mesh.IsValid() || !Mesh->GetSkeleton())
	{
		Errors.Add(TEXT("无法加载目标骨骼网格，或该网格没有 Skeleton：") + MeshPath);
		return 1;
	}

	FString PmxPath;
	FParse::Value(*Params, TEXT("Pmx="), PmxInput);
	const PMXDatas* PMXData = nullptr;
	if (!PmxInput.IsEmpty())
	{
		PmxPath = MMDVmdAbsolutePath(PmxInput);
		if (!IFileManager::Get().FileExists(*PmxPath))
		{
			Errors.Add(TEXT("Pmx 文件不存在：") + PmxPath);
			return 1;
		}
		PMXData = TMMDMeshBuilder::GetOrParsePMXData(PmxPath);
		Report->SetStringField(TEXT("source_pmx"), PmxPath);
	}
	if (!AssetName.Len())
	{
		FParse::Value(*Params, TEXT("Name="), AssetName);
		if (AssetName.IsEmpty()) AssetName = FPaths::GetBaseFilename(VmdPath);
		AssetName = MMDVmdSanitize(AssetName);
	}

	TVMDParser Parser;
	if (!Parser.ParseVMDFile(VmdPath))
	{
		Errors.Add(TEXT("VMD 解析失败"));
		return 1;
	}

	FMMDAnimationImportContext Context;
	FMMDAnimationImportSettings Settings;
	FMMDAnimationImportReport AnimationReport;
	if (!TMMDMeshBuilder::BuildAnimationImportContext(
		Mesh.Get(), PMXData, PmxPath, VmdPath, Context, &AnimationReport))
	{
		Errors.Add(TEXT("无法建立动画导入上下文"));
		return 1;
	}
	if (!TMMDMeshBuilder::AnalyzeVMDAnimationImport(Parser.VMDInfo, Context, Settings, AnimationReport))
	{
		Errors.Add(TEXT("VMD 轨道分析失败"));
		return 1;
	}
	AnimationReport.AssetName = AssetName;

	const FString AnimationPackage = AnimationReport.PackagePath / AnimationReport.AssetName;
	if (!FParse::Param(*Params, TEXT("Force")) && FindPackage(nullptr, *AnimationPackage))
	{
		Errors.Add(FString::Printf(TEXT("动画资产已存在：%s；如需覆盖请加 -Force"), *AnimationPackage));
		return 1;
	}

	TStrongObjectPtr<UAnimSequence> Animation(
		TMMDMeshBuilder::BuildVMDAnimation(Parser.VMDInfo, Context, Settings, &AnimationReport));
	if (!Animation.IsValid())
	{
		Errors.Add(TEXT("VMD 动画构建失败"));
		return 1;
	}
	Animation->WaitOnExistingCompression();
	FAssetCompilingManager::Get().FinishAllCompilation();
	if (!Animation->IsBoneCompressedDataValid())
	{
		Warnings.Add(TEXT("骨骼压缩数据未就绪，动画可用但压缩状态未确认"));
	}

	FString AnimationFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		Animation->GetPackage()->GetName(), AnimationFilename, FPackageName::GetAssetPackageExtension()))
	{
		Errors.Add(TEXT("无法解析动画保存路径"));
		return 1;
	}
	FSavePackageArgs Save;
	Save.TopLevelFlags = RF_Public | RF_Standalone;
	if (!UPackage::SavePackage(Animation->GetPackage(), nullptr, *AnimationFilename, Save))
	{
		Errors.Add(TEXT("保存动画资产失败：") + Animation->GetPackage()->GetName());
		return 1;
	}

	for (const FString& Warning : AnimationReport.Warnings) Warnings.Add(Warning);
	for (const FString& Error : AnimationReport.Errors) Errors.Add(Error);

	Report->SetStringField(TEXT("schema"), TEXT("mmd.cli.import-vmd.v1"));
	Report->SetStringField(TEXT("animation"), Animation->GetPathName());
	Report->SetStringField(TEXT("animation_file"), FPaths::ConvertRelativePathToFull(AnimationFilename));
	Report->SetNumberField(TEXT("length_seconds"), Animation->GetPlayLength());
	Report->SetNumberField(TEXT("source_bone_tracks"), AnimationReport.SourceBoneTrackCount);
	Report->SetNumberField(TEXT("matched_bone_tracks"), AnimationReport.MatchedBoneTrackCount);
	Report->SetNumberField(TEXT("source_morph_tracks"), AnimationReport.SourceMorphTrackCount);
	Report->SetNumberField(TEXT("matched_morph_tracks"), AnimationReport.MatchedMorphTrackCount);
	Report->SetNumberField(TEXT("max_frame"), AnimationReport.MaxFrame);
	Report->SetBoolField(TEXT("success"), true);

	UE_LOG(LogTemp, Display, TEXT("[MMDImportVmd] Animation=%s Length=%.3fs Bones=%d/%d Morphs=%d/%d"),
		*Animation->GetPathName(), Animation->GetPlayLength(),
		AnimationReport.MatchedBoneTrackCount, AnimationReport.SourceBoneTrackCount,
		AnimationReport.MatchedMorphTrackCount, AnimationReport.SourceMorphTrackCount);
	return 0;
}
