# 终末地渲染 V2：给接手者的说明（Toon BSDF 探针阶段结论）

初稿：2026-10-13；最近核查：2026-10-16。写给接手的 AI（GPT）。先读这份，再读同目录 `ENDFIELD_V2_HANDOFF.md`（历史工作区与踩坑）。

读者入门入口：[终末地插件着色入门与维护](ENDFIELD_NATIVE_RENDERING_GUIDE.md)，含三版方案对比、数据流图、实际 GI 对照和代码入口。

## 最新进度：v13 原创弱暖红过渡（2026-10-16）

用户授权在现有皮肤明暗交界加弱暖红。当前后端为 **`endfield-native-bsdf-v13-skin-redness`／Cooked协议13／Profile布局42**，Graph6／payload5／FaceData5／Frame1不变。生产表面两组静态校色继续关闭，生成器的新Preset设置 `RednessStrength=.18`、`RednessWidth=.22`（NoL半宽）、`RednessAngularWidth=.035`（归一π角半宽，约6.3°）、`RednessTint=(1,.4,.30)`；结构默认强度0，旧资产不被自动加红。

新增项是**插件原创风格化泛红**，不能称提取到的原作红边、真实扩散或厚度透射。它只改Skin本灯漫反射：几何signed NoL形成局部band，主光V5用连续 `cutoff-theta` margin另算角band后按coverage与水平光可靠性混合；NoV只作弱权重，`SurfaceShadow≤.1`关闭新项。灯色和衰减只在原生外层乘一次，独立spec／GI不改，无新RT／GBuffer／空间pass。

颜色配方仍recipe1，0..215 cube不变；Specular.RGB的216槽存强度／NoL宽度／角宽度，217槽存Tint RGB，218..255重复215。Specular.A／RD.A与原九平台保留，仅layout改42。shader兼容41并保持原cube响应、红强度0；42尾数据无效只关红，不让仍有效cube被误当RS。新六值有限净化并纳入角色cachekey。强度0保持同参数v12，Master0保留原关闭回退。

最终 `Editor3`／`Game1`构建通过，`generate13`保存15包，七父材质真实shader数21／40／29／21／40／29／40。最终以 `redness3` **NativeBackendProbe**为准：26项skinredness加8项AB，共34图／156次槽检查／0失败；Mode0和启动atlas64各8图／78次检查／0失败，三组Editor GlobalShader协议13验证通过，17项静态边界检查通过。外部像素分析72／72，独立 `redness3_rois.json`按最终报告与PNG SHA绑定，当前图已目检；[原图归档](Screenshots/2026-10-16-endfield-skin-redness/README.md)与[摘要](Screenshots/2026-10-16-endfield-skin-redness/sources.json)保留来源，不提交Saved和测试角色。

侧光固定右颊MAE1.30296，对照噪声.20222，平均RGB增量 `(+1.0911,-1.0911,-1.5356)`，仅说明很小局部暖红。背光未超自身噪声，不算新增效果。右颊画面编码亮度clear200.02576→block.068889→nativeShadowOff202.61194，新项开关效应分别1.30296／0／1.27037；关闭原生阴影后，两档强度各自的放块／不放块整图RGB相同，零直射灯的泛红开／关整图相同。它支持当前ROI的门控归因，不是整图shadowmask，也不是真实扩散或厚度透射。

`Saved/EFSkinRedness16/Ue5MMDTools/` BuildOnly三配置与必需文件／皮肤源码检查通过，12份final-freeze Source／HLSL在工作区／发行包哈希一致。clean-host、发行角色GPU、Cooked及性能未重跑；身体Skin、additional与Legacy未独立GPU验证。保护2,381项中2,379不变，仅自有Preset和OffsetLinear两包变化。当前源码继续冻结，收尾只更新文档与原PNG归档；下方v12结果保持真实版本日期。完整十项目标、肤质与全部GPU路径仍未验收。

## 历史：v12 逐灯皮肤响应（2026-10-16）

用户指出静态暖tint不足以形成皮肤质感。当前新增 `CharacterSkinLighting`，在原逐灯入口分别调整暗色与视角受光色，再交给已有RD、原生阴影和灯光累加。新版生成的EndfieldV2预设开启它，`CharacterSkinSurface`／`CharacterFaceSkinSurface`两组静态校色默认关闭；独立Face父材质仍保留暖色预览兜底，Stage会显式覆盖。旧设置／nullptr默认关闭新响应。使用与维护入口见[皮肤材质说明](ENDFIELD_SKIN_MATERIAL_GUIDE.md)。

后端为 **`endfield-native-bsdf-v12-skin-response`／Cooked协议12／payload5／Graph6／FaceData5／Frame1**，发布仍七父材质／15包。Skin的Specular.RGB改存插件原创6³颜色立方体，216点位于固定256槽，索引 `r+6*g+36*b`；输出linearRGB，坐标为底色的encodedRGB，非模型UV。Specular.A仍为RD.A；Diffuse.A空隙用布局41保存总开关、LUT权重、两组视角强度、RGB与角柔化。Hair／Cloth的32×8 RS、旧签名、Mode／GI／诊断平台保留。缓存按源、角色与净化设置隔离，不可把一个共享Profile随MID反复改写。

新 `Core/TMMDEndfieldSkinResponse.ush` 与 `Private/Rendering/MMDEndfieldSkinLutData.inl` 必须随插件分发；配方为原创亮度相关暖色吸收，不复制／重采样客户端SkinLUT。没有改安装引擎、增加GBuffer或RT，也没有实现邻域扩散／厚度透射／真实空间SSS。原作RefineMask未接，因此脸部view响应仍是头框架与几何近似；不能把公开作者的SSS命名直接当物理散射。原作主脸高光使用几何法线，不能改用SDF方向覆盖它。

