@echo off
rem build_plugin.bat - automates GenerateProjectFiles, build UE5Rules, then Build.bat for the project
rem Edit ENGINE and UPROJECT variables at top if different on your machine.
setlocal enabledelayedexpansion
:: === Configuration - edit these paths if needed ===
set "ENGINE=E:\ae\UE_5.5"
set "UPROJECT=Z:\Project\UEProject\MMDUETools\MMD\MMD.uproject"
set "LOG=%~dp0build_plugin.log"
necho Build started at %date% %time% > "%LOG%"necho Engine: %ENGINE% >> "%LOG%"necho UProject: %UPROJECT% >> "%LOG%"
necho. >> "%LOG%"necho 1) GenerateProjectFiles... >> "%LOG%"
if exist "%ENGINE%\Engine\Build\BatchFiles\GenerateProjectFiles.bat" (
    echo Running GenerateProjectFiles.bat >> "%LOG%"
    "%ENGINE%\Engine\Build\BatchFiles\GenerateProjectFiles.bat" -project="%UPROJECT%" -game >> "%LOG%" 2>&1
) else (
    echo ERROR: GenerateProjectFiles.bat not found at %ENGINE%\Engine\Build\BatchFiles >> "%LOG%"
    echo ERROR: GenerateProjectFiles.bat not found at %ENGINE%\Engine\Build\BatchFiles
    goto :END
)
necho. >> "%LOG%"necho 2) Build UE5Rules (dotnet then msbuild fallback) >> "%LOG%"
set "RULESDIR=%ENGINE%\Engine\Intermediate\Build\BuildRulesProjects\UE5Rules"
if exist "%RULESDIR%\UE5Rules.csproj" (
    pushd "%RULESDIR%"
    echo Running: dotnet build UE5Rules.csproj -c Release >> "%LOG%"
    dotnet build "UE5Rules.csproj" -c Release >> "%LOG%" 2>&1
    if errorlevel 1 (
        echo dotnet build failed, trying MSBuild fallback >> "%LOG%"
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe" (
            "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe" "UE5Rules.csproj" /p:Configuration=Release /m >> "%LOG%" 2>&1
        ) else (
            echo MSBuild not found. Please run this script from Developer Command Prompt for VS 2022. >> "%LOG%"
        )
    )
    popd
) else (
    echo RULES project not found at %RULESDIR% >> "%LOG%"
)
necho. >> "%LOG%"necho 3) Check for UE5Rules.dll >> "%LOG%"
if exist "%ENGINE%\Engine\Intermediate\Build\BuildRules\UE5Rules.dll" (
    echo RULES_OK >> "%LOG%"
    echo RULES_OK
) else (
    echo RULES_MISSING >> "%LOG%"
    echo RULES_MISSING
    echo Check log %LOG% for details. >> "%LOG%"
    goto :END
)
necho. >> "%LOG%"necho 4) Run full build (may take long) >> "%LOG%"
"%ENGINE%\Engine\Build\BatchFiles\Build.bat" UE5Editor Win64 Development "%UPROJECT%" -WaitMutex -FromMsBuild -NoHotReload -NoUBTMakefiles -NoPCH -Verbose >> "%LOG%" 2>&1nif errorlevel 1 (
    echo Build failed. See %LOG% for details.
) else (
    echo Build succeeded. See %LOG% for details.
)
n:ENDnecho Finished at %date% %time% >> "%LOG%"necho Log written to %LOG%
endlocal
pause
