// -run=MMDEndfieldImportSample -Pmx=<Resources 内绝对路径或相对路径> [-Name=YYB16thWing] [-Vmd=<Resources 内路径>] [-Frames=60] [-Report=<Saved 内 JSON>]
#include "MMDEndfieldImportSampleCommandlet.h"

#include "Editor/MMDOutlineMaterial.h"
#include "Import/TMMDMeshBuilder.h"
#include "AssetCompilingManager.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace
{
	bool EndfieldSampleHasNoLinkedAncestor(const FString& Path)
	{
		FString Current = Path;
		while (!Current.IsEmpty())
		{
			if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

    FString EndfieldSampleFullPath(const FString& Base, const FString& Input)
    {
        FString Result = FPaths::ConvertRelativePathToFull(Base, Input);
        FPaths::NormalizeFilename(Result);
        return FPaths::CollapseRelativeDirectories(Result) ? Result : FString();
    }

    bool EndfieldSampleWriteJson(const TSharedRef<FJsonObject>& Report, const FString& Path)
    {
        FString Json;
        return IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true)
            && FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    // 现有导入器会复用共享描边父材质；提前确认无需迁移，避免该入口写用户 Content。
    bool EndfieldSampleOutlineCanBeReused()
    {
        TStrongObjectPtr<UMaterial> Existing(LoadObject<UMaterial>(nullptr,
            TEXT("/Ue5MMDTools/Resources/MaterialInstance/M_MMD_Outline.M_MMD_Outline")));
        TStrongObjectPtr<UMaterial> Expected(CreateMMDOutlineMaterial(GetTransientPackage()));
        if (!Existing.IsValid() || !Expected.IsValid() || !IsMMDOutlineMaterialGraphValid(Existing.Get())) return false;
        const UMaterialExpressionCustom* ActualOffset = Cast<UMaterialExpressionCustom>(Existing->GetEditorOnlyData()->WorldPositionOffset.Expression);
        const UMaterialExpressionCustom* ExpectedOffset = Cast<UMaterialExpressionCustom>(Expected->GetEditorOnlyData()->WorldPositionOffset.Expression);
        return ActualOffset && ExpectedOffset && ActualOffset->Code == ExpectedOffset->Code;
    }
}

UMMDEndfieldImportSampleCommandlet::UMMDEndfieldImportSampleCommandlet()
{
    IsClient = false;
    IsServer = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UMMDEndfieldImportSampleCommandlet::Main(const FString& Params)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<FString> Errors;
    FString ReportPath;
    FParse::Value(*Params, TEXT("Report="), ReportPath);
    ReportPath = ReportPath.IsEmpty()
        ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/import_sample_report.json"))
        : EndfieldSampleFullPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), ReportPath);
    const FString SavedRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
    if (ReportPath.IsEmpty() || !FPaths::IsUnderDirectory(ReportPath, SavedRoot)
        || !EndfieldSampleHasNoLinkedAncestor(ReportPath))
    {
        UE_LOG(LogTemp, Error, TEXT("[EndfieldImportSample] Report 必须位于本工程 Saved，且路径祖先不能包含链接或 junction"));
        return 1;
    }
    FString MountRoot, OutputDirectory;
    ON_SCOPE_EXIT
    {
        if (!MountRoot.IsEmpty()) FPackageName::UnRegisterMountPoint(MountRoot, OutputDirectory);
        Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
        TArray<TSharedPtr<FJsonValue>> JsonErrors;
        for (const FString& Error : Errors)
        {
            JsonErrors.Add(MakeShared<FJsonValueString>(Error));
            UE_LOG(LogTemp, Error, TEXT("[EndfieldImportSample] %s"), *Error);
        }
        Report->SetArrayField(TEXT("errors"), JsonErrors);
        if (!EndfieldSampleWriteJson(Report, ReportPath))
            UE_LOG(LogTemp, Error, TEXT("[EndfieldImportSample] 无法写报告：%s"), *ReportPath);
    };

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
    if (!Plugin.IsValid()) { Errors.Add(TEXT("找不到当前插件")); return 1; }
    const FString ResourcesRoot = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources"));
    if (!EndfieldSampleHasNoLinkedAncestor(ResourcesRoot))
    {
        Errors.Add(TEXT("Resources 路径祖先包含链接或 junction，拒绝读取重定向样本")); return 1;
    }
    FString PmxInput, VmdInput, Name = TEXT("Character");
    if (!FParse::Value(*Params, TEXT("Pmx="), PmxInput) || PmxInput.IsEmpty())
    {
        Errors.Add(TEXT("必须显式提供 -Pmx=，不会选择其它模型"));
        return 1;
    }
    FParse::Value(*Params, TEXT("Vmd="), VmdInput);
    FParse::Value(*Params, TEXT("Name="), Name);
    for (TCHAR Character : Name)
        if (!((Character >= TEXT('a') && Character <= TEXT('z')) || (Character >= TEXT('A') && Character <= TEXT('Z'))
            || FChar::IsDigit(Character) || Character == TEXT('_')))
        {
            Errors.Add(TEXT("Name 仅允许英文字母、数字和下划线")); return 1;
        }
    if (Name.IsEmpty() || Name.Len() > 40) { Errors.Add(TEXT("Name 长度必须为 1..40")); return 1; }
    const FString SourcePmx = EndfieldSampleFullPath(ResourcesRoot, PmxInput);
    Report->SetStringField(TEXT("source_pmx"), SourcePmx);
    if (SourcePmx.IsEmpty() || !FPaths::IsUnderDirectory(SourcePmx, ResourcesRoot)
        || !FPaths::GetExtension(SourcePmx).Equals(TEXT("pmx"), ESearchCase::IgnoreCase)
        || !IFileManager::Get().FileExists(*SourcePmx) || !EndfieldSampleHasNoLinkedAncestor(SourcePmx))
    {
        Errors.Add(TEXT("Pmx 必须是当前插件 Resources 内已有 PMX，不读取仓库外模型")); return 1;
    }
    if (VmdInput.IsEmpty())
    {
        TArray<FString> Motions;
        IFileManager::Get().FindFilesRecursive(Motions, *ResourcesRoot, TEXT("CH4NGE.vmd"), true, false);
        if (Motions.Num() != 1) { Errors.Add(TEXT("仓库 CH4NGE.vmd 不唯一，请显式指定 Vmd")); return 1; }
        VmdInput = Motions[0];
    }
    const FString SourceVmd = EndfieldSampleFullPath(ResourcesRoot, VmdInput);
    Report->SetStringField(TEXT("source_vmd"), SourceVmd);
    if (SourceVmd.IsEmpty() || !FPaths::IsUnderDirectory(SourceVmd, ResourcesRoot)
        || !FPaths::GetExtension(SourceVmd).Equals(TEXT("vmd"), ESearchCase::IgnoreCase)
        || !IFileManager::Get().FileExists(*SourceVmd) || !EndfieldSampleHasNoLinkedAncestor(SourceVmd))
    {
        Errors.Add(TEXT("Vmd 必须位于当前插件 Resources")); return 1;
    }
    int32 LastFrame = 60;
    FParse::Value(*Params, TEXT("Frames="), LastFrame);
    if (LastFrame < 1 || LastFrame > 300) { Errors.Add(TEXT("Frames 必须在 1..300 之间")); return 1; }
    if (!EndfieldSampleOutlineCanBeReused())
    {
        Errors.Add(TEXT("共享 PMX 描边父材质缺失或需要升级；本入口拒绝迁移/写入原 Content，请先独立处理该父材质")); return 1;
    }

    TPMXParser PmxParser;
    TVMDParser VmdParser;
    if (!PmxParser.ParsePMXFile(SourcePmx) || !VmdParser.ParseVMDFile(SourceVmd))
    {
        Errors.Add(TEXT("PMX 或 VMD 解析失败")); return 1;
    }
    TArray<TSharedPtr<FJsonValue>> MissingTextures;
    for (const FString& RelativeTexture : PmxParser.PMXInfo.ModelTexturePaths)
    {
        const FString TexturePath = EndfieldSampleFullPath(FPaths::GetPath(SourcePmx), RelativeTexture);
        if (TexturePath.IsEmpty() || !FPaths::IsUnderDirectory(TexturePath, ResourcesRoot)
            || !EndfieldSampleHasNoLinkedAncestor(TexturePath))
        {
            Errors.Add(TEXT("PMX 贴图路径超出仓库 Resources：") + RelativeTexture); return 1;
        }
        if (!IFileManager::Get().FileExists(*TexturePath)) MissingTextures.Add(MakeShared<FJsonValueString>(TexturePath));
    }
    Report->SetArrayField(TEXT("missing_texture_sources"), MissingTextures);

    const FString UniqueName = TEXT("MMDBackgroundValidation_") + Name + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    OutputDirectory = EndfieldSampleFullPath(SavedRoot, TEXT("BackgroundValidation/ImportedAssets/") + UniqueName) + TEXT("/");
    MountRoot = TEXT("/Game/MMDModels/") + UniqueName + TEXT("/");
    if (!FPaths::IsUnderDirectory(OutputDirectory, SavedRoot) || !EndfieldSampleHasNoLinkedAncestor(OutputDirectory)
        || !IFileManager::Get().MakeDirectory(*OutputDirectory, true))
    {
        Errors.Add(TEXT("无法建立 Saved 独立导入目录")); MountRoot.Reset(); return 1;
    }
    FPackageName::RegisterMountPoint(MountRoot, OutputDirectory);
    const FString MeshPackage = MountRoot + TEXT("Model/") + UniqueName;
    FString MeshFilename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(MeshPackage, MeshFilename, FPackageName::GetAssetPackageExtension())
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(MeshFilename), OutputDirectory)
        || !EndfieldSampleHasNoLinkedAncestor(MeshFilename))
    {
        Errors.Add(TEXT("网格挂载没有映射到唯一 Saved 目录")); return 1;
    }
    if (FindPackage(nullptr, *MeshPackage) || IFileManager::Get().FileExists(*MeshFilename))
    {
        Errors.Add(TEXT("唯一沙盒名称冲突，拒绝覆盖已有资源")); return 1;
    }
    // 与现有集成测试相同：逻辑文件名隔离自动材质包，原目录继续提供真实贴图。
    const FString LogicalPmx = FPaths::GetPath(SourcePmx) / (UniqueName + TEXT(".pmx"));
    Report->SetStringField(TEXT("logical_import_pmx"), LogicalPmx);
    const bool PreviousStandard = TMMDMeshBuilder::IsUseStandardMaterials();
    TMMDMeshBuilder::SetUseStandardMaterials(true);
    ON_SCOPE_EXIT { TMMDMeshBuilder::SetUseStandardMaterials(PreviousStandard); };
    TStrongObjectPtr<USkeletalMesh> Mesh(TMMDMeshBuilder::BuildSkeletalMeshFromPMX(PmxParser.PMXInfo, MountRoot, UniqueName, LogicalPmx));
    if (!Mesh.IsValid() || !Mesh->GetSkeleton() || Mesh->GetOutermost()->GetName() != MeshPackage)
    {
        Errors.Add(TEXT("现有 PMX 导入器没有生成预期沙盒网格")); return 1;
    }

    VMDData& Motion = VmdParser.VMDInfo;
    Motion.BoneKeyframes.RemoveAll([LastFrame](const VMDBoneKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    Motion.MorphKeyframes.RemoveAll([LastFrame](const VMDMorphKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    Motion.CameraKeyframes.RemoveAll([LastFrame](const VMDCameraKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    Motion.LightKeyframes.RemoveAll([LastFrame](const VMDLightKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    Motion.ShadowKeyframes.RemoveAll([LastFrame](const VMDShadowKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    Motion.IKKeyframes.RemoveAll([LastFrame](const VMDIKKeyframe& Key) { return Key.FrameNumber > uint32(LastFrame); });
    FMMDAnimationImportContext Context;
    FMMDAnimationImportSettings Settings;
    FMMDAnimationImportReport AnimationReport;
    if (!TMMDMeshBuilder::BuildAnimationImportContext(Mesh.Get(), &PmxParser.PMXInfo, LogicalPmx, SourceVmd, Context, &AnimationReport)
        || !TMMDMeshBuilder::AnalyzeVMDAnimationImport(Motion, Context, Settings, AnimationReport))
    {
        Errors.Add(TEXT("现有 VMD 上下文或轨道分析失败")); return 1;
    }
    FString AnimationFilename;
    if (!AnimationReport.PackagePath.StartsWith(MountRoot)
        || !FPackageName::TryConvertLongPackageNameToFilename(AnimationReport.PackagePath / AnimationReport.AssetName,
            AnimationFilename, FPackageName::GetAssetPackageExtension())
        || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(AnimationFilename), OutputDirectory)
        || !EndfieldSampleHasNoLinkedAncestor(AnimationFilename))
    {
        Errors.Add(TEXT("动画输出不在所选 Saved 沙盒")); return 1;
    }
    TStrongObjectPtr<UAnimSequence> Animation(TMMDMeshBuilder::BuildVMDAnimation(Motion, Context, Settings, &AnimationReport));
    if (!Animation.IsValid() || AnimationReport.HasErrors()) { Errors.Add(TEXT("现有 VMD 构建失败")); return 1; }
    Animation->WaitOnExistingCompression();
    if (!Animation->IsBoneCompressedDataValid()) { Errors.Add(TEXT("动作骨骼压缩未完成")); return 1; }
    FAssetCompilingManager::Get().FinishAllCompilation();
    const FSkeletalMeshModel* Imported = Mesh->GetImportedModel();
    if (!Imported || Imported->LODModels.IsEmpty()) { Errors.Add(TEXT("网格没有 LOD0")); return 1; }

    TArray<TSharedPtr<FJsonValue>> AssetFiles;
    for (TObjectIterator<UPackage> Package; Package; ++Package)
    {
        if (!Package->GetName().StartsWith(MountRoot)) continue;
        FString Filename;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension())
            || !FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename), OutputDirectory)
            || !EndfieldSampleHasNoLinkedAncestor(Filename))
        {
            Errors.Add(TEXT("保存前发现资源路径超出所选 Saved 沙盒")); return 1;
        }
        FSavePackageArgs Save;
        Save.TopLevelFlags = RF_Public | RF_Standalone;
        if (!UPackage::SavePackage(*Package, nullptr, *Filename, Save))
        {
            Errors.Add(TEXT("保存沙盒资源失败：") + Package->GetName()); return 1;
        }
        AssetFiles.Add(MakeShared<FJsonValueString>(FPaths::ConvertRelativePathToFull(Filename)));
    }
    Report->SetStringField(TEXT("schema"), TEXT("mmd.endfield.sample-import.v1"));
    Report->SetStringField(TEXT("material_mode"), TEXT("standard"));
    Report->SetStringField(TEXT("output_directory"), OutputDirectory);
    Report->SetStringField(TEXT("virtual_root"), MountRoot);
    Report->SetStringField(TEXT("mesh"), Mesh->GetPathName());
    Report->SetStringField(TEXT("animation"), Animation->GetPathName());
    Report->SetNumberField(TEXT("animation_last_source_frame"), LastFrame);
    Report->SetNumberField(TEXT("animation_length_seconds"), Animation->GetPlayLength());
    Report->SetNumberField(TEXT("vertices"), Imported->LODModels[0].NumVertices);
    Report->SetNumberField(TEXT("triangles"), Imported->LODModels[0].IndexBuffer.Num() / 3);
    Report->SetNumberField(TEXT("materials"), Mesh->GetMaterials().Num());
    Report->SetNumberField(TEXT("bones"), Mesh->GetRefSkeleton().GetNum());
    Report->SetNumberField(TEXT("morph_targets"), Mesh->GetMorphTargets().Num());
    Report->SetNumberField(TEXT("asset_count"), AssetFiles.Num());
    Report->SetArrayField(TEXT("asset_files"), AssetFiles);
    TArray<TSharedPtr<FJsonValue>> AnimationWarnings;
    for (const FString& Warning : AnimationReport.Warnings) AnimationWarnings.Add(MakeShared<FJsonValueString>(Warning));
    Report->SetArrayField(TEXT("animation_warnings"), AnimationWarnings);
    Report->SetBoolField(TEXT("success"), true);
    const FString Manifest = OutputDirectory / TEXT("manifest.json");
    Report->SetStringField(TEXT("manifest"), Manifest);
    if (!EndfieldSampleWriteJson(Report, Manifest)) { Errors.Add(TEXT("无法保存样本 manifest")); return 1; }
    if (!EndfieldSampleWriteJson(Report, ReportPath)) { Errors.Add(TEXT("无法保存指定导入报告")); return 1; }
    UE_LOG(LogTemp, Display, TEXT("[EndfieldImportSample] Mesh=%s Vertices=%u Materials=%d Manifest=%s"),
        *Mesh->GetPathName(), Imported->LODModels[0].NumVertices, Mesh->GetMaterials().Num(), *Manifest);
    return 0;
}