Mode2须在应用预设前将atlas设为至少256；绑定时低分辨率回原曲线／Mode0，已经分配tile／cube Profile后不支持运行中降为64。新代码必须重编并重新Cook；v11／v8的成功结果不代表协议12通过。

最终 `Editor3`成功；`Game1`运行时模块构建成功后，仅验证用CPP／Legacy USH继续修正。`generate12b`完成15包、七父材质，shader数依次为共享三模式21／40／29、Face三模式21／40／29、描边40。`response3` Mode2=47图／195次检查／0失败（18项skinresponse，含back-off-repeat；21项facefix；8项AB）；Mode0与启动atlas64各8图／78次检查／0失败。Editor全局shader编译反射协议12验证通过，不等于Cooked库验证。源码／验证文件共14项冻结，来源见[本轮归档](Screenshots/2026-10-16-endfield-skin-response/README.md)与[摘要](Screenshots/2026-10-16-endfield-skin-response/sources.json)，不提交Saved或测试模型。

实际背光暖肤差异明显，侧光／视角变化较细微；主光设0的开／关图全部RGB逐字节一致。外部55项严格检查全通过：背光白衣MAE5.0864≤自身off-repeat5.3136；右颊效应11.1052／噪声1.3548，鼻部核心9.9101／噪声.09788。这些只区分响应与时序波动，不是画质分数，鼻梁硬边仍在，不能称真实SSS或完整肤质验收。`FaceAngularSoftness`默认仍 `2/255`，`.025`只用于独立对照。首轮 `generate12`因Legacy调用漏第五参失败后修正；`response1`发现通过setter恢复DF阴影属性会裁值，Editor2修复后 `response2`46图0失败，最终 `response3`再补背光关闭复拍。

最终 `EFSkinResponse16c` BuildOnly通过Editor Development／Game Development／Shipping三配置，`payload_files_verified`和`skin_response_sources_verified`为真。14份Source／Shader冻结文件在当时工作区与发行包逐字节匹配；v12收尾阶段仅补文档与截图归档，v13源码变更另行验证。v12本机发行目录 `Saved/EFSkinResponse16c/Ue5MMDTools/`，依据见[来源摘要](Screenshots/2026-10-16-endfield-skin-response/sources.json)。v12空白宿主、发行包角色绘制与完整Cook／打包游戏均未重跑；BuildOnly不证明最终二进制角色GPU，不能沿用较早v11分发与v8 Cook结果。

保护清单2,381项中2,379不变，仅自有Preset与OffsetLinear包变化；用户27资产、10旧PNG、Mobile文档、Face shader与安装引擎保持。完整十项目标、身体皮肤、真实空间SSS及终末地画质验收仍未完成。

## 历史：v11 脸部角阈值与透明附脸层（2026-10-16）

用户认可 v9 的体积方向，本轮聚焦脸部 SDF 与疑似头发自投影。V4 实际是几何方向场；现改为 V5 左右角阈值、前层深度和覆盖。头部框架由骨骼与附近 Skin 几何拟合并持久化，运行时跟随同一头骨；不再用含翅膀／描边的全身包围盒，也不依赖 UV0 或实际新增 UV2／3／4。自动图是可继续绘制 R/G 的几何底图，不能称为原作手绘或真实遮挡距离场。

鼻部残斑通过“关方向／关全部动态阴影／只关 Hair 投射／只关鼻壳 opacity”分离。主要残斑来自模型另叠的半透明假鼻影壳，主脸阈值本身无法控制它；有效外脸额外退出身体 view tint。新增通用附脸关联，按骨权重、几何朝向、真实贴合距离、面积覆盖和全部现有 LOD 验证，透明层复用宿主图并保留原 RGB／软 alpha。生产没有 YYB、B、槽11/12或特定UV判定；这些只出现在固定样本诊断中。

不透明／遮罩仍走原生 BasePass packed 导出／逐灯读回；可靠透明附脸层在原生 Substrate Forward 绘制中使用 inline 载荷及阴影。没有新增 GBuffer RT、脸部 SceneCapture 或角色绘制。透明层高光／GI 使用头部球面代理，选定方向光使用相同阈值，局部灯刷新单位 L 的 BSDF Context。透明 Forward 的方向光数量、GI 与不透明 Lumen 仍有差别，不承诺全灯一致。

当前 **Backend `endfield-native-bsdf-v11-forward-face`／Cooked协议11／payload5／Graph6／FaceData5／Frame1**。Mode2、atlas256等门控继续保留；旧V4自动绑定关闭，需重烘。精确接入时序、贴图编辑和代码入口见[脸部V5说明](ENDFIELD_FACE_V5_GUIDE.md)，实拍、失败重试与验证边界见[脸部V5归档](Screenshots/2026-10-16-endfield-face/README.md)。同日新增[皮肤与脸部专用材质](ENDFIELD_SKIN_MATERIAL_GUIDE.md)，七个发布父材质、15包；肤色写真实BaseColor，Body与Face各自从源RGB计算，覆盖独立于SDF阈值开关，透明附脸层alpha保留。该批Editor/Game及44张Mode2、8张Mode0实拍通过，独立BuildPlugin三配置与空白宿主七父材质编译也通过，详见[皮肤批次来源](Screenshots/2026-10-16-endfield-skin/README.md)。新Cook尚未重跑；不能沿用v8证明v11 Cooked运行已验收。

首轮 `overlay11` 的CPU检查虽全过，diagnostic18却发现不透明脸全红、阈值无响应。原因是误用 `!SUBSTRATE_INLINE_SHADING`：UE在所有BasePass均定义其为1。已换成真正的 `SUBSTRATE_OPAQUE_DEFERRED` 并重编；必须同时检查不透明packed与透明inline的绿红负对照，不能只看5槽绑定或进程exit0。

