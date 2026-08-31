import unreal


ASSET_PATHS = (
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon",
    "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon_Translucent",
)

SCALAR_DEFAULTS = {
    "shadow_step": 0.00,
    "shadow_softness": 0.045,
    "shadow_strength": 0.64,
    "ambient_strength": 0.10,
    "light_color_influence": 0.32,
    "highlight_step": 0.82,
    "highlight_strength": 0.28,
    "rim_strength": 0.10,
    "rim_power": 3.2,
    "local_light_strength": 0.72,
    "local_specular_strength": 0.55,
    "glamour_rim_strength": 3.00,
    "directional_sheen_strength": 0.95,
    "surface_profile": 0.0,
}


def parameter_name(expression):
    try:
        return str(expression.get_editor_property("parameter_name"))
    except Exception:
        return ""


def connect(source, destination, destination_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, destination, destination_input
    ):
        raise RuntimeError(f"连接失败: {destination_input}")


def update_material(asset_path):
    material = unreal.load_asset(asset_path)
    if not material:
        raise RuntimeError(f"找不到材质: {asset_path}")

    expressions = unreal.MaterialEditingLibrary.get_material_expressions(material)
    custom = next(
        (node for node in expressions if isinstance(node, unreal.MaterialExpressionCustom)),
        None,
    )
    base_texture = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D)
            and parameter_name(node) == "BaseColorMap"
        ),
        None,
    )
    diffuse_color = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionVectorParameter)
            and parameter_name(node) == "DiffuseColor"
        ),
        None,
    )
    light_data = next(
        (node for node in expressions if isinstance(node, unreal.MaterialExpressionTextureObject)),
        None,
    )
    base_multiply = next(
        (
            node
            for node in expressions
            if isinstance(node, unreal.MaterialExpressionMultiply)
            and node.get_name() == "MaterialExpressionMultiply_0"
        ),
        None,
    )
    if not custom or not base_texture or not diffuse_color or not light_data or not base_multiply:
        raise RuntimeError(f"R16 更新所需节点不完整: {asset_path}")

    scalar_nodes = {
        parameter_name(node): node
        for node in expressions
        if isinstance(node, unreal.MaterialExpressionScalarParameter)
    }
    missing_index = 0
    for priority, (name, default_value) in enumerate(SCALAR_DEFAULTS.items()):
        node = scalar_nodes.get(name)
        if not node:
            node = unreal.MaterialEditingLibrary.create_material_expression(
                material,
                unreal.MaterialExpressionScalarParameter,
                -420 + missing_index * 230,
                640,
            )
            node.set_editor_property("parameter_name", name)
            node.set_editor_property("default_value", default_value)
            node.set_editor_property("group", "Universal Toon")
            node.set_editor_property("sort_priority", priority)
            scalar_nodes[name] = node
            missing_index += 1

    custom.set_editor_property(
        "code",
        "float universal_anime_toon_revision_r24 = 0.0;\n"
        '#include "/Plugin/Ue5MMDTools/TMMDShader/MMDUniversalAnimeToonR56.usf"\n'
        "return float3(universal_anime_toon_revision_r24, "
        "universal_anime_toon_revision_r24, universal_anime_toon_revision_r24);",
    )
    custom.set_editor_property(
        "description",
        "R56 转正候选 Toon：稳定肤发 + 高输出黑缎 + 多环境宽珠光 Rim",
    )

    input_names = [
        "base_color",
        "light_data_tex",
        *SCALAR_DEFAULTS.keys(),
        "base_texture_color",
        "diffuse_color",
    ]
    custom_inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    connect(base_multiply, custom, "base_color")
    connect(light_data, custom, "light_data_tex")
    for name in SCALAR_DEFAULTS:
        connect(scalar_nodes[name], custom, name)
    connect(base_texture, custom, "base_texture_color", "RGB")
    connect(diffuse_color, custom, "diffuse_color", "RGB")

    if not unreal.MaterialEditingLibrary.connect_material_property(
        custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError(f"R56 Emissive 连接失败: {asset_path}")

    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(
            f"R56 编译失败 {asset_path}: "
            + " | ".join(str(item) for item in compile_errors)
        )
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(
        f"[UniversalAnimeToon] R56 转正候选材质完成: asset={asset_path}, "
        f"inputs={len(input_names)}, scalars={len(SCALAR_DEFAULTS)}"
    )


try:
    for path in ASSET_PATHS:
        update_material(path)
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] R56 更新失败: {error}")
    raise
