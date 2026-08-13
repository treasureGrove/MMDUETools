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

## 尚未验证

UE5.8 Editor/Cmd 启动仍被启动前的全平台 SDK 验证阻塞：LinuxArm64 和 VisionOS SDK 缺失。插件运行时投影需要在 Editor 成功启动后用实际场景和 RT 内容继续验收。

## P0 架构审计结论

当前插件仍是单一 `Ue5MMDTools` 模块，Runtime 模块依赖大量 Editor-only API。最小迁移边界应为：

- `MMDCore`：PMX/VMD 数据、解析器、数学和坐标转换。
- `MMDRuntime`：Actor、动画节点、Bullet、运行时渲染与灯光/阴影系统。
- `MMDEditor`：Importer、SkeletalMesh/AnimSequence/MorphTarget 资产创建、IK、Slate/UI、Sequencer。
- `MMDCompatibility`：版本差异 API 后端。

暂不在本轮与投影修复混合重构。
