#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "LevelSequence.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Subsystems/AssetEditorSubsystem.h"

namespace
{
	int32 ApplyProductionCharacterPresetInWorld(UWorld* World, const FString& TargetMeshPath, UMMDStageRenderPreset* Preset)
	{
		if (!World || !Preset) return 0;
		int32 AppliedCount = 0;
		for (TActorIterator<ASkeletalMeshActor> It(World); It; ++It)
		{
			USkeletalMeshComponent* Component = It->GetSkeletalMeshComponent();
			USkeletalMesh* MeshAsset = Component ? Component->GetSkeletalMeshAsset() : nullptr;
			if (!MeshAsset) continue;
			if (!TargetMeshPath.IsEmpty() && MeshAsset->GetPathName() != TargetMeshPath) continue;
			if (UMMDStageLibrary::ApplyCharacterRenderPreset(Component, Preset))
			{
				++AppliedCount;
			}
		}
		return AppliedCount;
	}

	bool ReadProductionDirectory(const TSharedPtr<FJsonObject>& Report, const TCHAR* Field, const FString& Output, FString& Directory)
	{
		if (!Report->TryGetStringField(Field, Directory) || Directory.IsEmpty()) return false;
		Directory = FPaths::ConvertRelativePathToFull(Output, Directory);
		FPaths::NormalizeDirectoryName(Directory);
		return FPaths::CollapseRelativeDirectories(Directory) && IFileManager::Get().DirectoryExists(*Directory);
	}

	bool ValidProductionRoot(const FString& Root, const TCHAR* Prefix)
	{
		if (!Root.StartsWith(Prefix) || !Root.EndsWith(TEXT("/"))) return false;
		const FString Leaf = Root.RightChop(FCString::Strlen(Prefix)).LeftChop(1);
		return !Leaf.IsEmpty() && !Leaf.Contains(TEXT("/"))
			&& FPackageName::IsValidLongPackageName(Root + TEXT("MMDProductionValidation"));
	}

	bool CanMountProductionDirectory(const FString& Root, const FString& Directory)
	{
		if (!FPackageName::MountPointExists(Root)) return true;
		FString Probe;
		return FPackageName::TryConvertLongPackageNameToFilename(Root + TEXT("MMDProductionValidation"), Probe)
			&& FPaths::IsSamePath(FPaths::ConvertRelativePathToFull(FPaths::GetPath(Probe)), Directory);
	}

