#requires -Version 7
<#
.SYNOPSIS
把仓库自带的 git hook 接上（.githooks/pre-push）。

.DESCRIPTION
只有一个设置：core.hooksPath 指向 .githooks。接上之后，每次 push 前会跑
Scripts/Test-MarkdownLinks.ps1，拦截 Markdown 相对链接的大小写和失效问题。

hook 只影响本机检出，不会改动提交历史。想临时跳过用 git push --no-verify。

.EXAMPLE
pwsh -File Scripts/Install-GitHooks.ps1
#>
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not (Test-Path '.githooks/pre-push')) { throw '缺少 .githooks/pre-push' }
    git config core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) { throw 'git config 失败' }
    # Git 会按索引里的可执行位决定是否运行 hook，Windows 检出时可能丢失
    git update-index --chmod=+x .githooks/pre-push 2>$null
    Write-Host "已启用 .githooks/pre-push（core.hooksPath = .githooks）"
    Write-Host "手动验证：pwsh -File Scripts/Test-MarkdownLinks.ps1"
}
finally { Pop-Location }
