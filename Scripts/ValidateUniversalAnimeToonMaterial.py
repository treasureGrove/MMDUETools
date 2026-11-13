import unreal


ASSET_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon"
TRANSLUCENT_ASSET_PATH = (
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent"
)
EXPECTED_PARAMETERS = {
    "BaseColorMap",
    "DiffuseColor",
    "Opacity",
    "shadow_step",
    "shadow_softness",
    "shadow_strength",
    "ambient_strength",
    "light_color_influence",
    "highlight_step",
    "highlight_strength",
    "rim_strength",
    "rim_power",
    "local_light_strength",
    "local_specular_strength",
    "glamour_rim_strength",
    "directional_sheen_strength",
    "surface_profile",
}


def validate_material():
    material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError(f"找不到资产: {ASSET_PATH}")

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    custom_nodes = [node for node in expressions if isinstance(node, unreal.MaterialExpressionCustom)]
    if len(custom_nodes) != 1:
        raise RuntimeError(f"Custom 节点数量错误: {len(custom_nodes)}")

    custom = custom_nodes[0]
    code = custom.get_editor_property("code")
    if "MMDUniversalAnimeToonR56.usf" not in code:
        raise RuntimeError("Custom 节点未引用独立 MMDUniversalAnimeToonR56.usf")

    input_names = {
        str(item.get_editor_property("input_name"))
        for item in custom.get_editor_property("inputs")
    }
    if "base_color" not in input_names or "light_data_tex" not in input_names:
        raise RuntimeError(f"Custom 输入不完整: {sorted(input_names)}")

    found_parameters = set()
    for expression in expressions:
        try:
            name = str(expression.get_editor_property("parameter_name"))
            if name and name != "None":
                found_parameters.add(name)
        except Exception:
            pass

    missing = EXPECTED_PARAMETERS - found_parameters
    if missing:
        raise RuntimeError(f"材质参数缺失: {sorted(missing)}")

    emissive_node = unreal.MaterialEditingLibrary.get_material_property_input_node(
        material, unreal.MaterialProperty.MP_EMISSIVE_COLOR
    )
    if emissive_node != custom:
        raise RuntimeError("Custom 输出未连接到 EmissiveColor")

    if material.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE:
        raise RuntimeError("基础材质 BlendMode 不是 Opaque")
    if not material.get_editor_property("two_sided"):
        raise RuntimeError("材质未启用 TwoSided")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError("HLSL 编译失败: " + " | ".join(str(item) for item in compile_errors))

    translucent = unreal.load_asset(TRANSLUCENT_ASSET_PATH)
    if not translucent:
        raise RuntimeError(f"找不到配套透明材质: {TRANSLUCENT_ASSET_PATH}")
    if translucent.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        raise RuntimeError("配套材质 BlendMode 不是 Translucent")
    if unreal.MaterialEditingLibrary.get_material_property_input_node(
        translucent, unreal.MaterialProperty.MP_OPACITY
    ) is None:
        raise RuntimeError("配套透明材质未连接 Opacity")
    translucent_errors = unreal.MaterialEditingLibrary.recompile_material(translucent)
    if translucent_errors:
        raise RuntimeError(
            "Translucent HLSL 编译失败: "
            + " | ".join(str(item) for item in translucent_errors)
        )

    unreal.log(
        f"[UniversalAnimeToon] 验证通过: expressions={len(expressions)}, "
        f"inputs={len(input_names)}, parameters={len(found_parameters)}, translucent=pass"
    )


try:
    validate_material()
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] 验证失败: {error}")
    raise
