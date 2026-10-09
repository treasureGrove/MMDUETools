[CmdletBinding()]
param(
    [string]$EngineRoot = $env:UE_ENGINE_ROOT,
    [string]$ProjectFile,
    [string]$Target,
    [ValidateSet('DebugGame', 'Development', 'Shipping')]
    [string]$Configuration = 'Development',
    [string]$Platform = 'Win64',
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'
try {
    if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\MMD.uproject' }
    $ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
    $projectSpec = Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json
    if (-not $Target) {
        $Target = [System.IO.Path]::GetFileNameWithoutExtension($ProjectFile) + 'Editor'
    }

    if (-not $EngineRoot) {
        $association = $projectSpec.EngineAssociation
        $sourceBuilds = Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
        if ($sourceBuilds) {
            $EngineRoot = $sourceBuilds.PSObject.Properties[$association].Value
        }
        if (-not $EngineRoot) {
            $installedEngine = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$association" -ErrorAction SilentlyContinue
            if ($installedEngine) { $EngineRoot = $installedEngine.InstalledDirectory }
        }
        $launcherManifest = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
        if (-not $EngineRoot -and (Test-Path -LiteralPath $launcherManifest)) {
            $installations = (Get-Content -LiteralPath $launcherManifest -Raw | ConvertFrom-Json).InstallationList
            $EngineRoot = ($installations | Where-Object AppName -EQ "UE_$association" | Select-Object -First 1).InstallLocation
        }
    }
    if (-not $EngineRoot) {
        throw '未找到项目关联的 UE 引擎，请传入 -EngineRoot 或设置 UE_ENGINE_ROOT。'
    }
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    $buildBatch = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
    if (-not (Test-Path -LiteralPath $buildBatch)) { throw "未找到构建入口：$buildBatch" }

    if (-not $LogPath) {
        $LogPath = Join-Path (Split-Path -Parent $ProjectFile) 'Saved\BackgroundValidation\build.log'
    }
    $LogPath = [System.IO.Path]::GetFullPath($LogPath)
    New-Item -ItemType Directory -Path (Split-Path -Parent $LogPath) -Force | Out-Null
    # UBT 管理规则和增量构建，不禁用 PCH，也不修改引擎项目。
    & $buildBatch $Target $Platform $Configuration "-Project=$ProjectFile" '-WaitMutex' '-NoHotReloadFromIDE' "-Log=$LogPath"
    exit $LASTEXITCODE
}
catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