最终`editor-final11`／`game-final11`与`generate11b`通过；后者12包／四父材质。`final11`42图／156槽、`final0`8图／78槽、`final-lights11`11图／78槽，共61图／312槽／0失败，实际协议11。外部像素检查确认主脸与软鼻壳区域绿→红、阈值暗／亮响应、旧Unlit四机位像素不变；三主图+两透明引用，源opacity .45／.001保持。鼻壳两槽各182/182顶点、100%面积，最大几何误差`.02013R`；8/9层未达98%故拒绝。四类灯测试仍有VSM Non-Nanite队列溢出警告，性能未验收。保护2,378项中2,373不变，仅5个自有V2包变化；本轮归档9张原PNG／源数据，不提交角色或Saved。

本轮没有完成独立Morph／下颌、动态LOD、非均匀缩放或跨引擎版本验证。完整十项继续，后续仍优先独立球面Hair高光法线、Skin LUT、双波瓣、眼睛、深度Rim和后处理。不要把本次鼻斑修复或原生接入等同于完整终末地画质验收。

## 历史：v9 暗部体积与二维高光（2026-10-15）

用户明确指出上一版仍然偏平，不能用原生接入或分发通过代替画质。v9恢复Cloth/Hair的双暗层、RD(NoF)、背光项及Cloth/Skin日间主光高光方向偏置，Skin补视角染色；真实Cloth04/Hair09 RS改为32×8二维重采样，保留RGB/A分工。AO仍为1兜底，Skin LUT和完整头发方程尚未恢复。

每盏灯重复暗底会洗平体积：现在从当前View的ForwardLightStruct识别主平行光，主光保留完整NPR，附加灯用Lambert受光面、原生阴影和分类高光。Generic改连续ramp，解决未分类翅膀NoL约.1便封顶。没有改GI入口/曝光或增加RT；同向平行光有识别歧义，其他未捕获入口不能假定正确分灯。传统聚簇接口同步、Rect保留GGX/LTC，但本批未重新GPU实测fmt0聚簇。

后端 `endfield-native-bsdf-v9-volume-rs2d`，Cooked协议9，Skin payload仍v4。**Mode2应用预设前设 `r.Substrate.ToonProfile.Resolution=256`；默认64会打印提示并整份回退原曲线/Mode0。** atlas内存和采样增加，未测性能。v8 Cook不能冒称v9已验证，需重新Cook。

本批Editor build3、Game game-build2、generate9b白名单12包/四父材质编译成功。YYB16th同清单/参考姿势/机位：volume9b的34图/156槽及atlas64两模式各8图/78槽通过，共50图/312槽/0失败。四机位Unlit对照逐像素一致；实际变化、来源、重试和边界见[前后实拍](Screenshots/2026-10-15-endfield-volume/README.md)。保护2,378项中2,375保持，只改三个自有V2包；既有dirty资产、Face、Config和引擎未改。v9没有重跑完整BuildPlugin/Cook。

**下一步优先恢复独立球面Hair高光法线，再柱面压平和与原法线混合。** 当前只把网格法线压平，竖直发片在固定主光/顶视机位的200次波瓣可小于1e-10，法线仰30–55度的区域才明显，不能只靠调大强度解决。MyZmd的ViewDirYOffset属于艺术控制，不是已提取客户端参数，不能冒称默认值。随后继续Skin LUT、P/flow可选增强、双波瓣、Eye/刘海分层、深度Rim和后处理；完整十项目标及画质验收未完成。

## 同日较早历史：公开复刻核对、v8 分发门控与底色描边（2026-10-15）

**完整十项目标没有缩减，当前仍未通过终末地画质验收。** 本节记录本轮实际验证；下方 v6、v4 和旧 SDF 内容继续保留为历史，不能混作本轮新验证。

先核查了四个公开复刻仓库，保存了 90 个源码／配置文件的固定快照，结论见[公开复刻流程审计](ENDFIELD_REPRODUCTION_REFERENCE_AUDIT.md)。MyZmdShaders 覆盖部位较全，但依赖未随仓库提供的定制管线，且没有明确源码许可证；congyu 的 MIT 实现主灯走 Toon、附加灯仍走 PBR；Morgana 有独立后处理，角色响应主要处理主灯；另一 UE 工程主要提供二进制材质。它们都是分项参考，不能当作原作完整金标准。本次公开参考仓库的代码与资源没有直接搬进发行包。

据此继续按五层组织：材质准备数据，BasePass 保存载荷，原生逐灯入口算自有响应，几何分层／单次屏幕效果处理描边与 Rim，最后统一校准后处理。原生阴影／GI 后端方向保持；眼睛、头发完整二维响应、深度 Rim、眉眼／刘海分层和后处理仍要继续补。

