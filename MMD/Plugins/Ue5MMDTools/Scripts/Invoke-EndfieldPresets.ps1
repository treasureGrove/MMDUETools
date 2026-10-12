#Requires -Version 7.4
<#
.SYNOPSIS
    终末地预设端到端验证：静态检查 → 编译 → 生成资产 → 校验报告。

.DESCRIPTION
    在 Windows 主机上运行（沙箱内无法执行 UE，因为 UE 只提供 Windows 二进制）。

    步骤：
      1. 运行两个静态检查器（shader 结构 + C++ 结构/接线），失败即中止。
      2. 用项目既有 BuildPlugin.ps1 编译 Editor 模块，保留真实退出码。
      3. 运行 MMDEndfieldPresets commandlet（-Generate）生成三套父材质与预设。
      4. 读取 JSON 报告，逐项核对成功标志与 SM6 编译结果。

    脚本不修改引擎、不重建旧预设、不触碰白名单之外的资产；
    失败时保留日志与报告原样，不把"有输出"当成通过。

.PARAMETER EngineRoot
    UE 引擎根目录。省略时由 BuildPlugin.ps1 从项目关联自动解析。

.PARAMETER SkipBuild
    跳过编译（仅当确认二进制已是最新时使用）。

.PARAMETER NoGenerate
    只做校验，不让 commandlet 写盘（用于预检）。

.EXAMPLE
    pwsh -File Invoke-EndfieldPresets.ps1
#>
[CmdletBinding()]
param(
    [string]$EngineRoot = $env:UE_ENGINE_ROOT,
    [switch]$SkipBuild,
    [switch]$NoGenerate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$script:RepoRoot = 'D:\projectGithub\MMDUETools'
$script:ProjectRoot = Join-Path $script:RepoRoot 'MMD'
$script:PluginRoot = Join-Path $script:ProjectRoot 'Plugins\Ue5MMDTools'
$script:ProjectFile = Join-Path $script:ProjectRoot 'MMD.uproject'
$script:LogRoot = Join-Path $script:RepoRoot 'MMD\Saved\EndfieldValidation'
$script:ReportPath = Join-Path $script:LogRoot 'generate-report.json'
$script:SummaryPath = Join-Path $script:LogRoot 'run-summary.json'

function Write-Step([string]$Message) {
    Write-Host ''
    Write-Host "=== $Message ===" -ForegroundColor Cyan
}

function Invoke-Python([string]$Script, [string[]]$Arguments) {
    # -I 隔离模式：忽略环境变量与用户站点目录，避免加载到非预期模块。
    $output = & python -I $Script @Arguments 2>&1
    $code = $LASTEXITCODE
    $output | ForEach-Object { Write-Host $_ }
    return $code
}

function Get-Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

$steps = [System.Collections.Generic.List[object]]::new()
$failed = $false

if (-not (Test-Path -LiteralPath $script:LogRoot)) {
    New-Item -ItemType Directory -Path $script:LogRoot -Force | Out-Null
}

# ---- 0. 记录被验证源码的哈希，便于事后追溯 ----
Write-Step '记录输入哈希'
$tracked = @(
    'Shaders\Core\TMMDEndfieldLighting.ush',
    'Shaders\TMMDShader\TMMDEndfieldCharacter.usf',
    'Source\Ue5MMDToolsEditor\Private\Editor\MMDEndfieldPresetsCommandlet.cpp',
    'Source\Ue5MMDToolsEditor\Private\Editor\MMDEndfieldFaceSdfBaker.cpp'
)
$hashes = @{}
foreach ($rel in $tracked) {
    $full = Join-Path $script:PluginRoot $rel
    $hash = Get-Sha256 $full
    $hashes[$rel] = $hash
    if ($null -eq $hash) { Write-Host "  [X] 缺少源文件：$rel" -ForegroundColor Red; $failed = $true }
    else { Write-Host "  [i] $rel = $hash" }
}

# ---- 1. 静态检查 ----
if (-not $failed) {
    Write-Step 'shader 结构静态检查'
    $code = Invoke-Python (Join-Path $script:PluginRoot 'Scripts\validate_endfield_shaders.py') @($script:PluginRoot)
    $steps.Add([pscustomobject]@{ Step = 'validate_shaders'; ExitCode = $code })
    if ($code -ne 0) { $failed = $true }

    Write-Step 'C++ 结构静态检查'
    $code = Invoke-Python (Join-Path $script:PluginRoot 'Scripts\validate_endfield_sources.py') @($script:PluginRoot)
    $steps.Add([pscustomobject]@{ Step = 'validate_sources'; ExitCode = $code })
    if ($code -ne 0) { $failed = $true }
}

# ---- 2. 编译 Editor 模块 ----
if (-not $failed -and -not $SkipBuild) {
    Write-Step '编译 Editor 模块'
    $buildScript = Join-Path $script:ProjectRoot 'Scripts\BuildPlugin.ps1'
    $buildArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
                   '-ProjectFile', $script:ProjectFile)
    if ($EngineRoot) { $buildArgs += @('-EngineRoot', $EngineRoot) }
    & pwsh @buildArgs
    $code = $LASTEXITCODE
    $steps.Add([pscustomobject]@{ Step = 'build'; ExitCode = $code })
    Write-Host "  编译退出码：$code"
    if ($code -ne 0) { $failed = $true }
}

