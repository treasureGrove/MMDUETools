// -run=MMDImportPmx -Pmx=<绝对路径> [-Name=<资产名>] [-StandardMaterials] [-Force] [-Report=<JSON>]
#include "MMDImportPmxCommandlet.h"

#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Import/TMMDMeshBuilder.h"
#include "Import/TPMXParser.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool MMDWriteJson(const TSharedRef<FJsonObject>& Report, const FString& Path)
	{
		FString Json;
		return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
			&& FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	// 与导入器一致：非字母数字下划线一律换成下划线，用于推导资产名。
	FString MMDSanitizeName(const FString& Input)
	{
		FString Result;
		for (const TCHAR Character : Input)
		{
			Result.AppendChar(FChar::IsAlnum(Character) || Character == TEXT('_') ? Character : TEXT('_'));
		}
		return Result;
	}

	FString MMDAbsolutePath(const FString& Input)
	{
		FString Result = FPaths::ConvertRelativePathToFull(Input);
		FPaths::NormalizeFilename(Result);
		return FPaths::CollapseRelativeDirectories(Result) ? Result : FString();
	}

	/** 长包名转磁盘目录；失败返回空。 */
	FString MMDPackageDirOnDisk(const FString& PackageDir)
	{
		FString ProbeFile;
		if (!FPackageName::TryConvertLongPackageNameToFilename(
			PackageDir / TEXT("Probe"), ProbeFile, FPackageName::GetAssetPackageExtension()))
		{
			return FString();
		}
		return FPaths::GetPath(ProbeFile);
	}
}

UMMDImportPmxCommandlet::UMMDImportPmxCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = false;
}

