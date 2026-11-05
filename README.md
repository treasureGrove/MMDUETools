# Ue5MMDTools

Ue5MMDTools 把 PMX 角色和 VMD 动作、表情、相机带进 Unreal Engine 5。在 **MMD STUDIO / 角色工坊** 里试动作、换布光、切镜头，再把当前预览做成 MP4。

[下载 Win64 插件包](https://github.com/treasureGrove/MMDUETools/releases/latest) · [图文操作指南](github.md) · [B 站演示](https://www.bilibili.com/video/BV1M2RXBPExk/)

## 待机动作预览

<p align="center">
  <img src="MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-nikki-face-fill/yyb_10th_idle.gif" width="576" alt="YYB 10 周年初音未来在薰紫摄影棚侧脸待机并眨眼">
</p>
<p align="center"><em>10 周年初音未来 · 薰紫摄影棚 · 侧脸待机</em></p>

这段 3.2 秒的待机由 `main` 开发版在 UE 5.8.3 中离屏渲染，启用 Bullet 物理并追加 VMD 眨眼表情。[画面与捕获记录](MMD/Plugins/Ue5MMDTools/Docs/Screenshots/2026-11-05-nikki-face-fill/README.md)

### 借物表

- **角色与模型**　初音未来，Crypton Future Media, INC.。模型为三目YYB（SANMUYYB）的 10 周年版 v1.02，用于非商业演示。[角色许可](https://piapro.jp/license/pcl) · [作者配布](https://bowlroll.net/file/146083)
- **动作**　Wait of Mei，由名古屋工业大学计算机科学系 MMDAgent Project Team 发布。取身体动作前 3.2 秒，另配眨眼和轻微嘴角变化。[动作原始来源与 CC BY 3.0 署名](MMD/Plugins/Ue5MMDTools/Resources/PreviewMotions/README.md)
- **场景**　Ue5MMDTools 薰紫摄影棚。

## 快速上手

解压 UE 5.8.3 Win64 插件包，将 `Ue5MMDTools/` 放进自己的 UE 工程 `Plugins/` 目录。启用插件并重启，从 **窗口 → Ue5MMDTools** 打开角色工坊。

导入 PMX 后，选择待机动作、场景和镜头。准备出片时点击 **按当前预览制作**。安装步骤、面板截图、场景预览和 MP4 输出说明见 [图文操作指南](github.md)。

[完整文档](MMD/Plugins/Ue5MMDTools/Docs/README.md) · [修改记录](MMD/Plugins/Ue5MMDTools/Docs/CHANGELOG.md)
