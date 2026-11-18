[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputRoot,
    [string]$EngineDir = $env:UE_ENGINE_ROOT,
    [string]$PluginFile,
    [string]$MsvcVersion = '14.50.35717',
    [string]$VsInstallDir = 'C:\Program Files\Microsoft Visual Studio\18\Community',
    [string]$WindowsSdkVersion = '10.0.22621.0',
    [string]$UbaRoot = 'D:\Temp\MMD-UBA',
    [string]$DevBranch,
    [switch]$SkipExport
)

# 构建 Ue5MMDTools 的 Win64 发布包：准备工程本地 AutoSDK、跑 BuildPlugin、
# 清理不可发布文件、生成 ZIP 与 SHA-256。详见 Docs/BUILD_ENVIRONMENT.md。
$ErrorActionPreference = 'Stop'

function Resolve-EngineRoot {
    param([string]$Explicit, [string]$Association)
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { throw "EngineDir 不存在：$Explicit" }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    $sourceBuilds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
    if ($sourceBuilds -and $Association) {
        $value = $sourceBuilds.PSObject.Properties[$Association].Value
        if ($value -and (Test-Path -LiteralPath $value)) { return (Resolve-Path -LiteralPath $value).Path }
    }
    # 源码构建也可能以 GUID 为键登记；按路径名匹配版本号回退，避免依赖具体键名。
    if ($sourceBuilds -and $Association) {
        foreach ($property in $sourceBuilds.PSObject.Properties) {
            if ($property.Name -like 'PS*' -or -not $property.Value) { continue }
            if ((Split-Path -Leaf ($property.Value -replace '/', '\')) -match "UE[_ ]?$([regex]::Escape($Association))" -and
                (Test-Path -LiteralPath $property.Value)) {
                return (Resolve-Path -LiteralPath $property.Value).Path
            }
        }
    }
    $installed = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Association" -ErrorAction SilentlyContinue
    if ($installed -and $installed.InstalledDirectory) { return (Resolve-Path -LiteralPath $installed.InstalledDirectory).Path }
    $commonAppData = [Environment]::GetFolderPath('CommonApplicationData')
    if ($commonAppData -and (Test-Path -LiteralPath $commonAppData)) {
        $manifest = Join-Path $commonAppData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
    } else { $manifest = $null }
    if ($manifest -and (Test-Path -LiteralPath $manifest)) {
        $entry = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).InstallationList |
            Where-Object AppName -EQ "UE_$Association" | Select-Object -First 1
        if ($entry) { return (Resolve-Path -LiteralPath $entry.InstallLocation).Path }
    }
    throw '未找到 UE 引擎，请传 -EngineDir 或设置 UE_ENGINE_ROOT。'
}

$projectFile = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\MMD.uproject')).Path
$repoRoot = Split-Path -Parent $PSScriptRoot
$engineRoot = Resolve-EngineRoot -Explicit $EngineDir -Association ((Get-Content $projectFile -Raw | ConvertFrom-Json).EngineAssociation)

$script:steps = [System.Collections.Generic.List[object]]::new()
function Add-Step {
    param([string]$Name, [string]$Detail)
    $script:steps.Add([pscustomobject]@{ step = $Name; detail = $Detail; at = (Get-Date).ToString('o') })
    Write-Host "[$Name] $Detail"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
# 守卫只看仓库与引擎：导出树本来就在 OutputRoot 内，不能用插件目录反查。
foreach ($guard in @($engineRoot, $repoRoot)) {
    if ($OutputRoot.TrimEnd('\').StartsWith($guard.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $guard.TrimEnd('\').StartsWith($OutputRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputRoot 必须在仓库与引擎之外：$OutputRoot"
    }
}
if (Test-Path -LiteralPath $OutputRoot) { throw "OutputRoot 必须尚不存在，拒绝覆盖：$OutputRoot" }
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

# --- 先从开发分支导出轻量发布树，避免把本机截图和测试资源打进安装包 ---
if (-not $SkipExport) {
    $exportScript = Join-Path $PSScriptRoot 'Export-ReleaseBranch.ps1'
    if (-not (Test-Path -LiteralPath $exportScript)) { throw "缺少导出脚本：$exportScript" }
    if (-not $DevBranch) {
        $DevBranch = (& git -C $repoRoot rev-parse --abbrev-ref HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $DevBranch) { throw '无法确定当前分支，请传 -DevBranch。' }
    }
    $sourceTree = Join-Path $OutputRoot 'src\Ue5MMDTools'
    $exportLog = Join-Path $OutputRoot 'export.log'
    & $exportScript -OutputDir $sourceTree -DevBranch $DevBranch *> $exportLog
    if ($LASTEXITCODE -ne 0) { throw "发布树导出失败，见 $exportLog" }
    $exportReportPath = "$sourceTree.report.json"
    $exportReport = Get-Content -LiteralPath $exportReportPath -Raw | ConvertFrom-Json
    if (-not $exportReport.success) { throw "发布树导出未通过检查：$exportReportPath" }
    $pluginFile = (Resolve-Path -LiteralPath (Join-Path $sourceTree 'Ue5MMDTools.uplugin')).Path
    $pluginRoot = $sourceTree
    Add-Step 'export' ("{0} 个文件, {1} MB, 断链 {2}，禁配 {3}" -f $exportReport.file_count, $exportReport.total_mb,
        @($exportReport.broken_links).Count, @($exportReport.forbidden_files).Count)
}
else {
    $pluginFile = if ($PluginFile) { (Resolve-Path -LiteralPath $PluginFile).Path }
                  else { (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\Plugins\Ue5MMDTools\Ue5MMDTools.uplugin')).Path }
    $pluginRoot = Split-Path -Parent $pluginFile
}

foreach ($guard in @($pluginRoot, $engineRoot, $repoRoot)) {
    if ($OutputRoot.TrimEnd('\').StartsWith($guard.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -and
        -not $pluginRoot.StartsWith($OutputRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputRoot 不能位于插件、引擎或仓库内部：$OutputRoot"
    }
}

$engineVersion = (Get-Content -LiteralPath (Join-Path $engineRoot 'Engine\Build\Build.version') -Raw | ConvertFrom-Json)
$engineLabel = '{0}.{1}.{2}' -f $engineVersion.MajorVersion, $engineVersion.MinorVersion, $engineVersion.PatchVersion
$pluginVersion = (Get-Content -LiteralPath $pluginFile -Raw | ConvertFrom-Json).VersionName
if (-not $pluginVersion) { $pluginVersion = '0.0.0' }

$script:steps = [System.Collections.Generic.List[object]]::new()
function Add-Step {
    param([string]$Name, [string]$Detail)
    $script:steps.Add([pscustomobject]@{ step = $Name; detail = $Detail; at = (Get-Date).ToString('o') })
    Write-Host "[$Name] $Detail"
}

$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
foreach ($guard in @($pluginRoot, $engineRoot)) {
    if ((Test-Path -LiteralPath $OutputRoot) -or
        $OutputRoot.TrimEnd('\').StartsWith($guard.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $guard.TrimEnd('\').StartsWith($OutputRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputRoot 必须是插件和引擎之外尚不存在的新目录：$OutputRoot"
    }
}
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

# --- 工程本地 AutoSDK：让 UBT 通过 AutoSDK 路径使用已安装的 MSVC ---
$msvcDir = Join-Path $VsInstallDir "VC\Tools\MSVC\$MsvcVersion"
if (-not (Test-Path -LiteralPath (Join-Path $msvcDir 'bin\Hostx64\x64\cl.exe'))) {
    throw "未找到 MSVC $MsvcVersion：$msvcDir（用 -VsInstallDir/-MsvcVersion 指定实际安装）"
}
$autoSdk = Join-Path $OutputRoot 'AutoSDK'
$toolchain = Join-Path $autoSdk "HostWin64\Win64\VS2026\$MsvcVersion"
New-Item -ItemType Directory -Path (Split-Path $toolchain -Parent) -Force | Out-Null
New-Item -ItemType Junction -Path $toolchain -Target $msvcDir -Force | Out-Null
Add-Step 'auto_sdk' "UE_SDKS_ROOT=$autoSdk ($MsvcVersion -> $msvcDir)"

$env:UE_SDKS_ROOT = $autoSdk
$env:UBA_ROOT = $UbaRoot
New-Item -ItemType Directory -Path $UbaRoot -Force | Out-Null
Add-Step 'uba_root' "UBA_ROOT=$UbaRoot"

# --- BuildPlugin：Editor/Game Development + Shipping ---
$packageDir = Join-Path $OutputRoot 'Ue5MMDTools'
$buildLog = Join-Path $OutputRoot 'build-plugin.log'
$runUat = Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
if (-not (Test-Path -LiteralPath $runUat)) { throw "未找到 RunUAT：$runUat" }
& $runUat BuildPlugin "-Plugin=$pluginFile" "-Package=$packageDir" `
    '-TargetPlatforms=Win64' '-HostPlatforms=Win64' '-WaitForUATMutex' '-UTF8Output' *> $buildLog
if ($LASTEXITCODE -ne 0) { throw "BuildPlugin 失败，exit=$LASTEXITCODE，见 $buildLog" }
if (-not (Select-String -LiteralPath $buildLog -Pattern 'BUILD SUCCESSFUL' -Quiet)) {
    throw "BuildPlugin 日志中没有 BUILD SUCCESSFUL，拒绝打包：$buildLog"
}
Add-Step 'build_plugin' 'Editor Development / Game Development / Game Shipping 均通过'

# --- 清理不可发布内容 ---
$removed = [System.Collections.Generic.List[string]]::new()
foreach ($relative in @('Intermediate', 'HostProject')) {
    $target = Join-Path $packageDir $relative
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force; $removed.Add($relative) }
}
Get-ChildItem -LiteralPath (Join-Path $packageDir 'Binaries') -Recurse -File -Filter *.pdb -ErrorAction SilentlyContinue |
    ForEach-Object { $removed.Add("Binaries/$($_.Name)"); Remove-Item -LiteralPath $_.FullName -Force }
Add-Step 'clean' $(if ($removed.Count) { '移除 ' + ($removed -join ', ') } else { '无可移除项' })

# --- 独立性检查：必需文件在、禁配文件不在 ---
$required = @(
    'Ue5MMDTools.uplugin',
    'Binaries/Win64/UnrealEditor-Ue5MMDTools.dll',
    'Binaries/Win64/UnrealEditor-Ue5MMDToolsEditor.dll',
    'Source/Ue5MMDTools/Ue5MMDTools.Build.cs',
    'ThirdParty/Bullet/lib/BulletCollision.lib',
    'Shaders/Core/TMMDEndfieldNativeBSDF.ush'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir ($relative -replace '/', '\')))) {
        throw "发布包缺少必需文件：$relative"
    }
}
$forbidden = @(Get-ChildItem -LiteralPath $packageDir -Recurse -File |
    Where-Object { $_.Extension -in '.pmx', '.pmd', '.wav', '.mp3' })
if ($forbidden.Count -gt 0) {
    throw "发布包含有禁配文件：$($forbidden[0].FullName)"
}
Add-Step 'audit' "必需文件齐全，禁配文件 0 个"

# --- ZIP 与校验值 ---
$zipName = "Ue5MMDTools-v$pluginVersion-UE$engineLabel-Win64.zip"
$zipPath = Join-Path $OutputRoot $zipName
Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $zipPath -CompressionLevel Optimal
$sha256 = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
$shaPath = "$zipPath.sha256"
# 校验文件用 LF 与双空格，兼容 sha256sum -c 与 PowerShell 两条路径。
[System.IO.File]::WriteAllText($shaPath, "$sha256  $zipName`n", [System.Text.UTF8Encoding]::new($false))
Add-Step 'archive' "$zipName ($([math]::Round((Get-Item $zipPath).Length / 1MB, 1)) MB, sha256=$sha256)"

$report = [ordered]@{
    schema              = 'mmd.build.package.v1'
    captured_at         = (Get-Date).ToString('o')
    plugin_version      = $pluginVersion
    engine              = $engineLabel
    engine_changelist   = $engineVersion.Changelist
    msvc_version        = $MsvcVersion
    windows_sdk         = $WindowsSdkVersion
    zip                 = $zipPath
    zip_bytes           = (Get-Item $zipPath).Length
    sha256              = $sha256
    removed             = @($removed)
    steps               = @($script:steps)
    build_log           = $buildLog
}
$reportPath = Join-Path $OutputRoot 'package-report.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8NoBOM
Write-Host "完成：$zipPath"
$report | ConvertTo-Json -Depth 6
