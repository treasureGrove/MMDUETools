#!/usr/bin/env python3
"""终末地 shader 静态检查器。

沙箱内没有 HLSL 编译器（UE 只带 Windows 版 DXC），因此提交前用本脚本做结构检查：
  1. AGENTS.md 铁律：Custom 节点内禁止独立函数定义、#define 宏。
  2. 跨 struct 的成员函数调用真实存在（解析实例的声明类型）。
  3. 成员函数的实参个数与形参个数一致。
  4. 花括号/圆括号配平。
  5. #include 目标存在。
  6. usf 末尾自包含 return 入口与 shader.OutputColor 实参个数一致。
  7. Custom 节点不得引用禁用的雾/场景纹理函数（SceneTexturesStruct 在 opaque 下未定义）。

用法：python3 -I tools/validate_endfield_shaders.py <插件根目录>
退出码 0 = 全部通过，1 = 有失败项。
"""
import re
import sys
from pathlib import Path

# 需要检查的入口文件（相对插件根目录）
TARGETS = [
    "Shaders/Core/TMMDEndfieldNative.ush",
    "Shaders/Core/TMMDEndfieldLighting.ush",
    "Shaders/TMMDShader/TMMDEndfieldCharacter.usf",
]
# 依赖库（提供 struct 定义，供跨 struct 调用解析）
DEPENDENCIES = [
    "Shaders/Core/MMDToonLighting.ush",
]

# 已知安全的内置对象：类型 -> 可调用的方法集合（由引擎提供，不检查形参）
ENGINE_OBJECTS = {
    "Texture2D": {"GetDimensions", "Load", "Sample", "SampleLevel", "SampleBias", "Gather"},
    "Texture3D": {"GetDimensions", "Load", "Sample", "SampleLevel"},
    "SamplerState": set(),
    "float4": set(),
    "float3": set(),
    "float2": set(),
    "uint": set(),
}

# 在 Opaque/Masked 下不可用的场景纹理入口（依赖 SceneTexturesStruct 重定向）
FORBIDDEN_CALLS = {
    "CalcSceneDepth": "SceneTexturesStruct 只在 translucent 基 pass 被 #define 重定向，"
                      "opaque/masked 下不可用；场景深度需由材质侧显式传入 Texture 输入",
    "CalcSceneColor": "同上：需显式传入场景纹理",
    "CalcSceneColorAndDepth": "同上：需显式传入场景纹理",
}


def strip_comments(text: str) -> str:
    """剥离注释，保留换行以维持结构。"""
    out = []
    i = 0
    n = len(text)
    in_line = False
    in_block = False
    while i < n:
        c = text[i]
        if in_line:
            if c == "\n":
                in_line = False
                out.append(c)
            else:
                out.append(" ")
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


def find_structs(code: str):
    """返回 {struct_name: body}。"""
    structs = {}
    for m in re.finditer(r"\bstruct\s+(\w+)\s*\{", code):
        name = m.group(1)
        depth = 1
        i = m.end()
        while i < len(code) and depth > 0:
            if code[i] == "{":
                depth += 1
            elif code[i] == "}":
                depth -= 1
            i += 1
        structs[name] = code[m.end(): i - 1]
    return structs


MEMBER_SIG = re.compile(
    r"\b(?:float|float2|float3|float4|half|half2|half3|half4|int|uint|bool|void)\s+"
    r"(\w+)\s*\(([^;{]*?)\)\s*\{",
    re.S,
)


def find_members(body: str):
    """在 struct 体内找出成员函数：名称 -> 形参个数。"""
    members = {}
    for m in MEMBER_SIG.finditer(body):
        params = m.group(2).strip()
        members[m.group(1)] = 0 if not params else len(
            [p for p in split_args(params) if p.strip()])
    return members


def split_args(call_text: str):
    """按顶层逗号切分，忽略括号/尖括号内的逗号。"""
    args = []
    depth = 0
    current = []
    for ch in call_text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    if "".join(current).strip():
        args.append("".join(current).strip())
    return args


def extract_call(code: str, open_paren_index: int):
    """从 '(' 起取出配平的实参文本。"""
    depth = 0
    i = open_paren_index
    while i < len(code):
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
            if depth == 0:
                return code[open_paren_index + 1: i]
        i += 1
    return None


def collect_instance_types(code: str, all_types):
    """找出形如 `TMMDXxx name;` 的局部/成员声明，返回 {变量名: 类型}。"""
    found = {}
    for m in re.finditer(r"\b(\w+)\s+(\w+)\s*;", code):
        type_name, var_name = m.group(1), m.group(2)
        if type_name in all_types:
            found[var_name] = type_name
    # 形参声明：`TMMDXxx name,` 或 `TMMDXxx name)`
    for m in re.finditer(r"\b(\w+)\s+(\w+)\s*[,)]", code):
        type_name, var_name = m.group(1), m.group(2)
        if type_name in all_types:
            found.setdefault(var_name, type_name)
        # 引擎纹理/采样器形参：`Texture2D name,` 也要登记，否则无法解析实例类型
        if type_name in ENGINE_OBJECTS:
            found.setdefault(var_name, type_name)
    return found


