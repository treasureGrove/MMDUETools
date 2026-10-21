# 暖暖方向的原生渲染预设

实现日期：2026-10-19。原作配置证据仍见[最初研究记录](Screenshots/2026-10-19-infinity-nikki/package-evidence.json)，此处说明原生迁移。完整游戏画质须另外对照，架构接入不作为视觉验收。

## 使用与数据路径

使用 `/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki`，显示名“无限暖暖 · 原生柔光衣装”。原生父材质与六类 Profile 位于 `Stages/Materials/InfinityNikki/`，共六个表面父材质、六个 Profile 及一个预设；最初三份旧 Unlit 材质保留为非 Substrate 兼容回退。

开启 Substrate、桌面 SM6 原生后端及 adaptive GBuffer format1。维护脚本 [Invoke-InfinityNikkiPresets.ps1](../Scripts/Invoke-InfinityNikkiPresets.ps1)只为本次进程传入启动参数，不改项目全局配置。`-CharacterSample <目录名>`选择已有 Saved 导入样本，`-NoGenerate`只校验现有预设，`-SkipCapture`只生成与编译。

材质从现有原生图复制，使用`FrontMaterial`、Substrate Toon与共享载荷。[暖暖几何入口](../Shaders/Core/TMMDInfinityNikkiGeometry.ush)复用头框架与投射覆盖，将连续几何法线传给逐灯响应，不使用终末地的角阈值作为受光分区。不透明与Masked在BasePass写入原生packed材质缓冲，延迟逐灯接收当前灯和原生阴影。透明Skin／附层使用已有Surface Forward inline路径。GI与反射继续在原生合成位置参与，暖暖材质不读取LightDataRT或辅助阴影RT。

运行时对自有父材质创建独立瞬态 Profile，保留源曲线 RGB，以 Diffuse alpha 的专用平台标记暖暖风格。共享后端据此选择 [TMMDInfinityNikkiNative.ush](../Shaders/Core/TMMDInfinityNikkiNative.ush)，不会将终末地 RD／RS 或皮肤颜色立方体写入暖暖 Profile。缓存键包含风格，避免同源 Profile 串用。终末地原有分支继续保留。

## 当前材质响应

- 连续 Burley 漫反射；皮肤使用局部包裹与微弱交界染色，受当前灯原生遮挡约束。
- 镜面直接计算GGX分布、Smith可见度与Fresnel，矩形灯使用LTC近似，不经过Toon高光色带。皮肤叠加宽、窄两种粗糙度波瓣，布料补Charlie纤维柔光；头发使用几何双瓣各向异性与纤维染色。
- 部位有独立粗糙度、镜面和金属度；Face／Skin 不继承终末地的静态肤色配方。保留源底色与透明度。
- 仅底色可显示，几何头框架和自动 FaceData 可选。投影覆盖控制复用现有受控原生材质；Hair 的投射抑制影响所有接收面，不能称只排除刘海对脸的投影。
- 暖暖预设显式覆盖主光源角、三灯镜面比例及樱庭天空色／强度，避免主题配方压低镜面或把皮肤整体染蓝。新增覆盖开关默认关闭；退出覆盖后恢复进入前的主光源角，终末地沿用旧配方。

这是独立的原创响应，不是已恢复的原作 shader。仍没有真实空间 SSS、预积分皮肤 LUT、原作 OIT、ShellFur 或宝石体折射；透明 GI 与不透明 Lumen 的能力也不同。

## 维护与验证

共享后端升至 `mmd-native-bsdf-v15-style-profiles`／Cooked协议15；Graph7、payload5和 FaceData5 保持。新增风格标记仍使用已有 Profile 存储，没有新增 GBuffer RT 或独立角色光照 pass。逐灯多一次风格查询的成本未实测；旧 RT 后台重复工作也未在此轮停用。Cooked 需重新 Cook，不能配旧协议库。

[生成器](../Source/Ue5MMDToolsEditor/Private/Editor/MMDInfinityNikkiPresetsCommandlet.cpp)预检十三包归属，只对自有包生成；原始 EndfieldV2 图只读加载。实际 SM6 材质资源必须完成编译并包含绘制 shader。重复生成应不重写资产。

实拍使用既有 NativeBackendProbe，检查实际原生父材质、源贴图／透明度、Profile风格标记与恢复；分别拍四类灯的有／无遮挡、GI／反射等隔离和原生 Toon 基线。原生适配成功、CPU标记正确、图像响应与视觉接近原作是不同的结论，须分别记录。

2026-10-19的17时批次完成首轮原生接入，171张实拍／506次槽检查属于当时版本的功能验证，视觉已被用户否决。[历史实际截图、失败记录和范围](Screenshots/2026-10-19-infinity-nikki-native/README.md)。当时误将常量Toon高光色带视为保留真实GGX，实际会压平镜面分布；后续已改为直接计算，不能引用旧批次证明新画质。SceneCapture默认不启用Lumen，维护脚本已显式设置捕获GI／反射方法与预览图元追踪；只有带此选项的结果可用于Lumen验证。

视觉仍须对照原作验收；空间SSS、原作OIT等缺口没有因接入原生管线自动消失。17时旧版的皮肤反射与AO像素差未超过三倍复拍噪声，该结果不能套用到后续版本。当前未重跑完整BuildPlugin／Cooked／Shipping／性能。

19时打磨批修正了镜面色带压平、面积光归一化及脸部柔化法线，更新暖暖材质与灯光配方；六父材质SM6编译和91图／401次槽检查通过，并覆盖动作第2秒及同舞台切换恢复。终末地BSDF与资产保持，角色ROI与打磨前基线基本一致，整图仍有背景阴影噪声。[最新同机位前后对照及限制](Screenshots/2026-10-19-infinity-nikki-polish/README.md)。
