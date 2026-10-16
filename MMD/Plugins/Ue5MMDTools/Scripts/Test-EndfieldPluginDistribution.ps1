param(
    [Parameter(Mandatory=$true)][string]$EngineDir,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [string]$PluginFile = (Join-Path $PSScriptRoot '../Ue5MMDTools.uplugin'),
    [string]$SharedDataCachePath,
    [switch]$BuildOnly
)
$ErrorActionPreference = 'Stop'
$engine_root = [IO.Path]::GetFullPath($EngineDir)
$plugin_file = (Resolve-Path -LiteralPath $PluginFile).Path
$plugin_root = Split-Path -Parent $plugin_file
$output_root = [IO.Path]::GetFullPath($OutputRoot)
function Test-Under([string]$Child, [string]$Parent) {
    return $Child.TrimEnd('\','/').StartsWith($Parent.TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}
# UAT 会清空 Package；只允许从不存在的新目录开始，不能覆盖既有发行物。
if ((Test-Path -LiteralPath $output_root) -or
    (Test-Under $output_root $plugin_root) -or (Test-Under $plugin_root $output_root) -or
    (Test-Under $output_root $engine_root) -or (Test-Under $engine_root $output_root) -or
    $output_root -eq $plugin_root -or $output_root -eq $engine_root) {
    throw 'OutputRoot 必须是插件与引擎之外尚不存在的新目录。'
}
$ancestor = Split-Path -Parent $output_root
while ($ancestor) {
    if (Test-Path -LiteralPath $ancestor) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'OutputRoot 的上级目录不能是链接或目录联接；UAT 会清理 Package，必须保证实际目标位置。'
        }
    }
    $parent = Split-Path -Parent $ancestor
    if ($parent -eq $ancestor) { break }
    $ancestor = $parent
}
# 输入路径也拒绝别名，避免真实插件目录经 junction 别名绕过上述隔离比较。
foreach ($source_path in @($plugin_file, $engine_root)) {
    $source_ancestor = $source_path
    while ($source_ancestor) {
        if ((Test-Path -LiteralPath $source_ancestor) -and
            ((Get-Item -LiteralPath $source_ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'PluginFile 和 EngineDir 的路径不能经过链接或目录联接；请提供实际路径。'
        }
        $source_parent = Split-Path -Parent $source_ancestor
        if ($source_parent -eq $source_ancestor) { break }
        $source_ancestor = $source_parent
    }
}
$version = Get-Content -LiteralPath (Join-Path $engine_root 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8 -or $version.PatchVersion -ne 3 -or $version.Changelist -ne 58210709) {
    throw 'Endfield 原生后端当前仅适配 UE 5.8.3 CL58210709。'
}
New-Item -ItemType Directory -Path $output_root | Out-Null
$package_dir = Join-Path $output_root 'Ue5MMDTools'
$report = [ordered]@{ schema='mmd.endfield.plugin-distribution.v1'; captured_at=(Get-Date).ToString('o'); plugin=$plugin_file; engine=$engine_root; expected_backend_patch='endfield-native-bsdf-v12-skin-response'; expected_global_shader_protocol=12; expected_graph_version=6; expected_face_data_version=5; expected_skin_profile_layout=41; build_plugin_verified=$false; payload_files_verified=$false; skin_response_sources_verified=$false; clean_host_adapter_verified=$false; rendering_verified=$false; cooked_runtime_verified=$false }
function Save-Report { $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output_root 'distribution.json') -Encoding utf8 }
try {
    & (Join-Path $engine_root 'Engine/Build/BatchFiles/RunUAT.bat') BuildPlugin "-Plugin=$plugin_file" "-Package=$package_dir" -TargetPlatforms=Win64 -HostPlatforms=Win64 -WaitForUATMutex -UTF8Output *> (Join-Path $output_root 'build-plugin.log')
    if ($LASTEXITCODE -ne 0) { throw "BuildPlugin 失败，exit=$LASTEXITCODE，见 build-plugin.log。" }
    $report.build_plugin_verified = $true
    $required = @(
        'Ue5MMDTools.uplugin', 'LICENSE', 'Binaries/Win64/UnrealEditor-Ue5MMDTools.dll',
        'Binaries/Win64/UnrealEditor-Ue5MMDToolsEditor.dll',
        'ThirdParty/Bullet/include/btBulletDynamicsCommon.h',
        'ThirdParty/Bullet/lib/BulletCollision.lib', 'ThirdParty/Bullet/lib/BulletDynamics.lib',
        'ThirdParty/Bullet/lib/BulletSoftBody.lib', 'ThirdParty/Bullet/lib/LinearMath.lib',
        'Source/Ue5MMDTools/Ue5MMDTools.Build.cs',
        'Shaders/Core/TMMDEndfieldNativePayload.ush', 'Shaders/Core/TMMDEndfieldNativeBSDF.ush',
        'Shaders/Core/TMMDEndfieldSkinMaterial.ush',
        'Shaders/Core/TMMDEndfieldSkinResponse.ush',
        'Source/Ue5MMDTools/Private/Rendering/MMDEndfieldSkinLutData.inl',
        'Docs/ENDFIELD_SKIN_MATERIAL_GUIDE.md', 'Docs/ENDFIELD_PLUGIN_DISTRIBUTION.md',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Opaque.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Masked.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Translucent.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Outline.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Face_Opaque.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Face_Masked.uasset',
        'Content/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Face_Translucent.uasset',
        'Content/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2.uasset'
    )
    $file_hashes = [ordered]@{}
    foreach ($relative in $required) {
        $file = Join-Path $package_dir $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "发行包缺少必需文件：$relative" }
        $file_hashes[$relative] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    }
    $report.files = $file_hashes
    foreach ($relative in @('Shaders/Core/TMMDEndfieldSkinResponse.ush', 'Source/Ue5MMDTools/Private/Rendering/MMDEndfieldSkinLutData.inl')) {
        $source_hash = (Get-FileHash -LiteralPath (Join-Path $plugin_root $relative) -Algorithm SHA256).Hash
        if ($file_hashes[$relative] -ne $source_hash) { throw "皮肤响应发行源码与本次源文件不一致：$relative" }
    }
    $report.skin_response_sources_verified = $true
    $sample_files = @(Get-ChildItem -LiteralPath (Join-Path $package_dir 'Resources') -File -Recurse |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.pmx','.pmd','.vmd','.vrm') })
    if ($sample_files.Count -ne 0) { throw '发行包包含仅用于本机验证的模型或动作。请检查 FilterPlugin 的 Resources 白名单。' }
    $report.sample_models_and_motions_excluded = $true
    $report.payload_files_verified = $true
    if (-not $BuildOnly) {
        $host_dir = Join-Path $output_root 'CleanHost'
        $host_plugins = Join-Path $host_dir 'Plugins'
        New-Item -ItemType Directory -Path $host_plugins | Out-Null
        Copy-Item -LiteralPath $package_dir -Destination (Join-Path $host_plugins 'Ue5MMDTools') -Recurse
        # 仅在新建的测试副本移出这一私有源码，确认二进制启动不依赖它；发行原包保留源码。
        $private_source = Join-Path $host_plugins 'Ue5MMDTools/Source/Ue5MMDTools/Private/Materials/MaterialExpressionMMDEndfieldNativePayload.cpp'
        if (Test-Path -LiteralPath $private_source -PathType Leaf) {
            Move-Item -LiteralPath $private_source -Destination (Join-Path $output_root 'payload-expression-source-reference.cpp')
        }
        $report.private_expression_source_available_in_probe = Test-Path -LiteralPath $private_source -PathType Leaf
        $host_project = Join-Path $host_dir 'EndfieldPluginHost.uproject'
        '{"FileVersion":3,"Plugins":[{"Name":"Ue5MMDTools","Enabled":true}]}' | Set-Content -LiteralPath $host_project -Encoding utf8
        $probe_dir = Join-Path $output_root 'adapter'
        $arguments = @($host_project, '-run=MMDEndfieldNativeBackendProbe', '-AdapterOnly',
            '-MMDEndfieldNativeBackend', '-MMDEndfieldNativeBackendMode=2', '-RenderOffscreen',
            '-DisablePlugins=RenderDocPlugin,AndroidFileServer','-AllowCommandletRendering','-d3d12','-sm6','-unattended','-nopause','-nosplash','-nosound','-NoCrashDialog','-stdout','-UTF8Output',
            "-OutDir=$probe_dir", "-Report=$probe_dir/report.json", "-abslog=$output_root/clean-host.log",
            '-ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True,[/Script/Engine.RendererSettings]:r.Substrate.Experimental.ToonUnifiedDiffuse=0,[/Script/Engine.RendererSettings]:r.Substrate.ProjectGBufferFormat=1,[/Script/Engine.RendererSettings]:r.Shadow.Virtual.Enable=1,[ConsoleVariables]:r.Substrate.ToonProfile.Resolution=256,[/Script/WindowsTargetPlatform.WindowsTargetSettings]:D3D12TargetedShaderFormats=PCD3D_SM6,[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=12,[DevOptions.Shaders]:PercentageUnusedShaderCompilingThreads=60')
        if ($SharedDataCachePath) {
            $arguments += @('-ddc=InstalledNoZenLocalFallback', "-LocalDataCachePath=$([IO.Path]::GetFullPath($SharedDataCachePath))")
            $report.shared_data_cache = [IO.Path]::GetFullPath($SharedDataCachePath)
        }
        & (Join-Path $engine_root 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') @arguments *> (Join-Path $output_root 'clean-host.stdout.txt')
        if ($LASTEXITCODE -ne 0) { throw "干净项目探针失败，exit=$LASTEXITCODE，见 clean-host.log。" }
        $probe = Get-Content -LiteralPath (Join-Path $probe_dir 'report.json') -Raw | ConvertFrom-Json
        if (-not $probe.backend_success -or -not $probe.adapter_only -or $probe.draw_verified -or -not $probe.published_materials_compile_verified) { throw '干净项目探针没有返回预期的适配器和已发布材质编译验证报告。' }
        if ([int]$probe.expected_global_shader_protocol -ne 12 -or [int]$probe.global_shader_protocol -ne 12 -or -not $probe.global_shader_protocol_verified) { throw '干净项目没有读到并验证当前协议12；旧编译记录不能证明新皮肤响应已分发。' }
        $report.clean_host_global_shader_protocol = [int]$probe.global_shader_protocol
        if (@($probe.published_materials).Count -ne 7) { throw '预期校验七个父材质（含脸部三种透明模式和专用描边），报告数量不符。' }
        foreach ($material in $probe.published_materials) {
            if (-not $material.shader_map_ready -or -not $material.compilation_finished -or
                -not $material.compiled_successfully -or -not $material.compilation_finalized -or
                -not $material.rendering_valid -or $material.shader_count -le 0 -or @($material.compile_errors).Count -ne 0) {
                throw "已发布材质没有完成有效编译：$($material.asset)"
            }
        }
        $report.clean_host_adapter_verified = $true
    }
    Save-Report
    Write-Output "发行结构验证通过：$output_root；画质与 Cooked 运行需独立验证。"
} catch {
    $report.error = $_.Exception.Message
    Save-Report
    throw
}
