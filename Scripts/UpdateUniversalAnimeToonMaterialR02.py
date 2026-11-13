import unreal


ASSET_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon"


def parameter_name(expression):
    try:
        return str(expression.get_editor_property("parameter_name"))
    except Exception:
        return ""


def update_material():
    material = unreal.load_asset(ASSET_PATH)
    if not material:
        raise RuntimeError(f"找不到独立父材质: {ASSET_PATH}")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    custom_nodes = [node for node in expressions if isinstance(node, unreal.MaterialExpressionCustom)]
    base_nodes = [
        node
        for node in expressions
        if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D)
        and parameter_name(node) == "BaseColorMap"
    ]
    diffuse_nodes = [
        node
        for node in expressions
        if isinstance(node, unreal.MaterialExpressionVectorParameter)
        and parameter_name(node) == "DiffuseColor"
    ]
    light_nodes = [node for node in expressions if isinstance(node, unreal.MaterialExpressionTextureObject)]
    base_multiply_nodes = [
        node
        for node in expressions
        if isinstance(node, unreal.MaterialExpressionMultiply)
        and node.get_name() == "MaterialExpressionMultiply_0"
    ]
    scalar_nodes = {
        parameter_name(node): node
        for node in expressions
        if isinstance(node, unreal.MaterialExpressionScalarParameter)
    }
    if (
        len(custom_nodes) != 1
        or len(base_nodes) != 1
        or len(diffuse_nodes) != 1
        or len(light_nodes) != 1
        or len(base_multiply_nodes) != 1
    ):
        raise RuntimeError(
            f"材质节点数量异常: custom={len(custom_nodes)}, base={len(base_nodes)}, "
            f"diffuse={len(diffuse_nodes)}, light={len(light_nodes)}, "
            f"base_multiply={len(base_multiply_nodes)}"
        )

    custom = custom_nodes[0]
    custom.set_editor_property(
        "code",
        "float universal_anime_toon_revision_r05 = 0.0;\n"
        '#include "/Plugin/Ue5MMDTools/TMMDShader/MMDUniversalAnimeToonR05.usf"\n'
        "return float3(universal_anime_toon_revision_r05, "
        "universal_anime_toon_revision_r05, "
        "universal_anime_toon_revision_r05);",
    )
    input_names = [str(item.get_editor_property("input_name")) for item in custom.get_editor_property("inputs")]
    for new_name in ("base_texture_color", "diffuse_color"):
        if new_name not in input_names:
            input_names.append(new_name)

    new_inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        new_inputs.append(custom_input)
    custom.set_editor_property("inputs", new_inputs)

    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        base_multiply_nodes[0], "", custom, "base_color"
    ):
        raise RuntimeError("base_color 重连失败")
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        light_nodes[0], "", custom, "light_data_tex"
    ):
        raise RuntimeError("light_data_tex 重连失败")
    for name in (
        "shadow_step",
        "shadow_softness",
        "shadow_strength",
        "ambient_strength",
        "light_color_influence",
        "highlight_step",
        "highlight_strength",
        "rim_strength",
        "rim_power",
    ):
        node = scalar_nodes.get(name)
        if not node:
            raise RuntimeError(f"找不到标量参数节点: {name}")
        if not unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, name):
            raise RuntimeError(f"标量参数重连失败: {name}")

    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        base_nodes[0], "RGB", custom, "base_texture_color"
    ):
        raise RuntimeError("BaseColorMap RGB 连接失败")
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        diffuse_nodes[0], "RGB", custom, "diffuse_color"
    ):
        raise RuntimeError("DiffuseColor RGB 连接失败")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError("R02 编译失败: " + " | ".join(str(item) for item in compile_errors))
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(
        f"[UniversalAnimeToon] R02 零 DiffuseColor 容错完成: inputs={len(input_names)}"
    )


try:
    update_material()
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] R02 更新失败: {error}")
    raise