- **v8 Cooked 协议门控已实现。** 插件生成的 shader 副本写入协议 8；专用 GlobalShader 的编译构造函数读取真实参数反射，将协议值保存在 `LAYOUT_FIELD`。Cooked 运行先关闭 Profile 路由，在 `OnPostEngineInit` 读取已加载 shader 的序列化协议，匹配后才允许创建自有 Profile 标记。同 v8 普通 Cook 应读到 0，启动参数不能把它冒充后端 Cook。它只证明这份 GlobalShader 的编译来源，不证明全部材质内核或 GPU 执行，仍需同 Cook 的 packed 开／关实拍。更旧、连该 GlobalShader 都没有的 Cook 可能被 UE 提前报缺 shader，必须重新 Cook，不能承诺优雅回退。
- **底色描边已有专用 V2 父材质。** 从当前槽 `base_color_tex * BaseColor` 派生颜色，按 Skin／Hair／Cloth 调亮度、饱和度；继续用原 PMX 槽开关、EdgeSize、顶点 EdgeScale、EdgeColor.A 及已有骨骼 Overlay／WPO。遮罩／透明槽另外读取表面覆盖率，避免透明区域冒出实心边，Opacity 不重复相乘。没有覆盖用户修改中的旧 `M_MMD_Outline`。底色与贴图在应用／刷新时复制，不能宣称材质颜色 Morph 已实时同步。
- **分发链路已补明确边界。** 发行过滤排除本机测试角色和动作，只保留插件所需资源；显式收录 Bullet 头文件／静态库，声明当前仅 Win64。启动校验不再要求读取插件私有 CustomOutput C++ 文件，改用模块编译协议与实际 shader；Editor／Cook 仍需要精确适配的 UE 5.8.3 CL58210709。安装引擎文件保持只读。独立 BuildPlugin 暴露的缺头依赖已修复；`EF8e` 的 Editor Development、Game Development、Shipping 与文件检查均过。移出私有 CustomOutput cpp 的空白宿主在修正 SM6 参数后通过：协议8、四父材质 shader 数30/54/48/40、0编译错误，没有模型绘制。

最终 `build4`（Editor）、`game-build2`（Game）、`generate1` 通过。旧 Librarian 的 `outline2` 为24图/70槽/0失败。新带翼模型 `yyb16-final2` 为24图/78槽、`yyb16-final0` 为8图/78槽，均0失败；Mode0是同V2原生Toon，不是旧Unlit。相同清单、参考姿势、环境、相机记录已逐项核对，`clothes01..07` 七槽已修正为通用 Cloth 识别。实际原图见[本轮归档](Screenshots/2026-10-15-endfield-distribution/README.md)。

后续固定使用 **YYB Hatsune Miku 16th 带翼款**：610,819顶点、997,023三角形、39材质。导入报告在本机 `Saved/Phase2Reference/Endfield2/distribution-2026-10-15/import16th.json`，清单在 `Saved/BackgroundValidation/ImportedAssets/MMDBackgroundValidation_YYB16thWing_D1E806A44E33873C9F0515A953702D95/manifest.json`。探针 `-SampleManifest=<清单>` 严格指定样本，失败不回退旧模型；`-PoseTime=-1` 参考姿势，`-FitSampleCamera` 容纳翅膀，`-BakeSdf` 在该模型上临时生成方向场，不能混用旧 `-LoadSdfFixture`。角色仅存Saved。这次没有验证其动画/Morph，VMD存在196条不匹配警告；body、耳机、wing仍Generic，不冒充Metal覆盖。

本轮 `package-v8b` 与 `package-vanilla-v8` 均完成全新SM6 Cook/Stage/Pak/Archive，程序SHA相同。正Cook实际游戏协议8；普通Cook即使启动请求Mode2仍协议0、禁止Profile标记、0重绑并成功回退原生。正Cook的正常响应与packed开/关实拍通过：12/12槽、2/2Skin，GI系数归零时明绿/明红；[Cooked来源及边界](Screenshots/2026-10-15-endfield-distribution/COOKED_VALIDATION.md)。协议字段与CPU报告不冒充全部GPU内核验证。两次带RenderDoc自动附加的首帧失败留档；仅CLI禁用捕获插件后四次成功，用户Config保持，不能称一般稳定性已获保证。

白布/翅膀层次偏平、完整头发二维响应、皮肤SSS、眼睛/刘海分层、深度Rim与后处理仍待继续。保护基线2378项中2376保持，只有两个自有V2包变化，另加自有Outline；用户旧资产及Face shader保持。

维护与分发细节见[原生后端](ENDFIELD_NATIVE_BACKEND.md)和[插件分发验证](ENDFIELD_PLUGIN_DISTRIBUTION.md)。继续以实际多灯、阴影、头部转动、Morph、透明发片和无配套贴图对照推进完整十项，不以编译通过或图片数量替代画质验收。

最终完整脚本`EF8f`已全程通过，报告与代码入口见[分发证据](Screenshots/2026-10-15-endfield-distribution/distribution-sources.json)；发行物位于本机`Saved/EF8f/Ue5MMDTools/`。测试夹具已移至本批Saved的`fixture-v8-archive`，工程Content未保留测试包；新角色Saved清单保留供下一轮固定复拍。

## 当前迭代：正式脸部 payload 与客户端 ramp（2026-10-14）

沿用用户确认的插件原生后端。新增 v6／图版本4：专用材质 CustomOutput 把头空间漫反射方向和旋转交给 BasePass，packed export/read 后在每盏灯 BSDF 中消费；独立原生 Normal 供高光／GI。仅单 Toon Skin／Mode2／adaptive fmt1 opaque/masked，有24 bytes预算及简化回退保护。

自动 V4 方向场不再丢失固定俯角以外的信息，保留 UV0并逐tap解镜像。它是几何方向场，不是实际遮挡距离场；保留 V3 旧阈值路径。当前跟随头骨刚性旋转，Morph／下颌局部变形修正未完成。Skin／Hair／Cloth真实256点 RD 以 alpha混明暗、RGB tint及亮度补偿处理；移除三档先量化。RS／F0／高光波瓣已继续对齐，Hair/Cloth二维RS目前固定切片，头发方向仍用几何近似。

Editor build4、Game game-build2 和11包 generate2 已通过。新版 fidelity2/3 共71图／350槽检查0失败，覆盖多仰角、组件旋转、多灯、原生遮挡、packed有效性与高光/GI对照。方向贴图GPU覆盖已实际可见，packed开启／关闭分别读回绿／红；禁用漫反射后的脸区均差.113/255，没有此前共享法线明显漂移。Mode0和后端关闭对照均通过。代表[原图与来源](Screenshots/2026-10-14-endfield-fidelity/README.md)，没有把绿色诊断或图片数量当画质验收。

