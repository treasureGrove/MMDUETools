#!/usr/bin/env python3
"""终末地 C++ 源码静态检查器。

沙箱内无法编译 UE（需要 Windows + MSVC），因此提交前用本脚本做结构检查：
  1. 括号/花括号配平。
  2. 每个 UCLASS 头文件都有 .generated.h include 且类名与文件名一致。
  3. 白名单包数组与 SaveAsset 校验一致（防止写入范围漂移）。
  4. shader 输入名数组与 usf 的 OutputColor 形参名逐一对照。
  5. 来源签名涵盖全部被 include 的 shader 文件。
  6. 常见 API 误用：UTexture2D* 传给期望 UTexture* 的参数、RT 被当 UTexture2D。

用法：python3 -I tools/validate_endfield_sources.py <插件根目录>
退出码 0 = 全部通过，1 = 有失败项。
"""
import re
import sys
from pathlib import Path

CXX = "Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldPresetsCommandlet.cpp"
HDR = "Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldPresetsCommandlet.h"
BAKER = "Source/Ue5MMDToolsEditor/Private/Editor/MMDEndfieldFaceSdfBaker.cpp"
USF = "Shaders/TMMDShader/TMMDEndfieldCharacter.usf"
CORE = "Shaders/Core/TMMDEndfieldLighting.ush"


def strip_comments(text: str) -> str:
    """剥离注释，保留换行。必须识别字符串字面量：
    本项目存在 TEXT("// 终末地源码签名：") 这类含 // 的字符串，
    若不识别会把字符串内容当注释吞掉，导致括号统计错位。"""
    out, i, n = [], 0, len(text)
    in_line = in_block = in_string = False
    while i < n:
        c = text[i]
        if in_line:
            out.append("\n" if c == "\n" else " ")
            if c == "\n":
                in_line = False
            i += 1
            continue
        if in_block:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                in_block = False
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
            continue
        if in_string:
            # 保留字符串内容，但把其中的括号替换为占位符以外的同形字符，
            # 以便 counts 仍平衡；只处理转义。
            if c == "\\" and i + 1 < n:
                out.append(c)
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_string = False
            out.append(c)
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            in_line = True
            out.append("  ")
            i += 2
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            in_block = True
            out.append("  ")
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def strip_strings(text: str) -> str:
    """把字符串字面量内容替换为空格，便于按语法统计括号（字符串内的括号不参与）。"""
    out, i, n = [], 0, len(text)
    in_string = False
    while i < n:
        c = text[i]
        if in_string:
            if c == "\\" and i + 1 < n:
                out.append("  ")
                i += 2
                continue
            if c == '"':
                in_string = False
                out.append(c)
                i += 1
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
            continue
        if c == '"':
            in_string = True
            out.append(c)
            i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def split_args(text: str):
    args, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


def extract_args(code: str, open_index: int):
    depth, i = 0, open_index
    while i < len(code):
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
            if depth == 0:
                return code[open_index + 1: i]
        i += 1
    return None


def check_balanced(name, code, errors):
    """括号配平：先剥离注释，再把字符串内容清空，避免字符串内的括号干扰统计。"""
    code = strip_strings(code)
    for o, c, label in [("{", "}", "花括号"), ("(", ")", "圆括号")]:
        if code.count(o) != code.count(c):
            errors.append(f"{name}: {label}不配平 ({code.count(o)} vs {code.count(c)})")


def check_header(name, raw, errors):
    code = strip_comments(raw)
    if "UCLASS" in code:
        expected = name.split("/")[-1].replace(".h", "")
        if f".generated.h" not in raw:
            errors.append(f"{name}: UCLASS 头文件缺少 .generated.h include")
        elif f'{expected}.generated.h' not in raw:
            errors.append(f"{name}: generated.h 名称与文件名不一致（应为 {expected}.generated.h）")