# ---- 3. 生成资产 ----
if (-not $failed) {
    Write-Step '生成终末地父材质与预设'
    $editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not $EngineRoot -or -not (Test-Path -LiteralPath $editorCmd)) {
        Write-Host "  [X] 未找到 UnrealEditor-Cmd.exe。请用 -EngineRoot 指定引擎根目录。" -ForegroundColor Red
        $failed = $true
    }
    else {
        if (Test-Path -LiteralPath $script:ReportPath) { Remove-Item -LiteralPath $script:ReportPath -Force }
        # 启动参数沿用项目已验证的 MainViewGenerate 批次：
        # 固定 d3d12/sm6、离线渲染、独立 DDC，避免共享缓存干扰 shader 编译结果。
        $cacheRoot = Join-Path $script:LogRoot 'DDC'
        $logPath = Join-Path $script:LogRoot 'generate.log'
        $runArgs = [System.Collections.Generic.List[string]]::new()
        $runArgs.Add($script:ProjectFile)
        $runArgs.Add('-run=MMDEndfieldPresets')
        $runArgs.Add("-Report=$script:ReportPath")
        if (-not $NoGenerate) { $runArgs.Add('-Generate') }
        foreach ($flag in @('-AllowCommandletRendering', '-RenderOffscreen', '-d3d12', '-sm6',
                            '-unattended', '-nopause', '-nosplash', '-nosound', '-NoCrashDialog',
                            '-stdout', '-UTF8Output', '-DisablePlugins=RenderDocPlugin',
                            '-ddc=InstalledNoZenLocalFallback')) {
            $runArgs.Add($flag)
        }
        $runArgs.Add("-LocalDataCachePath=$cacheRoot")
        $runArgs.Add("-abslog=$logPath")
        & $editorCmd $runArgs.ToArray()
        $code = $LASTEXITCODE
        $steps.Add([pscustomobject]@{ Step = 'generate'; ExitCode = $code })
        Write-Host "  生成退出码：$code"
        if ($code -ne 0) { $failed = $true }
    }
}

# ---- 4. 校验报告 ----
if (-not $failed) {
    Write-Step '校验生成报告'
    if (-not (Test-Path -LiteralPath $script:ReportPath -PathType Leaf)) {
        Write-Host "  [X] 未生成报告：$script:ReportPath" -ForegroundColor Red
        $failed = $true
    }
    else {
        $report = Get-Content -LiteralPath $script:ReportPath -Raw | ConvertFrom-Json
        Write-Host "  success       = $($report.success)"
        Write-Host "  generated     = $($report.generated)"
        Write-Host "  签名          = $($report.source_signature)"
        Write-Host "  材质输入数量  = $($report.main_input_count)"
        Write-Host "  预设          = $($report.preset)"
        if (-not $report.success) {
            Write-Host '  [X] 报告判定失败：' -ForegroundColor Red
            foreach ($e in $report.errors) { Write-Host "      $e" -ForegroundColor Red }
            $failed = $true
        }
        foreach ($m in $report.materials) {
            if (-not $m.passed) {
                Write-Host "  [X] 材质未通过：$($m.asset)" -ForegroundColor Red
                foreach ($e in $m.errors) { Write-Host "      $e" -ForegroundColor Red }
                $failed = $true
            }
        }
        foreach ($a in $report.assets) {
            if (-not $a.passed) {
                Write-Host "  [X] 资产未通过：$($a.asset)" -ForegroundColor Red
                $failed = $true
            }
        }
    }
}

# ---- 5. 汇总 ----
$summary = [pscustomobject]@{
    CheckedAt    = (Get-Date).ToUniversalTime().ToString('o')
    Success      = (-not $failed)
    Generate     = (-not $NoGenerate)
    SourceHashes = $hashes
    Steps        = $steps
    Report       = $script:ReportPath
    Note         = '静态检查与资产生成通过不代表画面达到终末地目标；请人工看图验收。'
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $script:SummaryPath -Encoding utf8

Write-Step '结果'
if ($failed) {
    Write-Host '验证失败。请检查以上 [X] 项与日志，不要把"有输出"当成通过。' -ForegroundColor Red
    exit 1
}
Write-Host '验证步骤全部通过。' -ForegroundColor Green
Write-Host "汇总：$script:SummaryPath"
Write-Host '注意：画面是否达到终末地水准需要人工看图判断，本脚本不做视觉验收。'
exit 0