完整十项范围持续有效，眼睛、深度rim、描边、后处理及完整头发响应仍未完成。本轮尚未重新Cook／验证发行包；以下v4打包及V3自动SDF段均为历史记录。下一步已查明Eye真实Specular/Smoothness=0且Matcap分层，描边需新增自有V2底色采样材质并复用现overlay/WPO链路，不能覆盖用户dirty Outline资产；rim应按真实深度差/分辨率公式接入一次合成，不能每盏灯重复叠加。

## 原生着色后端实施（2026-10-14，已完成本批验证）

用户已授权实施“插件内写类似自定义 ShadingModel、取得原生渲染结果”的方案。新增固定 UE 5.8.3 / CL58210709 的 shader 目录适配：仅修改工程 Intermediate 中的副本，通过更深虚拟路径接入；安装引擎源文件不改。自有 `.ush` 接逐灯光 Context、ShadowTerms、AreaLight 和 50 项原生灯光／阴影输入，补传统聚簇灯光以及 GI／反射最终贡献入口。

使用带角色签名的瞬态 ToonProfile 隔离新响应；模式和 GI 倍率由 Profile alpha 传递，RGB 不变，普通材质保持原生。模式 0 保真，1 阴影诊断，2 三档原型；**模式 2 不是最终终末地复刻**。Editor build8、Game game-build3 构建通过。r3–r8 的模式／阴影／GI 对照各 11 图／70 槽检查，另 r11 的传统 GBuffer=0／聚簇开启也是 11 图／70 槽，全通过，共 77 图／490 槽。真实材质 shader 为 21／40／29，无默认或代理 fallback。

UAT package3 完整 Cook／Stage／Pak／Archive exit0，package4 用最新 Game 二进制重新 Stage。相同 Cook 的 final0/1/2/6/7 实际打包游戏均 exit0：12／12 专属槽重绑和 CPU 控制读回，第120帧真实视口 PNG、第160帧报告。阴影诊断与两路间接光归零有明确像素变化，发行包无需源目录映射；不能仅用启动参数或 CPU metadata 证明内核。最终 v4 签名 `E3E3466769C5FCEEDE7D0860A85BF3829288DA93`。r9/r10（含关闭后端）暴露原生传统聚簇 VS／PS 接口不匹配，v4 在工程缓存中修补，r11 重新实测通过。

失败及中断日志保留 Saved：pass0 移动宏条件、r1 实验白名单已修，r2 中断；前两次 Cook／Stage 的资源过滤及 Zen 记录读取失败已换命令行配置重新跑通。首次打包 game0 第1帧 `0xc0000374` 原因未定位，同命令 game0r 和 final0 成功；不要写成已修复。安装引擎 shader、原有 Content／Config 与用户 Face 共2,378项 SHA256 保持，13个测试包已移回 Saved 备份，未提交／推送。代表开发实拍见[本日归档](Screenshots/2026-10-14-endfield-native-backend/README.md)，不能当最终视觉验收。

仍需传递正式的逐像素头部／SDF／独立高光法线 payload；原生阴影标量无法区分 caster 身份。SDF Version3 的侧向错误、专用 UV 和合理自阴影方案仍待继续，不能把下方旧检查通过当成画质验收。完整后端机制、启用方式及数据边界见 [原生着色后端](ENDFIELD_NATIVE_BACKEND.md)。

下一轮先接正式逐像素 payload，再恢复真实客户端 ramp 与各角色方程。现有后端已保留原生灯光、VSM、GI／反射结果，但原生剔除／早退、材质存储格式仍有边界；非零独立 SSR 和 BRDF 加权前天空／capture 辐射未接。本批 Cook 只验证通用几何夹具，不等于已经验证 SDF、骨骼/Morph、透明发片或最终终末地画面。

## 最新实施状态：ush、自动 SDF 与接触阴影（2026-10-13）

**本批已打通自动 SDF 的实际消费，鼻灰点用关闭角色接触阴影修复；完整终末地目标仍未完成。** `Core/TMMDEndfieldNative.ush` 通过一个 struct Custom 输出 normal、PatternUV、face mask 与可选 shadow coverage，原生 Toon 保留真实光照和普通投影。默认 `FaceShadowSuppress=0`、`bCharacterContactShadow=false`；不要为修鼻点关闭整个角色 CastShadow。接触开关随材质恢复，旧预设默认保留角色原开关。

