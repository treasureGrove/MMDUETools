[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('import-pmx', 'import-vmd', 'render', 'validate')]
    [string]$Command,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

# Ue5MMDTools 命令行入口：把子命令转成 UnrealEditor-Cmd 的 Commandlet 调用。
# 用法：
#   .\ue5mmd.ps1 import-pmx -Pmx D:\model\a.pmx [-Name A] [-Package /Game/MMDModels] [-StandardMaterials] [-Force]
#   .\ue5mmd.ps1 import-vmd -Vmd D:\motion.vmd -Mesh /Game/MMDModels/A/Model/A [-Pmx ...] [-Name Walk] [-Force]
#   .\ue5mmd.ps1 render -ModelAsset /Game/... [-Motion D:\m.vmd] -Output D:\out\movie.mp4 [-Preset ...] [-Stage ...]
#   .\ue5mmd.ps1 validate
# 公共参数：-Project <xxx.uproject>  -Engine <引擎目录>  -Json  （打印 JSON 结果）
$ErrorActionPreference = 'Stop'

function Parse-Extra {
    param([string[]]$Items)
    $options = @{}
    $flags = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $Items.Count; $i++) {
        $item = $Items[$i]
        if (-not $item.StartsWith('-')) { throw "无法识别的参数：$item" }
        $name = $item.TrimStart('-')
        if ($i + 1 -lt $Items.Count -and -not $Items[$i + 1].StartsWith('-')) {
            $options[$name] = $Items[$i + 1]
            $i++
        }
        else {
            $flags.Add($name)
        }
    }
    return [pscustomobject]@{ Options = $options; Flags = $flags }
}

$parsed = Parse-Extra -Items $Rest
$options = $parsed.Options
$flags = $parsed.Flags

# 每个子命令允许转发的参数（白名单，避免把宿主工程参数误传给 Commandlet）。
$allowed = @{
    'import-pmx' = @('Pmx', 'Name', 'Report', 'StandardMaterials', 'Force')
    'import-vmd' = @('Vmd', 'Mesh', 'Pmx', 'Name', 'Report', 'Force')
    'render'     = @('ModelDirectory', 'ModelAsset', 'Motion', 'AnimationAsset', 'Face', 'Camera', 'Output',
                     'Preset', 'Stage', 'CameraMode', 'CameraLocation', 'CameraRotation', 'CameraFOV',
                     'Physics', 'ImportVmdMorphs', 'PlaybackRate', 'Width', 'Height', 'Fps',
                     'MaxFrames', 'Duration', 'Warmup', 'ShadowNormalBias', 'Report', 'NoPhysics',
                     'NoCaptureOutline', 'NoCaptureShadows', 'NoCaptureIndirect', 'StageUseStudioLight', 'ConventionalShadows')
    'validate'   = @()
}
$commandlet = @{
    'import-pmx' = 'MMDImportPmx'
    'import-vmd' = 'MMDImportVmd'
    'render'     = 'MMDCreateMovie'
    'validate'   = 'MMDValidateImport'
}

foreach ($name in $options.Keys) {
    if ($name -in @('Project', 'Engine', 'Json', 'Log')) { continue }
    if ($name -notin $allowed[$Command]) { throw "子命令 $Command 不接受参数 -$name" }
}
foreach ($name in $flags) {
    if ($name -in @('Json', 'Quiet')) { continue }
    if ($name -notin $allowed[$Command]) { throw "子命令 $Command 不接受开关 -$name" }
}

# 工程：显式 -Project，或在当前目录向上找一个 .uproject。
$projectFile = $options['Project']
if (-not $projectFile) {
    $found = Get-ChildItem -LiteralPath (Get-Location).Path -Filter *.uproject -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($found) { $projectFile = $found.FullName }
}
if (-not $projectFile -or -not (Test-Path -LiteralPath $projectFile)) {
    throw '未找到工程，请用 -Project <xxx.uproject> 指定。'
}
$projectFile = (Resolve-Path -LiteralPath $projectFile).Path
$projectDir = Split-Path -Parent $projectFile

# 引擎：显式 -Engine，或按工程 EngineAssociation 从注册表读取。
$engineDir = $options['Engine']
if (-not $engineDir) {
    $association = (Get-Content -LiteralPath $projectFile -Raw | ConvertFrom-Json).EngineAssociation
    $builds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($builds -and $association) {
        $value = $builds.PSObject.Properties[$association].Value
        if ($value -and (Test-Path -LiteralPath $value)) { $engineDir = $value }
    }
    if (-not $engineDir -and $builds -and $association) {
        $match = $builds.PSObject.Properties |
            Where-Object { $_.Name -notlike 'PS*' -and $_.Value -and (Split-Path -Leaf ($_.Value -replace '/', '\')) -match "UE[_ ]?$([regex]::Escape($association))" } |
            Select-Object -First 1
        if ($match) { $engineDir = $match.Value }
    }
}
if (-not $engineDir) { throw '未找到引擎，请用 -Engine <引擎目录> 指定。' }
$editorCmd = Join-Path $engineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $editorCmd)) { throw "未找到 UnrealEditor-Cmd.exe：$editorCmd" }

# 报告：未显式指定时由本期命令写到 Saved。
$reportPath = $options['Report']
$logPath = $options['Log']
if (-not $logPath) { $logPath = Join-Path $projectDir "Saved\Logs\ue5mmd-$Command.log" }
New-Item -ItemType Directory -Path (Split-Path $logPath) -Force | Out-Null

$args = @($projectFile, "-run=$($commandlet[$Command])")
foreach ($name in $options.Keys) {
    if ($name -in @('Project', 'Engine', 'Json', 'Quiet', 'Log')) { continue }
    $args += "-$name=$($options[$name])"
}
foreach ($name in $flags) {
    if ($name -in @('Json', 'Quiet')) { continue }
    $args += "-$name"
}
$args += @('-unattended', '-nosplash', '-nullrhi', '-nopause', '-NoCrashDialog', '-stdout', "-abslog=$logPath")

if (-not $flags.Contains('Quiet')) {
    Write-Host "运行 $Command → $($commandlet[$Command])" -ForegroundColor Cyan
    Write-Host ($args -join ' ') -ForegroundColor DarkGray
}
& $editorCmd @args
$exit = $LASTEXITCODE

if ($flags.Contains('Json') -and $reportPath -and (Test-Path -LiteralPath $reportPath)) {
    Get-Content -LiteralPath $reportPath -Raw
}
elseif ($exit -ne 0) {
    Write-Host "失败（exit=$exit）。日志尾部：" -ForegroundColor Red
    Get-Content -LiteralPath $logPath -Tail 20
}
exit $exit
