[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$OutputDir,
    [string]$DevBranch = 'dev',
    [string]$RepoSlug = 'treasureGrove/MMDUETools',
    [int]$MaxTotalMB = 150,
    [int]$MaxFileMB = 50
)

# 把开发分支里的插件按白名单导出成“仓库根即插件根”的发布树。
# 只读取已跟踪文件、只写入 OutputDir，不切分支、不提交、不推送。
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$repo = (& git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$pluginRoot = 'MMD/Plugins/Ue5MMDTools/'
$rootPages = @('README.md', 'github.md')
$mediaPattern = '\.(png|jpg|jpeg|gif|webp|mp4)$'

$dirty = & git -C $repo status --porcelain --untracked-files=no -- $pluginRoot @rootPages
if ($dirty) { throw "导出范围内有未提交修改，先提交或还原：`n$($dirty -join "`n")" }
$sourceCommit = (& git -C $repo rev-parse HEAD).Trim()

$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
if (Test-Path -LiteralPath $OutputDir) { throw "输出目录已存在，拒绝覆盖：$OutputDir" }

$tracked = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$trackedDirs = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($path in (& git -C $repo -c core.quotepath=false ls-files)) {
    [void]$tracked.Add($path)
    $slash = $path.LastIndexOf('/')
    while ($slash -gt 0) {
        $path = $path.Substring(0, $slash)
        if (-not $trackedDirs.Add($path)) { break }
        $slash = $path.LastIndexOf('/')
    }
}

function Resolve-DevPath([string]$BaseDir, [string]$Relative) {
    $parts = [System.Collections.Generic.List[string]]::new()
    foreach ($part in (($BaseDir, $Relative) -join '/').Split('/')) {
        if ($part -eq '' -or $part -eq '.') { continue }
        if ($part -eq '..') {
            if ($parts.Count -eq 0) { return $null }
            $parts.RemoveAt($parts.Count - 1)
            continue
        }
        $parts.Add($part)
    }
    return ($parts -join '/')
}

function Get-ParentDir([string]$Path) {
    $slash = $Path.LastIndexOf('/')
    if ($slash -lt 0) { return '' }
    return $Path.Substring(0, $slash)
}

function Get-LinkTargets([string]$Text) {
    $targets = [System.Collections.Generic.List[string]]::new()
    foreach ($m in [regex]::Matches($Text, '\]\((<[^>]+>|[^)\s]+)')) { $targets.Add($m.Groups[1].Value) }
    foreach ($m in [regex]::Matches($Text, '\b(?:src|href)="([^"]+)"')) { $targets.Add($m.Groups[1].Value) }
    return $targets
}

function Split-LinkTarget([string]$Target) {
    $raw = $Target.Trim('<', '>')
    if ($raw -match '^(?:[A-Za-z][A-Za-z0-9+.-]*:|#|//)') { return $null }
    $m = [regex]::Match($raw, '^([^?#]*)([?#].*)?$')
    $pathPart = [uri]::UnescapeDataString($m.Groups[1].Value)
    if (-not $pathPart) { return $null }
    return [pscustomobject]@{ Path = $pathPart; Suffix = $m.Groups[2].Value }
}

# 开发分支路径 -> 发布树路径
$export = [System.Collections.Generic.Dictionary[string, string]]::new([System.StringComparer]::Ordinal)
foreach ($path in $tracked) {
    if (-not $path.StartsWith($pluginRoot)) { continue }
    $p = $path.Substring($pluginRoot.Length)
    $keep = $p -in @('Ue5MMDTools.uplugin', 'LICENSE') -or
        $p -match '^(Source|Shaders|Config|ThirdParty|Content|Scripts)/' -or
        $p -match '^Resources/(Icon128\.png|PlaceholderButtonIcon\.svg)$' -or
        $p -match '^Resources/(Garden|PreviewMotions|DisplayScenes)/' -or
        ($p -match '^Docs/[^/]+\.md$' -and $p -notmatch '^Docs/(PHASE_TWO_|MMDTOOLS_ROADMAP|ENDFIELD_V2_HANDOFF|MOBILE_MMD_EDITOR)')
    if ($keep) { $export[$path] = $p }
}
foreach ($page in $rootPages) {
    if (-not $tracked.Contains($page)) { throw "缺少首页文件：$page" }
    $export[$page] = $page
    # 首页直接展示的图片和视频随发布树走，其余截图留在开发分支。
    $text = [System.IO.File]::ReadAllText((Join-Path $repo $page))
    foreach ($target in (Get-LinkTargets $text)) {
        $link = Split-LinkTarget $target
        if (-not $link -or $link.Path -notmatch $mediaPattern) { continue }
        $devTarget = Resolve-DevPath (Get-ParentDir $page) $link.Path
        if ($devTarget -and $devTarget.StartsWith($pluginRoot) -and $tracked.Contains($devTarget)) {
            $export[$devTarget] = $devTarget.Substring($pluginRoot.Length)
        }
    }
}

$exportedOut = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
foreach ($out in $export.Values) { [void]$exportedOut.Add($out) }

function Get-RelativeLink([string]$FromDir, [string]$To) {
    $from = 'X:/' + $FromDir
    $rel = [System.IO.Path]::GetRelativePath($from, 'X:/' + $To).Replace('\', '/')
    return $rel.Replace(' ', '%20')
}

function Get-DevUrl([string]$Kind, [string]$DevPath) {
    $encoded = ($DevPath.Split('/') | ForEach-Object { [uri]::EscapeDataString($_) }) -join '/'
    return "https://github.com/$RepoSlug/$Kind/$DevBranch/$encoded"
}

$stats = [ordered]@{ relative = 0; to_dev_branch = 0 }
$broken = [System.Collections.Generic.List[string]]::new()

function Convert-Link([string]$DevFile, [string]$OutFile, [string]$Target) {
    $link = Split-LinkTarget $Target
    if (-not $link) { return $Target }
    $devTarget = Resolve-DevPath (Get-ParentDir $DevFile) $link.Path
    $outDir = Get-ParentDir $OutFile
    if ($null -eq $devTarget) { $broken.Add("$DevFile -> $Target"); return $Target }

    if ($export.ContainsKey($devTarget)) {
        $stats.relative++
        return (Get-RelativeLink $outDir $export[$devTarget]) + $link.Suffix
    }
    if ($devTarget -eq '' -or $trackedDirs.Contains($devTarget)) {
        $asPlugin = ($devTarget + '/')
        if ($asPlugin.StartsWith($pluginRoot)) {
            $outTarget = $asPlugin.Substring($pluginRoot.Length).TrimEnd('/')
            $hasContent = $outTarget -eq '' -or ($exportedOut | Where-Object { $_.StartsWith($outTarget + '/') } | Select-Object -First 1)
            if ($hasContent) {
                $stats.relative++
                $rel = if ($outTarget -eq '' -and $outDir -eq '') { '.' } else { Get-RelativeLink $outDir $outTarget }
                return $rel.TrimEnd('/') + '/' + $link.Suffix
            }
        }
        $stats.to_dev_branch++
        return (Get-DevUrl 'tree' $devTarget) + $link.Suffix
    }
    if ($tracked.Contains($devTarget)) {
        $stats.to_dev_branch++
        $suffix = $link.Suffix
        if (-not $suffix -and $devTarget -match $mediaPattern) { $suffix = '?raw=true' }
        return (Get-DevUrl 'blob' $devTarget) + $suffix
    }
    $broken.Add("$DevFile -> $Target")
    return $Target
}

$totalBytes = 0L
$oversized = [System.Collections.Generic.List[string]]::new()
$forbidden = [System.Collections.Generic.List[string]]::new()
$utf8 = [System.Text.UTF8Encoding]::new($false)
foreach ($entry in $export.GetEnumerator()) {
    $source = Join-Path $repo $entry.Key
    $dest = Join-Path $OutputDir $entry.Value
    [void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $dest))

    # 第三方模型、舞蹈动作和音频不得进入发布树；附许可的预览动作除外。
    if ($entry.Value -match '\.(pmx|pmd|wav|mp3)$' -or
        ($entry.Value -match '\.vmd$' -and $entry.Value -notmatch '^Resources/PreviewMotions/')) {
        $forbidden.Add($entry.Value)
    }

    if ($entry.Value -match '\.md$') {
        $devFile = $entry.Key
        $outFile = $entry.Value
        $text = [System.IO.File]::ReadAllText($source)
        $text = [regex]::Replace($text, '(\]\()(<[^>]+>|[^)\s]+)', {
            param($m) $m.Groups[1].Value + (Convert-Link $devFile $outFile $m.Groups[2].Value)
        })
        $text = [regex]::Replace($text, '(\b(?:src|href)=")([^"]+)(")', {
            param($m) $m.Groups[1].Value + (Convert-Link $devFile $outFile $m.Groups[2].Value) + $m.Groups[3].Value
        })
        [System.IO.File]::WriteAllText($dest, $text, $utf8)
    }
    else {
        [System.IO.File]::Copy($source, $dest)
    }
    $length = (Get-Item -LiteralPath $dest).Length
    $totalBytes += $length
    if ($length -gt $MaxFileMB * 1MB) { $oversized.Add(('{0} ({1:N1} MB)' -f $entry.Value, ($length / 1MB))) }
}

$report = [ordered]@{
    source_commit       = $sourceCommit
    dev_branch          = $DevBranch
    output_dir          = $OutputDir
    file_count          = $export.Count
    total_mb            = [math]::Round($totalBytes / 1MB, 1)
    links_relative      = $stats.relative
    links_to_dev_branch = $stats.to_dev_branch
    broken_links        = @($broken)
    forbidden_files     = @($forbidden)
    oversized_files     = @($oversized)
    uplugin_at_root     = Test-Path -LiteralPath (Join-Path $OutputDir 'Ue5MMDTools.uplugin')
}
$report.success = $report.uplugin_at_root -and $broken.Count -eq 0 -and $forbidden.Count -eq 0 -and
    $oversized.Count -eq 0 -and $report.total_mb -le $MaxTotalMB
$reportPath = $OutputDir.TrimEnd('\', '/') + '.report.json'
[System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 4), $utf8)
$report | ConvertTo-Json -Depth 4
if (-not $report.success) { exit 1 }
