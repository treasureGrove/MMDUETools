@echo off
rem 调用项目构建脚本，保留真实退出码。
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildPlugin.ps1" %*
exit /b %errorlevel%
