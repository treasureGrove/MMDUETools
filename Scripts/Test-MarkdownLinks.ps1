#requires -Version 7
<#
.SYNOPSIS
检查仓库内 Markdown 的相对链接和图片是否都能解析，并区分大小写。

.DESCRIPTION
GitHub 对仓库内路径区分大小写。Windows 的 Test-Path 不区分，所以
README 里写 Docs/ 而目录是 docs/ 时，本地完全看不出来，线上却整页裂图。
这个脚本按真实目录项逐段比对，大小写不一致也算失败。

检查范围：所有被 git 跟踪的 .md 文件中的
  - Markdown 链接与图片：[text](path)、![alt](path)
  - HTML 属性：<img src>、<a href>
跳过 http(s)、mailto、页内锚点和空链接。

用法：
  pwsh -File Scripts/Test-MarkdownLinks.ps1
  pwsh -File Scripts/Test-MarkdownLinks.ps1 -Root . -Verbose

退出码：0 全部通过；1 存在坏链接或大小写不一致。
#>
[CmdletBinding()]
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).Path

function Get-TrackedMarkdown {
    param([string]$RepoRoot)
    Push-Location $RepoRoot
    try {
        $files = git ls-files '*.md' 2>$null
    }
    finally { Pop-Location }
    if (-not $files) { return @() }
    return $files | Where-Object { $_ -and (Test-Path (Join-Path $RepoRoot $_)) }
}

# 逐段比对真实目录项，从而实现大小写敏感
function Resolve-CaseSensitive {
    param([string]$RepoRoot, [string]$RelativePath)

    $segments = $RelativePath -split '/'
    $current = $RepoRoot
    $walked = @()

    for ($i = 0; $i -lt $segments.Count; $i++) {
        $segment = $segments[$i]
        if ($segment -eq '' -or $segment -eq '.') { continue }

        if ($segment -eq '..') {
            $current = Split-Path -Parent $current
            $walked = $walked[0..($walked.Count - 2)]
            continue
        }

        $entries = Get-ChildItem -LiteralPath $current -Force -ErrorAction SilentlyContinue
        $exact = $entries | Where-Object { $_.Name -ceq $segment } | Select-Object -First 1
        if ($exact) {
            $walked += $exact.Name
            if ($i -lt $segments.Count - 1 -and -not $exact.PSIsContainer) {
                return [pscustomobject]@{ Ok = $false; Reason = '父路径是文件不是目录' }
            }
            $current = Join-Path $current $exact.Name
            continue
        }

        $nearMiss = $entries | Where-Object { $_.Name -eq $segment } | Select-Object -First 1
        if ($nearMiss) {
            $shown = ($walked + $nearMiss.Name) -join '/'
            return [pscustomobject]@{ Ok = $false; Reason = "大小写不一致（实际为 $shown）" }
        }

        return [pscustomobject]@{ Ok = $false; Reason = '路径不存在' }
    }

    return [pscustomobject]@{ Ok = $true; Reason = '' }
}

function Get-LinkTargets {
    param([string]$Text)
    $targets = [System.Collections.Generic.List[string]]::new()
    foreach ($m in [regex]::Matches($Text, '!?\[[^\]]*\]\(\s*(<[^>]+>|[^)\s]+)')) { $targets.Add($m.Groups[1].Value) }
    foreach ($m in [regex]::Matches($Text, '\b(?:src|href)="([^"]+)"')) { $targets.Add($m.Groups[1].Value) }
    return $targets
}

$Red = @()
$files = Get-TrackedMarkdown -RepoRoot $Root
if (-not $files) {
    Write-Warning "没有找到被跟踪的 Markdown 文件（Root=$Root）"
    exit 0
}

$checked = 0
foreach ($relItem in $files) {
    $rel = $relItem -replace '\\', '/'
    $full = Join-Path $Root $rel
    $text = Get-Content -LiteralPath $full -Raw
    $dir = Split-Path -Parent $rel
    if ($null -eq $dir) { $dir = '' }
    $dir = $dir -replace '\\', '/'

    foreach ($raw in (Get-LinkTargets -Text $text)) {
        $target = $raw.Trim('<', '>')
        if ($target -match '^(?:[A-Za-z][A-Za-z0-9+.-]*:|#|//)') { continue }

        $pathPart = ($target -split '[?#]')[0]
        if (-not $pathPart) { continue }
        $pathPart = [uri]::UnescapeDataString($pathPart)

        $joined = if ($dir) { "$dir/$pathPart" } else { $pathPart }
        $normalized = ($joined -split '/' | Where-Object { $_ -ne '' -and $_ -ne '.' }) -join '/'

        $checked++
        $result = Resolve-CaseSensitive -RepoRoot $Root -RelativePath $normalized
        if (-not $result.Ok) {
            $Red += [pscustomobject]@{ File = $rel; Target = $target; Reason = $result.Reason }
            Write-Host ("FAIL  {0}`n      -> {1}  ({2})" -f $rel, $target, $result.Reason) -ForegroundColor Red
        }
    }
}

if ($Red.Count -gt 0) {
    Write-Host ""
    Write-Host ("检查 {0} 个链接，失败 {1} 个。" -f $checked, $Red.Count) -ForegroundColor Red
    $Red | ForEach-Object { "  {0} -> {1}  [{2}]" -f $_.File, $_.Target, $_.Reason }
    exit 1
}

Write-Host ("检查 {0} 个相对链接，全部通过（大小写敏感）。" -f $checked) -ForegroundColor Green
exit 0
