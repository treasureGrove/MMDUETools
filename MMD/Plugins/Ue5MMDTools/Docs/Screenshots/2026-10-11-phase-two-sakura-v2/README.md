# 二期樱庭水镜目标与实拍

归档日期：2026-10-11。

`goal_sakura_generated.png` 是用户选定的 B 樱庭水镜生成式需求图，来自 `Saved/ArtDirection/20261009/B_sakura.png`，生成日期为 2026-10-09。此处仅复制归档，没有改画或调色；它不是插件引擎实拍。

![生成式需求图](goal_sakura_generated.png)

实际截图捕获于 2026-10-11，UE 5.8.3 / CL58210709，后台未打包 GameViewport，1280×720；每组材质就绪后捕获，物理 178 刚体顺序推进 2.6 秒。它们是插件测试角色，不是商业游戏实拍。所有 PNG 原样复制，没有裁切、改画或调色。

| 原图 | 来源批次完成时间（SGT） | 条件 |
| --- | --- | --- |
| [baseline_body.png](baseline_body.png) | 14:57:30，MainViewBaseline583Repair3 | 旧实现与灯光；指定 Endfield 父材质的修改前有效基线 |
| [v2_character.png](v2_character.png) | 15:30:35，MainViewV2583CaptureRecover3 | V2 shader 与灯光同时改变；不是纯 shader A/B |
| [revision4_body.png](revision4_body.png) | 16:05:23，Revision4/Capture | 新湿石、疏花、格栅与背景；背景构图回退 |
| [revision4_character.png](revision4_character.png) | 16:05:23，同批 | 鼻部平滑修订；角色自阴影开启 |
| [revision4_character_self_shadow_off.png](revision4_character_self_shadow_off.png) | 16:05:23，同批 | 同镜头仅关闭角色自阴影的诊断对照 |

**视觉验收未通过。** 月亮入画但远岸建筑、水岸丢失；人物宽亮带、鼻颊塑造和湿石质感仍不足。编译通过及每组 17 张诊断图不代表美术迭代完成。

SHA256、原始文件路径、报告哈希、捕获批次范围及文件写入时间见 [manifest.json](manifest.json)。验收标准、实际修改、失败/恢复边界与下一步单变量方案见[二期樱庭水镜记录](../../PHASE_TWO_SAKURA_RENDERING.md)。