- 预设仍为 `/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`，现在 11 个包（新增共享 identity offset gradient）、图版本 3。Skin 默认 SphereBlend=1，Boundary=.55，offset 限制 [-.9,.34]，原生漫反射和高光共用法线这一限制保留。没有修改引擎、用户 Face 或项目默认 Substrate 开关。
- SDF Version3：三角形栅格化到 UV，R/G=独立左右角阈值/π；B 标记经过几何与阈值检查的镜像共 UV，运行时按当前几何侧解码而不翻 UV；A=有效脸部。复杂冲突回退，RGB padding 不扩大 mask。75% 球面与25%几何，固定俯角 .35，仅描述正前方连续亮区，真实遮挡交给 UE。
- 导入的同步/异步路径自动烘焙、保存 `UMMDEndfieldFaceData` 并按槽绑定。已有网格用 `MMDEndfieldBindSdf` 默认只读检查，加 `-Generate` 才保存显式网格和派生纹理；旧单张 `MMDEndfieldBakeSdf` 继续可用。自动路径保护同名非工具纹理，重烘无覆盖会清除旧缓存。runtime 类使用强引用，实际 Cook/打包尚未验证。
- 最终构建 `build13`、11包 `generate4` 通过，静态 shader/C++ 检查通过。p17：29 图、140 次槽检查、0失败；5槽共29,472有效 texel，主脸15,777有效、15,510镜像、56复杂冲突。真实 GPU mask 与 SDF 开关对照均已自行看图。p18：Substrate=0 独立回读 Version3，12图/105槽/0失败，包含绑定、分类切换、恢复与接触开关恢复。
- 最终 `verify3` 原生资产只读校验通过、没有写包；p19 原生独立回读 Version3，14图/140槽/0失败，接触开关恢复通过。下一轮从这些最终结果继续。
- p15 同 SDF/姿态的接触开关因果对照确认鼻尖灰点来自接触阴影；仅关闭接触投影已改善，所以脸面 ShadowPassSwitch 仅作备用，默认不移除脸面对场景的投影。p13/p14 Version2 遮罩窄是镜像 UV 冲突，p16 保存已有夹具包的部分加载错误均保留失败日志，p17 已修复并重跑。
- [本批真实截图与来源](Screenshots/2026-10-13-endfield-sdf-contact/README.md)。Saved 下 `gpt-v2/` 保存详细报告、UV诊断与数学检查；测试模型/纹理不纳入插件 Content。保护范围598项只有两个 Endfield shader 为本批有意改动，其余596项哈希保持。未提交/推送。

下一项从真实客户端 ramp 继续：`Saved/Phase2Reference/Endfield2/toon-probe/gpt-v2/native_role_ramp_plan.json` 已核对9个代表材质、13张ramp及 asm证据。RD 的 A 是明暗权重、RGB 是色度 tint，不能直接复制 RGB 到 native Profile；RS 需包含波瓣幅度，二维纵轴与粗糙度相关。Skin body/face01像素相同；Hair 还需水平/仰角修正与内部暗部；Cloth04最常见；Metal并非独立客户端角色；眼球样例用Body RD+Matcap且Specular/Smoothness为0。完整十项清单仍有效，不把本批基础链路当作终点。

## 用户既定复刻方案重新核对（2026-10-13）

用户指出上一轮只交付通用 Toon 基础接入，偏离了已与 Claude 对齐的终末地复刻目标。已读取本机 Claude Code 原会话 `46e9bd89-c2be-4366-ac69-673d031aaace.jsonl`：2026-10-13 11:55 SGT 的完整清单明确包括以下全部内容；用户本聊天再次明确确认采用 SDF 方案。

1. 引擎光照接入：角色实际接收场景阴影、自阴影、GI、AO、反射，并与环境融合。
2. 明暗结构：从本机真实客户端的材质覆盖值与 ramp 像素对齐多段明暗、阴影色相和高光响应。
3. 阴影：自阴影与场景遮挡并入 ramp，完成多灯明暗合成；实验开关失败是待修问题，不是删除目标的理由。
4. 脸部：**球面法线 + 自动烘焙 SDF + 偏移采样**，随头部和主光方向更新，解决灰块、鼻影与眼窝。自动生成的 SDF 是用户已接受的派生数据；不能再以旧通用性约定为由禁用。仍须支持仅 base_color 的可用回退，不要求用户手工制作配套贴图。
5. 头发：各向异性高光环、内部暗体积与刘海投影；原生节点不消费 Tangent 时，寻找插件内的补充实现，不以普通 Anisotropy 参数代替最终效果。
6. 材质分路：皮肤、头发、布料、金属、眼球各自的 ramp、高光与粗糙度；眼部、眉毛、睫毛与头发阴影层按真实资料完善。
7. 边缘光与描边：深度边缘光、描边随底色变化、宽度及规则对齐真实数据。
8. 后处理与融合：角色打光、曝光、tonemap、bloom、grading 对齐本机客户端，不能继续只复制旧舞台参数。
9. 通用性：只有 base_color 也完整，自动派生数据和可选 ramp/细节贴图有兜底；这不等于取消已经批准的自动 SDF。
10. 回退与分发：非 Substrate 回退，半透明/遮罩发片、骨骼与 Morph 支持，实际应用到插件角色和预设。

**基础接入只是实施状态，不是最终方案或验收终点。** 下方记录上一轮 SDF 尚未接通时的历史缺口；本批完成状态以上方最新节为准。真实 ramp、深度 rim、眼部分层等继续按清单完善。验收依据实际终末地画面，不以编译、材质数量或 PNG 数量代替。所有项处理并自行看图之后才向用户展示验收截图；本批归档只用于开发验证。

## GPT 接手后的最新状态（2026-10-13）

**灰模已修复，独立 V2 原生 Toon 预设已生成并通过回读与运行时验证；终末地画质仍未验收通过。** 当前资产为 `/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2`，包含三种原生父材质、六个共享 Profile，并保留旧 Endfield Unlit 父材质作为关闭 Substrate 时的回退。

