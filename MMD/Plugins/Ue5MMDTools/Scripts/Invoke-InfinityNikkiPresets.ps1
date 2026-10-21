[CmdletBinding()]
param(
    [string]$EngineRoot = $env:UE_ENGINE_ROOT,
    [string]$CharacterSample,
    [switch]$SkipBuild,
    [switch]$NoGenerate,
    [switch]$SkipCapture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$project_root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$project_file = Join-Path $project_root 'MMD.uproject'
if (-not $EngineRoot) {
    $association = (Get-Content -LiteralPath $project_file -Raw | ConvertFrom-Json).EngineAssociation
    $manifest = Join-Path $env:ProgramData 'Epic/UnrealEngineLauncher/LauncherInstalled.dat'
    if (Test-Path -LiteralPath $manifest) {
        $EngineRoot = ((Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object AppName -EQ "UE_$association" | Select-Object -First 1).InstallLocation
    }
}
if (-not $EngineRoot) { throw '请用 -EngineRoot 指定项目关联的 UE 引擎。' }
$editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editor -PathType Leaf)) { throw "未找到 $editor" }
$output_dir = Join-Path $project_root ('Saved/InfinityNikkiValidation/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $output_dir -Force | Out-Null

# 输出独立于旧预设；失败保留完整日志，不以进程正常退出代替 shader 或截图校验。
if (-not $SkipBuild) {
    & (Join-Path $project_root 'Scripts/BuildPlugin.ps1') -EngineRoot $EngineRoot -ProjectFile $project_file `
        -LogPath (Join-Path $output_dir 'build.log')
    if ($LASTEXITCODE -ne 0) { throw "Editor 编译失败：$output_dir" }
}
$render_flags = @('-AllowCommandletRendering', '-RenderOffscreen', '-d3d12', '-sm6', '-unattended',
    '-nopause', '-nosplash', '-nosound', '-NoCrashDialog', '-stdout', '-UTF8Output',
    '-DisablePlugins=RenderDocPlugin', '-ddc=InstalledNoZenLocalFallback',
    '-MMDEndfieldNativeBackend', '-MMDEndfieldNativeBackendMode=2',
    '-ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True,[/Script/Engine.RendererSettings]:r.Substrate.Experimental.ToonUnifiedDiffuse=0,[/Script/Engine.RendererSettings]:r.Substrate.ProjectGBufferFormat=1,[/Script/Engine.RendererSettings]:r.Shadow.Virtual.Enable=1,[ConsoleVariables]:r.Substrate.ToonProfile.Resolution=256')
$preset_report = Join-Path $output_dir 'presets.json'
$generate_flags = @()
if (-not $NoGenerate) { $generate_flags += '-Generate' }
& $editor $project_file '-run=MMDInfinityNikkiPresets' @generate_flags "-Report=$preset_report" @render_flags `
    "-abslog=$(Join-Path $output_dir 'generate.log')" *> (Join-Path $output_dir 'generate.stdout.txt')
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $preset_report)) {
    throw "预设生成或校验失败：$output_dir"
}
$preset_result = Get-Content -LiteralPath $preset_report -Raw | ConvertFrom-Json
if (-not $preset_result.success -or $preset_result.materials.Count -ne 6 -or $preset_result.profile_count -ne 6 -or
    $preset_result.render_path -ne 'native_substrate') { throw '原生预设报告未通过。' }
foreach ($material in $preset_result.materials) {
    if (-not $material.passed -or $material.shader_count -le 0) {
        throw "父材质真实编译未通过：$($material.asset)"
    }
}
if (-not $SkipCapture) {
    $capture_report = Join-Path $output_dir 'capture.json'
    $sample_flags = @()
    if ($CharacterSample) {
        $sample_root = Join-Path $project_root 'Saved/BackgroundValidation/ImportedAssets'
        $sample_manifest = [IO.Path]::GetFullPath((Join-Path $sample_root "$CharacterSample/manifest.json"))
        if (-not $sample_manifest.StartsWith($sample_root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'CharacterSample 必须是已有导入样本的目录名。'
        }
        $sample_flags += "-SampleManifest=$sample_manifest"
    }
    & $editor $project_file '-run=MMDEndfieldNativeBackendProbe' `
        '-NativePreset=/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki.DA_Render_InfinityNikki' `
        '-Tag=nikki_native' '-Exp=ab,backend,lighting' '-PoseTime=-1' '-Frames=48' '-LumenTracking' `
        @sample_flags `
        "-Report=$capture_report" "-OutDir=$(Join-Path $output_dir 'screenshots')" @render_flags `
        "-abslog=$(Join-Path $output_dir 'capture.log')" *> (Join-Path $output_dir 'capture.stdout.txt')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $capture_report)) {
        throw "引擎实拍校验失败：$output_dir"
    }
    $capture_result = Get-Content -LiteralPath $capture_report -Raw | ConvertFrom-Json
    if (-not $capture_result.success -or -not $capture_result.draw_verified -or $capture_result.image_count -le 0) {
        throw "原生实拍报告未通过：$capture_report"
    }
}
Write-Host "无限暖暖风格预设验证完成：$output_dir"
