[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$EngineRoot,
    [string]$ProjectFile,
    [ValidateSet('All', 'Imports', 'Materials')]
    [string]$Stage = 'All',
    [switch]$SkipBuild,
    [switch]$RepairBaseMasked,
    [switch]$PreviewScreenshots,
    [int]$TimeoutSeconds = 1200
)

$ErrorActionPreference = 'Stop'
try {
    if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\MMD.uproject' }
    $ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    $commandletExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not (Test-Path -LiteralPath $commandletExe)) { throw "未找到后台入口：$commandletExe" }
    if ($TimeoutSeconds -le 0 -or $TimeoutSeconds -gt 86400) { throw 'TimeoutSeconds 必须在 1 到 86400 之间。' }

    $reportDirectory = Join-Path (Split-Path -Parent $ProjectFile) 'Saved\BackgroundValidation'
    New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'BuildPlugin.ps1') -EngineRoot $EngineRoot -ProjectFile $ProjectFile
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    $stages = @()
    if ($Stage -eq 'All' -or $Stage -eq 'Imports') {
        $stages += @{ Name = 'imports'; Commandlet = 'MMDValidateImport'; Flags = @('-NullRHI'); Report = 'import-tests.json' }
    }
    if ($Stage -eq 'All' -or $Stage -eq 'Materials') {
        $stages += @{ Name = 'materials'; Commandlet = 'MMDValidateMaterials'; Flags = @('-AllowCommandletRendering', '-RenderOffscreen', '-d3d12'); Report = 'materials.json' }
    }
    foreach ($validationStage in $stages) {
        $stageName = $validationStage.Name
        $stageLog = Join-Path $reportDirectory "$stageName.log"
        $stageReport = Join-Path $reportDirectory $validationStage.Report
        # 不允许上次成功的报告掩盖本次启动失败。
        if (Test-Path -LiteralPath $stageReport) {
            Move-Item -LiteralPath $stageReport -Destination "$stageReport.previous" -Force
        }
        if ($stageName -eq 'materials' -and $RepairBaseMasked) {
            $validationStage.Flags += '-RepairBaseMasked'
        }
        if ($stageName -eq 'materials' -and $PreviewScreenshots) {
            $validationStage.Flags += '-PreviewScreenshots'
        }
        $arguments = @(
            ('"' + $ProjectFile + '"'),
            ('-run=' + $validationStage.Commandlet),
            '-unattended', '-nopause', '-nosplash', '-nosound', '-NoCrashDialog',
            '-stdout', '-FullStdOutLogOutput', '-UTF8Output', '-DisablePlugins=RenderDocPlugin',
            ('-abslog="' + $stageLog + '"')
        ) + $validationStage.Flags
        # 仅启动隐藏的 commandlet，不启动完整编辑器或交互窗口。
        $validationProcess = Start-Process -FilePath $commandletExe -ArgumentList $arguments -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $reportDirectory "$stageName.stdout.log") `
            -RedirectStandardError (Join-Path $reportDirectory "$stageName.stderr.log")
        if (-not $validationProcess.WaitForExit($TimeoutSeconds * 1000)) {
            Stop-Process -Id $validationProcess.Id -Force -ErrorAction SilentlyContinue
            throw "$stageName 验证超时，请检查 $stageLog"
        }
        $validationProcess.Refresh()
        $commandletExitCode = $validationProcess.ExitCode
        if (-not (Test-Path -LiteralPath $stageReport)) {
            throw "$stageName 未生成本次报告；退出码 $commandletExitCode，请检查 $stageLog"
        }
        $validationResult = Get-Content -LiteralPath $stageReport -Raw -Encoding UTF8 | ConvertFrom-Json
        Write-Output "$stageName : passed=$($validationResult.passed), failed=$($validationResult.failed), exit=$commandletExitCode"
        if ($commandletExitCode -ne 0 -or -not $validationResult.success) {
            throw "$stageName 验证失败，请检查 $stageReport 和 $stageLog"
        }
    }
    exit 0
}
catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
