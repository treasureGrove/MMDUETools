[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [string]$ProjectFile,
    [switch]$SkipBuild,
    [switch]$SkipScreenshots,
    [switch]$RegenerateTextures,
    [ValidateSet('', 'NeonConcert', 'MoonMirror', 'SakuraTheater', 'SilverCity', 'OpenWorldToon', 'IndustrialAnime', 'CinematicAnime')]
    [string]$CapturePreset = '',
    [string]$OutputDirectory,
    [ValidateRange(32, 1024)][int]$TextureResolution = 256,
    [int]$TextureSeed = 2026,
    [ValidateRange(1, 86400)][int]$TimeoutSeconds = 2400
)

$ErrorActionPreference = 'Stop'
try {
    if (-not $ProjectFile) { $ProjectFile = Join-Path $PSScriptRoot '..\MMD.uproject' }
    $ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
    $EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
    if (($TextureResolution -band ($TextureResolution - 1)) -ne 0) { throw 'TextureResolution 必须是二次幂。' }
    $projectDirectory = Split-Path -Parent $ProjectFile
    $reportDirectory = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $projectDirectory 'Saved\BackgroundValidation\Stages' }
    $reportPath = Join-Path $reportDirectory 'stages.json'
    $screenshotDirectory = Join-Path $reportDirectory 'screenshots'
    New-Item -ItemType Directory -Force -Path $reportDirectory, $screenshotDirectory | Out-Null
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot 'BuildPlugin.ps1') -EngineRoot $EngineRoot -ProjectFile $ProjectFile -LogPath (Join-Path $reportDirectory 'build.log')
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    $commandletExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
    if (-not (Test-Path -LiteralPath $commandletExe)) { throw "未找到后台引擎入口：$commandletExe" }
    if (Test-Path -LiteralPath $reportPath) {
        Move-Item -LiteralPath $reportPath -Destination "$reportPath.previous" -Force
    }
    $arguments = @(
        $ProjectFile, '-run=MMDStagePresets', '-Generate',
        "-Report=$reportPath", "-ScreenshotDir=$screenshotDirectory",
        "-TextureResolution=$TextureResolution", "-TextureSeed=$TextureSeed",
        '-AllowCommandletRendering', '-RenderOffscreen', '-d3d12',
        '-unattended', '-nopause', '-nosplash', '-nosound', '-NoCrashDialog',
        '-stdout', '-FullStdOutLogOutput', '-UTF8Output', '-DisablePlugins=RenderDocPlugin',
        "-abslog=$(Join-Path $reportDirectory 'stages.log')"
    )
    if (-not $SkipScreenshots) { $arguments += '-Screenshots' }
    if ($RegenerateTextures) { $arguments += '-RegenerateTextures' }
    if ($CapturePreset) { $arguments += "-CapturePreset=$CapturePreset" }
    $quotedArguments = foreach ($argument in $arguments) {
        if ($argument.Contains('"') -or $argument.Contains("`n")) { throw '后台启动参数不能含引号或换行。' }
        '"' + $argument + '"'
    }
    # 窗口隐藏且使用离屏渲染；截图来自 UE 的真实舞台场景。
    $stageProcess = Start-Process -FilePath $commandletExe -ArgumentList $quotedArguments -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $reportDirectory 'stages.stdout.log') `
        -RedirectStandardError (Join-Path $reportDirectory 'stages.stderr.log')
    if (-not $stageProcess.WaitForExit($TimeoutSeconds * 1000)) {
        Stop-Process -Id $stageProcess.Id -Force -ErrorAction SilentlyContinue
        throw '舞台生成或验证超时，请检查 stages.log。'
    }
    $stageProcess.Refresh()
    if (-not (Test-Path -LiteralPath $reportPath)) { throw "未生成本次舞台报告，退出码 $($stageProcess.ExitCode)。" }
    $stageReport = Get-Content -LiteralPath $reportPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($stageProcess.ExitCode -ne 0 -or -not $stageReport.success) { throw "舞台验证失败，请检查 $reportPath。" }
    if (-not $SkipScreenshots) {
        $presetLabels = @{
            NeonConcert = '霓虹演唱会'; MoonMirror = '月光镜庭'; SakuraTheater = '樱庭水镜'
            SilverCity = '银白柔光 · 白银之城方向'; OpenWorldToon = '明快卡通 · 原神方向'
            IndustrialAnime = '工业动画 · 终末地方向'; CinematicAnime = '电影冷暖 · 鸣潮方向'
            Reference_Genshin = '原神方向 · 固定灯光母材质对比'; Reference_Wuthering = '鸣潮方向 · 固定灯光母材质对比'
            Reference_Endfield = '终末地方向 · 固定灯光母材质对比'; Reference_SilverCity = '白银之城方向 · 固定灯光母材质对比'
        }
        $cards = foreach ($shot in $stageReport.screenshots) {
            $beautyFile = [IO.Path]::GetFileName($shot.captures[0].output)
            $detailFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'character_detail' | Select-Object -First 1).output)
            $motionFile = [IO.Path]::GetFileName(($shot.captures | Where-Object time_seconds -GT 0 | Select-Object -First 1).output)
            $faceFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'face_detail' | Select-Object -First 1).output)
            $turnFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'face_turn' | Select-Object -First 1).output)
            $heroFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'hero_full_body' | Select-Object -First 1).output)
            $environmentFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'environment_only' | Select-Object -First 1).output)
            $offFile = [IO.Path]::GetFileName(($shot.captures | Where-Object framing -EQ 'lights_off' | Select-Object -First 1).output)
            if (-not $detailFile) { $detailFile = $beautyFile }
            if (-not $motionFile) { $motionFile = $beautyFile }
            if (-not $faceFile) { $faceFile = $detailFile }
            if (-not $turnFile) { $turnFile = $faceFile }
            if (-not $heroFile) { $heroFile = $beautyFile }
            if (-not $environmentFile) { $environmentFile = $faceFile }
            if (-not $offFile) { $offFile = $faceFile }
            $label = [Net.WebUtility]::HtmlEncode($(if ($presetLabels.ContainsKey($shot.name)) { $presetLabels[$shot.name] } else { $shot.name }))
            $asset = [Net.WebUtility]::HtmlEncode($shot.preset)
            $beautyUrl = 'screenshots/' + [Uri]::EscapeDataString($beautyFile)
            $detailUrl = 'screenshots/' + [Uri]::EscapeDataString($detailFile)
            $motionUrl = 'screenshots/' + [Uri]::EscapeDataString($motionFile)
            $faceUrl = 'screenshots/' + [Uri]::EscapeDataString($faceFile)
            $turnUrl = 'screenshots/' + [Uri]::EscapeDataString($turnFile)
            $heroUrl = 'screenshots/' + [Uri]::EscapeDataString($heroFile)
            $environmentUrl = 'screenshots/' + [Uri]::EscapeDataString($environmentFile)
            $offUrl = 'screenshots/' + [Uri]::EscapeDataString($offFile)
            @"
<article><h2>$label</h2><img loading="lazy" src="$heroUrl" data-wide="$beautyUrl" data-hero="$heroUrl" data-detail="$detailUrl" data-motion="$motionUrl" data-face="$faceUrl" data-turn="$turnUrl" data-environment="$environmentUrl" data-off="$offUrl" alt="$label"><code>$asset</code></article>
"@
        }
        $gallery = @"
<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>MMD 舞台与渲染预设</title>
<style>body{margin:0;background:#11151f;color:#eef3fa;font:16px/1.6 system-ui,sans-serif}main{max-width:1400px;margin:auto;padding:36px}h1{font-size:30px;margin:0}p{color:#adbacb}nav{display:flex;gap:12px;margin:22px 0;flex-wrap:wrap}button{padding:10px 18px;border:1px solid #556580;background:#222b3a;color:white;border-radius:8px;cursor:pointer}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(460px,1fr));gap:24px}article{background:#1b2230;border-radius:12px;overflow:hidden}h2{font-size:18px;margin:16px 20px}img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain;background:black}code{display:block;padding:14px 20px;font-size:12px;overflow-wrap:anywhere;color:#94b7d5}@media(max-width:550px){main{padding:20px}section{grid-template-columns:1fr}}</style>
<main><h1>MMD 舞台与角色母材质</h1><p>UE 原生后台截图。按钮切换本次已渲染画面，无需启动编辑器；角色采用真实灯光与天空环境响应，舞台动效使用固定时刻。</p><nav><button data-view="hero">角色全身</button><button data-view="wide">舞台全景</button><button data-view="detail">角色近景</button><button data-view="motion">舞台另一时刻</button><button data-view="face">脸部近景</button><button data-view="turn">抬头检查</button></nav><section>$($cards -join "`n")</section><p>美术参考：<a href="https://www.bilibili.com/video/BV1fJ411G7N3/">scyrax 符华 Addiction</a> · <a href="https://www.bilibili.com/video/BV1kb4y1m7e7/">原神官方角色演示</a> · <a href="https://www.bilibili.com/video/BV1VuDZYDE9L/">鸣潮官方战斗演示</a> · <a href="https://www.bilibili.com/video/BV11erNBfE2g/">终末地官方干员演示</a> · <a href="https://www.bilibili.com/video/BV1BdEMzQEqq/">白银之城官方实机</a>。风格响应根据公开画面设计。</p><p>离屏截图没有主视图，自定义级联阴影使用正常回退；舞台原生阴影已开启。角色模型仅用于 Saved 测试，没有写入插件舞台资产。</p></main>
<script>if('$CapturePreset'==='SakuraTheater'){[['environment','仅环境光'],['off','关灯验证']].forEach(([view,label])=>{const button=document.createElement('button');button.dataset.view=view;button.textContent=label;document.querySelector('nav').append(button);});}document.querySelectorAll('button').forEach(button=>button.onclick=()=>document.querySelectorAll('img').forEach(img=>img.src=img.dataset[button.dataset.view]));</script></html>
"@
        $galleryPath = Join-Path $reportDirectory 'gallery.html'
        [IO.File]::WriteAllText($galleryPath, $gallery, [Text.UTF8Encoding]::new($false))
        Write-Output "预设截图画廊：$galleryPath"
    }
    Write-Output "舞台资产生成和验证通过：$reportPath"
    if (-not $SkipScreenshots) { Write-Output "后台截图：$screenshotDirectory" }
    exit 0
}
catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
