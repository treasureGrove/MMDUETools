# 二期参考与实际交付链核对

核对日期：2026-10-11。B「樱庭水镜」是 2026-10-09 生成式需求图；终末地是人物材质方向。社区源码、离线资源记录、插件引擎实拍分别作为不同证据，不能互相代替。

**范围更新：**用户最新要求只改准确的独立参考预设，钟离/终末地口述尚未唯一对应。本文是已完成的结构审查；主樱庭旧 Genshin 绑定和预览缺口不自动成为修复任务。依赖目标的生成/验证已撤回授权，详见[当前预设范围](PHASE_TWO_PRESET_SCOPE.md)。

## 公开工程的完整输入与输出

本次沿源码、配置、资源引用、材质构建、pass 和最终合成核对，而非仅按 README 的效果名判断。没有运行这些工程、未知脚本或安装器，没有将其模型、角色贴图或整套管线复制到插件。

| 参考与固定提交 | 实际链路 | 对通用插件的意义 |
| --- | --- | --- |
| [MyZmdShaders / 65074573](https://github.com/qiudashu233/MyZmdShaders/tree/650745732f8251db150744eab60eb01613d1ecc8) | Unity 2022.3.20f1 / URP 14.0.10；模型 normal、ORM、split normal、spec ramp、SDF、expression 与 eye UV；GBufferBase / CharacterForward / outline / depth / shadow；另有 LUT 后处理 | shader 依赖 tree 与 manifest 中缺失的 `com.unity.render-pipelines.danbaidong`，未找到完整运行时绑定；未找到明确根许可。仅作方法参考，不能称可直接运行或移植的完整实现 |
| [Endfield_Character_Rendering / 2634c570](https://github.com/congyuxiaoyoudao/Endfield_Character_Rendering/tree/2634c570011f0dd2c9199dc0e719136c69ca933e) | Unity 2022.3.57f1c1 / URP 14.0.11；人工分配各角色材质；配套 N/P/ST/SDF/RD 纹理、UV7 平滑法线、眉眼与刘海代理、多 pass、SSAO、阴影、ACES、曝光和调色 | 根 MIT 为作者声明，URP 模板/依赖另受 Unity 许可约束。可独立重写几何/法线/光照概念；不能把绑定模型 UV 的 SDF、lip mask 和专有贴图当通用输入 |
| [Arknights-Endfield-MME-Shader / f9e90932](https://github.com/chris0214/Arknights-Endfield-MME-Shader/tree/f9e90932a25678101e3e28a8d91663fa0706a4ed) | MMD + MME 的 DX9/SM3 材质生成工作流；按 Hair / Face / Cloth 检查专用纹理并生成效果文件；材质、环境和输出合成共同参与 | 作者代码根 MIT；兼容纹理与第三方资源需要分别核对。生成器的输入语义校验和分材质组织有参考价值，不能把专有纹理链直接用于所有 PMX |

Unity 示例的实际配置还需区别于功能名：Skin 的 SSS 加法已注释；当前 Iris 使用 `REFRACTION_NONE`；Hair 绑定的 SpecularRampMap 未进入实际高光；EndfieldRenderFeature 在样例 renderer 中关闭。Hair 的额外灯贡献被清零，一项各向异性色没有乘真实主灯色。配置存在不证明功能生效，也不能原样照搬来满足插件真实灯光响应。

MyZmd 的 cloth 使用 ORM 的 R/G/B/A 对应 metallic/reflectivity/AO/smoothness，环境来自指定 cubemap；Hair 使用配套 split normal 的 RG/BA 分别控制表面与镜面法线。Face 与 Eye 还依赖模型特定 UV。其画面不仅由一个 BRDF 决定，缺失的 renderer hook、资源、材质和头部绑定不能用替换函数补齐。

MME 的生成器要求 Face 的 SDF/ColorMask/RD/LUT/ST，Hair 的 HN/P/RD/RS/ST/HairLine，Cloth 的 N/P/RD/RS/LUT。Hair 默认高光 key 从相机方向构造，Cloth 环境默认为 MatCap，切换后才用固定 HDR；这些展示效果不能直接作为真实庭院环境响应的证据。最终 Post 有显式 gamma/tonemap，不能原样重复套到 UE 的场景线性和引擎后处理。

本插件继续保持 BaseColor 最小输入。可选自有通用贴图改变微表面；大衣褶皱、发束曲率、眼角膜和服装轮廓仍需要相应几何。人物环境输入当前为灯光 RT、主视图 CSM、Sky SH 和 SkyLight 环境镜面；开启 Lumen 并不使 Unlit Custom 自动获得局部 GI、SSR、screen AO 或所有点光阴影。

## 本机实际消费路径

以下为源码、文件哈希、资产字符串和宿主逻辑核对；新主视图运行结果另记，不能把静态核对称为视觉通过。

| 环节 | 2026-10-11 检查结果 | 交付要求 |
| --- | --- | --- |
| PMX 导入 | `TMMDMeshBuilder` 仍首先绑定旧 Resources/MaterialInstance 的 Base/Hair/Face | 应显式应用选定 Reference preset，并保留恢复源材质和缺资源兜底 |
| 材质构建 | Reference builder 生成四风格 × 三种 blend，44 Custom 输入；源码签名覆盖 Character、Core lighting 和 BRDF | shader 与保存的父材质必须一起更新；不能只核 include 文件 |
| 预设应用 | `MMDStageLibrary` 创建逐槽 MID，复制原 base texture、颜色/透明度并设置材质角色与风格参数 | 逐槽读回真实 MID 父类、角色、base texture、实际参数与源码签名 |
| 舞台生成 | StagePresets 创建预设、Blueprint、地图和保存的舞台相机；即使不传 `-Generate` 也会调用 Ensure，可能升级并保存材质 | 不把该命令的无 Generate 模式当纯只读验证；消费者检查使用独立宿主 |
| 主 Content | 原有 496 文件仍在；与 Revision4 私有生成结果有 27 项差异；主 Sakura preset 指向旧 Genshin 父类 | 私有工程成功不能证明主插件已接上新 Endfield 预设；正式更新需要受审增量资产与实际消费验证 |
| 主编译产物 | 主插件 DLL 为 2026-10-09 的历史构建，私有 DLL 为 2026-10-11；BuildId 相同只证明兼容 | 修改 C++ 后须重新构建主 Editor，并在新进程验证，不能以相同 BuildId 宣称源码最新 |
| 旧主视图宿主 | 只 Load 三张地图确认存在，随后新建 native StageActor 并另设 preset；报告的 `material` 只是预设标签 | 增加真实地图打开协议，检查已保存 Blueprint / preset / camera 和 35 个实际材质槽 |
| 工具缩略预览 | `UMMDToolPreviewRenderer` 使用 SceneCapture2D；当前 view extension 对 SceneCapture 返回，不提交该视图灯光写入和 CSM | 不能只套 Reference 材质便称光照接入；要验证 RT 与 world/视图的对应，避免读到其他世界的历史 RT |
| 编辑器预览面板 | `MMDViewPanel` 用普通 EditorViewport 和独立 LookDev；没有自动套舞台预设；全局 PreviewLightOverride 没有 world 绑定 | 实际地图与编辑器预览分别验收；后续桥接不能污染同编辑器其他世界的灯光 |

## 本轮有限验证协议

生产只变背景映射；BRDF、Character、Garden、StageActor、灯光、姿态和曝光保持 Revision4。私有宿主新增两个独立模式：

- Native2：使用原 17 帧中的 body 与 face，核对同动画时间、曝光和三盏灯的参数，比较背景恢复前后。
- Map1：命令行打开实际生成 Sakura 地图，使用保存的 Blueprint、preset 和 camera；只对相同临时测试角色显式应用地图舞台自己的 preset。逐槽记录 35 个 MID、base texture、role、style constant 和 source signature；读取最终合成 PP 和实际 GPU 灯光/CSM。

Map1 为诊断关闭景深与运动模糊，并冻结舞台/角色时间；若需要选择已保存的相机，报告实际是否由宿主选择。它证明保存资产被消费，不证明工具面板预览已接通，也不证明与生成式 B 一致。

所有 UE 操作通过共享门禁串行、隐藏执行并等待自然退出；保留父输入、产物、资产重建记录和旧报告。每次 shader/灯光修改仍须看真实 beauty 与必要的单变量对照；技术检查通过不能替代视觉判断。当前本轮执行结果尚待归档。

## 后续顺序

先确认背景恢复和真实地图的材质消费，再进行 Hair 直接镜面隔离。关闭 SoftSpecular、环境镜面、rim、shift 与 detail，仅比较直接镜面 0 / 0.50；若仍沿发片呈纵条，补私有 N/Ns/T/roughness/T·H 诊断，依据实际发片曲率修正。之后独立处理 Skin 的鼻颊法线与遮挡、Cloth 的低频折面与高频织纹、局部环境输入及预览世界隔离。用户维护的 `TMMDAnimeFace.usf` 不修改。

本轮不提交或推送；不把 Saved、构建产物、测试角色或游戏资源归档入 Git。实际截图和批次结果见[樱庭目标与验收](PHASE_TWO_SAKURA_RENDERING.md)。
