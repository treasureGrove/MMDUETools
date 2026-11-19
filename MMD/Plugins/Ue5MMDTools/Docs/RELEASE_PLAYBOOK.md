# 发布与传播 playbook

这份文件只解决一件事：**让已经做好的东西被看见。** 代码质量不需要在这里加分，发布页、首页和渠道决定有多少人走到代码前。

## 一、每次发布前的最小检查

- [ ] 首页第一屏的 GIF 能播：在匿名窗口打开 `https://github.com/treasureGrove/MMDUETools`，图片不能是裂的。相对路径区分大小写（`docs/` 不是 `Docs/`）。
- [ ] Release 说明里有至少一张图或一段动图，且用的是绝对链接。
- [ ] `支持范围` 表与本次实际验证的版本一致，不把「没测」写成「支持」。
- [ ] ZIP 与 `.sha256` 同时上传，文件名带 UE 版本与平台。
- [ ] 发布后 30 分钟内发一条 X／B站动态，文案带一个具体数字（帧数、秒数、大小），不要只写「新版本发布」。

## 二、发布说明模板

````markdown
## Ue5MMDTools vX.Y.Z

一句话：这次解决谁的什么问题。

<p align="center">
  <img src="https://github.com/treasureGrove/MMDUETools/raw/main/docs/Screenshots/<日期>-<主题>/<文件>.gif" width="720" alt="实际成片节选">
</p>

### 新增
- 面向使用者的行为变化，一句话一条。

### 修复
- 症状 → 原因 → 现在的行为。

### 验证
- 构建配置（Editor / Game / Shipping）与结果。
- 实际出片参数：分辨率、帧率、帧数、物理步进数。
- 已知未验证的范围，单独写清楚。

### 安装
下载 `Ue5MMDTools-vX.Y.Z-UE5.8.3-Win64.zip`，解压到 `YourProject/Plugins/`。
````

一条铁律：**没有图的 release 不发。** 图不一定是新功能截图，一张成片帧也够，但它必须来自这次的真实构建。

## 三、渠道与写法

按性价比排序，每次只推两条，别一次全发。

| 渠道 | 发什么 | 写法要点 |
| --- | --- | --- |
| X / Twitter | 15–30 秒竖版或横版动图 | 英文一句话 + 日文一行；`#UE5 #MMD #MikuMikuDance #VMD #初音ミク` |
| Reddit `r/unrealengine` | 同一条动图 | 标题写效果不写工具名；正文再给仓库链接和「MIT、可商用」 |
| Reddit `r/MMD`、`r/Vocaloid` | 成片对比帧 | MMD 原片 vs UE5 成片，并排贴 |
| B 站 / 抖音 / 小红书 | 竖版 15 秒 | 前 3 秒必须出现成片画面，标题带「MMD 进 UE5」 |
| YouTube（英文） | 60–90 秒流程 | 标题含 `MMD in Unreal Engine 5`，开字幕 |
| 知乎 / Qiita / Zenn | 技术长文 | 一篇讲「MMD 物理怎么进 UE」，一篇讲「一键出片管线」 |
| Fab | 免费上架 | 与 GitHub 同版本，Release 说明互相链接 |

## 四、互链与收录（一次性动作，长期有效）

1. 向 `awesome-unreal`、`awesome-mmd`、`awesome-vtuber` 提 PR，一行描述加链接。
2. 与 [MMD2Unreal](https://github.com/JDui/MMD2Unreal)、[IM4U](https://github.com/tekifuta/IM4U) 互相在 README 里引一行。同赛道互助比互相无视更能带来搜索权重。
3. 在 [Blender MMD Tools](https://github.com/UuuNyaa/blender_mmd_tools) 的 Discussions 里回答「能不能直接进 UE」，顺带给出本插件。不要硬广。
4. 每个 release 的说明里链回首页；首页链回 release。爬虫和人都顺着走。

## 五、让仓库看起来是活的

- Issue 24 小时内至少回一句，哪怕是「收到，先确认版本」。
- 超过 30 天没有进展的 PR，合并或关闭，不要留着当装饰。
- Discussions 固定三个分类：`Showcase`（用插件做的成片）、`Help`、`Requests`。
- 首页顶部 `支持范围` 表随验证更新；「未验证」本身是可信度，不是缺点。

## 六、看什么数字

| 指标 | 现在 | 下一目标 |
| --- | --- | --- |
| Star | 5 | 50（进入该关键词搜索的可见区） |
| Release 下载 | 9 | 100 |
| Star ÷ 下载 | 0.56 | 0.3–0.5 之间都算健康 |

比值过高说明曝光不足，过低说明下载了但不认可。**先投曝光，不要先改产品。**

## 七、节点

- UE 新版本发布后一周内，发一条「已在 X.Y 上跑通」的验证动态。
- 8 月 31 日（初音生日）、9 月初（MMD 相关纪念日）前后发成片。
- 热门 VMD 动作配布后，用同一动作出一版对比，标题带动作名——这是最容易被转发的时机。
