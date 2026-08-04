# Ue5MMDTools 项目说明

MMD 相关 UE5 工具插件项目。核心代码在 `MMD/Plugins/Ue5MMDTools/`，shader 在 `MMD/Plugins/Ue5MMDTools/Shaders/TMMDShader/`。

---

## ⚠️ Shader 编写铁律（UE 材质 Custom 节点）

材质 **Custom 节点的代码会被粘贴进一个生成的函数体内**（`CustomExpressionN(...) { ... }`），因此：

1. **禁止定义独立的函数** → 会报 `function definition is not allowed here`。
2. **禁止用 `#define` 宏** 包装逻辑（本项目已弃用）。
3. **禁止修改引擎文件**（`MaterialTemplate.ush` 等）——插件要分发，用户引擎不是魔改的。
4. **禁止内联语句块代替函数**——那会丢失函数级代码复用。

### ✅ 正确写法：struct + 成员函数（MStar 风格）

struct 声明（含成员函数）是"类型声明"，在函数体内合法——这是 Custom 节点里做函数复用的标准做法。

`.usf` 文件模板：

```hlsl
// xxx.usf
struct TMyShader
{
    // 成员函数 = 真正的 HLSL 函数，可复用
    float3 OutputColor(FMaterialPixelParameters Parameters, ...)
    {
        ...
        return result;
    }
};

// 自包含入口：文件末尾自行实例化 + return
TMyShader S;
return S.OutputColor(Parameters, ...);
```

材质 Custom 节点：

```
OutputType: float3
Code:
    #include "/Plugin/Ue5MMDTools/TMMDShader/xxx.usf"
    return 0;   // 兜底返回值（正常不可达），防某些编译变体报"无返回值"
```

### 节点输入约定

- **需连线**的输入（以各文件头注释为准）：`BaseColor`、`LightDataTex`、`ShadowStep`、`HighlightStep`、`ShadowColor` 等。
- **自动可用、无需连线**：
  - `Parameters`（`FMaterialPixelParameters`）
  - `LightDataTexSampler`（Texture2D 输入 `LightDataTex` 自动生成的采样器）
- include 虚拟路径：`/Plugin/Ue5MMDTools/TMMDShader/`。

### 已按此规范写的文件（新 shader 照抄）

- `MMD/Plugins/Ue5MMDTools/Shaders/TMMDShader/MMDAnimeToonLighting.usf` — 共享 toon 光照（struct `TMMDAnimeToonLighting`）
- `TMMDAnimeSkin.usf` — 皮肤（struct `TMMDAnimeSkin`）
- `TMMDAnimeCloth.usf` — 布料（`#include "MMDAnimeToonLighting.usf"` 复用）
- `TMMDAnimeHair.usf` — TODO 占位

---

## 其他约定

- 局部变量加 `_mmd` 前缀防撞名（内联/展开场景下）。
- 注释用**中文**，只保留必要注释。
- 引擎 `MaterialTemplate.ush` 每个进程只读一次缓存，改了必须重启编辑器才生效——**不要动它**。
- 若需在 struct 基础上叠加（如 rim/SSS），删掉文件末尾的实例化+return，改为在节点里手动调用 `S.OutputColor(...)` 后继续拼。
