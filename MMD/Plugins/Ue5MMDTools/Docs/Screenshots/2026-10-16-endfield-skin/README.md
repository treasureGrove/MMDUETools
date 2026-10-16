# 2026-10-16：独立皮肤／脸部材质

捕获与归档日期：2026-10-16，Asia/Singapore。UE 5.8.3 CL58210709，Win64 D3D12 SM6，后台 `RenderOffscreen`；YYB16th 带翼款、参考姿势。下图均是引擎输出的 1280×720 PNG 原字节，没有重绘、调色或生成式效果图。来源、机位和 SHA256 见 [sources.json](sources.json)。

使用与代码入口见[皮肤／脸部材质说明](../../ENDFIELD_SKIN_MATERIAL_GUIDE.md)。这轮调整皮肤的底色、粗糙度与高光输入；既有原生灯光、投影、GI 和 SDF 明暗逻辑继续工作。

## 同机位肤色开关

两张使用同一场景、相机、光照和曝光。身体肤色组均关闭，第一张关闭脸部肤色组，第二张开启中档脸部参数。原图的 RGB 和 alpha 没有改写，差异来自材质中的反射底色准备。

关闭：

![关闭独立脸部肤色](surface-off.png)

中档，作为本轮选定起点：

![独立脸部肤色开启](skin-medium.png)

中档比原来更暖，也保留妆容和五官细节；柔和档变化较轻，暖档更偏桃色。选择依据是实际画面检查，没有把某个 RGB 数值当作终末地肤色标准。

## 预设与诊断

实际 `DA_Render_EndfieldV2` 预设默认状态；身体组和脸部组均启用，各自使用独立参数：

![预设默认肤色](skin-default.png)

关闭 SDF 阈值明暗控制，肤色仍生效；此图仍保留几何光照和原生投影：

![关闭SDF阈值仍保留肤色](skin-direction-off.png)

关闭 GI、天空与反射的组合隔离，不能称为只关闭 Lumen：

![环境贡献组合关闭](skin-gi-off.png)

主光强度加倍，用于检查材质随现有光照变化：

![主光加倍](skin-main-double.png)

以上诊断改变了不同开关，不能彼此作为单因素肤色对照。完整运行还包含对应的肤色关闭配对、主光减半、高光关闭及复拍。

## 验证范围

最终 `skin2` 运行包含44张实拍、195次槽检查；`mode0b` 为8张实拍、78次槽检查，均0失败。15包生成和七个父材质的真实 shader map 编译通过。独立像素分析核对肤色响应及白衣、翅膀、头发、背景对照区域；原样复拍存在时间积累噪声，不声称逐像素一致，也不把 CPU 参数检查称为全部 GPU 内核验证。

主脸三槽、透明附脸层两槽使用同一套肤色逻辑；源 opacity 保持 `1/1/1/.45/.001`。39个源槽的底色、透明度、参数、Profile 和父材质均检查恢复。新回归另验证 FaceSkin 开启、Payload／Direction 关闭时，移除 FaceData 或破坏宿主名／纹理会清版本0，恢复有效绑定后回到版本5且保持 Direction 关闭，避免沿用旧图染脸。

首轮 `skin1` 的44张图及既有检查通过，随后只读审查发现上述失效回退遗漏；修复后重建并执行 `skin2`。本页仅展示修复后的运行。较早 `before` 报告没有相机旋转字段，未作严格跨运行像素比较。

本模型 `body01` 槽仍为 Generic，未把混合衣物贴图强行判成皮肤，因此这次脸部实拍不能代替身体 Skin 美术验收。没有实现厚度透射、原作 SkinLUT 或真正屏幕 SSS；极端侧光鼻梁硬边与完整头发、眼睛效果仍待迭代。透明 Forward 的 GI 也不等同于不透明 Lumen。

Editor／Game 构建已通过。本轮未重跑完整 Cook／Shipping 游戏实拍，不沿用旧版 Cook 证明当前材质已验收。

2,378项既有保护文件中2,373项字节保持，变化仅五个自有 EndfieldV2 包；新增三个 Face 包。`FilterPlugin.ini` 另显式增加两份说明和两批截图目录。安装引擎 shader、用户 Face shader、既有未提交的旧场景／材质／Outline 和 Mobile 文档保留，不提交测试角色、Saved 或构建产物。

## 独立分发验证

同日 `EFSkin16` 的 BuildPlugin 通过 Editor Development、Game Development、Game Shipping 三种构建，包内新 Face 材质、Skin USH、Bullet 依赖与说明齐全，测试角色和动作已排除。新建空白宿主只使用发行包及明确记录的渲染启动选项；测试副本移走私有 Payload CustomOutput `.cpp` 后，七个发布父材质仍完成真实编译，shader 数依次为30／54／48／40／30／54／48，实际 GlobalShader 编译反射协议11，0失败。包内七份冻结源码与原工程逐字节相同。

这是独立插件构建、启动适配及材质编译检查，没有在空白宿主绘制角色，也没有执行完整 Cook 或 Shipping 游戏。本批角色实拍来自前述 `skin2`／`mode0b`；不把两种验证合并成未执行过的发行游戏测试。报告和日志 SHA256 见 [distribution-sources.json](distribution-sources.json)。发行构建完成后仅补充本文与交付记录，没有再改源码、shader 或材质包。
