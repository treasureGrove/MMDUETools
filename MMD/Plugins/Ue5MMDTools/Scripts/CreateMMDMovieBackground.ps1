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

$ErrorActionPreference = 'Stop'
$movieProcess = $null
$encodeProcess = $null
$probeProcess = $null
$outputRoot = $null
$outputCreated = $false
$cancelRequest = $null
$phase = 'prepare'

function Resolve-MovieInput([string]$InputPath) {
    if ($InputPath -match '["\r\n]') { throw '输入路径不能包含引号或换行。' }
    return (Resolve-Path -LiteralPath $InputPath).Path.Replace('\', '/')
}

function Get-MovieSHA256([string]$InputPath) {
    $hashStream = [IO.File]::OpenRead($InputPath)
    $shaAlgorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($shaAlgorithm.ComputeHash($hashStream)).Replace('-', '') }
    finally { $shaAlgorithm.Dispose(); $hashStream.Dispose() }
}

function Assert-MovieAssetPath([string]$AssetPath) {
    if ($AssetPath -notmatch '^/[^/\s"\\.]+(?:/[^/\s"\\.]+)+\.[^/\s"\\.]+$') {
        throw "资产路径须为 Unreal 对象路径（如 /Game/MMD/Asset.Asset）：$AssetPath"
    }
}

function Test-MovieCancellation {
    return $cancelRequest -and (Test-Path -LiteralPath $cancelRequest -PathType Leaf)
}

function Assert-MovieNotCancelled {
    if (Test-MovieCancellation) {
        throw [OperationCanceledException]::new('已取消后台制作，保留已输出的文件和日志。')
    }
}

function Wait-MovieChildExit([Diagnostics.Process]$Process) {
    while (-not $Process.WaitForExit(250)) { Assert-MovieNotCancelled }
    $Process.WaitForExit()
    Assert-MovieNotCancelled
    if ($null -eq $Process.ExitCode) { throw '无法读取后台子进程的退出码。' }
    return $Process.ExitCode
}

function Close-MovieChild([Diagnostics.Process]$Process) {
    if (-not $Process) { return }
    try {
        if (-not $Process.HasExited) {
            # UE 可能仍有 ShaderCompileWorker，只回收本次启动的子进程树。
            $killer = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\taskkill.exe') `
                -ArgumentList @('/PID', "$($Process.Id)", '/T', '/F') -WindowStyle Hidden -Wait -PassThru
            $killer.Dispose()
            if (-not $Process.WaitForExit(5000)) { $Process.Kill() }
        }
    }
    catch { Write-Warning "后台子进程清理未完成：$($_.Exception.Message)" }
    finally { $Process.Dispose() }
}

