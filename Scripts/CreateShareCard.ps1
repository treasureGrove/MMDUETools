#requires -Version 7
<#
.SYNOPSIS
生成 1200×630 的社交分享卡（og:image / X / Discord 卡片）。

.DESCRIPTION
以仓库内已有的实际成片帧为底图，左侧压暗后写入标题、定位与仓库地址。
不包含任何第三方模型文件；输出 PNG 只用于项目自身的仓库链接预览。

.PARAMETER Source
底图路径，默认使用深蓝展厅侧脸成片帧。

.PARAMETER Output
输出路径，默认写入当日归档目录。

.EXAMPLE
pwsh -File Scripts/CreateShareCard.ps1
#>
param(
    [string]$Source = 'docs/Screenshots/2026-11-05-homepage-gallery/slate_portrait.png',
    [string]$Output = 'docs/Screenshots/2026-11-19-share/share-card-1200x630.png',
    [string]$Title = 'Ue5MMDTools',
    [string]$Subtitle = '在 UE5 里做 MMD 短片',
    [string]$Highlights = 'PMX / VMD  ·  物理  ·  布光  ·  一键出片',
    [string]$Meta = 'UE 5.8.3  ·  Win64  ·  MIT',
    [string]$Repo = 'github.com/treasureGrove/MMDUETools'
)

$ErrorActionPreference = 'Stop'
$magick = (Get-Command magick -ErrorAction Stop).Source

if (-not (Test-Path $Source)) { throw "底图不存在：$Source" }
$outDir = Split-Path -Parent $Output
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("sharecard-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null

try {
    $bg      = Join-Path $tmp 'bg.png'
    $fg      = Join-Path $tmp 'fg.png'
    $mask    = Join-Path $tmp 'mask.png'
    $scrim   = Join-Path $tmp 'scrim.png'
    $fgW     = 840                    # 630 高时的等比宽度
    $bgWidth = 1200

    # 1) 全幅底图：等比放大后模糊，作为左右过渡的背景
    & $magick $Source -resize "$($bgWidth)x630^" -gravity center -extent "$($bgWidth)x630" -blur 0x45 $bg
    if ($LASTEXITCODE -ne 0) { throw '背景生成失败' }

    # 2) 前景：等比到 630 高，左边缘 90px 做透明过渡，避免硬接缝
    & $magick $Source -resize "x630" $fg
    & $magick -size "$($fgW)x630" xc:black -fill white -draw "rectangle 90,0 $($fgW-1),629" -blur 0x28 $mask
    & $magick $fg $mask -alpha off -compose CopyOpacity -composite $fg
    if ($LASTEXITCODE -ne 0) { throw '前景生成失败' }

    # 3) 左侧压暗，保证文字对比度
    & $magick -size 760x630 xc:'#080E18' -alpha set -channel A -fx 'max(0, 1 - i/680)' +channel $scrim
    if ($LASTEXITCODE -ne 0) { throw '压暗层生成失败' }

    # 4) 合成与排版
    # ImageMagick 在 Windows 上按家族名找不到微软雅黑，直接指定字体文件
    $fontTitle = 'C:/Windows/Fonts/msyhbd.ttc'
    $fontBody  = 'C:/Windows/Fonts/msyh.ttc'
    if (-not (Test-Path $fontTitle)) { $fontTitle = 'C:/Windows/Fonts/simhei.ttf' }
    if (-not (Test-Path $fontBody)) { $fontBody = 'C:/Windows/Fonts/simhei.ttf' }
    & $magick $bg `
        $fg -geometry "+360+0" -composite `
        $scrim -geometry '+0+0' -composite `
        -fill '#4FD1C5' -draw 'roundrectangle 80,150 146,154 2,2' `
        -font $fontTitle -pointsize 68 -fill white -annotate +78+242 $Title `
        -font $fontBody -pointsize 27 -fill '#D6E2F0' -annotate +80+298 $Subtitle `
        -font $fontBody -pointsize 18 -fill '#9FB3CC' -annotate +80+352 $Highlights `
        -font $fontBody -pointsize 17 -fill '#7E93AC' -annotate +80+388 $Meta `
        -font $fontBody -pointsize 17 -fill '#8FB6E8' -annotate +80+560 $Repo `
        -depth 8 -strip $Output
    if ($LASTEXITCODE -ne 0) { throw '合成失败' }

    $info = & $magick identify -format '%wx%h %b' $Output
    "已生成 $Output  ($info)"
}
finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
}