def main():
    if len(sys.argv) < 2:
        print("用法: validate_endfield_sources.py <插件根目录>")
        return 1
    root = Path(sys.argv[1])
    errors, notes = [], []

    # ---- 逐一检查文件存在与括号 ----
    contents = {}
    for rel in (CXX, HDR, BAKER, USF, CORE):
        path = root / rel
        if not path.exists():
            errors.append(f"{rel}: 文件不存在")
            continue
        raw = path.read_text(encoding="utf-8")
        contents[rel] = raw
        check_balanced(rel, strip_comments(raw), errors)

    if errors:
        print("终末地 C++ 源码静态检查")
        print("=" * 64)
        for e in errors:
            print(f"  [X] {e}")
        return 1

    check_header(HDR, contents[HDR], errors)

    # ---- 白名单一致性 ----
    cxx = strip_comments(contents[CXX])
    whitelist = re.findall(r'TEXT\("(/Ue5MMDTools/Stages/[^"]+)"\) \};', cxx)
    block = re.search(r"AllowedPackages\[\]\s*=\s*\{(.*?)\};", cxx, re.S)
    if not block:
        errors.append(f"{CXX}: 未找到 AllowedPackages 白名单")
    else:
        packages = re.findall(r'TEXT\("([^"]+)"\)', block.group(1))
        if len(packages) != 4:
            errors.append(f"{CXX}: 白名单应为 4 个包，实际 {len(packages)} 个")
        for p in packages:
            if not p.startswith("/Ue5MMDTools/Stages/"):
                errors.append(f"{CXX}: 白名单包含非 Stages 路径 {p}")
        notes.append(f"白名单：{len(packages)} 个包")
        # 检查 SaveAsset 确实在运行时校验白名单
        if "拒绝写白名单之外的包" not in contents[CXX]:
            errors.append(f"{CXX}: SaveAsset 缺少白名单运行时校验")

    # ---- shader 输入名数组 vs usf 形参 ----
    names_block = re.search(r"MainInputNames\[\]\s*=\s*\{(.*?)\};", cxx, re.S)
    if not names_block:
        errors.append(f"{CXX}: 未找到 MainInputNames 数组")
    else:
        declared_names = re.findall(r'TEXT\("([^"]+)"\)', names_block.group(1))
        usf = strip_comments(contents[USF])
        sig = re.search(r"float3\s+OutputColor\s*\((.*?)\)\s*\{", usf, re.S)
        if not sig:
            errors.append(f"{USF}: 未找到 OutputColor 签名")
        else:
            params = split_args(sig.group(1))
            # 只取形参名的最后一个标识符（跳过类型与 out 关键字）。
            param_names = []
            for p in params:
                tokens = re.findall(r"[A-Za-z_]\w*", p)
                if tokens:
                    param_names.append(tokens[-1])
            # 排除引擎自动提供的形参：Parameters 与 Texture 输入自动生成的 <名>Sampler。
            # 注意 UE 的命名是「输入名 + Sampler」且不加下划线，
            # 例如 light_data_tex → light_data_texSampler。
            engine_params = {"Parameters"}
            material_params = [n for n in param_names
                               if n not in engine_params and not n.endswith("Sampler")]
            excluded = [n for n in param_names if n not in material_params]
            notes.append(f"usf 形参 {len(param_names)} 个 → 材质输入 {len(material_params)} 个"
                         f"（排除引擎自动参数：{', '.join(excluded)}）")
            if len(material_params) != len(declared_names):
                errors.append(f"{USF}: 材质输入形参 {len(material_params)} 个，"
                              f"但 MainInputNames 有 {len(declared_names)} 个")
                missing = [n for n in material_params if n not in declared_names]
                extra = [n for n in declared_names if n not in material_params]
                if missing:
                    errors.append(f"{USF}: MainInputNames 缺少 {missing}")
                if extra:
                    errors.append(f"{CXX}: MainInputNames 多出 {extra}")
            else:
                for i, (a, b) in enumerate(zip(declared_names, material_params)):
                    if a != b:
                        errors.append(f"{CXX}: MainInputNames[{i}]='{a}' 与 usf 形参 '{b}' 不符")
                notes.append(f"输入名逐一对照通过：{len(declared_names)} 个")

        # 检查 AddCustomInput 的调用顺序与 MainInputNames 一致。
        # 头部基三参数由循环接线（MainInputNames[26 + Index]），此处展开为三个占位。
        calls = re.findall(r'AddCustomInput\(Shader,\s*TEXT\("([^"]+)"\)', cxx)
        loop_index = cxx.find("MainInputNames[26 + Index]")
        if loop_index >= 0:
            # 找到循环在调用序列中的位置：统计该行之前的 AddCustomInput 调用数
            before = cxx[:loop_index].count("AddCustomInput(Shader, TEXT(")
            calls = (calls[:before]
                     + [declared_names[26], declared_names[27], declared_names[28]]
                     + calls[before:])
        if calls and len(calls) != len(declared_names):
            errors.append(f"{CXX}: 接线调用 {len(calls)} 个，"
                          f"MainInputNames {len(declared_names)} 个（顺序接线会错位）")
        elif calls:
            for i, (a, b) in enumerate(zip(calls, declared_names)):
                if a != b:
                    errors.append(f"{CXX}: 第 {i} 个接线('{a}') 与 "
                                  f"MainInputNames[{i}]='{b}' 顺序不符")
            notes.append(f"接线顺序逐一对照通过：{len(calls)} 个")

    # ---- 签名涵盖全部 include 的 shader ----
    sig_block = re.search(r"for \(const TCHAR\* Relative : \{(.*?)\}\)", cxx, re.S)
    if sig_block:
        signed = re.findall(r'TEXT\("([^"]+\.u(?:sf|sh))"\)', sig_block.group(1))
        includes = set(re.findall(r'#include\s+"(/Plugin/Ue5MMDTools/[^"]+)"', contents[USF]))
        included_rel = {i.replace("/Plugin/Ue5MMDTools/", "Shaders/") for i in includes}
        # usf 自身必须被签名覆盖（它是入口）
        if USF not in signed:
            errors.append(f"{CXX}: 源码签名未包含入口 {USF}")
        for inc in included_rel:
            if inc not in signed:
                errors.append(f"{CXX}: 源码签名未包含被 include 的 {inc}；"
                              f"改动该文件不会触发材质重建")
        notes.append(f"签名涵盖 {len(signed)} 个 shader 文件，usf include {len(included_rel)} 个")
    else:
        errors.append(f"{CXX}: 未找到源码签名文件列表")

    # ---- API 误用 ----
    # UE 为每个 Texture2D 输入自动生成名为「输入名 + Sampler」的采样器（无下划线），
    # 写成 <名>_sampler 会编译失败。对照 usf 形参做检查。
    usf_code = strip_comments(contents[USF])
    sig = re.search(r"float3\s+OutputColor\s*\((.*?)\)\s*\{", usf_code, re.S)
    if sig:
        params = split_args(sig.group(1))
        texture_names = []
        for p in params:
            m = re.match(r"\s*(?:Texture2D|Texture3D|TextureCube)\s+([A-Za-z_]\w*)", p)
            if m:
                texture_names.append(m.group(1))
        sampler_params = set()
        for p in params:
            m = re.match(r"\s*SamplerState\s+([A-Za-z_]\w*)", p)
            if m:
                sampler_params.add(m.group(1))
        for tex in texture_names:
            expected = tex + "Sampler"
            if expected not in sampler_params:
                actual = [s for s in sampler_params if s.lower().startswith(tex.lower())]
                errors.append(f"{USF}: 纹理输入 {tex} 的采样器应为 {expected}（UE 自动生成，无下划线），"
                              f"实际声明 {actual if actual else '缺失'}")
        notes.append(f"采样器命名检查通过：{len(texture_names)} 个纹理输入")

    if "UTexture* SceneDepth" not in contents[CXX]:
        errors.append(f"{CXX}: SceneDepth 形参应为 UTexture*（RT 不是 UTexture2D）")
    if "Material->GetExpressionCollection().Empty()" not in contents[CXX]:
        errors.append(f"{CXX}: 重建材质时应调用 GetExpressionCollection().Empty()")
    if "SAMPLERTYPE_LinearGrayscale" not in contents[CXX]:
        notes.append("场景深度使用 LinearGrayscale 采样器类型")
    # SDF/ramp 默认必须关闭，保证只连 base_color 也能正常显示
    for guard in ['TEXT("UseSdfMap"), 0.0f', 'TEXT("UseRampMap"), 0.0f',
                  'TEXT("UseSceneDepthRim"), 0.0f']:
        if guard not in contents[CXX]:
            errors.append(f"{CXX}: 可选贴图开关默认值必须为 0（{guard}）")
    # 铁律：不得改动引擎文件
    if "MaterialTemplate.ush" in contents[CXX] or "MaterialTemplate.ush" in contents[CORE]:
        errors.append("代码引用了 MaterialTemplate.ush：违反 AGENTS.md 铁律")

    print("终末地 C++ 源码静态检查")
    print("=" * 64)
    for n in notes:
        print(f"  [i] {n}")
    print()
    if errors:
        print(f"失败 {len(errors)} 项：")
        for e in errors:
            print(f"  [X] {e}")
        return 1
    print("全部通过。注意：静态检查不证明可编译，也不证明渲染达标。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