try {
    if ($OutputDirectory -match '["\r\n]') { throw '输出路径不能包含引号或换行。' }
    $outputRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory).Replace('\', '/')
    # 每次制作使用新目录，保留已有成片、帧和诊断记录。
    if (Test-Path -LiteralPath $outputRoot) { throw "输出目录已存在，请使用新目录：$outputRoot" }
    New-Item -ItemType Directory -Path $outputRoot | Out-Null
    $outputCreated = $true
    $cancelRequest = Join-Path $outputRoot 'cancel.request'
    Assert-MovieNotCancelled

    if (-not $ProjectFile) {
        $ProjectFile = Join-Path $PSScriptRoot '..\..\..\MMD.uproject'
        if (-not (Test-Path -LiteralPath $ProjectFile -PathType Leaf)) { throw '请提供宿主工程的 -ProjectFile。' }
    }
    $ProjectFile = Resolve-MovieInput $ProjectFile
    $EngineRoot = Resolve-MovieInput $EngineRoot
    if ([bool]$ModelDirectory -eq [bool]$ModelAsset) { throw 'ModelDirectory 与 ModelAsset 必须选择一个。' }
    if ([bool]$Motion -eq [bool]$AnimationAsset) { throw 'Motion 与 AnimationAsset 必须选择一个。' }
    if ($AnimationAsset -and $Face) { throw 'AnimationAsset 已包含表情曲线，不能追加 Face VMD。' }
    if ($ModelDirectory) { $ModelDirectory = Resolve-MovieInput $ModelDirectory }
    if ($ModelAsset) { Assert-MovieAssetPath $ModelAsset }
    if ($Motion) { $Motion = Resolve-MovieInput $Motion }
    if ($AnimationAsset) { Assert-MovieAssetPath $AnimationAsset }
    if ($Face) { $Face = Resolve-MovieInput $Face }
    if ($Camera) { $Camera = Resolve-MovieInput $Camera }
    if ($Audio) { $Audio = Resolve-MovieInput $Audio }
    if ($CameraMode -eq 'Fixed') {
        if (-not $CameraLocation -or -not $CameraRotation -or -not $PSBoundParameters.ContainsKey('CameraFOV') -or
            $CameraLocation -match '["\r\n]' -or $CameraRotation -match '["\r\n]') {
            throw '固定机位必须提供 CameraLocation、CameraRotation 和 CameraFOV。'
        }
    }
    if ($MatchPreviewRendering) {
        foreach ($key in @('PreviewSubstrate', 'PreviewNativeMode', 'PreviewUnifiedDiffuse', 'PreviewGBufferFormat',
                'PreviewToonResolution', 'PreviewIndirectDiffuse', 'PreviewIndirectSpecular', 'PreviewVirtualShadows')) {
            if (-not $PSBoundParameters.ContainsKey($key)) { throw "匹配预览渲染缺少参数 -$key。" }
        }
    }
    if ($Preset -notmatch '^[A-Za-z0-9_]+$' -or $Stage -notmatch '^[A-Za-z0-9_]+$') {
        throw 'Preset 和 Stage 必须使用预设标识。'
    }
    if (($Width % 2) -ne 0 -or ($Height % 2) -ne 0) { throw 'H.264 输出的宽、高必须为偶数。' }
    $editorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not (Test-Path -LiteralPath $editorExe)) { throw "未找到后台 UE：$editorExe" }
    if (-not $FFmpeg) {
        $ffmpegCommand = Get-Command ffmpeg -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $ffmpegCommand) { throw '未找到 ffmpeg。请把 ffmpeg 和 ffprobe 放到 PATH，或传入 -FFmpeg。' }
        $FFmpeg = $ffmpegCommand.Source
    }
    $FFmpeg = Resolve-MovieInput $FFmpeg
    $ffprobeExe = Join-Path (Split-Path -Parent $FFmpeg) 'ffprobe.exe'
    if (-not (Test-Path -LiteralPath $ffprobeExe -PathType Leaf)) {
        $ffprobeCommand = Get-Command ffprobe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $ffprobeCommand) { throw '未找到 ffprobe。请将它放在 ffmpeg 同一目录或 PATH。' }
        $ffprobeExe = $ffprobeCommand.Source
    }

    if ($Audio -and $Duration -eq 0) {
        $audioDurationText = & $ffprobeExe '-v' 'error' '-show_entries' 'format=duration' '-of' 'default=nw=1:nk=1' $Audio
        if ($LASTEXITCODE -ne 0) { throw '无法读取音频时长。' }
        $Duration = [double]::Parse(($audioDurationText | Select-Object -First 1), [Globalization.CultureInfo]::InvariantCulture)
    }
    if (-not $SkipBuild) {
        $phase = 'build'
        $buildScript = Join-Path $PSScriptRoot '..\..\..\Scripts\BuildPlugin.ps1'
        if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
            throw '宿主工程未提供 Scripts/BuildPlugin.ps1；请先构建插件，再加 -SkipBuild。'
        }
        & $buildScript -EngineRoot $EngineRoot -ProjectFile $ProjectFile -LogPath (Join-Path $outputRoot 'build.log')
        if ($LASTEXITCODE -ne 0) { throw "构建失败，退出码 $LASTEXITCODE" }
    }
    Assert-MovieNotCancelled

    $numberCulture = [Globalization.CultureInfo]::InvariantCulture
    $movieArgs = @(
        ('"' + $ProjectFile + '"'), '-run=MMDCreateMovie',
        ('-Output="' + $outputRoot + '"'), ('-Preset=' + $Preset), ('-Stage=' + $Stage),
        ('-CameraMode=' + $CameraMode), ('-Physics=' + $Physics), ('-ImportVmdMorphs=' + $ImportVmdMorphs),
        ('-PlaybackRate=' + $PlaybackRate.ToString('0.########', $numberCulture)),
        ('-Width=' + $Width), ('-Height=' + $Height), ('-Fps=' + $Fps),
        '-AllowCommandletRendering', '-RenderOffscreen', '-d3d12', '-sm6',
        '-unattended', '-nopause', '-nosplash', '-nosound', '-NoCrashDialog',
        '-stdout', '-UTF8Output', '-FullStdOutLogOutput', '-DisablePlugins=RenderDocPlugin',
        '-ddc=InstalledNoZenLocalFallback'
    )
    if ($MatchPreviewRendering) {
        $rendererSettings = '[/Script/Engine.RendererSettings]'
        $substrateSetting = if ($PreviewSubstrate -eq 1) { 'True' } else { 'False' }
        $previewOverrides = @(
            "${rendererSettings}:r.Substrate=$substrateSetting",
            "${rendererSettings}:r.Substrate.Experimental.ToonUnifiedDiffuse=$PreviewUnifiedDiffuse",
            "${rendererSettings}:r.Substrate.ProjectGBufferFormat=$PreviewGBufferFormat",
            "${rendererSettings}:r.Shadow.Virtual.Enable=$PreviewVirtualShadows",
            "[ConsoleVariables]:r.Shadow.Virtual.Enable=$PreviewVirtualShadows",
            "[ConsoleVariables]:r.Substrate.ToonProfile.Resolution=$PreviewToonResolution"
        )
        if ($PreviewNativeMode -eq -1) {
            # 后端依次读取 Engine 和 Game，两个配置层都需要覆盖。
            $disabledBackend = '[Ue5MMDTools.EndfieldNativeBackend]:Enabled=False'
            $previewOverrides += $disabledBackend
            $movieArgs += '-ini:Game:' + $disabledBackend
        } else {
            $movieArgs += @(
                '-MMDEndfieldNativeBackend', "-MMDEndfieldNativeBackendMode=$PreviewNativeMode",
                ('-MMDEndfieldNativeIndirectDiffuseScale=' + $PreviewIndirectDiffuse.ToString('R', $numberCulture)),
                ('-MMDEndfieldNativeIndirectSpecularScale=' + $PreviewIndirectSpecular.ToString('R', $numberCulture))
            )
        }
        $movieArgs += '-ini:Engine:' + ($previewOverrides -join ',')
    } else {
        $movieArgs += @(
            '-MMDEndfieldNativeBackend', '-MMDEndfieldNativeBackendMode=2',
            '-ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True,[/Script/Engine.RendererSettings]:r.Substrate.Experimental.ToonUnifiedDiffuse=0,[/Script/Engine.RendererSettings]:r.Substrate.ProjectGBufferFormat=1,[ConsoleVariables]:r.Substrate.ToonProfile.Resolution=256'
        )
    }
    $movieArgs += '-abslog="' + (Join-Path $outputRoot 'engine.log') + '"'
    if ($ModelDirectory) { $movieArgs += '-ModelDirectory="' + $ModelDirectory + '"' }
    if ($ModelAsset) { $movieArgs += '-ModelAsset="' + $ModelAsset + '"' }
    if ($Motion) { $movieArgs += '-Motion="' + $Motion + '"' }
    if ($AnimationAsset) { $movieArgs += '-AnimationAsset="' + $AnimationAsset + '"' }
    if ($CameraMode -eq 'Fixed') {
        $movieArgs += '-CameraLocation="' + $CameraLocation + '"'
        $movieArgs += '-CameraRotation="' + $CameraRotation + '"'
        $movieArgs += '-CameraFOV=' + $CameraFOV.ToString('0.########', $numberCulture)
    }
    if ($Duration -gt 0) { $movieArgs += '-Duration=' + $Duration.ToString('0.########', $numberCulture) }
    if ($Face) { $movieArgs += '-Face="' + $Face + '"' }
    if ($Camera) { $movieArgs += '-Camera="' + $Camera + '"' }
    if ($CaptureDiagnostic -eq 'NoShadows') { $movieArgs += '-NoCaptureShadows' }
    if ($CaptureDiagnostic -eq 'NoIndirect') { $movieArgs += '-NoCaptureIndirect' }
    if ($CaptureDiagnostic -eq 'NoOutline') { $movieArgs += '-NoCaptureOutline' }
    if ($ShadowNormalBias -ge 0) { $movieArgs += '-ShadowNormalBias=' + $ShadowNormalBias.ToString('0.###', $numberCulture) }
    if ($ConventionalShadows) { $movieArgs += '-ConventionalShadows' }
    $started = [DateTimeOffset]::Now
    $request = [ordered]@{
        started_at = $started.ToString('o'); project = $ProjectFile; model_directory = $ModelDirectory
        motion = $Motion; face = $Face; camera = $Camera; audio = $Audio
        duration_seconds = $Duration; width = $Width; height = $Height; fps = $Fps; preset = $Preset; stage = $Stage; camera_mode = $CameraMode
        model_asset = $ModelAsset; animation_asset = $AnimationAsset; physics_enabled = ($Physics -eq 1); playback_rate = $PlaybackRate
        import_vmd_morphs = ($ImportVmdMorphs -eq 1)
        sources = @()
    }
    if ($CameraMode -eq 'Fixed') {
        $request.fixed_camera_location = $CameraLocation
        $request.fixed_camera_rotation = $CameraRotation
        $request.fixed_camera_fov = $CameraFOV
    }
    if ($MatchPreviewRendering) {
        $request.preview_rendering = [ordered]@{
            substrate = $PreviewSubstrate; native_mode = $PreviewNativeMode; unified_diffuse = $PreviewUnifiedDiffuse
            gbuffer_format = $PreviewGBufferFormat; toon_resolution = $PreviewToonResolution
            indirect_diffuse = $PreviewIndirectDiffuse; indirect_specular = $PreviewIndirectSpecular; virtual_shadows = $PreviewVirtualShadows
        }
    }
    foreach ($sourceFile in @($Motion, $Face, $Camera, $Audio)) {
        if ($sourceFile) {
            $request.sources += @{ path = $sourceFile; sha256 = (Get-MovieSHA256 $sourceFile) }
        }
    }
    $request | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $outputRoot 'request.json') -Encoding UTF8
    Assert-MovieNotCancelled
    $phase = 'render'
    $movieProcess = Start-Process -FilePath $editorExe -ArgumentList $movieArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $outputRoot 'engine.stdout.log') `
        -RedirectStandardError (Join-Path $outputRoot 'engine.stderr.log')
    $null = $movieProcess.Handle
    $movieProcess.Id | Set-Content -LiteralPath (Join-Path $outputRoot 'engine.pid')
    Write-Output "MMD 后台渲染已启动，PID=$($movieProcess.Id)，目录=$outputRoot"
    $deadline = $started.AddSeconds($TimeoutSeconds)
    $lastCount = -1
    $nextProgress = $started
    $cancelDeadline = $null
    while (-not $movieProcess.WaitForExit(250)) {
        $now = [DateTimeOffset]::Now
        if (Test-MovieCancellation) {
            if (-not $cancelDeadline) { $cancelDeadline = $now.AddSeconds(8) }
            if ($now -gt $cancelDeadline) { Assert-MovieNotCancelled }
            continue
        }
        if ($now -gt $deadline) {
            throw '后台渲染超时，已保留已完成的帧和日志。'
        }
        if ($now -ge $nextProgress) {
            $nextProgress = $now.AddSeconds(1)
            $frameCount = @(Get-ChildItem -LiteralPath (Join-Path $outputRoot 'frames') -Filter 'frame_*.png' -ErrorAction SilentlyContinue).Count
            if ($frameCount -ge ($lastCount + 30)) {
                Write-Output "MMD 已输出 $frameCount 帧"
                $lastCount = $frameCount
            }
        }
    }
    $movieProcess.WaitForExit()
    Assert-MovieNotCancelled
    $engineExit = $movieProcess.ExitCode
    if ($null -eq $engineExit) { throw '无法读取后台 UE 的退出码。' }
    $reportPath = Join-Path $outputRoot 'report.json'
    if (-not (Test-Path -LiteralPath $reportPath)) { throw "UE 未生成制作报告，退出码 $engineExit。请查看 engine.log。" }
    $renderReport = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($engineExit -eq 2 -and $renderReport.cancelled) { throw [OperationCanceledException]::new('已取消后台渲染。') }
    if ($engineExit -ne 0 -or -not $renderReport.success) {
        throw "UE 制作失败（退出码 $engineExit）：$($renderReport.error)。报告：$reportPath"
    }
    $frames = @(Get-ChildItem -LiteralPath (Join-Path $outputRoot 'frames') -Filter 'frame_*.png').Count
    if ($frames -le 0 -or $frames -ne $renderReport.frames) { throw '渲染报告与实际图像帧数不一致。' }

    $phase = 'encode'
    Assert-MovieNotCancelled
    $movieFile = Join-Path $outputRoot 'mmd_movie.mp4'
    $encodeArgs = @('-hide_banner', '-nostdin', '-n', '-framerate', "$Fps", '-start_number', '0', '-i', (Join-Path $outputRoot 'frames/frame_%06d.png'))
    if ($Audio) { $encodeArgs += @('-i', $Audio) }
    $encodeArgs += @('-map', '0:v:0', '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p')
    if ($Audio) {
        $videoDuration = ([double]$frames / $Fps).ToString('0.########', $numberCulture)
        $encodeArgs += @('-map', '1:a:0', '-c:a', 'aac', '-b:a', '192k', '-af', 'apad', '-t', $videoDuration)
    }
    $encodeArgs += @('-movflags', '+faststart', $movieFile)
    $quotedEncodeArgs = $encodeArgs | ForEach-Object { '"' + $_ + '"' }
    $encodeProcess = Start-Process -FilePath $FFmpeg -ArgumentList $quotedEncodeArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $outputRoot 'encode.stdout.log') `
        -RedirectStandardError (Join-Path $outputRoot 'encode.log')
    $null = $encodeProcess.Handle
    $encodeExit = Wait-MovieChildExit $encodeProcess
    if ($encodeExit -ne 0) { throw "视频编码失败，退出码 $encodeExit。请查看 encode.log。" }
    $phase = 'verify'
    Assert-MovieNotCancelled
    $probePath = Join-Path $outputRoot 'video_probe.json'
    $probeArgs = @('-v', 'error', '-count_frames', '-show_format', '-show_streams', '-of', 'json', ('"' + $movieFile + '"'))
    $probeProcess = Start-Process -FilePath $ffprobeExe -ArgumentList $probeArgs -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $probePath -RedirectStandardError (Join-Path $outputRoot 'video_probe.log')
    $null = $probeProcess.Handle
    if ((Wait-MovieChildExit $probeProcess) -ne 0) { throw '成片校验失败。请查看 video_probe.log。' }
    $probe = Get-Content -LiteralPath $probePath -Raw -Encoding UTF8 | ConvertFrom-Json
    $videoStream = $probe.streams | Where-Object codec_type -EQ 'video' | Select-Object -First 1
    if (-not $videoStream -or [int]$videoStream.nb_read_frames -ne $frames) { throw '成片视频帧数与渲染帧数不一致。' }
    if ($Audio -and -not ($probe.streams | Where-Object codec_type -EQ 'audio')) { throw '成片缺少音轨。' }
    $movieHash = Get-MovieSHA256 $movieFile
    Assert-MovieNotCancelled
    [ordered]@{
        success = $true; completed_at = [DateTimeOffset]::Now.ToString('o'); frames = $frames
        elapsed_seconds = ([DateTimeOffset]::Now - $started).TotalSeconds; movie = $movieFile
        sha256 = $movieHash
        size_bytes = (Get-Item -LiteralPath $movieFile).Length
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'production.json') -Encoding UTF8
    Write-Output "MMD 制作完成：$movieFile"
    exit 0
}
catch {
    $cancelled = $_.Exception -is [OperationCanceledException]
    if ($outputCreated) {
        @{ success = $false; cancelled = $cancelled; phase = $phase; error = $_.Exception.Message; failed_at = [DateTimeOffset]::Now.ToString('o') } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'production_failed.json') -Encoding UTF8
    }
    if ($cancelled) { Write-Output $_.Exception.Message; exit 2 }
    Write-Error $_ -ErrorAction Continue
    exit 1
}
finally {
    Close-MovieChild $probeProcess
    Close-MovieChild $encodeProcess
    Close-MovieChild $movieProcess
}