- 灰模根因是 UE5.8 `PostEditChange()` 使用 `EMaterialShaderPrecompileMode::None`，只建立材质表达式数据，没有主动编译绘制 shader。新增 `ForceRecompileForRendering(Default)` 后底色恢复。`IsValidForRendering()` 在本机引擎只检查冻结数据非空，不能代替 `CompiledSuccessfully` / `IsCompilationFinalized` / `GetShaderNum()>0`。
- 探针 `FParse::Value` 默认在逗号处停止，旧 `-Exp=a,b,c` 实际只执行首项；现在显式关闭该行为并拒绝未知实验。新增已保存预设入口 `-NativePreset=<路径>`，支持 `ab,runtime,shadow,lighting`。
- 三种原生材质真实编译通过，21 / 40 / 29 个绘制 shader。半透明使用 Coverage Weight（底色 alpha × Opacity）与 `TLM_SurfacePerPixelLighting`；GreyTransmittance 和 Legacy 是同一个枚举值，不再当两种能力测试。
- `UMMDStageLibrary` 在 Toon 可用且对应新父材质存在时选择原生路径。新 MID 先设置 Profile、刷新代理，再复制源输入；部位分类改变时仅重建仍由预设持有的当前槽，恢复快照保持不变。原有预设不配置新字段时保持旧行为。
- `p12`（Substrate=1、UnifiedDiffuse=0）：53 图 / 0 失败；105 次槽位检查无默认材质或渲染代理回退，角色切换、贴图保留、其他槽保留和恢复均通过。`p11`（Substrate=0）：9 图 / 0 失败，自动选旧父材质。最终 `verify2` 磁盘回读通过、没有写包。
- `p10` 只把多灯统一漫反射开关改为 1 后明显过暗；目前保留默认 0。阴影仍能进入每灯 ramp，但多灯统一方案没有视觉通过，不能以它作为生成前提。
- 上一轮 V2 Profile 禁用 offset/hatching 贴图，**未接入已批准的自动 SDF，这是实施缺口**。几何球面法线仅在有效头部半径附近生效，默认关闭。引擎 Toon 节点未消费自定义 Tangent，移除了无效控制；各向异性目前只能用引擎默认基，尚不能满足既定高光环目标。
- 原有 598 个 Content / shader / Config 文件与接手时的哈希相同；未还原工作区旧改动，未改用户 Face、引擎文件或 `DefaultEngine.ini`，未提交/推送。用户在本聊天已明确允许 GPT 使用多个子代理，旧的并行限制不再适用。

实际截图与来源见 [原生 V2 归档](Screenshots/2026-10-13-endfield-native-v2/README.md)。完整日志在 `Saved/Phase2Reference/Endfield2/toon-probe/gpt-v2/`。`p9` 的 35 项失败是新增代理检查没有初始化输出指针造成的误报；原失败报告保留，`p10/p12` 已修正并重新实测。第一次 `verify` 的六个 Profile 失败来自反射比较 transient 曲线句柄；现精确比较全部序列化曲线字段，`verify2` 通过，没有放宽 ramp 数值。

### 下一步从这里继续

1. 固定 `p12` 的资产、镜头与光照作为原生基线，处理鼻部灰点、脸部阴影、发片高光与布料层次。不要重新排查已经解决的灰模或继续使用 `p5` 的成功判定。
2. 按上方既定方案接通自动 SDF 与球面法线，处理鼻部灰点；头发天使环须补插件内可实现的方案。自动派生 SDF 已获用户许可，不再排除；仍不修改引擎、不要求用户制作专属配套贴图。
3. 天空、自阴影与外部遮挡响应明确；关闭 GI 的像素变化接近时序噪声，不能宣称 Lumen GI 已完整验证。继续用实际主视图和受控间接光场景检查，之后再做 Cook/打包。
4. 后处理、深度边缘光、原作描边及眼部专用分层仍未对齐。现有真实材质值是参考依据，UE Profile/粗糙度等候选值不是原作等价方程。
5. 项目默认未开启 Substrate；本批全部开关仅作用于后台进程。资产已支持自动回退，不要把回退画面当新的画质成果。

## 0. Claude 中断时现状（历史记录，以下成功判定已更正）

用户否决了现有 Unlit 版（"好丑，纯 unlit 感"）。改用 UE5.8 原生 **Substrate Toon BSDF + UToonProfile** 的路线，探针已**跑通并出图**，引擎灯光确实作用到了角色；但**材质没有显示 base color 贴图（灰模）**，原因未查清。没有任何成果可给用户看。

## 1. Claude 当时记录的事实（历史，须结合上方更正）

探针：`-run=MMDEndfieldToonProbe`，源码 `Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldToonProbeCommandlet.cpp`（未提交，约 1630 行，全部 Transient，不写盘）。输出目录 `MMD/Saved/Phase2Reference/Endfield2/toon-probe/p5/`（8 张图 + `p5_report.json`）。

| 事实 | 证据 |
|---|---|
| Substrate 命令行即可开启，不必改 ini | `-ini:Engine:[/Script/Engine.RendererSettings]:r.Substrate=True` → 报告 `substrate_enabled: true`，`ProjectGBufferFormat=1`（非 blendable），`ClosuresPerPixel=4`。仓库 `DefaultEngine.ini` 未改 |
| 当时把 Toon BSDF 判作已编译 | p4/p5 的 `ready=1` 是宽松判定，实际 success/finalized=0；不能证明原生 Toon 已渲染，现以 p12 为准 |
| 引擎光照作用到角色：有投影、有体积、接收阴影 | `p5_toon_full.png`：角色在地面有投影，身体有明暗起伏。对照 `p5_unlit_full.png` 是平的 |
| MID 能被应用到全部槽位 | 日志 `slot N parent=ToonProbe_Opaque base_tex=…(1)`，但见 §2 |
| 半透明槽位仍走旧 Unlit 父材质 `M_MMD_Endfield_Translucent` | 日志 slot 4/7/8 的 parent。Toon BSDF 的半透明用 `SubstrateWeight`（Coverage Weight）接，探针里实现了但 `toon_translucent_ready` 为 false，**未验证** |
| 当时放宽了 "ready" 判定 | 后续确认这是误判：冻结数据非空不代表绘制 shader 已编译。已恢复严格检查并主动编译 |

## 2. 当时未解决的主问题：为什么是灰模（已解决，保留排查历史）

