[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$EngineRoot,
    [string]$ModelDirectory,
    [string]$Motion,
    [Parameter(Mandatory = $true)] [string]$OutputDirectory,
    [string]$Face,
    [string]$Camera,
    [ValidateSet('Vmd', 'Showcase', 'Fixed')] [string]$CameraMode = 'Vmd',
    [string]$Audio,
    [string]$ProjectFile,
    [string]$FFmpeg,
    [string]$Preset = 'InfinityNikki',
    # 默认白色影棚；更多展示：RoseStudio / SlateGallery / PearlAtrium / LilacStudio。
    # 也保留 SkyGlass / VioletStage / WarmAtelier 和已有完整舞台 ID。
    [string]$Stage = 'Studio',
    [ValidateSet('None', 'NoShadows', 'NoIndirect', 'NoOutline')] [string]$CaptureDiagnostic = 'None',
    [ValidateRange(-1, 10)] [double]$ShadowNormalBias = -1,
    [switch]$ConventionalShadows,
    [ValidateRange(0, 3600)] [double]$Duration = 0,
    [ValidateRange(320, 3840)] [int]$Width = 1280,
    [ValidateRange(180, 2160)] [int]$Height = 720,
    [ValidateSet(15, 30, 60)] [int]$Fps = 30,
    [ValidateRange(60, 86400)] [int]$TimeoutSeconds = 14400,
    [switch]$SkipBuild,
    [string]$ModelAsset,
    [string]$AnimationAsset,
    [string]$CameraLocation,
    [string]$CameraRotation,
    [ValidateRange(1, 170)] [double]$CameraFOV,
    [ValidateSet(0, 1)] [int]$Physics = 1,
    [ValidateRange(0.1, 4)] [double]$PlaybackRate = 1,
    [switch]$MatchPreviewRendering,
    [ValidateSet(0, 1)] [int]$PreviewSubstrate,
    [ValidateSet(-1, 0, 1, 2)] [int]$PreviewNativeMode,
    [ValidateSet(0, 1)] [int]$PreviewUnifiedDiffuse,
    [ValidateSet(0, 1)] [int]$PreviewGBufferFormat,
    [int]$PreviewToonResolution,
    [ValidateRange(0, 8)] [double]$PreviewIndirectDiffuse,
    [ValidateRange(0, 8)] [double]$PreviewIndirectSpecular,
    [ValidateSet(0, 1)] [int]$PreviewVirtualShadows,
    [ValidateSet(0, 1)] [int]$ImportVmdMorphs = 1
)

# 保留源码宿主的旧入口；插件安装也使用同一份实现。
$runner = Join-Path $PSScriptRoot '..\Plugins\Ue5MMDTools\Scripts\CreateMMDMovieBackground.ps1'
if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
    Write-Error "未找到插件后台制作脚本：$runner"
    exit 1
}
if (-not $ProjectFile) {
    $PSBoundParameters['ProjectFile'] = Join-Path $PSScriptRoot '..\MMD.uproject'
}
& $runner @PSBoundParameters
exit $LASTEXITCODE
