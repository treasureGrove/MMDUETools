# Ue5MMDTools

Ue5MMDTools 把 PMX 角色和 VMD 动作、表情、相机带进 Unreal Engine 5。在 **MMD STUDIO / 角色工坊** 里试动作、换布光、切镜头，再把当前预览做成 MP4。

[下载 Win64 插件包](https://github.com/treasureGrove/MMDUETools/releases/latest) · [完整面板指南](MMD/Plugins/Ue5MMDTools/Docs/TOOL_PANEL_GUIDE.md) · [B 站演示](https://www.bilibili.com/video/BV1M2RXBPExk/)

## 待机动作预览

<p align="center">
  <img src="MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-maya-portraits/maya_think.gif" width="560" alt="雛乃木まや在深蓝展厅侧头思考的头肩特写">
</p>
<p align="center"><em>侧头思考 · 深蓝展厅 · 头肩特写</em></p>

<p align="center">
  <img src="MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-maya-portraits/maya_breath.gif" width="42%" alt="雛乃木まや在珍珠回廊轻柔呼吸的胸像">
  <img src="MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-maya-portraits/maya_clothes.gif" width="42%" alt="雛乃木まや在薰紫摄影棚整理衣摆的半身镜头">
</p>
<p align="center"><em>轻柔呼吸 · 珍珠回廊　｜　整理衣摆 · 薰紫摄影棚</em></p>

三段由 UE 5.8.3 离屏渲染，制作时启用了 Bullet 物理。[捕获记录](MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-maya-portraits/README.md)

### 借物表

- **角色**　公式 MMD 模型「雛乃木まや」Ver1.10，角色原案 KAZUYA（和夜），建模 金子卵黄。[官方配布](https://hinanogimaya.com/mmd-download/) · [模型使用条款](https://hinanogimaya.com/license/)
- **动作**　MMDAgent 的 Breath of Mei 与 Idle of Mei，由名古屋工业大学计算机科学系 MMDAgent Project Team 发布。[动作原始来源与 CC BY 3.0 署名](MMD/Plugins/Ue5MMDTools/Resources/PreviewMotions/README.md)
- **场景与布光**　Ue5MMDTools 的深蓝展厅、珍珠回廊和薰紫摄影棚。

## 安装

1. 从 [GitHub Releases](https://github.com/treasureGrove/MMDUETools/releases) 下载 UE 5.8.3 Win64 插件包。解压后把 `Ue5MMDTools/` 放入工程的 `Plugins/`，目标为 `YourProject/Plugins/Ue5MMDTools/Ue5MMDTools.uplugin`。
2. 启用插件并按 UE 提示重启。从源码构建可用 [`MMD/Plugins/Ue5MMDTools/`](MMD/Plugins/Ue5MMDTools/)，需要 UE 5.8 / Win64 的 C++ 工具链。Bullet 头文件和 Windows 库已随插件提供。
3. 进入关卡编辑器，打开 **窗口（Window） → Ue5MMDTools**。也可在编辑器控制台运行 `MMD.OpenPanel`。

> 仅安装插件时使用自己的 UE 工程即可。仓库内的 `MMD.uproject` 是开发宿主，还声明了 `ModelContextProtocol`、`AllToolsets` 和 `ToolsetRegistry` 等本仓库未附带的依赖。自定义原生渲染后端另有固定引擎版本要求，见下文。

## 快速上手

### 1. 载入角色与动作

在 **角色** 页点击 **导入 PMX 模型**，选择 `.pmx` 文件，保留模型与贴图的相对路径。等待底部提示完成，右侧显示角色。已经导入过时，使用 **加载已有角色**，选择对应的 MMD Actor 蓝图。普通导入产物可在内容浏览器的 `/Game/MMDModels` 下找到。

标准 MMD 骨架加载后默认尝试播放 **恬静待机**。也可直接选择 **轻柔呼吸 / 侧头思考 / 整理衣摆**。底部 **实时物理** 需要模型包含刚体数据；可通过播放、定格、时间轴和逐帧按钮检查姿势。

[角色页操作实拍](MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-10-23-interaction/README.md)

### 2. 选择风格与场景

1. 切到 **风格**，目标保持 **预览角色**。搜索或找到 **终末地 V2**、**无限暖暖** 等卡片，再点 **预览风格 →**。这次切换是临时的，随时可点 **恢复原材质**。
2. 切到 **场景**。保留默认 **纯白影棚**，或选择 **樱雾柔光 / 深蓝展厅 / 珍珠回廊 / 薰紫摄影棚**。场景卡会一起切换展示布景和配套光照；三套旧云空场景收在折叠区。
3. 用视口上方的 **全身 / 半身 / 面部 / 侧面 / 背面** 检查镜头。完整舞台的 **预览舞台** 只影响预览区；需要把舞台加入当前关卡时才点 **放入关卡**。

[五套摄影棚对照](MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-10-25-studio-collection/README.md)

### 3. 保存动画或合成序列

在 **角色 → 导入自己的 VMD／表情／相机** 中使用 **导入 VMD 动作** 生成 `AnimSequence`，按需 **追加表情** 或 **导入 VMD 相机**。内置动作卡可以直接用于预览和后台 MP4。需要单独保存内置动作资产时，从[内置动作目录](MMD/Plugins/Ue5MMDTools/Resources/PreviewMotions/)导入对应 VMD。

打开 **合成关卡序列**，核对 `Model`、`Animation` 和 `Camera` 后点 **Create Master Sequence**。导入 VMD 相机会填入相机槽，**烘焙物理动画**完成后会填入动画槽。需要沿用当前预览机位、风格和展示布景时，使用下一节的后台制作入口。

## 后台生成 MP4

保存导入的角色后，在面板里选好动作、风格、展示场景和透视机位，点击顶部的 **按当前预览制作**。后台从动作第 0 秒开始输出 30 fps MP4，沿用预览画幅、倍速与物理开关。进度和取消按钮留在面板，文件写入 `Saved/MMDProduction/`。需要 Windows D3D12 SM6 以及可调用的 `ffmpeg` 和 `ffprobe`。

[制作按钮位置与面板实拍](MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-04-preview-production/README.md)

需要 VMD 镜头、独立表情或音乐时，可用脚本指定素材。后台会输出 PNG 帧、MP4、动画和序列；在编辑器内执行 `MMD.OpenProduction "作品目录"` 可重开动作和镜头。[后台制作指南](MMD/Plugins/Ue5MMDTools/Docs/MMD_PRODUCTION_WORKFLOW.md)

## 原生风格与兼容性

- 终末地 V2 和无限暖暖的**插件自定义原生后端**依赖 **UE 5.8.3 / CL58210709 / Win64 D3D12 SM6**，并需显式启用 Substrate 和后端。按[配置模板](MMD/Plugins/Ue5MMDTools/Scripts/EndfieldNative.example.ini)与[原生渲染指南](MMD/Plugins/Ue5MMDTools/Docs/ENDFIELD_NATIVE_RENDERING_GUIDE.md)合并相关配置，保留本工程的阴影与其他设置，再重启。仅应用风格卡片不保证进入截图中的原生渲染路径。
- Reference 和旧风格保留独立的兼容路径。通用发丝、布料和描边的参数见[舞台与 Reference 预设说明](MMD/Plugins/Ue5MMDTools/Docs/STAGE_RENDER_PRESETS.md)。插件的原生适配不修改安装引擎文件。
- 不同模型的骨骼命名、IK 和特殊 Morph 可能需要适配。材质、骨骼及 UV Morph 尚未完整支持为 UE 动画通道。

更多资料：[文档入口](MMD/Plugins/Ue5MMDTools/Docs/README.md) · [渲染架构](MMD/Plugins/Ue5MMDTools/Docs/RENDERING_ARCHITECTURE.md) · [源码构建与分发验证](MMD/Plugins/Ue5MMDTools/Docs/ENDFIELD_PLUGIN_DISTRIBUTION.md) · [修改记录](MMD/Plugins/Ue5MMDTools/Docs/CHANGELOG.md)
