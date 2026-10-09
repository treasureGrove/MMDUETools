[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$ProjectFile,
    [ValidateSet('Prepare', 'Package', 'Smoke', 'All')][string]$Stage = 'All',
    [switch]$IncludeStagePresets,
    [switch]$CaptureVisuals,
    [ValidateSet('NeonConcert', 'SakuraTheater')][string]$VisualStage = 'NeonConcert',
    [int]$TimeoutSeconds = 7200,
    [int]$SmokeTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
function Assert-UnderPath([string]$Candidate, [string]$Parent) {
    $resolvedCandidate = [IO.Path]::GetFullPath($Candidate)
    $resolvedParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    if (-not $resolvedCandidate.StartsWith($resolvedParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw "路径超出隔离目录：$resolvedCandidate"
    }
    return $resolvedCandidate
}
function Save-Utf8([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force | Out-Null
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Invoke-Hidden([string]$Executable, [string[]]$Arguments, [string]$Name, [int]$Limit) {
    $quotedArguments = foreach ($argument in $Arguments) {
        if ($argument.Contains('"') -or $argument.Contains("`n")) { throw '后台启动参数不能含引号或换行。' }
        '"' + $argument + '"'
    }
    $process = Start-Process -FilePath $Executable -ArgumentList $quotedArguments -WorkingDirectory $script:ProbeDirectory `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $script:ReportDirectory "$Name.stdout.log") `
        -RedirectStandardError (Join-Path $script:ReportDirectory "$Name.stderr.log")
    if (-not $process.WaitForExit($Limit * 1000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "$Name 后台执行超时。"
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "$Name 失败：退出码 $($process.ExitCode)，查看 $script:ReportDirectory。" }
}

try {
    if ($CaptureVisuals) { $IncludeStagePresets = $true }
    if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\MMD.uproject' }
    if ($TimeoutSeconds -le 0 -or $TimeoutSeconds -gt 86400 -or $SmokeTimeoutSeconds -le 0 -or $SmokeTimeoutSeconds -gt 3600) {
        throw '后台执行时限超出支持范围。'
    }
    $ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    $projectDirectory = Split-Path -Parent $ProjectFile
    $validationDirectory = Join-Path $projectDirectory 'Saved\BackgroundValidation'
    $script:ProbeDirectory = Assert-UnderPath (Join-Path $validationDirectory 'RuntimeProbe') $validationDirectory
    $reportDirectoryName = if ($VisualStage -eq 'SakuraTheater') { 'RuntimePackageSakura' } else { 'RuntimePackage' }
    $script:ReportDirectory = Join-Path $validationDirectory $reportDirectoryName
    New-Item -ItemType Directory -Path $script:ProbeDirectory, $script:ReportDirectory -Force | Out-Null
    $probeProject = Join-Path $script:ProbeDirectory 'MMDPluginRuntimeProbe.uproject'
    $probePlugin = Assert-UnderPath (Join-Path $script:ProbeDirectory 'Plugins\Ue5MMDTools') $script:ProbeDirectory
    $specPath = Join-Path $script:ReportDirectory 'runtime-spec.json'
    $packageReport = Join-Path $script:ReportDirectory 'package.json'
    $smokeReport = Join-Path $script:ReportDirectory 'runtime-smoke.json'
    $visualReport = Join-Path $script:ReportDirectory 'runtime-visual.json'
    $visualDirectory = Join-Path $script:ReportDirectory 'screenshots'
    $archiveDirectory = Assert-UnderPath (Join-Path $script:ProbeDirectory 'Saved\RuntimePackage') $script:ProbeDirectory
    $dotnetExe = Get-ChildItem -LiteralPath (Join-Path $EngineRoot 'Engine\Binaries\ThirdParty\DotNet') -Filter dotnet.exe -Recurse -File |
        Where-Object FullName -Match 'win-x64[\\/]dotnet\.exe$' | Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    $ubtDll = Join-Path $EngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll'
    $uatDll = Join-Path $EngineRoot 'Engine\Binaries\DotNET\AutomationTool\AutomationTool.dll'
    if (-not $dotnetExe -or -not (Test-Path -LiteralPath $ubtDll) -or -not (Test-Path -LiteralPath $uatDll)) {
        throw '未找到引擎内置 DotNet/UBT/UAT。'
    }

    if ($Stage -eq 'Prepare' -or $Stage -eq 'All') {
        $importsPath = Join-Path $validationDirectory 'import-tests.json'
        $imports = Get-Content -LiteralPath $importsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $imports.success -or $imports.failed -ne 0 -or $imports.test_count -lt 30) {
            throw '先运行本批完整导入回归，至少 30 项通过后才能准备打包探针。'
        }
        $manifestFiles = Get-ChildItem -LiteralPath (Join-Path $validationDirectory 'ImportedAssets') -Filter manifest.json -Recurse -File | Sort-Object LastWriteTimeUtc -Descending
        $selected = @()
        foreach ($sourceName in @("Librarian's Sword.pmx", 'YYB_AssistantGenWorks_Miku_NoSword.pmx')) {
            $match = $null
            foreach ($file in $manifestFiles) {
                $manifest = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
                if ([IO.Path]::GetFileName($manifest.source_pmx) -eq $sourceName) { $match = $manifest; break }
            }
            if (-not $match) { throw "缺少真实导入清单：$sourceName" }
            $selected += $match
        }
        if (-not $selected[1].animation) { throw '角色清单缺少实际 VMD 动画产物，请重新运行最新导入回归。' }

        # 只刷新克隆的输入目录，保留探针的增量构建缓存；所有输出仍留在 Saved。
        foreach ($directory in @('Source', 'Shaders', 'ThirdParty', 'Content')) {
            $cloneInput = Assert-UnderPath (Join-Path $probePlugin $directory) $script:ProbeDirectory
            if (Test-Path -LiteralPath $cloneInput) { Remove-Item -LiteralPath $cloneInput -Recurse -Force }
        }
        New-Item -ItemType Directory -Path $probePlugin -Force | Out-Null
        $sourcePlugin = Join-Path $projectDirectory 'Plugins\Ue5MMDTools'
        Copy-Item -LiteralPath (Join-Path $sourcePlugin 'Ue5MMDTools.uplugin') -Destination $probePlugin
        foreach ($directory in @('Source', 'Shaders', 'ThirdParty', 'Content')) {
            Copy-Item -LiteralPath (Join-Path $sourcePlugin $directory) -Destination (Join-Path $probePlugin $directory) -Recurse
        }
        $probeModelsDirectory = Assert-UnderPath (Join-Path $script:ProbeDirectory 'Content\MMDModels') $script:ProbeDirectory
        if (Test-Path -LiteralPath $probeModelsDirectory) {
            Remove-Item -LiteralPath $probeModelsDirectory -Recurse -Force
        }
        $models = @()
        foreach ($manifest in $selected) {
            $sourceDirectory = Assert-UnderPath $manifest.output_directory (Join-Path $validationDirectory 'ImportedAssets')
            $modelName = Split-Path -Leaf ($sourceDirectory.TrimEnd('\', '/'))
            if ($modelName -notmatch '^MMDBackgroundValidation_[0-9A-Fa-f]{32}$') { throw "不支持的沙盒模型名：$modelName" }
            $destinationDirectory = Assert-UnderPath (Join-Path $script:ProbeDirectory "Content\MMDModels\$modelName") $script:ProbeDirectory
            foreach ($asset in $manifest.asset_files) {
                $sourceAsset = Assert-UnderPath $asset $sourceDirectory
                $relative = $sourceAsset.Substring($sourceDirectory.TrimEnd('\', '/').Length).TrimStart('\', '/')
                $destination = Assert-UnderPath (Join-Path $destinationDirectory $relative) $destinationDirectory
                New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
                Copy-Item -LiteralPath $sourceAsset -Destination $destination -Force
            }
            $previewAnimation = ''
            if ($CaptureVisuals -and [int]$manifest.vertices -gt 5000) {
                $previewFile = Assert-UnderPath (Join-Path $sourceDirectory 'Animation\MMDStagePreview8s.uasset') $sourceDirectory
                if (-not (Test-Path -LiteralPath $previewFile -PathType Leaf)) { throw '当前角色目录缺少 MMDStagePreview8s.uasset，请先完成本批后台舞台截图。' }
                $previewDestination = Assert-UnderPath (Join-Path $destinationDirectory 'Animation\MMDStagePreview8s.uasset') $destinationDirectory
                New-Item -ItemType Directory -Path (Split-Path -Parent $previewDestination) -Force | Out-Null
                Copy-Item -LiteralPath $previewFile -Destination $previewDestination -Force
                $previewAnimation = "/Game/MMDModels/$modelName/Animation/MMDStagePreview8s.MMDStagePreview8s"
            }
            $models += @{
                mesh = "/Game/MMDModels/$modelName/Model/$modelName.$modelName"
                animation = [string]$manifest.animation
                preview_animation = $previewAnimation
                bones = [int]$manifest.bones
                vertices = [int]$manifest.vertices
                materials = [int]$manifest.materials
                outline_materials = [int]$manifest.outline_materials
                rigid_bodies = [int]$manifest.rigid_bodies
                joints = [int]$manifest.joints
                animation_frames = [int]$manifest.animation_frames
                animation_morph_curves = [int]$manifest.animation_morph_curves
            }
        }
        Save-Utf8 $specPath (@{ models = $models; source_manifests = $selected; created_utc = [DateTime]::UtcNow.ToString('o') } | ConvertTo-Json -Depth 15)
        Save-Utf8 $probeProject (@{
            FileVersion = 3; EngineAssociation = '5.8'
            Modules = @(@{ Name = 'MMDPluginRuntimeProbe'; Type = 'Runtime'; LoadingPhase = 'Default' })
            Plugins = @(@{ Name = 'Ue5MMDTools'; Enabled = $true })
        } | ConvertTo-Json -Depth 6)
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Source\MMDPluginRuntimeProbe.Target.cs') @'
using UnrealBuildTool;
public class MMDPluginRuntimeProbeTarget : TargetRules {
    public MMDPluginRuntimeProbeTarget(TargetInfo target) : base(target) {
        Type = TargetType.Game; DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("MMDPluginRuntimeProbe");
    }
}
'@
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Source\MMDPluginRuntimeProbeEditor.Target.cs') @'
using UnrealBuildTool;
public class MMDPluginRuntimeProbeEditorTarget : TargetRules {
    public MMDPluginRuntimeProbeEditorTarget(TargetInfo target) : base(target) {
        Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8; ExtraModuleNames.Add("MMDPluginRuntimeProbe");
    }
}
'@
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Source\MMDPluginRuntimeProbe\MMDPluginRuntimeProbe.Build.cs') @'
using UnrealBuildTool;
public class MMDPluginRuntimeProbe : ModuleRules {
    public MMDPluginRuntimeProbe(ReadOnlyTargetRules target) : base(target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Ue5MMDTools", "Json", "JsonUtilities", "RenderCore", "RHI" });
    }
}
'@
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Config\DefaultEngine.ini') @'
[/Script/EngineSettings.GameMapsSettings]
GameDefaultMap=/Engine/Maps/Entry
EditorStartupMap=/Engine/Maps/Entry
[/Script/Engine.RendererSettings]
r.DefaultFeature.AutoExposure=False
'@
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Config\DefaultGame.ini') @'
[/Script/UnrealEd.ProjectPackagingSettings]
bCookAll=False
bCookMapsOnly=False
bSkipEditorContent=True
bIncludeDebugFiles=False
IncludePrerequisites=False
UsePakFile=True
bUseIoStore=False
+DirectoriesToAlwaysCook=(Path="/Game/MMDModels")
+DirectoriesToAlwaysCook=(Path="/Ue5MMDTools/Rendering")
'@
        if ($IncludeStagePresets) {
            $stageReportPath = Join-Path $validationDirectory 'Stages\stages.json'
            if (-not (Test-Path -LiteralPath $stageReportPath)) { throw '先运行 BuildStagePresetsBackground.ps1 生成舞台资产。' }
            $stageAssetReport = Get-Content -LiteralPath $stageReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if (-not $stageAssetReport.success) { throw '舞台资产验证未通过。' }
            [IO.File]::AppendAllText((Join-Path $script:ProbeDirectory 'Config\DefaultGame.ini'),
                "`r`n+DirectoriesToAlwaysCook=(Path=`"/Ue5MMDTools/Stages`")`r`n", [Text.UTF8Encoding]::new($false))
        }
        Save-Utf8 (Join-Path $script:ProbeDirectory 'Source\MMDPluginRuntimeProbe\MMDPluginRuntimeProbe.cpp') @'
#include "Modules/ModuleManager.h"
#include "Animation/AnimCompressionTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/AGN_MMDSkeletalControl.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Rendering/UMMDAnimeLightDataSubsystem.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Actors/AMMDActor.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ContentStreaming.h"
#include "Import/MMDModelDataAsset.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "RHI.h"
#include "Stage/MMDStageActor.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

class FMMDPluginRuntimeProbeModule final : public FDefaultGameModuleImpl {
    FDelegateHandle InitHandle, ScreenshotHandle;
    FTSTicker::FDelegateHandle VisualTickerHandle;
    TArray<TStrongObjectPtr<UMMDStageRenderPreset>> VisualSourcePresets;
    TArray<TStrongObjectPtr<UWorld>> VisualStageMaps;
    TStrongObjectPtr<UMMDStageRenderPreset> VisualPreset;
    TStrongObjectPtr<USkeletalMesh> VisualMesh;
    TStrongObjectPtr<UAnimSequence> VisualAnimation;
    TStrongObjectPtr<AMMDStageActor> VisualStage;
    TStrongObjectPtr<AActor> VisualCharacterOwner;
    TStrongObjectPtr<USkeletalMeshComponent> VisualCharacter;
    TStrongObjectPtr<UMMDPhysicsPlaybackAnimInstance> VisualPlayback;
    TStrongObjectPtr<ACameraActor> VisualCamera;
    TArray<TSharedPtr<FJsonValue>> VisualErrors, VisualCaptures;
    TArray<FColor> VisualForeground;
    TArray<TWeakObjectPtr<ULightComponent>> DiagnosticLights;
    TArray<float> DiagnosticIntensities;
    bool bDiagnosticSkyVisible = false;
    TArray<TObjectPtr<UMaterialInterface>> OriginalVisualSlotOverlays;
    UMaterialInterface* OriginalVisualGlobalOverlay = nullptr;
    float OriginalVisualBoundsScale = 1.0f;
    bool bCapturingBackground = false;
    bool bVisualSkyUpdateIssued = false;
    int32 VisualReadinessWaitFrames = 0;
    float VisualExposureBias = 0.18f;
    FString VisualStageName = TEXT("NeonConcert");
    FString VisualReportPath, VisualDirectory, VisualFilename;
    int32 VisualIndex = 0, VisualWarmup = 0;
    bool bVisualSetup = true, bScreenshotPending = false, bVisualFinished = false;
    double VisualStarted = 0.0;

    bool VisualVerify(bool Condition, const FString& Message) {
        if (!Condition) VisualErrors.Add(MakeShared<FJsonValueString>(Message));
        return Condition;
    }
    bool IsSakuraVisual() const { return VisualStageName == TEXT("SakuraTheater"); }
    int32 ExpectedVisualCaptures() const { return IsSakuraVisual() ? 35 : 24; }
    bool IsVisualSkyReady() const {
        const USkyLightComponent* Sky = VisualStage.IsValid() ? VisualStage->GardenSkyLight.Get() : nullptr;
        const FTexture* Processed = Sky ? Sky->GetProcessedSkyTexture() : nullptr;
        return Sky && Sky->IsRegistered() && Sky->SourceType == SLS_SpecifiedCubemap
            && Sky->Cubemap && Processed && Processed->TextureRHI.IsValid();
    }
    void FinishVisuals() {
        if (bVisualFinished) return;
        bVisualFinished = true;
        if (bScreenshotPending) FScreenshotRequest::Reset();
        bScreenshotPending = false; bCapturingBackground = false;
        VisualForeground.Reset();
        if (VisualCharacter.IsValid()) VisualCharacter->SetVisibility(true, true);
        if (ScreenshotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
        const bool bSelfShadowEnabled = VisualCharacter.IsValid()
            && VisualCharacter->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")))
            && VisualCharacterOwner.IsValid() && !VisualCharacterOwner->Tags.Contains(FName(TEXT("MMDShadowExclude")))
            && !VisualCharacter->ComponentTags.Contains(FName(TEXT("MMDShadowExclude")));
        if (VisualStage.IsValid() && VisualCharacter.IsValid()) VisualStage->RestoreCharacterMaterials(VisualCharacter.Get());
        const bool bSelfShadowTagRestored = VisualCharacter.IsValid()
            && !VisualCharacter->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")));
        VisualVerify(bSelfShadowTagRestored, TEXT("Character self-shadow tag did not restore"));
        const bool bOutlineRestored = VisualCharacter.IsValid()
            && VisualCharacter->MaterialSlotsOverlayMaterial == OriginalVisualSlotOverlays
            && VisualCharacter->OverlayMaterial == OriginalVisualGlobalOverlay
            && FMath::IsNearlyEqual(VisualCharacter->BoundsScale, OriginalVisualBoundsScale);
        VisualVerify(bOutlineRestored, TEXT("Character outline overlay/bounds did not restore"));
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        Report->SetStringField(TEXT("capture_source"), TEXT("Packaged GameViewport / FScreenshotRequest"));
        Report->SetStringField(TEXT("theme"), VisualStageName);
        Report->SetBoolField(TEXT("sky_required"), IsSakuraVisual());
        Report->SetBoolField(TEXT("sky_ready"), IsVisualSkyReady());
        Report->SetBoolField(TEXT("foreground_opened"), false);
        Report->SetBoolField(TEXT("scene_capture_light_override"), false);
        Report->SetBoolField(TEXT("mmd_self_shadow_excluded"), VisualCharacterOwner.IsValid()
            && VisualCharacterOwner->Tags.Contains(FName(TEXT("MMDShadowExclude"))));
        Report->SetBoolField(TEXT("mmd_self_shadow_enabled"), bSelfShadowEnabled);
        Report->SetBoolField(TEXT("self_shadow_tag_restored"), bSelfShadowTagRestored);
        Report->SetBoolField(TEXT("outline_state_restored"), bOutlineRestored);
        Report->SetArrayField(TEXT("captures"), VisualCaptures);
        TArray<TSharedPtr<FJsonValue>> Maps;
        for (const TStrongObjectPtr<UWorld>& Map : VisualStageMaps) Maps.Add(MakeShared<FJsonValueString>(Map->GetPathName()));
        Report->SetArrayField(TEXT("cooked_stage_maps"), Maps);
        Report->SetArrayField(TEXT("errors"), VisualErrors);
        Report->SetNumberField(TEXT("failed"), VisualErrors.Num());
        const bool Success = VisualErrors.IsEmpty() && VisualCaptures.Num() == ExpectedVisualCaptures();
        Report->SetBoolField(TEXT("success"), Success);
        FString Json;
        const bool Saved = !VisualReportPath.IsEmpty() && FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *VisualReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("[MMDVisualProbe] success=%d captures=%d errors=%d report=%s"), Success && Saved, VisualCaptures.Num(), VisualErrors.Num(), *VisualReportPath);
        FPlatformMisc::RequestExitWithStatus(false, Success && Saved ? 0 : 1);
    }
    bool BeginVisuals(const TSharedPtr<FJsonObject>& Spec, UWorld* World,
        TArray<TStrongObjectPtr<UMMDStageRenderPreset>>&& Presets) {
        FParse::Value(FCommandLine::Get(), TEXT("MMDVisualReport="), VisualReportPath);
        FParse::Value(FCommandLine::Get(), TEXT("MMDVisualDir="), VisualDirectory);
        FParse::Value(FCommandLine::Get(), TEXT("MMDVisualStage="), VisualStageName);
        if (!VisualVerify(VisualStageName == TEXT("NeonConcert") || IsSakuraVisual(), TEXT("Unsupported visual stage"))) return false;
        FParse::Value(FCommandLine::Get(), TEXT("MMDVisualExposure="), VisualExposureBias);
        VisualSourcePresets = MoveTemp(Presets);
        if (IsSakuraVisual()) {
            UMMDStageRenderPreset* Scene = nullptr;
            for (const TStrongObjectPtr<UMMDStageRenderPreset>& Preset : VisualSourcePresets)
                if (Preset->GetName() == TEXT("DA_Render_SakuraTheater")) Scene = Preset.Get();
            if (!VisualVerify(Scene != nullptr, TEXT("Cooked Sakura visual preset missing"))) return false;
            VisualExposureBias = Scene->ExposureBias;
        }
        if (!VisualVerify(World && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport
            && World->GetFirstPlayerController(), TEXT("Actual game viewport/controller unavailable"))
            || !VisualVerify(!VisualReportPath.IsEmpty() && !VisualDirectory.IsEmpty(), TEXT("Visual output paths missing"))) return false;
        IFileManager::Get().MakeDirectory(*VisualDirectory, true);
        for (const TCHAR* Theme : { TEXT("NeonConcert"), TEXT("MoonMirror"), TEXT("SakuraTheater") }) {
            const FString Name = FString(TEXT("MMDStage_")) + Theme;
            const FString Path = FString(TEXT("/Ue5MMDTools/Stages/Maps/")) + Name + TEXT(".") + Name;
            UWorld* Map = LoadObject<UWorld>(nullptr, *Path);
            if (!VisualVerify(Map != nullptr, TEXT("Cooked stage map missing: ") + Path)) return false;
            VisualStageMaps.Emplace(Map);
        }
        const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
        if (Spec && Spec->TryGetArrayField(TEXT("models"), Models)) {
            for (const TSharedPtr<FJsonValue>& Value : *Models) {
                const TSharedPtr<FJsonObject> Model = Value->AsObject();
                if (Model->GetIntegerField(TEXT("vertices")) <= 5000) continue;
                FString AnimationPath;
                if (!VisualVerify(Model->TryGetStringField(TEXT("preview_animation"), AnimationPath) && !AnimationPath.IsEmpty(),
                    TEXT("Current character preview_animation missing; prepare again with -CaptureVisuals"))) return false;
                VisualMesh.Reset(LoadObject<USkeletalMesh>(nullptr, *Model->GetStringField(TEXT("mesh"))));
                VisualAnimation.Reset(LoadObject<UAnimSequence>(nullptr, *AnimationPath));
                break;
            }
        }
        if (!VisualVerify(VisualMesh.IsValid() && VisualAnimation.IsValid()
            && VisualAnimation->GetSkeleton() == VisualMesh->GetSkeleton() && VisualAnimation->GetPlayLength() >= 7.5f,
            TEXT("Cooked current Miku/8s VMD fixture missing or mismatched"))) return false;
        IStreamingManager::Get().StreamAllResources(30.0f);
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags = RF_Transient;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        VisualStage.Reset(World->SpawnActor<AMMDStageActor>(AMMDStageActor::StaticClass(), FTransform::Identity, Spawn));
        VisualCharacterOwner.Reset(World->SpawnActor<AMMDActor>(AMMDActor::StaticClass(), FTransform::Identity, Spawn));
        VisualCamera.Reset(World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Spawn));
        if (!VisualVerify(VisualStage.IsValid() && VisualCharacterOwner.IsValid() && VisualCamera.IsValid(), TEXT("Visual actors could not spawn"))) return false;
        // 自投影由角色预设的组件标签启用，不添加显式阴影排除。
        VisualCharacter.Reset(CastChecked<AMMDActor>(VisualCharacterOwner.Get())->GetMeshComponent());
        VisualCharacter->SetSkeletalMeshAsset(VisualMesh.Get());
        VisualCharacter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        VisualCharacter->SetCastShadow(true);
        VisualCharacter->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        if (!VisualCharacter->IsRegistered()) VisualCharacter->RegisterComponent();
        VisualCharacter->SetWorldTransform(FTransform(FRotator(0, 180, 0), FVector(0, -55, 2)));
        VisualCharacter->SetComponentTickEnabled(false);
        VisualPlayback.Reset(UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(VisualCharacter.Get(), VisualAnimation.Get(), false, true));
        if (!VisualVerify(VisualPlayback.IsValid() && VisualPlayback->IsPhysicsEnabled(), TEXT("Visual VMD default physics unavailable"))) return false;
        VisualCharacter->SetComponentTickEnabled(false);
        UMMDOutlineLibrary::ApplyMMDOutline(VisualCharacter.Get());
        OriginalVisualSlotOverlays = VisualCharacter->MaterialSlotsOverlayMaterial;
        OriginalVisualGlobalOverlay = VisualCharacter->OverlayMaterial;
        OriginalVisualBoundsScale = VisualCharacter->BoundsScale;
        VisualStage->Theme = IsSakuraVisual() ? EMMDStageTheme::SakuraTheater : EMMDStageTheme::NeonConcert;
        VisualStage->bUseManualTime = true;
        VisualStage->ManualTimeSeconds = 0.0f;
        VisualStage->SetActorTickEnabled(false);
        UCameraComponent* Camera = VisualCamera->GetCameraComponent();
        Camera->bConstrainAspectRatio = false;
        Camera->PostProcessBlendWeight = 1.0f;
        FPostProcessSettings& PP = Camera->PostProcessSettings;
        PP.bOverride_AutoExposureMethod = true; PP.AutoExposureMethod = AEM_Manual;
        PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true; PP.AutoExposureApplyPhysicalCameraExposure = false;
        PP.bOverride_AutoExposureBias = true; PP.AutoExposureBias = VisualExposureBias;
        PP.bOverride_BloomIntensity = true; PP.BloomIntensity = 0.06f;
        PP.bOverride_BloomThreshold = true; PP.BloomThreshold = 0.7f;
        PP.bOverride_ColorSaturation = true; PP.ColorSaturation = FVector4(1, 1, 1, 1);
        PP.bOverride_ColorContrast = true; PP.ColorContrast = FVector4(1, 1, 1, 1);
        PP.bOverride_ColorGamma = true; PP.ColorGamma = FVector4(1, 1, 1, 1);
        PP.bOverride_ColorGain = true; PP.ColorGain = FVector4(1, 1, 1, 1);
        PP.bOverride_WhiteTemp = true; PP.WhiteTemp = 6500.0f;
        PP.bOverride_FilmToe = true; PP.FilmToe = 0.25f;
        PP.bOverride_FilmSlope = true; PP.FilmSlope = 0.82f;
        PP.bOverride_VignetteIntensity = true; PP.VignetteIntensity = 0.12f;
        PP.bOverride_FilmGrainIntensity = true; PP.FilmGrainIntensity = 0.0f;
        PP.bOverride_MotionBlurAmount = true; PP.MotionBlurAmount = 0.0f;
        APlayerController* Controller = World->GetFirstPlayerController();
        Controller->bAutoManageActiveCameraTarget = false;
        if (APawn* Pawn = Controller->GetPawn()) Pawn->SetActorHiddenInGame(true);
        Controller->SetViewTarget(VisualCamera.Get());
        if (IConsoleVariable* DelegateMode = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"))) DelegateMode->Set(1, ECVF_SetByCode);
        ScreenshotHandle = UGameViewportClient::OnScreenshotCaptured().AddRaw(this, &FMMDPluginRuntimeProbeModule::CaptureVisual);
        VisualStarted = FPlatformTime::Seconds();
        VisualTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMMDPluginRuntimeProbeModule::TickVisuals));
        return true;
    }
    bool SetupVisual() {
        const TCHAR* Names[] = { TEXT("OpenWorldToon"), TEXT("CinematicAnime"), TEXT("IndustrialAnime"), TEXT("SilverCity") };
        const TCHAR* Images[] = { TEXT("genshin"), TEXT("wuthering"), TEXT("endfield"), TEXT("silver_city") };
        const bool Diagnostic = IsSakuraVisual() && VisualIndex >= 24;
        const int32 Family = Diagnostic ? 0 : VisualIndex / 6;
        const int32 DiagnosticIndex = Diagnostic ? VisualIndex - 24 : -1;
        const bool Face = Diagnostic ? DiagnosticIndex < 8 || DiagnosticIndex == 10 : VisualIndex % 6 == 2 || VisualIndex % 6 == 3;
        const bool Cloth = Diagnostic ? DiagnosticIndex == 8 : VisualIndex % 6 >= 4;
        const bool DetailOff = !Diagnostic && VisualIndex % 2 != 0;
        if ((!Face && !Cloth && !Diagnostic) || VisualIndex == 24) {
            UMMDStageRenderPreset* Source = nullptr;
            UMMDStageRenderPreset* Scene = nullptr;
            for (const TStrongObjectPtr<UMMDStageRenderPreset>& Preset : VisualSourcePresets) {
                if (Preset->GetName() == FString(TEXT("DA_Render_")) + Names[Family]) Source = Preset.Get();
                if (Preset->GetName() == FString(TEXT("DA_Render_")) + VisualStageName) Scene = Preset.Get();
            }
            if (!VisualVerify(Source && Scene && Source->CharacterMaterialOpaque && Source->CharacterMaterialMasked
                && Source->CharacterMaterialTranslucent, TEXT("Cooked reference/scene comparison preset missing"))) return false;
            VisualPreset.Reset(DuplicateObject<UMMDStageRenderPreset>(IsSakuraVisual() ? Scene : Source, GetTransientPackage()));
            if (!VisualVerify(VisualPreset.IsValid(), TEXT("Transient comparison preset unavailable"))) return false;
            VisualPreset->SetFlags(RF_Transient); VisualPreset->ClearFlags(RF_Public | RF_Standalone);
            VisualPreset->bOverrideStageTheme = false;
            VisualPreset->Theme = IsSakuraVisual() ? EMMDStageTheme::SakuraTheater : EMMDStageTheme::NeonConcert;
            if (IsSakuraVisual()) {
                // 樱庭四套对比只替换角色父材质，场景光照与响应完整沿用原预设。
                VisualPreset->CharacterMaterialOpaque = Source->CharacterMaterialOpaque;
                VisualPreset->CharacterMaterialMasked = Source->CharacterMaterialMasked;
                VisualPreset->CharacterMaterialTranslucent = Source->CharacterMaterialTranslucent;
                VisualExposureBias = VisualPreset->ExposureBias;
                FPostProcessSettings& PP = VisualCamera->GetCameraComponent()->PostProcessSettings;
                PP.AutoExposureBias = VisualPreset->ExposureBias; PP.BloomIntensity = VisualPreset->BloomIntensity;
                PP.BloomThreshold = 0.6f; PP.VignetteIntensity = 0.20f;
                PP.ColorSaturation = FVector4(VisualPreset->ColorGradingSaturation, VisualPreset->ColorGradingSaturation, VisualPreset->ColorGradingSaturation, 1);
                PP.ColorContrast = FVector4(VisualPreset->ColorGradingContrast, VisualPreset->ColorGradingContrast, VisualPreset->ColorGradingContrast, 1);
                PP.ColorGamma = FVector4(VisualPreset->ColorGradingGamma, VisualPreset->ColorGradingGamma, VisualPreset->ColorGradingGamma, 1);
                PP.ColorGain = FVector4(VisualPreset->ColorGradingGain.R, VisualPreset->ColorGradingGain.G, VisualPreset->ColorGradingGain.B, 1);
                PP.WhiteTemp = VisualPreset->WhiteTemperature; PP.FilmToe = VisualPreset->FilmToe;
            } else {
                VisualPreset->BaseTint = Scene->BaseTint; VisualPreset->AccentTint = Scene->AccentTint; VisualPreset->SecondaryTint = Scene->SecondaryTint;
                VisualPreset->KeyLightColor = Scene->KeyLightColor; VisualPreset->FillLightColor = Scene->FillLightColor; VisualPreset->RimLightColor = Scene->RimLightColor;
                VisualPreset->KeyLightRotation = FRotator(-38, 55, 0); VisualPreset->FillLightRotation = FRotator(-20, 140, 0); VisualPreset->RimLightRotation = FRotator(-12, -90, 0);
                VisualPreset->KeyLightIntensity = 3.3f; VisualPreset->FillLightIntensity = 0.4f; VisualPreset->RimLightIntensity = 0.7f; VisualPreset->AccentLightIntensity = 0.6f;
                VisualPreset->AmbientStrength = 0.14f;
                VisualPreset->ExposureBias = VisualExposureBias; VisualPreset->BloomIntensity = 0.06f;
                VisualPreset->ColorGradingSaturation = 1; VisualPreset->ColorGradingContrast = 1; VisualPreset->ColorGradingGamma = 1;
                VisualPreset->ColorGradingGain = FLinearColor::White; VisualPreset->WhiteTemperature = 6500; VisualPreset->FilmToe = 0.25f;
            }
            VisualStage->ApplyStageRenderPreset(VisualPreset.Get());
            VisualStage->EvaluateStageAtTime(0.0f);
            if (IsSakuraVisual() && !bVisualSkyUpdateIssued) {
                USkyLightComponent* Sky = VisualStage->GardenSkyLight.Get();
                if (!VisualVerify(Sky && Sky->IsRegistered() && Sky->IsVisible() && Sky->SourceType == SLS_SpecifiedCubemap
                    && Sky->Cubemap, TEXT("Sakura specified-cubemap SkyLight unavailable"))) return false;
                // 注册后只处理一次排队的天空更新，暖帧期间仅检查资源，不逐帧重捕获。
                if (!IsVisualSkyReady()) {
                    VisualCharacter->GetWorld()->SendAllEndOfFrameUpdates();
                    USkyLightComponent::UpdateSkyCaptureContents(VisualCharacter->GetWorld());
                    FlushRenderingCommands();
                }
                bVisualSkyUpdateIssued = true;
            }
            if (!VisualVerify(VisualStage->ApplyCharacterRenderPreset(VisualCharacter.Get(), true), TEXT("Visual reference material application failed"))) return false;
            // 固定步长推进真实 VMD 和默认 Bullet，再停止自动 Tick，四套材质共用同一物理姿态。
            VisualPlayback->ResetPhysics(); VisualPlayback->SetPlaying(true);
            for (int32 Step = 0; Step < 156; ++Step) {
                ++GFrameCounter;
                VisualPlayback->SetPosition(Step / 60.0f, false);
                VisualCharacter->TickAnimation(1.0f / 60.0f, false);
                VisualCharacter->RefreshBoneTransforms();
                VisualCharacter->HandleExistingParallelEvaluationTask(true, true);
            }
            VisualPlayback->SetPlaying(false);
            VisualCharacter->UpdateComponentToWorld();
            VisualCharacter->MarkRenderDynamicDataDirty();
            if (!VisualVerify(UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(VisualCharacter.Get()), TEXT("Visual head geometry bridge unavailable"))) return false;
            VisualCharacter->MarkRenderStateDirty();
            VisualCharacter->GetWorld()->SendAllEndOfFrameUpdates();
            FlushRenderingCommands();
            if (!VisualVerify(VisualPlayback->IsPhysicsInitialized() && VisualPlayback->GetPhysicsRigidBodyCount() > 0, TEXT("Visual default physics was not initialized"))) return false;
        }
        if (Diagnostic) {
            if (DiagnosticIndex == 0) {
                TInlineComponentArray<ULightComponent*> Lights(VisualStage.Get());
                DiagnosticLights.Reset(); DiagnosticIntensities.Reset();
                for (ULightComponent* Light : Lights) {
                    DiagnosticLights.Add(Light); DiagnosticIntensities.Add(Light->Intensity);
                }
                bDiagnosticSkyVisible = VisualStage->GardenSkyLight->IsVisible();
            }
            for (int32 Index = 0; Index < DiagnosticLights.Num(); ++Index) {
                ULightComponent* Light = DiagnosticLights[Index].Get();
                if (!Light) return false;
                const bool Enabled = DiagnosticIndex == 0 || DiagnosticIndex >= 5
                    || (DiagnosticIndex == 1 && Light == VisualStage->KeyLight.Get())
                    || (DiagnosticIndex == 2 && Light == VisualStage->FillLight.Get())
                    || (DiagnosticIndex == 3 && Light == VisualStage->RimLight.Get());
                Light->SetIntensity(Enabled ? DiagnosticIntensities[Index] : 0.0f);
            }
            VisualStage->GardenSkyLight->SetVisibility(bDiagnosticSkyVisible
                && (DiagnosticIndex == 0 || DiagnosticIndex >= 4));
            VisualPreset->bCharacterSelfShadow = DiagnosticIndex != 5;
            VisualPreset->bCharacterOutline = DiagnosticIndex < 7 || DiagnosticIndex == 10;
            if (!VisualVerify(VisualStage->ApplyCharacterRenderPreset(VisualCharacter.Get(), true), TEXT("Diagnostic self-shadow application failed"))) return false;
            UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(VisualCharacter.Get());
        }
        FVector Location(0, -330, 149), Target(0, -55, 124);
        float FOV = 40.0f;
        if (IsSakuraVisual()) { Location = FVector(65, -440, 145); Target = FVector(0, -5, 75); FOV = 50.0f; }
        if (Face) {
            int32 HeadIndex = INDEX_NONE;
            for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") }) {
                HeadIndex = VisualCharacter->GetBoneIndex(FName(Name));
                if (HeadIndex != INDEX_NONE) break;
            }
            if (!VisualVerify(HeadIndex != INDEX_NONE, TEXT("Visual head bone could not resolve"))) return false;
            const FVector HeadLocation = VisualCharacter->GetBoneTransform(HeadIndex).GetLocation();
            Location = HeadLocation + (IsSakuraVisual() ? FVector(38, -118, 17) : FVector(15, -118, 15));
            Target = HeadLocation + FVector(0, -3, 8); FOV = 35.0f;
        }
        VisualCamera->SetActorLocationAndRotation(Location, (Target - Location).Rotation());
        if (Cloth) {
            Location = FVector(70, -205, 138); Target = FVector(0, -55, 125); FOV = 35.0f;
            VisualCamera->SetActorLocationAndRotation(Location, (Target - Location).Rotation());
        }
        VisualCamera->GetCameraComponent()->SetFieldOfView(FOV);
        if (IsSakuraVisual()) {
            FPostProcessSettings& PP = VisualCamera->GetCameraComponent()->PostProcessSettings;
            PP.bOverride_DepthOfFieldFocalDistance = true; PP.DepthOfFieldFocalDistance = FVector::Distance(Location, Target);
            PP.bOverride_DepthOfFieldFstop = true; PP.DepthOfFieldFstop = Face ? 2.8f : (Cloth ? 5.6f : 4.0f);
            PP.bOverride_DepthOfFieldSensorWidth = true; PP.DepthOfFieldSensorWidth = 36.0f;
        }
        VisualCharacter->GetWorld()->GetFirstPlayerController()->SetViewTarget(VisualCamera.Get());
        for (int32 Slot = 0; Slot < VisualCharacter->GetNumMaterials(); ++Slot) {
            UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(VisualCharacter->GetMaterial(Slot));
            if (!Material) continue;
            for (const TCHAR* Name : { TEXT("HairDetailStrength"), TEXT("ClothDetailStrength") }) {
                float Strength = 0.0f;
                if (!Material->GetScalarParameterValue(FMaterialParameterInfo(Name), Strength)) continue;
                if (!DetailOff) Material->GetMaterial()->GetScalarParameterValue(FMaterialParameterInfo(Name), Strength);
                Material->SetScalarParameterValue(Name, DetailOff ? 0.0f : Strength);
            }
        }
        VisualFilename = FPaths::Combine(VisualDirectory, FString(TEXT("runtime_reference_")) + Images[Family]
            + (Face ? TEXT("_face") : (Cloth ? TEXT("_cloth") : TEXT("_body"))) + (DetailOff ? TEXT("_details_off.png") : TEXT(".png")));
        if (Diagnostic) {
            const TCHAR* DiagnosticNames[] = { TEXT("full"), TEXT("key_only"), TEXT("fill_only"), TEXT("rim_only"), TEXT("sky_only"), TEXT("self_shadow_off"), TEXT("restored"), TEXT("face_outline_off"), TEXT("cloth_outline_off"), TEXT("body_outline_off"), TEXT("face_outline_restored") };
            VisualFilename = FPaths::Combine(VisualDirectory, FString(TEXT("runtime_character_")) + DiagnosticNames[VisualIndex - 24] + TEXT(".png"));
        }
        VisualWarmup = 12;
        VisualReadinessWaitFrames = 0;
        return true;
    }
    bool TickVisuals(float DeltaSeconds) {
        if (bVisualFinished) return false;
        if (FPlatformTime::Seconds() - VisualStarted > 90.0) {
            VisualVerify(false, TEXT("Actual GameViewport screenshot sequence timed out")); FinishVisuals(); return false;
        }
        if (VisualIndex >= ExpectedVisualCaptures()) { FinishVisuals(); return false; }
        if (bVisualSetup) {
            bVisualSetup = false;
            if (!SetupVisual()) { FinishVisuals(); return false; }
            return true;
        }
        if (bScreenshotPending) return true;
        // 固定帧数不足以等待打包后的首次 PSO/骨骼渲染资源；就绪后才开始暖帧。
        if (!bCapturingBackground && (VisualCharacter->IsPSOPrecaching()
            || !VisualCharacter->GetSceneProxy() || !VisualCharacter->GetMeshObject()
            || (IsSakuraVisual() && !IsVisualSkyReady()))) {
            ++VisualReadinessWaitFrames; VisualWarmup = 12; return true;
        }
        if (--VisualWarmup > 0) return true;
        bScreenshotPending = true;
        FScreenshotRequest::RequestScreenshot(VisualFilename, false, false, false, FIntRect(), true);
        return true;
    }
    void CaptureVisual(int32 Width, int32 Height, const TArray<FColor>& IncomingColors) {
        if (!bScreenshotPending || bVisualFinished) return;
        bScreenshotPending = false;
        if (!bCapturingBackground) {
            VisualForeground = IncomingColors;
            VisualCharacter->SetVisibility(false, true);
            bCapturingBackground = true; VisualWarmup = 12;
            return;
        }
        // 同机位另拍无角色背景，防止舞台非空被误判为角色成功渲染。
        const TArray<FColor>& Colors = VisualForeground;
        int32 CharacterPixels = 0;
        if (Colors.Num() == Width * Height && IncomingColors.Num() == Colors.Num()) {
            for (int32 Y = Height / 10; Y < Height * 9 / 10; ++Y) {
                for (int32 X = Width / 5; X < Width * 4 / 5; ++X) {
                    const FColor& Foreground = Colors[Y * Width + X];
                    const FColor& Background = IncomingColors[Y * Width + X];
                    const int32 Difference = FMath::Abs(int32(Foreground.R) - Background.R)
                        + FMath::Abs(int32(Foreground.G) - Background.G) + FMath::Abs(int32(Foreground.B) - Background.B);
                    CharacterPixels += Difference > 30 ? 1 : 0;
                }
            }
        }
        VisualCharacter->SetVisibility(true, true);
        bCapturingBackground = false;
        TSharedRef<FJsonObject> Capture = MakeShared<FJsonObject>();
        Capture->SetStringField(TEXT("theme"), VisualStageName);
        Capture->SetBoolField(TEXT("sky_ready"), IsVisualSkyReady());
        const bool ExpectedSkyVisible = IsSakuraVisual() && (VisualIndex < 24 || VisualIndex == 24 || VisualIndex >= 28);
        Capture->SetBoolField(TEXT("sky_visible"), VisualStage->GardenSkyLight->IsVisible());
        Capture->SetStringField(TEXT("output"), VisualFilename);
        const int32 Phase = VisualIndex % 6;
        Capture->SetStringField(TEXT("framing"), VisualIndex >= 24
            ? (VisualIndex == 32 ? TEXT("cloth_detail") : (VisualIndex == 33 ? TEXT("character_detail") : TEXT("face_detail")))
            : (Phase >= 4 ? TEXT("cloth_detail") : (Phase >= 2 ? TEXT("face_detail") : TEXT("character_detail"))));
        Capture->SetBoolField(TEXT("generic_surface_details_enabled"), VisualIndex >= 24 || VisualIndex % 2 == 0);
        Capture->SetStringField(TEXT("character"), VisualMesh->GetPathName());
        Capture->SetStringField(TEXT("animation"), VisualAnimation->GetPathName());
        Capture->SetStringField(TEXT("material"), VisualPreset->CharacterMaterialOpaque->GetPathName());
        Capture->SetNumberField(TEXT("animation_time_seconds"), 2.6f);
        Capture->SetNumberField(TEXT("stage_time_seconds"), 0.0f);
        Capture->SetNumberField(TEXT("warmup_frames"), 12);
        Capture->SetNumberField(TEXT("render_readiness_wait_frames"), VisualReadinessWaitFrames);
        Capture->SetNumberField(TEXT("visible_character_pixels"), CharacterPixels);
        Capture->SetNumberField(TEXT("exposure_bias"), VisualExposureBias);
        Capture->SetNumberField(TEXT("width"), Width); Capture->SetNumberField(TEXT("height"), Height);
        Capture->SetBoolField(TEXT("physics_initialized"), VisualPlayback->IsPhysicsInitialized());
        Capture->SetNumberField(TEXT("physics_rigid_bodies"), VisualPlayback->GetPhysicsRigidBodyCount());
        int32 RoleCounts[6] = {};
        for (int32 Slot = 0; Slot < VisualCharacter->GetNumMaterials(); ++Slot) {
            const int32 Role = static_cast<int32>(UMMDStageLibrary::GetResolvedCharacterMaterialRole(VisualCharacter.Get(), Slot));
            if (Role >= 0 && Role < 6) ++RoleCounts[Role];
        }
        TSharedRef<FJsonObject> Roles = MakeShared<FJsonObject>();
        const TCHAR* RoleNames[] = { TEXT("Generic"), TEXT("Skin"), TEXT("Hair"), TEXT("Cloth"), TEXT("Metal"), TEXT("Eye") };
        for (int32 Role = 0; Role < 6; ++Role) Roles->SetNumberField(RoleNames[Role], RoleCounts[Role]);
        Capture->SetObjectField(TEXT("material_role_counts"), Roles);
        bool Passed = VisualVerify(Width == 1280 && Height == 720 && Colors.Num() == Width * Height, TEXT("GameViewport screenshot dimensions invalid"));
        int32 OutlineSlots = 0;
        for (UMaterialInterface* Outline : VisualCharacter->MaterialSlotsOverlayMaterial) {
            float ScreenMode = 0.0f;
            if (Outline && Outline->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineScreenSpace")), ScreenMode) && ScreenMode > 0.5f) ++OutlineSlots;
        }
        Capture->SetNumberField(TEXT("preset_outline_slots"), OutlineSlots);
        Capture->SetBoolField(TEXT("preset_outline_enabled"), VisualPreset->bCharacterOutline);
        Passed &= VisualVerify(VisualPreset->bCharacterOutline ? OutlineSlots > 0 : OutlineSlots == 0, TEXT("Character preset outline toggle mismatch"));
        if (IsSakuraVisual()) Passed &= VisualVerify(VisualStage->GardenSkyLight->IsVisible() == ExpectedSkyVisible, TEXT("Diagnostic SkyLight visibility mismatched"));
        FLinearColor HeadCenter = FLinearColor::Transparent;
        bool HeadValid = false;
        for (int32 Slot = 0; Slot < VisualCharacter->GetNumMaterials(); ++Slot) {
            UMaterialInterface* Material = VisualCharacter->GetMaterial(Slot);
            if (Material && Material->GetVectorParameterValue(FMaterialParameterInfo(UMMDHeadBoneSubsystem::ParamHeadPositionRadius), HeadCenter)) {
                HeadValid = FMath::IsFinite(HeadCenter.A) && HeadCenter.A > 0.0f; break;
            }
        }
        Capture->SetBoolField(TEXT("head_geometry_frame_valid"), HeadValid);
        Capture->SetNumberField(TEXT("head_geometry_radius"), HeadCenter.A);
        Passed &= VisualVerify(HeadValid, TEXT("MainView head geometry radius invalid"));
        Passed &= VisualVerify(VisualStage->Theme == (IsSakuraVisual() ? EMMDStageTheme::SakuraTheater : EMMDStageTheme::NeonConcert), TEXT("Actual visual stage theme mismatched"));
        if (IsSakuraVisual()) Passed &= VisualVerify(IsVisualSkyReady(), TEXT("Sakura processed SkyLight RHI unavailable"));
        Passed &= VisualVerify(CharacterPixels >= 2500, TEXT("Character missing from the actual GameViewport screenshot"));
        int32 MinLuma = 255, MaxLuma = 0;
        for (const FColor& Pixel : Colors) {
            const int32 Luma = (int32(Pixel.R) + Pixel.G + Pixel.B) / 3;
            MinLuma = FMath::Min(MinLuma, Luma); MaxLuma = FMath::Max(MaxLuma, Luma);
        }
        Passed &= VisualVerify(MaxLuma - MinLuma >= 12, TEXT("Actual GameViewport image was blank"));
        TArray64<uint8> Png;
        if (Passed) FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Colors.GetData(), Colors.Num()), Png);
        Passed &= VisualVerify(!Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png, *VisualFilename), TEXT("GameViewport PNG could not save"));
        // 直接读取主视图写入的灯光与阴影元数据，绝不注入离屏灯光覆盖。
        UMMDAnimeLightDataSubsystem* Lights = UMMDAnimeLightDataSubsystem::Get();
        UTextureRenderTarget2D* LightRT = Lights ? Lights->GetLightDataRenderTarget() : nullptr;
        FTextureRenderTargetResource* Resource = LightRT ? LightRT->GameThread_GetRenderTargetResource() : nullptr;
        TArray<FLinearColor> LightPixels;
        const bool Read = Resource && LightRT->SizeX == 64 && LightRT->SizeY == 2
            && Resource->ReadLinearColorPixels(LightPixels, FReadSurfaceDataFlags(RCM_MinMax, CubeFace_MAX), FIntRect(0, 0, 64, 2)) && LightPixels.Num() == 128;
        int32 Cascades = 0;
        if (Read) for (int32 Index = 0; Index < 4; ++Index) Cascades += LightPixels[64 + Index * 4].A > 0.0f ? 1 : 0;
        const bool ShadowValid = Read && Cascades == 4 && LightPixels[64 + 17].A > 0.5f;
        const float ActiveLights = Read ? LightPixels[64 + 18].R : 0.0f;
        Capture->SetBoolField(TEXT("gpu_lighting_readback"), Read);
        Capture->SetBoolField(TEXT("main_view_shadow_valid"), ShadowValid);
        Capture->SetNumberField(TEXT("gpu_shadow_cascades"), Cascades);
        Capture->SetNumberField(TEXT("gpu_active_lights"), ActiveLights);
        Capture->SetNumberField(TEXT("gpu_main_light_intensity"), Read ? LightPixels[1].A : 0.0f);
        const bool bSelfShadowIncluded = VisualCharacter->ComponentTags.Contains(FName(TEXT("MMDShadowInclude")))
            && !VisualCharacter->ComponentTags.Contains(FName(TEXT("MMDShadowExclude")))
            && !VisualCharacterOwner->Tags.Contains(FName(TEXT("MMDShadowExclude")));
        Capture->SetBoolField(TEXT("character_self_shadow_enabled"), bSelfShadowIncluded);
        Passed &= VisualVerify(bSelfShadowIncluded == VisualPreset->bCharacterSelfShadow, TEXT("Character self-shadow preset/tag mismatch"));
        if (IsSakuraVisual() && VisualIndex >= 24) {
            const int32 DiagnosticIndex = VisualIndex - 24;
            const float ExpectedPower = DiagnosticIndex == 2 ? VisualPreset->FillLightIntensity
                : DiagnosticIndex == 3 ? VisualPreset->RimLightIntensity
                : DiagnosticIndex == 4 ? 0.0f : VisualPreset->KeyLightIntensity;
            Capture->SetStringField(TEXT("diagnostic"), FPaths::GetBaseFilename(VisualFilename));
            Passed &= VisualVerify(Read && Lights && !Lights->HasPreviewLightOverride()
                && (DiagnosticIndex == 4 ? ActiveLights < 0.5f && !ShadowValid : ShadowValid && ActiveLights >= 1.0f)
                && FMath::IsNearlyEqual(LightPixels[1].A, ExpectedPower, 0.0001f), TEXT("Diagnostic MainView light isolation invalid"));
        } else {
            Passed &= VisualVerify(Read && ShadowValid && ActiveLights >= 3.0f && Lights && !Lights->HasPreviewLightOverride()
                && FMath::IsNearlyEqual(LightPixels[1].A, FMath::Clamp(VisualPreset->KeyLightIntensity, 0.0f, 10.0f), 0.0001f), TEXT("MainView lighting/shadow metadata invalid"));
        }
        Capture->SetBoolField(TEXT("passed"), Passed);
        VisualCaptures.Add(MakeShared<FJsonValueObject>(Capture));
        ++VisualIndex; bVisualSetup = true;
    }
public:
    void StartupModule() override {
        if (FParse::Param(FCommandLine::Get(), TEXT("MMDValidationSmoke"))) {
            InitHandle = FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FMMDPluginRuntimeProbeModule::RunSmoke);
        }
    }
    void ShutdownModule() override {
        if (InitHandle.IsValid()) FCoreDelegates::OnFEngineLoopInitComplete.Remove(InitHandle);
        if (VisualTickerHandle.IsValid()) FTSTicker::RemoveTicker(VisualTickerHandle);
        if (ScreenshotHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotHandle);
    }
    void RunSmoke() {
        TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Errors, ModelResults;
        auto Verify = [&Errors](bool Condition, const FString& Message) {
            if (!Condition) Errors.Add(MakeShared<FJsonValueString>(Message));
            return Condition;
        };
        FString SpecPath, ReportPath, Text;
        FParse::Value(FCommandLine::Get(), TEXT("MMDValidationSpec="), SpecPath);
        FParse::Value(FCommandLine::Get(), TEXT("MMDValidationReport="), ReportPath);
        TSharedPtr<FJsonObject> Spec;
        const bool HasSpec = FFileHelper::LoadFileToString(Text, *SpecPath)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Spec);
        Verify(HasSpec, TEXT("Cannot load runtime validation spec"));
        FAGN_MMDSkeletalControl RuntimeNode;
        Verify(FAGN_MMDSkeletalControl::StaticStruct() != nullptr && RuntimeNode.bEnablePhysics,
            TEXT("Runtime skeletal control struct/constructor unavailable"));
        Verify(LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/LightDataRT.LightDataRT")) != nullptr, TEXT("Cooked LightDataRT unavailable"));
        Verify(LoadObject<UTextureRenderTarget2D>(nullptr, TEXT("/Ue5MMDTools/Rendering/MMDShadowMapRT.MMDShadowMapRT")) != nullptr, TEXT("Cooked shadow RT unavailable"));
        const bool ValidateStages = FParse::Param(FCommandLine::Get(), TEXT("MMDStagePresetSmoke"));
        TArray<TSharedPtr<FJsonValue>> PresetResults, StageResults;
        TArray<TStrongObjectPtr<UMMDStageRenderPreset>> LoadedPresets;
        UWorld* ProbeWorld = nullptr;
        if (ValidateStages) {
            for (const TCHAR* PresetName : {TEXT("NeonConcert"), TEXT("MoonMirror"), TEXT("SakuraTheater"),
                TEXT("SilverCity"), TEXT("OpenWorldToon"), TEXT("IndustrialAnime"), TEXT("CinematicAnime")}) {
                const FString AssetName = FString(TEXT("DA_Render_")) + PresetName;
                const FString AssetPath = FString(TEXT("/Ue5MMDTools/Stages/Presets/")) + AssetName + TEXT(".") + AssetName;
                UMMDStageRenderPreset* Preset = LoadObject<UMMDStageRenderPreset>(nullptr, *AssetPath);
                if (!Verify(Preset != nullptr, TEXT("Cooked render preset missing: ") + AssetPath)) continue;
                LoadedPresets.Emplace(Preset);
                Verify(Preset->CharacterMaterialOpaque && Preset->CharacterMaterialMasked && Preset->CharacterMaterialTranslucent,
                    TEXT("Cooked character material references missing"));
                const FString Family = FString(PresetName) == TEXT("SilverCity") ? TEXT("SilverCity")
                    : FString(PresetName) == TEXT("IndustrialAnime") ? TEXT("Endfield")
                    : (FString(PresetName) == TEXT("MoonMirror") || FString(PresetName) == TEXT("CinematicAnime")) ? TEXT("Wuthering") : TEXT("Genshin");
                for (UMaterialInterface* Parent : {Preset->CharacterMaterialOpaque.Get(), Preset->CharacterMaterialMasked.Get(), Preset->CharacterMaterialTranslucent.Get()}) {
                    Verify(Parent && Parent->GetPathName().Contains(TEXT("M_MMD_Reference_") + Family + TEXT("_")), TEXT("Cooked reference family mismatch: ") + AssetPath);
                }
                Verify(Preset->SurfaceMaterial && Preset->PanelMaterial && Preset->GlowMaterial,
                    TEXT("Cooked stage material references missing"));
                Verify(Preset->BeamMaterial != nullptr, TEXT("Cooked stage beam material missing"));
                Verify(Preset->BackdropMaterial != nullptr, TEXT("Cooked stage backdrop material missing"));
                Verify(FMath::IsFinite(Preset->ToonAmount) && FMath::IsFinite(Preset->LightScale), TEXT("Cooked preset values invalid"));
                PresetResults.Add(MakeShared<FJsonValueString>(AssetPath));
            }
            if (GEngine) for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
                if (Context.WorldType == EWorldType::Game && Context.World()) { ProbeWorld = Context.World(); break; }
            }
            Verify(ProbeWorld != nullptr, TEXT("Runtime game world missing"));
            for (const TCHAR* ThemeName : {TEXT("NeonConcert"), TEXT("MoonMirror"), TEXT("SakuraTheater")}) {
                const FString AssetName = FString(TEXT("BP_MMDStage_")) + ThemeName;
                const FString AssetPath = FString(TEXT("/Ue5MMDTools/Stages/Blueprints/")) + AssetName + TEXT(".") + AssetName + TEXT("_C");
                UClass* StageClass = LoadObject<UClass>(nullptr, *AssetPath);
                if (!Verify(StageClass && StageClass->IsChildOf(AMMDStageActor::StaticClass()), TEXT("Cooked stage Blueprint missing: ") + AssetPath) || !ProbeWorld) continue;
                AMMDStageActor* StageActor = ProbeWorld->SpawnActor<AMMDStageActor>(StageClass, FTransform::Identity);
                if (!Verify(StageActor != nullptr, TEXT("Cooked stage spawn failed"))) continue;
                Verify(StageActor->RenderPreset != nullptr, TEXT("Stage Blueprint lost its preset"));
                StageActor->bUseManualTime = true;
                StageActor->EvaluateStageAtTime(0.0f);
                TArray<UInstancedStaticMeshComponent*> Groups;
                StageActor->GetComponents(Groups);
                TArray<FTransform> OriginalTransforms;
                int32 InstanceCount = 0;
                for (UInstancedStaticMeshComponent* Group : Groups) {
                    Verify(Group->GetStaticMesh() && Group->GetMaterial(0), TEXT("Cooked stage geometry/material missing"));
                    for (int32 Index = 0; Index < Group->GetInstanceCount(); ++Index) {
                        FTransform Transform; Group->GetInstanceTransform(Index, Transform); OriginalTransforms.Add(Transform); ++InstanceCount;
                    }
                }
                StageActor->EvaluateStageAtTime(1.6f);
                int32 FlatIndex = 0, Changed = 0;
                for (UInstancedStaticMeshComponent* Group : Groups) for (int32 Index = 0; Index < Group->GetInstanceCount(); ++Index) {
                    FTransform Transform; Group->GetInstanceTransform(Index, Transform);
                    Verify(Transform.IsValid(), TEXT("Cooked stage animation transform invalid"));
                    if (OriginalTransforms.IsValidIndex(FlatIndex) && !Transform.Equals(OriginalTransforms[FlatIndex], 1.e-3)) ++Changed;
                    ++FlatIndex;
                }
                Verify(InstanceCount > 20 && Changed > 0, TEXT("Cooked stage motion/geometry missing"));
                TSharedRef<FJsonObject> StageResult = MakeShared<FJsonObject>();
                StageResult->SetStringField(TEXT("blueprint"), AssetPath);
                StageResult->SetNumberField(TEXT("instances"), InstanceCount);
                StageResult->SetNumberField(TEXT("moving_instances"), Changed);
                for (float SpecialTime : {std::numeric_limits<float>::max(), std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                    StageActor->EvaluateStageAtTime(SpecialTime);
                    for (UInstancedStaticMeshComponent* Group : Groups) for (int32 Index = 0; Index < Group->GetInstanceCount(); ++Index) {
                        FTransform Transform; Group->GetInstanceTransform(Index, Transform);
                        Verify(Transform.IsValid(), TEXT("Cooked stage produced invalid transform for extreme time"));
                    }
                }
                StageResult->SetBoolField(TEXT("extreme_time_checked"), true);
                StageResults.Add(MakeShared<FJsonValueObject>(StageResult));
                StageActor->Destroy();
            }
        }
        const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
        if (HasSpec && Spec->TryGetArrayField(TEXT("models"), Models)) {
            Verify(Models->Num() == 2, TEXT("Expected character and accessory models"));
            for (const TSharedPtr<FJsonValue>& ModelValue : *Models) {
                const TSharedPtr<FJsonObject> Model = ModelValue->AsObject();
                TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
                const FString MeshPath = Model->GetStringField(TEXT("mesh"));
                Result->SetStringField(TEXT("mesh"), MeshPath);
                TStrongObjectPtr<USkeletalMesh> Mesh(LoadObject<USkeletalMesh>(nullptr, *MeshPath));
                if (!Verify(Mesh.IsValid(), TEXT("Cooked mesh unavailable: ") + MeshPath)) continue;
                Verify(Mesh->GetSkeleton() != nullptr && Mesh->GetRefSkeleton().GetNum() == Model->GetIntegerField(TEXT("bones")), TEXT("Cooked skeleton data mismatch"));
                Verify(Mesh->GetMaterials().Num() == Model->GetIntegerField(TEXT("materials")), TEXT("Cooked material slot count mismatch"));
                for (const FSkeletalMaterial& Material : Mesh->GetMaterials()) {
                    Verify(Material.MaterialInterface != nullptr && Material.MaterialInterface->GetMaterial() != nullptr, TEXT("Cooked base material hard reference missing"));
                }
                const UMMDSkeletalMeshUserData* Data = Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>();
                Verify(Data && Data->ModelDataAsset && Data->ModelDataAsset->Bones.Num() == Mesh->GetRefSkeleton().GetNum() - 1, TEXT("Cooked ModelDataAsset hard reference missing"));
                if (Data && Data->ModelDataAsset) {
                    Verify(Data->ModelDataAsset->Rigids.Num() == Model->GetIntegerField(TEXT("rigid_bodies")), TEXT("Cooked rigid body serialization mismatch"));
                    Verify(Data->ModelDataAsset->Joints.Num() == Model->GetIntegerField(TEXT("joints")), TEXT("Cooked joint serialization mismatch"));
                }
                TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>(GetTransientPackage()));
                Component->SetSkeletalMeshAsset(Mesh.Get());
                if (ValidateStages) {
                    for (const TStrongObjectPtr<UMMDStageRenderPreset>& Preset : LoadedPresets) {
                        TArray<UMaterialInterface*> Originals;
                        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index) Originals.Add(Component->GetMaterial(Index));
                        Verify(UMMDStageLibrary::ApplyCharacterRenderPreset(Component.Get(), Preset.Get()), TEXT("Cooked character preset application failed"));
                        int32 RoleCounts[6] = {};
                        int32 MaskedHairSlots = 0;
                        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index) {
                            const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Component.Get(), Index);
                            const int32 RoleIndex = static_cast<int32>(Role);
                            if (RoleIndex >= 0 && RoleIndex < 6) ++RoleCounts[RoleIndex];
                            UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Index));
                            float BoundRole = -1.0f;
                            Verify(MID && MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("MaterialRole")), BoundRole)
                                && FMath::IsNearlyEqual(BoundRole, static_cast<float>(RoleIndex)), TEXT("Cooked material role binding mismatch"));
                            UMaterialInterface* Source = Originals[Index];
                            if (Source && Source->GetBlendMode() == BLEND_Translucent) {
                                FLinearColor Base = FLinearColor::White;
                                if (!Source->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DiffuseColor")), Base))
                                    Source->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), Base);
                                float Opacity = Base.A;
                                Source->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")), Opacity);
                                const bool MaskedHair = Preset->bUseMaskedHair && Role == EMMDMaterialRole::Hair
                                    && FMath::IsFinite(Base.A) && FMath::IsFinite(Opacity) && Base.A >= 0.999f && Opacity >= 0.999f;
                                Verify(MID && MID->GetBlendMode() == (MaskedHair ? BLEND_Masked : BLEND_Translucent),
                                    TEXT("Cooked hair depth or overlay transparency mismatch"));
                                if (MaskedHair) ++MaskedHairSlots;
                            }
                        }
                        if (Model->GetIntegerField(TEXT("vertices")) > 5000) {
                            Verify(RoleCounts[1] > 0 && RoleCounts[2] > 0 && RoleCounts[3] > 0 && RoleCounts[4] > 0 && RoleCounts[5] > 0, TEXT("Cooked character role classification missing"));
                            Verify(MaskedHairSlots > 0, TEXT("Cooked Miku hair failed to use the depth-writing masked parent"));
                        }
                        if (Component->GetNumMaterials() > 0) {
                            const EMMDMaterialRole OriginalRole = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Component.Get(), 0);
                            Verify(UMMDStageLibrary::SetCharacterMaterialRoleOverride(Component.Get(), 0, EMMDMaterialRole::Metal), TEXT("Cooked role override failed"));
                            Verify(UMMDStageLibrary::GetResolvedCharacterMaterialRole(Component.Get(), 0) == EMMDMaterialRole::Metal, TEXT("Cooked explicit role not resolved"));
                            float OverrideValue = -1.0f;
                            Verify(Component->GetMaterial(0)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("MaterialRole")), OverrideValue)
                                && FMath::IsNearlyEqual(OverrideValue, 4.0f), TEXT("Cooked explicit role not bound to MID"));
                            Verify(UMMDStageLibrary::ClearCharacterMaterialRoleOverride(Component.Get(), 0), TEXT("Cooked role override clear failed"));
                            Verify(UMMDStageLibrary::GetResolvedCharacterMaterialRole(Component.Get(), 0) == OriginalRole, TEXT("Cooked role classification not restored"));
                            float ClearedValue = -1.0f;
                            Verify(Component->GetMaterial(0)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("MaterialRole")), ClearedValue)
                                && FMath::IsNearlyEqual(ClearedValue, static_cast<float>(OriginalRole)), TEXT("Cooked cleared role not rebound to MID"));
                        }
                        Verify(UMMDStageLibrary::RestoreCharacterMaterials(Component.Get()), TEXT("Cooked character material restore failed"));
                        for (int32 Index = 0; Index < Originals.Num(); ++Index) {
                            Verify(Component->GetMaterial(Index) == Originals[Index], TEXT("Cooked preset restore changed original material"));
                        }
                    }
                }
                Verify(UMMDOutlineLibrary::ApplyMMDOutline(Component.Get()), TEXT("Runtime outline application failed"));
                int32 EnabledOutlines = 0;
                if (Data && Verify(Data->OutlineMaterials.Num() == Mesh->GetMaterials().Num(), TEXT("Cooked outline slot map missing"))) {
                    for (int32 Index = 0; Index < Data->OutlineMaterials.Num(); ++Index) {
                        UMaterialInterface* Material = Data->OutlineMaterials[Index];
                        Verify(Component->GetOverlayMaterial(true, Index) == Material, TEXT("Runtime overlay slot mismatch"));
                        if (Material) {
                            ++EnabledOutlines;
                            Verify(Material->GetMaterial() != nullptr, TEXT("Cooked outline parent missing"));
                        }
                    }
                }
                Verify(EnabledOutlines == Model->GetIntegerField(TEXT("outline_materials")), TEXT("Cooked outline count mismatch"));
                Result->SetNumberField(TEXT("outline_materials"), EnabledOutlines);
                const FString AnimationPath = Model->GetStringField(TEXT("animation"));
                if (!AnimationPath.IsEmpty()) {
                    TStrongObjectPtr<UAnimSequence> Animation(LoadObject<UAnimSequence>(nullptr, *AnimationPath));
                    if (Verify(Animation.IsValid(), TEXT("Cooked animation unavailable"))) {
                        Verify(Animation->GetSkeleton() == Mesh->GetSkeleton(), TEXT("Animation skeleton hard reference mismatch"));
                        Verify(Animation->IsBoneCompressedDataValid() && Animation->IsCurveCompressedDataValid(), TEXT("Cooked animation compression unavailable"));
                        if (ValidateStages && ProbeWorld && Data && Data->ModelDataAsset) {
                            Verify(!Data->ModelDataAsset->Rigids.IsEmpty(), TEXT("Cooked character physics data missing"));
                            AActor* PhysicsOwner = ProbeWorld->SpawnActor<AActor>();
                            USkeletalMeshComponent* PhysicsMesh = PhysicsOwner ? NewObject<USkeletalMeshComponent>(PhysicsOwner) : nullptr;
                            if (Verify(PhysicsMesh != nullptr, TEXT("Cooked physics playback component unavailable"))) {
                                PhysicsOwner->SetRootComponent(PhysicsMesh);
                                PhysicsOwner->AddInstanceComponent(PhysicsMesh);
                                PhysicsMesh->SetSkeletalMeshAsset(Mesh.Get());
                                PhysicsMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
                                PhysicsMesh->RegisterComponent();
                                UMMDPhysicsPlaybackAnimInstance* Playback = UMMDPhysicsPlaybackAnimInstance::PlayMMDAnimation(PhysicsMesh, Animation.Get());
                                if (Verify(Playback && Playback->IsPhysicsEnabled(), TEXT("Cooked playback did not enable default physics"))) {
                                    TArray<FTransform> PhysicalPose;
                                    auto SamplePlayback = [PhysicsMesh, Playback](bool Enabled) {
                                        Playback->SetPhysicsEnabled(Enabled);
                                        Playback->ResetPhysics();
                                        Playback->SetPlaying(true);
                                        for (int32 Step = 0; Step < 36; ++Step) {
                                            ++GFrameCounter;
                                            Playback->SetPosition(Step / 60.0f, false);
                                            PhysicsMesh->TickAnimation(1.0f / 60.0f, false);
                                            PhysicsMesh->RefreshBoneTransforms();
                                            PhysicsMesh->HandleExistingParallelEvaluationTask(true, true);
                                        }
                                    };
                                    SamplePlayback(true);
                                    const bool Initialized = Playback->IsPhysicsInitialized();
                                    PhysicalPose = PhysicsMesh->GetComponentSpaceTransforms();
                                    SamplePlayback(false);
                                    const TArray<FTransform>& PlainPose = PhysicsMesh->GetComponentSpaceTransforms();
                                    int32 ChangedPhysicsBones = 0;
                                    for (int32 Index = 0; Index < FMath::Min(PhysicalPose.Num(), PlainPose.Num()); ++Index) {
                                        Verify(PhysicalPose[Index].IsValid(), TEXT("Cooked physical pose contains invalid transform"));
                                        ChangedPhysicsBones += !PhysicalPose[Index].Equals(PlainPose[Index], 1.e-3) ? 1 : 0;
                                    }
                                    Verify(Initialized && ChangedPhysicsBones > 0, TEXT("Cooked default physics did not affect character pose"));
                                    Result->SetBoolField(TEXT("physics_initialized"), Initialized);
                                    Result->SetNumberField(TEXT("physics_rigid_bodies"), Playback->GetPhysicsRigidBodyCount());
                                    Result->SetNumberField(TEXT("physics_changed_bones"), ChangedPhysicsBones);
                                }
                                ProbeWorld->DestroyActor(PhysicsOwner);
                            }
                        }
                        int32 SampleCount = 0, VaryingBones = 0;
                        for (int32 Bone = 0; Bone < Mesh->GetRefSkeleton().GetNum(); ++Bone) {
                            FTransform First = Mesh->GetRefSkeleton().GetRefBonePose()[Bone];
                            Animation->GetBoneTransform(First, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(0.0), false);
                            bool Varies = false;
                            for (const double Fraction : {0.0, 0.37, 0.983333333}) {
                                FTransform Pose = Mesh->GetRefSkeleton().GetRefBonePose()[Bone];
                                Animation->GetBoneTransform(Pose, FSkeletonPoseBoneIndex(Bone), FAnimExtractContext(Animation->GetPlayLength() * Fraction), false);
                                Verify(Pose.IsValid(), TEXT("Cooked compressed bone sample invalid"));
                                Varies |= !Pose.Equals(First, 1.e-3);
                                ++SampleCount;
                            }
                            VaryingBones += Varies ? 1 : 0;
                        }
                        Verify(VaryingBones > 0, TEXT("Cooked animation is unexpectedly static"));
                        TArray<FName> Curves;
                        {
                            const UAnimSequence::FScopedCompressedAnimSequence Compressed = Animation->GetCompressedData();
                            for (const FAnimCompressedCurveIndexedName& Curve : Compressed.Get().IndexedCurveNames) Curves.Add(Curve.CurveName);
                        }
                        Verify(Curves.Num() == Model->GetIntegerField(TEXT("animation_morph_curves")), TEXT("Cooked morph curve count mismatch"));
                        for (const FName Curve : Curves) {
                            for (const double Fraction : {0.0, 0.37, 0.983333333}) {
                                Verify(FMath::IsFinite(Animation->EvaluateCurveData(Curve, FAnimExtractContext(Animation->GetPlayLength() * Fraction), false)), TEXT("Cooked compressed curve sample invalid"));
                            }
                        }
                        Result->SetStringField(TEXT("animation"), AnimationPath);
                        Result->SetNumberField(TEXT("sampled_transforms"), SampleCount);
                        Result->SetNumberField(TEXT("varying_bones"), VaryingBones);
                        Result->SetNumberField(TEXT("morph_curves"), Curves.Num());
                    }
                }
                ModelResults.Add(MakeShared<FJsonValueObject>(Result));
            }
        } else Verify(false, TEXT("Runtime spec contains no models"));
        FlushRenderingCommands();
        Report->SetArrayField(TEXT("models"), ModelResults);
        if (ValidateStages) {
            Report->SetArrayField(TEXT("render_presets"), PresetResults);
            Report->SetArrayField(TEXT("stages"), StageResults);
        }
        Report->SetArrayField(TEXT("errors"), Errors);
        Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
        Report->SetNumberField(TEXT("failed"), Errors.Num());
        FString Json;
        bool Saved = !ReportPath.IsEmpty() && FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
            && FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp, Display, TEXT("[MMDValidationSmoke] success=%d errors=%d report=%s"), Saved && Errors.IsEmpty(), Errors.Num(), *ReportPath);
        if (Saved && Errors.IsEmpty() && FParse::Param(FCommandLine::Get(), TEXT("MMDCaptureVisuals"))) {
            if (!BeginVisuals(Spec, ProbeWorld, MoveTemp(LoadedPresets))) FinishVisuals();
            return;
        }
        FPlatformMisc::RequestExitWithStatus(false, Saved && Errors.IsEmpty() ? 0 : 1);
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FMMDPluginRuntimeProbeModule, MMDPluginRuntimeProbe, "MMDPluginRuntimeProbe");
'@
        Write-Output "Prepared isolated runtime probe: $probeProject"
    }

    if ($Stage -eq 'Package' -or $Stage -eq 'All') {
        if (-not (Test-Path -LiteralPath $specPath) -or -not (Test-Path -LiteralPath $probeProject)) { throw '先使用 -Stage Prepare 生成探针。' }
        if (Test-Path -LiteralPath $packageReport) { Move-Item -LiteralPath $packageReport -Destination "$packageReport.previous" -Force }
        Invoke-Hidden $dotnetExe @($ubtDll, 'MMDPluginRuntimeProbeEditor', 'Win64', 'Development', "-Project=$probeProject", '-WaitMutex', '-NoHotReloadFromIDE') 'probe-editor-build' $TimeoutSeconds
        $cookMapArgument = '-map=/Engine/Maps/Entry'
        if ($IncludeStagePresets) {
            $cookMapArgument += '+/Ue5MMDTools/Stages/Maps/MMDStage_NeonConcert+/Ue5MMDTools/Stages/Maps/MMDStage_MoonMirror+/Ue5MMDTools/Stages/Maps/MMDStage_SakuraTheater'
        }
        Invoke-Hidden $dotnetExe @($uatDll, 'BuildCookRun', "-project=$probeProject", '-target=MMDPluginRuntimeProbe', '-platform=Win64',
            '-clientconfig=Development', '-nop4', '-unattended', '-utf8output', '-build', '-cook', '-stage', '-pak', '-archive',
            "-archivedirectory=$archiveDirectory", $cookMapArgument, '-nocompileuat') 'package' $TimeoutSeconds
        $executable = Get-ChildItem -LiteralPath $archiveDirectory -Recurse -Filter 'MMDPluginRuntimeProbe.exe' -File |
            Where-Object FullName -Match '[\\/]Binaries[\\/]Win64[\\/]' | Select-Object -First 1 -ExpandProperty FullName
        $paks = @(Get-ChildItem -LiteralPath $archiveDirectory -Recurse -Filter '*.pak' -File)
        if (-not $executable -or $paks.Count -eq 0) { throw 'UAT 未生成实际运行文件或 pak。' }
        Save-Utf8 $packageReport (@{ success = $true; executable = $executable; archive_directory = $archiveDirectory; pak_count = $paks.Count } | ConvertTo-Json)
        Write-Output "Cook/stage/pak complete: $archiveDirectory"
    }

    if ($Stage -eq 'Smoke' -or $Stage -eq 'All') {
        $package = Get-Content -LiteralPath $packageReport -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $package.success) { throw '打包探针没有成功记录。' }
        $executable = Assert-UnderPath $package.executable $script:ProbeDirectory
        if (Test-Path -LiteralPath $smokeReport) { Move-Item -LiteralPath $smokeReport -Destination "$smokeReport.previous" -Force }
        if ($CaptureVisuals -and (Test-Path -LiteralPath $visualReport)) { Move-Item -LiteralPath $visualReport -Destination "$visualReport.previous" -Force }
        # UE5.8 Slate MakeWindow 在 RenderOffscreen 下创建 dummy window，GameEngine 跳过 ShowWindow。
        $smokeArguments = @('-MMDValidationSmoke', "-MMDValidationSpec=$specPath", "-MMDValidationReport=$smokeReport",
            '-RenderOffscreen', '-unattended', '-nosplash', '-nosound', '-NoCrashDialog',
            "-abslog=$(Join-Path $script:ReportDirectory 'runtime.log')")
        if ($IncludeStagePresets) { $smokeArguments += @('-MMDStagePresetSmoke', '-d3d12') }
        else { $smokeArguments += '-NullRHI' }
        if ($CaptureVisuals) {
            $smokeArguments += @('-MMDCaptureVisuals', "-MMDVisualStage=$VisualStage", "-MMDVisualReport=$visualReport", "-MMDVisualDir=$visualDirectory", '-ResX=1280', '-ResY=720', '-ForceRes', '-windowed')
        }
        Invoke-Hidden $executable $smokeArguments 'runtime-smoke' $SmokeTimeoutSeconds
        $smoke = Get-Content -LiteralPath $smokeReport -Raw -Encoding UTF8 | ConvertFrom-Json
        if (-not $smoke.success -or $smoke.failed -ne 0) { throw "Cooked Runtime smoke 失败：$smokeReport" }
        Write-Output "Cooked runtime smoke PASS: $smokeReport"
        if ($CaptureVisuals) {
            $visual = Get-Content -LiteralPath $visualReport -Raw -Encoding UTF8 | ConvertFrom-Json
            $expectedVisualCaptures = if ($VisualStage -eq 'SakuraTheater') { 35 } else { 24 }
            if (-not $visual.success -or $visual.failed -ne 0 -or $visual.captures.Count -ne $expectedVisualCaptures) { throw "Cooked MainView visual 验证失败：$visualReport" }
            if ($visual.theme -ne $VisualStage -or ($VisualStage -eq 'SakuraTheater' -and -not $visual.sky_ready)) { throw "Cooked MainView 场景/天空验证失败：$visualReport" }
            $visualGalleryTitle = if ($VisualStage -eq 'SakuraTheater') { 'B · 樱庭水镜主视图对比' } else { 'MMD 打包主视图对比' }
            $visualBodyLabel = if ($VisualStage -eq 'SakuraTheater') { '角色全身' } else { '角色半身' }
            $visualDefaultView = if ($VisualStage -eq 'SakuraTheater') { 'body' } else { 'face' }
            $visualComparison = ''
            if ($VisualStage -eq 'SakuraTheater' -and (Test-Path -LiteralPath (Join-Path $validationDirectory 'SakuraDetailV5\before\cloth.png'))) {
                $visualComparison = '<h2>本轮法线细节与描边实拍</h2><p>同一模型、动作、相机、舞台、灯光与曝光。本轮调整通用折痕、发丝法线与分部位细描边。</p><section><article><h2>布料与发丝：调整前</h2><img src="../SakuraDetailV5/before/cloth.png" alt="上一轮布料与发丝"></article><article><h2>当前</h2><img src="screenshots/runtime_reference_genshin_cloth.png" alt="当前布料与发丝"></article><article><h2>脸部与发束：调整前</h2><img src="../SakuraDetailV5/before/face.png" alt="上一轮人物近景"></article><article><h2>当前</h2><img src="screenshots/runtime_reference_genshin_face.png" alt="当前人物近景"></article></section><h2>描边开关对照</h2><section><article><h2>细描边开启</h2><img src="screenshots/runtime_character_face_outline_restored.png" alt="角色细描边开启"></article><article><h2>描边关闭</h2><img src="screenshots/runtime_character_face_outline_off.png" alt="角色细描边关闭"></article></section><h2>四套材质响应</h2>'
            }
            $visualLabels = @{
                genshin = '原神方向'; wuthering = '鸣潮方向'; endfield = '终末地方向'; silver_city = '白银之城方向'
            }
            $visualCards = foreach ($family in @('genshin', 'wuthering', 'endfield', 'silver_city')) {
                $bodyUrl = "screenshots/runtime_reference_${family}_body.png"
                $faceUrl = "screenshots/runtime_reference_${family}_face.png"
                $defaultUrl = if ($visualDefaultView -eq 'body') { $bodyUrl } else { $faceUrl }
                @"
<article><h2>$($visualLabels[$family])</h2><img src="$defaultUrl" data-face="$faceUrl" data-body="$bodyUrl" data-cloth="screenshots/runtime_reference_${family}_cloth.png" data-cloth-off="screenshots/runtime_reference_${family}_cloth_details_off.png" data-face-off="screenshots/runtime_reference_${family}_face_details_off.png" data-body-off="screenshots/runtime_reference_${family}_body_details_off.png" alt="$($visualLabels[$family])"><p>相同角色、VMD 姿势、舞台、灯光、曝光与相机</p></article>
"@
            }
            $visualDiagnostics = ''
            if ($VisualStage -eq 'SakuraTheater') {
                $diagnosticLabels = @{ full='完整光照'; key_only='只有暖主光'; fill_only='只有冷补光'; rim_only='只有轮廓光'; sky_only='只有天空'; self_shadow_off='关闭角色自投影'; restored='重新启用自投影' }
                $diagnosticCards = foreach ($diagnostic in @('full','key_only','fill_only','rim_only','sky_only','self_shadow_off','restored')) {
                    '<article><h2>' + $diagnosticLabels[$diagnostic] + '</h2><img src="screenshots/runtime_character_' + $diagnostic + '.png" alt="' + $diagnosticLabels[$diagnostic] + '"></article>'
                }
                $visualDiagnostics = '<h2>人物光照拆分实拍</h2><p>同一机位与物理姿态，分别切换真实场景灯和角色自投影。暗面由方向性补光和天空支撑。</p><section>' + ($diagnosticCards -join "`n") + '</section>'
            }
            $visualGallery = @"
<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>$visualGalleryTitle</title>
<style>body{margin:0;background:#11151f;color:#eef3fa;font:16px/1.6 system-ui,sans-serif}main{max-width:1400px;margin:auto;padding:32px}p{color:#adbacb}nav{display:flex;gap:12px;margin:20px 0}button{padding:10px 18px;border:1px solid #556580;background:#222b3a;color:white;border-radius:8px;cursor:pointer}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(460px,1fr));gap:24px}article{background:#1b2230;border-radius:12px;overflow:hidden}article h2,article p{margin:14px 20px}img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain}@media(max-width:550px){main{padding:20px}section{grid-template-columns:1fr}}</style>
<main><h1>$visualGalleryTitle</h1><p>后台 RenderOffscreen 实际 GameViewport 截图。默认 Bullet 物理已推进；主视图 GPU 灯光数据和四级阴影均已读回验证。角色预设默认启用真实主光自投影，关闭选项或恢复原材质会恢复原阴影标签。</p>$visualComparison<nav><button data-view="face">脸部近景</button><button data-view="body">$visualBodyLabel</button></nav><section>$($visualCards -join "`n")</section><p>四种材质均使用通用几何着色与插件通用细节贴图，不需要模型专用的脸部阴影图。</p>$visualDiagnostics</main>
<script>let framing='$visualDefaultView',details=true;const cloth=document.createElement('button');cloth.dataset.view='cloth';cloth.textContent='布料近景';document.querySelector('nav').append(cloth);const toggle=document.createElement('button');toggle.textContent='关闭法线与织纹';document.querySelector('nav').append(toggle);function refresh(){document.querySelectorAll('img[data-face]').forEach(img=>img.src=img.getAttribute('data-'+framing+(details?'':'-off')));}document.querySelectorAll('button[data-view]').forEach(button=>button.onclick=()=>{framing=button.dataset.view;refresh();});toggle.onclick=()=>{details=!details;toggle.textContent=details?'关闭法线与织纹':'开启法线与织纹';refresh();};</script></html>
"@
            Save-Utf8 (Join-Path $script:ReportDirectory 'gallery.html') $visualGallery
            Write-Output "Cooked MainView screenshots PASS: $visualReport"
        }
    }
    exit 0
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