	void OpenMMDProduction(const TArray<FString>& Args)
	{
		if (Args.Num() < 1 || Args.Num() > 2 || !GEditor || IsRunningCommandlet())
		{
			UE_LOG(LogTemp, Warning, TEXT("MMD.OpenProduction 用法：在编辑器控制台执行 MMD.OpenProduction \"样片输出目录\" [-NoPreset]")); return;
		}
		const bool bNoPreset = Args.Num() >= 2 && (Args[1].Equals(TEXT("-NoPreset"), ESearchCase::IgnoreCase)
			|| Args[1].Equals(TEXT("NoPreset"), ESearchCase::IgnoreCase));
		FString Output = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Args[0]);
		FPaths::NormalizeDirectoryName(Output);
		FPaths::CollapseRelativeDirectories(Output);
		FString Json;
		TSharedPtr<FJsonObject> Report;
		FString ModelDirectory, ModelRoot, ModelSourceType, AssetsDirectory, AssetsRoot, SequencePath, MeshPath;
		if (!IFileManager::Get().DirectoryExists(*Output)
			|| !FFileHelper::LoadFileToString(Json, *(Output / TEXT("report.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Report) || !Report.IsValid()
			|| !ReadProductionDirectory(Report, TEXT("assets_directory"), Output, AssetsDirectory)
			|| !Report->TryGetStringField(TEXT("assets_mount_root"), AssetsRoot)
			|| !Report->TryGetStringField(TEXT("sequence"), SequencePath)
			|| !ValidProductionRoot(AssetsRoot, TEXT("/Game/MMDProductions/"))
			|| !FPaths::IsUnderDirectory(AssetsDirectory, Output))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 报告缺少有效作品信息或目录不存在：%s"), *Output); return;
		}
		Report->TryGetStringField(TEXT("model_source_type"), ModelSourceType);
		Report->TryGetStringField(TEXT("mesh"), MeshPath);
		const bool bProjectModel = ModelSourceType == TEXT("project_asset");
		if (bProjectModel)
		{
			FString ModelAssetPath, MeshFilename;
			Report->TryGetStringField(TEXT("model_asset"), ModelAssetPath);
			const FString MeshPackage = FPackageName::ObjectPathToPackageName(MeshPath);
			if (MeshPath != ModelAssetPath || !MeshPackage.StartsWith(TEXT("/Game/"))
				|| !FPackageName::IsValidLongPackageName(MeshPackage)
				|| MeshPath != MeshPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(MeshPackage)
				|| !FPackageName::TryConvertLongPackageNameToFilename(MeshPackage, MeshFilename, FPackageName::GetAssetPackageExtension())
				|| !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(MeshFilename), FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()))
				|| IFileManager::Get().FileSize(*MeshFilename) <= 0)
			{
				UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 原工程模型资产不存在或路径无效：%s"), *MeshPath); return;
			}
		}
		else if ((!ModelSourceType.IsEmpty() && ModelSourceType != TEXT("cache"))
			|| !ReadProductionDirectory(Report, TEXT("model_directory"), Output, ModelDirectory)
			|| !Report->TryGetStringField(TEXT("model_mount_root"), ModelRoot)
			|| !ValidProductionRoot(ModelRoot, TEXT("/Game/MMDModels/"))
			|| !CanMountProductionDirectory(ModelRoot, ModelDirectory))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 模型缓存无效或挂载名称已被占用。")); return;
		}
		const FString Package = FPackageName::ObjectPathToPackageName(SequencePath);
		if (!Package.StartsWith(AssetsRoot) || !FPackageName::IsValidLongPackageName(Package)
			|| SequencePath != Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 序列路径不属于本次作品。")); return;
		}
		const FString SequenceFile = AssetsDirectory / Package.RightChop(AssetsRoot.Len()) + FPackageName::GetAssetPackageExtension();
		if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(SequenceFile), AssetsDirectory)
			|| IFileManager::Get().FileSize(*SequenceFile) <= 0
			|| !CanMountProductionDirectory(AssetsRoot, AssetsDirectory))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 序列文件不存在或相同挂载名指向其他目录；未覆盖现有挂载。")); return;
		}
		// 挂载保留至编辑器退出，供序列随后加载模型、贴图和已烘焙动画。
		if (!bProjectModel && !FPackageName::MountPointExists(ModelRoot)) FPackageName::RegisterMountPoint(ModelRoot, ModelDirectory + TEXT("/"));
		if (!FPackageName::MountPointExists(AssetsRoot)) FPackageName::RegisterMountPoint(AssetsRoot, AssetsDirectory + TEXT("/"));
		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath);
		UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (!Sequence || !Editors || !Editors->OpenEditorForAsset(Sequence))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMD.OpenProduction] 无法打开作品序列：%s"), *SequencePath); return;
		}
		FString CharacterPreset = TEXT("Original"), StagePreset = TEXT("Studio");
		Report->TryGetStringField(TEXT("character_preset"), CharacterPreset);
		Report->TryGetStringField(TEXT("stage_preset"), StagePreset);
		if (!bNoPreset && !CharacterPreset.IsEmpty()
			&& !CharacterPreset.Equals(TEXT("Original"), ESearchCase::IgnoreCase)
			&& !CharacterPreset.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			if (UMMDStageRenderPreset* PresetObj = LoadObject<UMMDStageRenderPreset>(nullptr, *CharacterPreset))
			{
				UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
				if (ApplyProductionCharacterPresetInWorld(EditorWorld, MeshPath, PresetObj) == 0)
				{
					TWeakObjectPtr<UWorld> WeakWorld(EditorWorld);
					TWeakObjectPtr<UMMDStageRenderPreset> WeakPreset(PresetObj);
					TSharedRef<int32> RemainingTries = MakeShared<int32>(12);
					FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
						[WeakWorld, WeakPreset, MeshPath, RemainingTries](float) -> bool
						{
							if (!WeakWorld.IsValid() || !WeakPreset.IsValid() || --(*RemainingTries) < 0) return false;
							return ApplyProductionCharacterPresetInWorld(WeakWorld.Get(), MeshPath, WeakPreset.Get()) == 0;
						}), 0.05f);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[MMD.OpenProduction] 已打开作品序列：%s（角色预设：%s，舞台/场景：%s）；源模型没有复制或保存。"),
			*SequencePath, *CharacterPreset, *StagePreset);
	}

	FAutoConsoleCommand OpenProductionCommand(TEXT("MMD.OpenProduction"),
		TEXT("MMD.OpenProduction <OutputDir> [-NoPreset]：挂载本地样片报告并打开可编辑 LevelSequence（默认同步应用报告记录的角色风格预设）。"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OpenMMDProduction));
}