def check_file(plugin_root: Path, rel_path: str, errors: list, notes: list, all_types: dict):
    path = plugin_root / rel_path
    if not path.exists():
        errors.append(f"{rel_path}: 文件不存在")
        return
    raw = path.read_text(encoding="utf-8")
    code = strip_comments(raw)

    if re.search(r"^\s*#\s*define\b", code, re.M):
        errors.append(f"{rel_path}: 出现 #define（AGENTS.md 禁止在 Custom 节点用宏包装逻辑）")

    for m in re.finditer(r'#include\s+"([^"]+)"', code):
        inc = m.group(1)
        if inc.startswith("/Plugin/"):
            actual = plugin_root / inc.replace("/Plugin/Ue5MMDTools/", "Shaders/")
            if not actual.exists():
                errors.append(f"{rel_path}: include 目标不存在 {inc}")

    for open_ch, close_ch, label in [("{", "}", "花括号"), ("(", ")", "圆括号")]:
        if code.count(open_ch) != code.count(close_ch):
            errors.append(f"{rel_path}: {label}不配平 "
                          f"({code.count(open_ch)} vs {code.count(close_ch)})")

    # 禁用函数检查
    for name, reason in FORBIDDEN_CALLS.items():
        for m in re.finditer(r"\b" + name + r"\s*\(", code):
            line = code[: m.start()].count("\n") + 1
            errors.append(f"{rel_path}:{line}: 调用了禁用函数 {name}()：{reason}")

    if rel_path.endswith(".usf"):
        # 自包含入口要求：末尾是 `T<Type> shader;` + `return shader.OutputColor(...)`。
        # 实参列表可能很长（64 个），因此搜索全文件而非固定尾部窗口。
        if not re.search(r"return\s+shader\s*\.\s*OutputColor\s*\(", code):
            errors.append(f"{rel_path}: 缺少自包含 return shader.OutputColor(...) 入口")

    # 跨 struct 成员函数检查
    for name, body in find_structs(code).items():
        own_members = all_types.get(name, {})
        instance_types = collect_instance_types(body, all_types)
        # 也解析形参里的 struct 实例
        for m in re.finditer(r"\b(\w+)\s*\.\s*(\w+)\s*\(", body):
            instance, method = m.group(1), m.group(2)
            if instance in ("View", "ResolvedView", "Parameters", "SceneTexturesStruct"):
                continue
            # 先解析实例的声明类型；实例名本身可能是引擎类型（如 `Texture2D x;`）
            resolved = instance_types.get(instance, instance)
            if resolved in ENGINE_OBJECTS:
                if method not in ENGINE_OBJECTS[resolved]:
                    errors.append(f"{rel_path}: 引擎对象 {instance}.{method}() 未知"
                                  f"（类型 {resolved}）")
                continue
            if resolved not in all_types:
                errors.append(f"{rel_path}: 无法解析实例 {instance} 的类型"
                              f"（调用 {instance}.{method}()）")
                continue
            members = all_types.get(resolved, {})
            if method not in members:
                errors.append(f"{rel_path}: {resolved} 不存在成员函数 {method}()")
                continue
            args_text = extract_call(body, m.end() - 1)
            if args_text is None:
                continue
            actual = len(split_args(args_text))
            expected = members[method]
            if expected != actual:
                errors.append(f"{rel_path}: {instance}.{method}() 实参 {actual} 个，"
                              f"形参 {expected} 个（{resolved}）")


def check_entry_matches_signature(plugin_root: Path, errors: list, all_types: dict):
    rel = "Shaders/TMMDShader/TMMDEndfieldCharacter.usf"
    path = plugin_root / rel
    if not path.exists():
        return
    code = strip_comments(path.read_text(encoding="utf-8"))
    members = all_types.get("TMMDEndfieldCharacter", {})
    if "OutputColor" not in members:
        errors.append(f"{rel}: 未找到 TMMDEndfieldCharacter.OutputColor 定义")
        return
    declared = members["OutputColor"]
    call = re.search(r"return\s+shader\s*\.\s*OutputColor\s*\(", code)
    if not call:
        errors.append(f"{rel}: 未找到 shader.OutputColor 调用")
        return
    args_text = extract_call(code, call.end() - 1)
    if args_text is None:
        errors.append(f"{rel}: OutputColor 调用括号不配平")
        return
    args = split_args(args_text)
    if len(args) != declared:
        errors.append(f"{rel}: OutputColor 实参 {len(args)} 个，与形参 {declared} 个不一致")
    else:
        print(f"  [OK] OutputColor 形参/实参数量一致：{declared}")


def main():
    if len(sys.argv) < 2:
        print("用法: validate_endfield_shaders.py <插件根目录>")
        return 1
    plugin_root = Path(sys.argv[1])
    errors, notes = [], []
    all_types = {}

    # 先收集所有依赖库与目标文件的 struct 定义
    for rel in DEPENDENCIES + TARGETS:
        path = plugin_root / rel
        if not path.exists():
            if rel in DEPENDENCIES:
                notes.append(f"依赖 {rel} 不存在，跳过")
            continue
        code = strip_comments(path.read_text(encoding="utf-8"))
        for name, body in find_structs(code).items():
            all_types[name] = find_members(body)

    print("终末地 shader 静态检查")
    print("=" * 64)
    print(f"  解析到 {len(all_types)} 个 struct 定义")
    for target in TARGETS:
        check_file(plugin_root, target, errors, notes, all_types)
    check_entry_matches_signature(plugin_root, errors, all_types)

    print()
    for note in notes:
        print(f"  [i] {note}")
    print()
    if errors:
        print(f"失败 {len(errors)} 项：")
        for err in errors:
            print(f"  [X] {err}")
        return 1
    print("全部通过。注意：静态检查不证明 HLSL 可编译，也不证明渲染达标。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
