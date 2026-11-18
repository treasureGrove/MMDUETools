# Ue5MMDTools Starter（UE 5.8.3）

最小可用工程：只有一个空的 `Config/` 和一个 `.uproject`，不含任何第三方模型或贴图。

## 用法

1. 下载最新 Release 里的 `Ue5MMDTools-v<版本>-UE5.8.3-Win64.zip`。
2. 解压出 `Ue5MMDTools/` 目录，放到本工程的 `Plugins/` 下：

   ```text
   MMDStarter/
     MMDStarter.uproject
     Plugins/
       Ue5MMDTools/
         Ue5MMDTools.uplugin
   ```

3. 双击 `MMDStarter.uproject` 打开。首次启动会提示缺少模块或需要编译，选择“是”由引擎处理；预编译包已包含 Win64 二进制，通常直接可用。
4. 打开 **窗口 → Ue5MMDTools**，进入角色工坊。
5. 从 [MODEL_SOURCES.md](https://github.com/treasureGrove/MMDUETools/blob/main/MODEL_SOURCES.md) 选一个模型，按原作者页面下载后，用 **导入 PMX 模型** 导入。

## 说明

- 本工程不启用原生 Toon 预设所需的额外配置。需要终末地／暖暖原生风格时，按 `Docs/ENDFIELD_NATIVE_RENDERING_GUIDE.md` 合并对应 ini 设置。
- 出片需要系统可用的 `ffmpeg` 与 `ffprobe`。
- 工程保持空白，导入角色后会生成 `/Game/MMDModels/<模型名>/` 下的资产。
