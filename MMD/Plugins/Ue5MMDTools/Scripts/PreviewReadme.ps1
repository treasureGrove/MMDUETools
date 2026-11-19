#requires -Version 7
<#
.SYNOPSIS
把仓库里的 Markdown 渲染成接近 GitHub 的本地预览页，用于截图验收。

.DESCRIPTION
正文走 GitHub 的 markdown API，所以表格、HTML 块和锚点的渲染结果与线上一致。
相对图片与 docs/ 路径依赖仓库根目录，所以预览页必须由仓库根目录起的静态服务器提供：

    python -m http.server 8765 --bind 127.0.0.1 --directory .
    # 然后打开 http://127.0.0.1:8765/_preview/<名字>.html

shields.io 徽章离线不可用，会替换成 _preview/badges 下的本地副本。

.PARAMETER File
要渲染的 Markdown 文件，默认 README.md。

.PARAMETER Name
预览页文件名（不含扩展名），默认取 Markdown 文件名。

.EXAMPLE
pwsh -File Scripts/PreviewReadme.ps1 -File README.ja.md
#>
param(
    [string]$File = 'README.md',
    [string]$Name
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path $File)) { throw "Markdown 不存在：$File" }
if (-not $Name) { $Name = [System.IO.Path]::GetFileNameWithoutExtension($File) }

$root = Split-Path -Parent $PSScriptRoot
$previewDir = Join-Path $root '_preview'
$badgeDir = Join-Path $previewDir 'badges'
New-Item -ItemType Directory -Force -Path $badgeDir | Out-Null

$badges = @(
    @{ Pattern = 'https://img.shields.io/github/v/release/treasureGrove/MMDUETools[^)\s]*'; File = 'release.svg'; Url = 'https://img.shields.io/github/v/release/treasureGrove/MMDUETools?style=flat-square&label=release' },
    @{ Pattern = 'https://img.shields.io/badge/UE-[^)\s]*'; File = 'unreal.svg'; Url = 'https://img.shields.io/badge/UE-5.8.3%20%2F%20Win64-0b6fcb?style=flat-square' },
    @{ Pattern = 'https://img.shields.io/badge/license-[^)\s]*'; File = 'license.svg'; Url = 'https://img.shields.io/badge/license-MIT-2f855a?style=flat-square' },
    @{ Pattern = 'https://img.shields.io/github/stars/treasureGrove/MMDUETools[^)\s]*'; File = 'stars.svg'; Url = 'https://img.shields.io/github/stars/treasureGrove/MMDUETools?style=flat-square&color=f5b301' }
)
foreach ($b in $badges) {
    $target = Join-Path $badgeDir $b.File
    if (-not (Test-Path $target)) {
        try { Invoke-WebRequest -Uri $b.Url -OutFile $target -TimeoutSec 20 -UseBasicParsing } catch { Write-Warning "徽章下载失败：$($b.File)" }
    }
}

$md = Get-Content $File -Raw
foreach ($b in $badges) {
    $md = $md -replace $b.Pattern, "_preview/badges/$($b.File)"
}

$payload = @{ text = $md; mode = 'gfm'; context = 'treasureGrove/MMDUETools' } | ConvertTo-Json -Compress
$body = $payload | gh api --method POST /markdown --input -
if (-not $body) { throw 'GitHub markdown API 没有返回内容' }

$css = @'
:root { color-scheme: light; }
* { box-sizing: border-box; }
body { margin:0; background:#fff; color:#1f2328; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", "Noto Sans SC", "Noto Sans JP", "Microsoft YaHei", Helvetica, Arial, sans-serif; font-size:15px; line-height:1.6; }
.markdown-body { max-width: 1012px; margin: 0 auto; padding: 28px 32px 56px; }
.markdown-body h1 { font-size:2em; border-bottom:1px solid #d1d9e0; padding-bottom:.3em; margin:.67em 0; font-weight:600; }
.markdown-body h2 { font-size:1.4em; border-bottom:1px solid #d1d9e0; padding-bottom:.3em; margin:22px 0 14px; font-weight:600; }
.markdown-body h3 { font-size:1.15em; margin:20px 0 12px; font-weight:600; }
.markdown-body p { margin:0 0 14px; }
.markdown-body a { color:#0969da; text-decoration:none; }
.markdown-body img { max-width:100%; }
.markdown-body blockquote { margin:0 0 14px; padding:0 1em; color:#59636e; border-left:.25em solid #d1d9e0; }
.markdown-body table { border-collapse:collapse; width:100%; margin:0 0 14px; font-size:12.5px; table-layout:fixed; }
.markdown-body th, .markdown-body td { border:1px solid #d1d9e0; padding:5px 8px; word-break:break-word; }
.markdown-body th { font-weight:600; background:#f6f8fa; }
.markdown-body tr:nth-child(2n) td { background:#f6f8fa; }
.markdown-body code { background:#eff1f3; padding:.15em .35em; border-radius:5px; font-size:85%; font-family: ui-monospace, "SF Mono", Menlo, monospace; }
.markdown-body pre { background:#f6f8fa; padding:14px; border-radius:6px; overflow:auto; }
.markdown-body pre code { background:none; padding:0; }
.markdown-body li { margin:.25em 0; }
.markdown-body hr { border:0; border-top:1px solid #d1d9e0; margin:22px 0; }
'@

$i = 0
$body = [regex]::Replace($body, '<h2 dir="auto">(.*?)</h2>', {
    param($m)
    $script:i++
    '<h2 id="sec-' + $script:i + '" dir="auto">' + $m.Groups[1].Value + '</h2>'
})

$page = @"
<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><base href="/"><title>$Name 预览</title><style>$css</style></head>
<body><article class="markdown-body">$body</article></body></html>
"@
$out = Join-Path $previewDir "$Name.html"
$page | Out-File -Encoding utf8 $out
"已生成 $out"
