# UE5.8 迁移状态

## 当前分支

`feature/ue5.8-mainline`

## 已验证

- UE5.8.1 安装路径：`E:/ae/UE_5.8`
- `MMDEditor Win64 Development` 构建通过。
- 使用现有 VS2026 内的 MSVC 14.44 工具链构建，不需要安装 VS2022。
- `Ue5MMDTools` 和 `MMD` DLL 已生成。
- `.uplugin` 已声明 `IKRig` 依赖。
- UE5.8 Target 使用 `BuildSettingsVersion.V7` 和 `EngineIncludeOrderVersion.Unreal5_8`。

## 本轮 API 兼容

- `Rig/Solvers/IKRig_FBIKSolver.h` 在 UE5.8 改为 `Rig/Solvers/IKRigFullBodyIK.h`。
- UE5.8 的 `UIKRigController::AddSolver` 使用字符串类型路径：`/Script/IKRig.FullBodyIKSolver`。
- `FEditorViewportClient::InputAxis` 使用 `FInputKeyEventArgs`。
- `SEditorViewport::MakeViewportToolbar` 已弃用且为 `final`，改用 `BuildViewportToolbar`。

## 投影根因调查

UE5.8 的 `SceneCapturePixelShader.usf` 中：

- `SCS_SceneDepth` 输出 `CalcSceneDepth(Input.UV)`。
- `CalcSceneDepth` 调用 `ConvertFromDeviceZ`，输出线性 SceneDepth。
- 因此材质侧 `SampleMMDShadow` 使用沿光轴的线性厘米深度比较，不是 reversed-Z 量纲错误。

真正发现的确定性问题是自定义 Custom Render Pass UserData 布局缺字段：

- 引擎 `FSceneCaptureCustomRenderPassUserData` 在 `bIgnoreScreenPercentage` 后有 `bExcludeFromSceneTextureExtents`。
- 插件镜像结构遗漏该字段，导致后续 `FIntPoint`、FName 等成员整体错位。
- 已补齐 `bExcludeFromSceneTextureExtents`，并通过 UE5.8 增量构建。

## 2026-11-10 跨版本兼容层与多版本后端签名表

- 新增 [`MMDVersionCompat.h`](../Source/Ue5MMDTools/Public/MMDVersionCompat.h)，统一封装 `MMD_UE_VERSION_AT_LEAST` / `MMD_UE_VERSION_BEFORE` 宏。
- `FMMDShadowCustomRenderPass.h` 中的 `bExcludeFromSceneTextureExtents` 通过 `#if MMD_UE_VERSION_AT_LEAST(5, 8)` 宏保护，兼顾 UE 5.8 内存对齐与旧版本源码兼容。
- `MMDEndfieldNativeBackend.cpp` 的硬编码版本校验重构为 `FMMDEndfieldBackendEngineProfile` / `MMDEndfieldBackendEngineProfiles[]` 多版本签名表结构（当前登记 `UE 5.8.3 / CL58210709` 的 11 份私有 shader SHA-1），后续接入新引擎补丁只需追加签名项。
- `MMD/Scripts/BuildPlugin.ps1` 自动探测工程本地 `Saved/PreviewProduction/2026-11-04/AutoSdk` 并设置 `UE_SDKS_ROOT`，保证无全局 VS2022 环境下的后台一键构建。

## P0 架构审计结论

当前插件仍是单一 `Ue5MMDTools` 模块，Runtime 模块依赖大量 Editor-only API。最小迁移边界应为：

- `MMDCore`：PMX/VMD 数据、解析器、数学和坐标转换。
- `MMDRuntime`：Actor、动画节点、Bullet、运行时渲染与灯光/阴影系统。
- `MMDEditor`：Importer、SkeletalMesh/AnimSequence/MorphTarget 资产创建、IK、Slate/UI、Sequencer。
- `MMDCompatibility`：版本差异 API 后端。

暂不在本轮与投影修复混合重构。
