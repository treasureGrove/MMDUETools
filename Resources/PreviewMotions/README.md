# MMD Studio 展示动作

当前面板使用 **MMDAgent 的 Mei 女性角色动作**。用户否决了前轮程序生成的抬手／站姿效果，现已撤下那四组动作及生成脚本，不再将其作为默认或分发预设。

- 恬静待机：`MMDAgent/mei_wait/mei_wait.vmd`，4秒。
- 轻柔呼吸：`MMDAgent/mei_breath/mei_breath.vmd`，2.5秒。
- 侧头思考：`MMDAgent/mei_idle/mei_idle_think.vmd`，85帧／约2.833秒。
- 整理衣摆：`MMDAgent/mei_idle/mei_idle_touch_clothes.vmd`，2秒。

原始二进制文件取自 [MMDAgent Sample Script 1.8 官方包](https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/)，**未改写动作**。每组原始 `README.txt`／`COPYRIGHT.txt` 一起保留，精确包内路径和 SHA-256 见 [来源清单](MMDAgent/source_manifest.json)。

**MMDAgent Motion "Wait of Mei", "Breath of Mei", "Idle of Mei"**

Copyright (c) 2009-2015 Nagoya Institute of Technology, Department of Computer Science. Released by MMDAgent Project Team.

这些动作按 **[CC BY 3.0](https://creativecommons.org/licenses/by/3.0/)** 分发，保留署名和许可说明；不是本插件原创，也不由项目代码 LICENSE 重新许可。作者另有[官方动作许可说明](https://mmdagent.wordpress.com/2013/12/13/license-terms-of-for-the-all-files-other-than-the-3-d-model-mei/)。没有下载或捆绑 Mei 角色模型、语音、图片，模型许可与动作许可分别核对。

动作含完整肩／肘／腕／手指轨道，通过现有 VMD 导入器及 ModelData／骨架映射转换，保留作者原始时长与插值。预览动画只驻留内存，Bullet 使用当前模型自己的刚体和关节。当前面板关闭源 Morph 导入以保持预览不写 Skeleton 元数据；缺失骨骼跳过，身体比例差异仍可能造成手部接触偏移。