int32 UMMDImportPmxCommandlet::Main(const FString& Params)
{
	const double StartSeconds = FPlatformTime::Seconds();
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	TArray<FString> Warnings;

	FString ReportPath;
	if (!FParse::Value(*Params, TEXT("Report="), ReportPath) || ReportPath.IsEmpty())
	{
		ReportPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MMDImportPmx.json"));
	}
	else
	{
		ReportPath = MMDAbsolutePath(ReportPath);
	}

	ON_SCOPE_EXIT
	{
		Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
		TArray<TSharedPtr<FJsonValue>> JsonErrors;
		for (const FString& Error : Errors)
		{
			JsonErrors.Add(MakeShared<FJsonValueString>(Error));
			UE_LOG(LogTemp, Error, TEXT("[MMDImportPmx] %s"), *Error);
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
		if (!MMDWriteJson(Report, ReportPath))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMDImportPmx] 无法写报告：%s"), *ReportPath);
		}
	};

	FString PmxInput;
	if (!FParse::Value(*Params, TEXT("Pmx="), PmxInput) || PmxInput.IsEmpty())
	{
		Errors.Add(TEXT("必须提供 -Pmx=<模型绝对路径>"));
		return 1;
	}
	const FString PmxPath = MMDAbsolutePath(PmxInput);
	Report->SetStringField(TEXT("source_pmx"), PmxPath);
	if (PmxPath.IsEmpty() || !IFileManager::Get().FileExists(*PmxPath)
		|| !FPaths::GetExtension(PmxPath).Equals(TEXT("pmx"), ESearchCase::IgnoreCase))
	{
		Errors.Add(TEXT("Pmx 必须是已存在的 .pmx 文件"));
		return 1;
	}

	FString AssetName;
	if (!FParse::Value(*Params, TEXT("Name="), AssetName) || AssetName.IsEmpty())
	{
		AssetName = FPaths::GetBaseFilename(PmxPath);
	}
	AssetName = MMDSanitizeName(AssetName);
	if (AssetName.Len() > 40) AssetName.LeftInline(40);
	if (!AssetName.Equals(FPaths::GetBaseFilename(PmxPath)))
	{
		Warnings.Add(FString::Printf(TEXT("资产名已规范化为 %s"), *AssetName));
	}

	// 导入器把资产固定放在 /Game/MMDModels/<模型名>/ 下，这里用同样的规则推导路径。
	const FString ModelName = MMDSanitizeName(FPaths::GetBaseFilename(PmxPath));
	const FString AssetRoot = TEXT("/Game/MMDModels/") + ModelName + TEXT("/");
	const FString ExpectedMeshPackage = AssetRoot + TEXT("Model/") + AssetName;
	Report->SetStringField(TEXT("asset_root"), AssetRoot);
	Report->SetStringField(TEXT("mesh_package"), ExpectedMeshPackage);

	// 覆盖保护必须在导入前判断：导入器自身会把资产写盘，事后检查必然为真。
	FString ExpectedMeshFile;
	if (FPackageName::TryConvertLongPackageNameToFilename(
		ExpectedMeshPackage, ExpectedMeshFile, FPackageName::GetAssetPackageExtension())
		&& IFileManager::Get().FileExists(*ExpectedMeshFile))
	{
		Report->SetStringField(TEXT("existing_file"), ExpectedMeshFile);
		if (!FParse::Param(*Params, TEXT("Force")))
		{
			Errors.Add(FString::Printf(TEXT("资产已存在：%s；如需覆盖请加 -Force"), *ExpectedMeshPackage));
			return 1;
		}
		Report->SetBoolField(TEXT("overwrote_existing"), true);
	}

	TPMXParser Parser;
	if (!Parser.ParsePMXFile(PmxPath))
	{
		Errors.Add(TEXT("PMX 解析失败"));
		return 1;
	}

	const bool bPreviousStandard = TMMDMeshBuilder::IsUseStandardMaterials();
	const bool bStandardMaterials = FParse::Param(*Params, TEXT("StandardMaterials")) || bPreviousStandard;
	TMMDMeshBuilder::SetUseStandardMaterials(bStandardMaterials);
	ON_SCOPE_EXIT { TMMDMeshBuilder::SetUseStandardMaterials(bPreviousStandard); };

	TStrongObjectPtr<USkeletalMesh> Mesh(
		TMMDMeshBuilder::BuildSkeletalMeshFromPMX(Parser.PMXInfo, FString(), AssetName, PmxPath));
	if (!Mesh.IsValid() || !Mesh->GetSkeleton())
	{
		Errors.Add(TEXT("PMX 导入没有生成骨骼网格"));
		return 1;
	}
	FAssetCompilingManager::Get().FinishAllCompilation();

	// 导入器自己负责保存；这里以磁盘结果为准，避免把“已保存”误判成“未保存”。
	const FString AssetDir = MMDPackageDirOnDisk(AssetRoot);
	if (AssetDir.IsEmpty() || !IFileManager::Get().DirectoryExists(*AssetDir))
	{
		Errors.Add(TEXT("导入后找不到资产目录：") + AssetRoot);
		return 1;
	}
	TArray<FString> AllFiles;
	IFileManager::Get().FindFilesRecursive(AllFiles, *AssetDir, TEXT("*.uasset"), true, false);
	if (AllFiles.Num() == 0)
	{
		Errors.Add(TEXT("导入没有产生任何 .uasset：") + AssetDir);
		return 1;
	}
	// 同一模型目录可能还留有之前导入的资产，这里只统计本次命名的文件。
	TArray<TSharedPtr<FJsonValue>> AssetFiles;
	for (const FString& File : AllFiles)
	{
		if (FPaths::GetBaseFilename(File).Contains(AssetName))
		{
			AssetFiles.Add(MakeShared<FJsonValueString>(FPaths::ConvertRelativePathToFull(File)));
		}
	}
	if (AssetFiles.Num() == 0)
	{
		Errors.Add(TEXT("没有找到属于本次导入的资产文件：") + AssetName);
		return 1;
	}

	const FSkeletalMeshModel* Imported = Mesh->GetImportedModel();
	const int32 VertexCount = (Imported && !Imported->LODModels.IsEmpty()) ? Imported->LODModels[0].NumVertices : 0;
	const int32 TriangleCount = (Imported && !Imported->LODModels.IsEmpty()) ? Imported->LODModels[0].IndexBuffer.Num() / 3 : 0;

	Report->SetStringField(TEXT("schema"), TEXT("mmd.cli.import-pmx.v1"));
	Report->SetStringField(TEXT("asset_name"), AssetName);
	Report->SetStringField(TEXT("asset_dir"), FPaths::ConvertRelativePathToFull(AssetDir));
	Report->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Report->SetStringField(TEXT("skeleton"), Mesh->GetSkeleton()->GetPathName());
	Report->SetNumberField(TEXT("vertices"), VertexCount);
	Report->SetNumberField(TEXT("triangles"), TriangleCount);
	Report->SetNumberField(TEXT("materials"), Mesh->GetMaterials().Num());
	Report->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum());
	Report->SetNumberField(TEXT("morph_targets"), Mesh->GetMorphTargets().Num());
	Report->SetNumberField(TEXT("asset_count"), AssetFiles.Num());
	Report->SetArrayField(TEXT("asset_files"), AssetFiles);
	Report->SetStringField(TEXT("material_mode"), bStandardMaterials ? TEXT("standard") : TEXT("default"));
	Report->SetBoolField(TEXT("success"), true);

	UE_LOG(LogTemp, Display, TEXT("[MMDImportPmx] Mesh=%s Vertices=%d Materials=%d Bones=%d Morphs=%d Assets=%d"),
		*Mesh->GetPathName(), VertexCount, Mesh->GetMaterials().Num(),
		Mesh->GetRefSkeleton().GetNum(), Mesh->GetMorphTargets().Num(), AssetFiles.Num());
	return 0;
}
