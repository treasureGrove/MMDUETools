# ValidateMMDStandardMaterials.py
# MMDStandard 系列材质闭环验证（结构 + HLSL 编译）：
#   1. 三个父材质存在、Unlit + TwoSided + 输出挂 EmissiveColor
#   2. Custom 节点唯一且 include 正确的 usf
#   3. 全部预期参数（texture/vector/scalar）齐备
#   4. recompile 无错误（Translucent 版另查 Opacity 连接）
# 用法：编辑器 Python 控制台或 -ExecutePythonScript 直接执行。

import unreal


OPAQUE_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Opaque"
TRANSLUCENT_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Translucent"
LITE_TOON_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_LiteToon"

EXPECTED_USF = {
    OPAQUE_PATH: "MMDStandard.usf",
    TRANSLUCENT_PATH: "MMDStandard.usf",
    LITE_TOON_PATH: "MMDLiteToon.usf",
}

STANDARD_INPUTS = {
    "base_texture_color", "diffuse_color", "ambient_color", "specular_color",
    "specular_power", "toon_tex", "toon_use_texture", "toon_strength",
    "sphere_tex", "sphere_mode", "light_data_tex", "mmd_shadow_map",
    "shadow_strength", "ambient_strength", "other_lights_strength",
}
LITE_TOON_INPUTS = {
    "base_color", "light_data_tex", "shadow_step", "shadow_softness",
    "shadow_strength", "rim_strength", "mmd_shadow_map",
}

STANDARD_PARAMETERS = {
    "BaseColorMap", "ToonTex", "SphereTex",
    "DiffuseColor", "AmbientColor", "SpecularColor",
    "specular_power", "toon_use_texture", "toon_strength", "sphere_mode",
    "shadow_strength", "ambient_strength", "other_lights_strength",
}
LITE_TOON_PARAMETERS = {
    "BaseColorMap", "shadow_step", "shadow_softness", "shadow_strength", "rim_strength",
}


def validate_material(asset_path):
    material = unreal.load_asset(asset_path)
    if not material:
        raise RuntimeError(f"找不到材质: {asset_path}")

    is_lite = asset_path == LITE_TOON_PATH
    expected_inputs = LITE_TOON_INPUTS if is_lite else STANDARD_INPUTS
    expected_parameters = LITE_TOON_PARAMETERS if is_lite else STANDARD_PARAMETERS
    expected_blend = unreal.BlendMode.BLEND_OPAQUE
    if asset_path == TRANSLUCENT_PATH:
        expected_blend = unreal.BlendMode.BLEND_TRANSLUCENT

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    custom_nodes = [node for node in expressions if isinstance(node, unreal.MaterialExpressionCustom)]
    if len(custom_nodes) != 1:
        raise RuntimeError(f"{asset_path}: Custom 节点数量错误: {len(custom_nodes)}")

    custom = custom_nodes[0]
    if EXPECTED_USF[asset_path] not in str(custom.get_editor_property("code")):
        raise RuntimeError(f"{asset_path}: Custom 未引用 {EXPECTED_USF[asset_path]}")

    input_names = {
        str(item.get_editor_property("input_name"))
        for item in custom.get_editor_property("inputs")
    }
    missing_inputs = expected_inputs - input_names
    if missing_inputs:
        raise RuntimeError(f"{asset_path}: Custom 输入缺失: {sorted(missing_inputs)}")

    found_parameters = set()
    for expression in expressions:
        try:
            name = str(expression.get_editor_property("parameter_name"))
            if name and name != "None":
                found_parameters.add(name)
        except Exception:
            pass
    missing_parameters = expected_parameters - found_parameters
    if missing_parameters:
        raise RuntimeError(f"{asset_path}: 材质参数缺失: {sorted(missing_parameters)}")

    if unreal.MaterialEditingLibrary.get_material_property_input_node(
        material, unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ) != custom:
        raise RuntimeError(f"{asset_path}: Custom 输出未连接 EmissiveColor")

    if material.get_editor_property("blend_mode") != expected_blend:
        raise RuntimeError(f"{asset_path}: BlendMode 不符合预期 ({expected_blend})")
    if not material.get_editor_property("two_sided"):
        raise RuntimeError(f"{asset_path}: 未启用 TwoSided")

    if asset_path == TRANSLUCENT_PATH:
        if unreal.MaterialEditingLibrary.get_material_property_input_node(
            material, unreal.MaterialProperty.MP_OPACITY
        ) is None:
            raise RuntimeError(f"{asset_path}: 未连接 Opacity")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(f"{asset_path}: HLSL 编译失败: " + " | ".join(str(e) for e in compile_errors))

    unreal.log(
        f"[MMDStandard] 验证通过: {asset_path}, inputs={len(input_names)}, "
        f"parameters={len(found_parameters & expected_parameters)}"
    )


def main():
    for asset_path in (OPAQUE_PATH, TRANSLUCENT_PATH, LITE_TOON_PATH):
        validate_material(asset_path)
    unreal.log("[MMDStandard] 全部材质验证通过 ✓")


try:
    main()
except Exception as error:
    unreal.log_error(f"[MMDStandard] 验证失败: {error}")
    raise