p5 日志显示每个槽位 `base_tex=body_png(1)/head_png(1)/skin_png(1)…`，即 **MID 的 `base_color_tex` 参数确实被设了真实贴图**，`BaseColor=(1,1,1,1)`。但出图却是均匀灰，连眼睛、嘴唇颜色都没有（脸上只有红蓝眼线是别的东西）。

已排除：参数没传（上面日志证明传了）。

待查（按可能性排序，我没来得及验证）：
1. **Toon BSDF 的 `BaseColor` 引脚实际没吃到贴图**：材质图里 `Tinted = base_color_tex(RGB) × BaseColor` → `Toon->BaseColor`（见探针 `ToonProbeBuildMaterial`，约 290–360 行）。检查采样节点输出索引（RGB 是输出 0，alpha 是 4；若连错通道会得到灰）。
2. **漫反射 ramp 乘出来的灰**：`UToonProfile::DiffuseRamp` 的颜色（探针里 Lit=0.92 灰、暗部偏蓝灰）与 BaseColor 相乘。如果 ramp 曲线 key 设置有误（`ToonProbeSetRamp`，约 140–165 行），或 `FRuntimeCurveLinearColor` 的 alpha/通道取值有问题，整体会被压成灰。
3. **贴图 sRGB/压缩问题**：`body_png` 等是否在 commandlet 离屏下已完成流式加载（日志有 `Waiting for textures to be ready`，p4 的 `Blocking on texture streaming`）。若 mip 未就绪可能出占位灰。可对比 unlit 版同一贴图是否正常（unlit 版颜色正常，说明贴图本身可用，所以这条概率低）。
4. **`SetShadingModel(MSM_DefaultLit)` + Substrate** 下材质 `FrontMaterial` 的混合：确认没有被引擎默认材质覆盖。
5. 脸部两个红蓝眼线颜色是正确的贴图色，说明**部分槽位的贴图是通的**（眼睛槽 `Eyetex_png`、眉毛槽）；灰的是 body/skin/head 槽 —— 它们的贴图都带 alpha / 或是同一张 body_png。重点查这几个槽，可能是 `Opaque` 路径下 alpha 通道被当作什么处理，或它们实际走的是另一个材质。

建议第一步：在探针里临时把 `Toon->BaseColor` 直接接 `BaseTexture`（去掉 Tinted），再接一个纯红常量，二分定位是贴图问题还是 ramp 问题。每次只改一个变量。

## 2.5 探针其余实验尚未跑

`-Exp=` 支持：`ab, shadow, ramp, lighting, face, hair, override, translucent, all`。目前只跑了 `ab`。其余（多灯统一漫反射、ramp 是否含阴影、球面法线脸、各向异性发丝、**MID 覆盖 ToonProfile 是否生效**、半透明）都没验证。

注意：冷 DDC 首次编 Substrate shader 约 150 秒；热 DDC 后一次运行约 20 秒。DDC 目录 `toon-probe/ddc-sub`。

## 3. 已知引擎约束（已读源码确认）

- `r.Substrate`、`r.Substrate.Experimental.ToonUnifiedDiffuse`、`r.Substrate.Experimental.ToonReflectionQuantizationEnabled` 均 **ReadOnly**，只能启动时设置。UnifiedDiffuse 默认 0（多灯时每盏灯各过一遍 ramp，暗部可能出台阶），需要在 `-ini:` 里一起设并 A/B。
- `UMaterialInstance` 有 `ToonProfileOverride` / `bOverrideToonProfile`（`MaterialInstance.h:671,693`），"按角色分路换 ramp"理论可行，**运行时生效未验证**。
- `FToonProfileStruct` 构造函数未导出（缺 ENGINE_API），插件里要用 `GetDefault<UToonProfile>()->Settings` 拷贝默认值。
- 插件运行时只按混合模式选父材质：`Source/Ue5MMDTools/Private/Stage/MMDStageLibrary.cpp:299 ApplyCharacterRenderPreset`，角色靠 `MaterialRole` 标量区分。
- 用户工程必须开 `r.Substrate=True`（重启 + 全量重编）。这是分发层面的重大取舍，要用户拍板；Unlit 版保留作非 Substrate 回退。

## 4. 逆向数据（对齐真实终末地）

客户端：`E:\Games\Installers\Hypergryph Launcher\games\Arknights Endfield`（Unity 2021.3.34f5 / HGRP）。已取得（在 `MMD/Saved/Phase2Reference/Endfield2/`，**未复核**）：

- `material-values/material_values.json`（7 MB，含 role_summary / outline_summary / shadow_color_eval）
- `material-values/ramps/*.json`（67 张真实 ramp 像素）
- `mv_work/shadow_eval.json`（300 条"亮部→阴影色"的色相/明度评估）

**没取到**：字节码光照方程、后处理（tonemap/bloom/grading）、角色打光 rig、描边规则。上一轮 `StaticProbe/` 的 `.asm.txt` 是字节码真值，`*.reconstructed.hlsl` 不可信。工具与路径坑见记忆/交接文档。

## 5. 用户的验收标准（重要）

- 不要再给"没有灰脸 = 通过"这类自说自话的结论。用户一眼看出 Unlit 版是平的。
- **所有考虑项处理完、自己看过图之后**才给用户截图。清单见交接文档 §4。
- 用户的订阅额度吃紧（一次会话烧掉约 50%）：**不要开多个子代理并行**，不要重复读大文件，先读已有产出再决定补什么。
- 不弹前台窗口：UE 一律 `UnrealEditor-Cmd.exe -RenderOffscreen`。

## 6. 工作区（未提交）

以下为 Claude 中断时的记录，探针的宽松就绪判定现已更正。见 `ENDFIELD_V2_HANDOFF.md` §6；27 个被旧生成器重写的 `Content/` 资产本批保持接手时现状，后续必须逐文件核对和保存现有改动，不能直接笼统还原。
