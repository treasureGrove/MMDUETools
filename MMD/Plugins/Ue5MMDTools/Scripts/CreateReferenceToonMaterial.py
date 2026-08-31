import unreal


ASSET_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_ReferenceToon"
ASSET_NAME = "M_MMD_ReferenceToon"
ASSET_FOLDER = "/Ue5MMDTools/Resources/MaterialInstance"
WHITE_TEXTURE_PATH = "/Ue5MMDTools/Resources/DefaultTexture/T_White_1x1"
LIGHT_DATA_PATH = "/Ue5MMDTools/Rendering/LightDataRT"
SHADOW_MAP_PATH = "/Ue5MMDTools/Rendering/MMDShadowMapRT"


def add_expression(material, expression_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)


def add_scalar(material, name, value, x, y, priority):
    expression = add_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", value)
    expression.set_editor_property("group", "Reference Toon")
    expression.set_editor_property("sort_priority", priority)
    return expression


def connect(source, destination, destination_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, destination, destination_input
    ):
        raise RuntimeError(f"连接失败: {destination_input}")


def create_material():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.log(f"[MMDReferenceToon] 新材质已存在，保持原样: {ASSET_PATH}")
        return

    white_texture = unreal.load_asset(WHITE_TEXTURE_PATH)
    light_data = unreal.load_asset(LIGHT_DATA_PATH)
    shadow_map = unreal.load_asset(SHADOW_MAP_PATH)
    if not white_texture or not light_data or not shadow_map:
        raise RuntimeError("默认纹理或渲染目标加载失败")

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME, ASSET_FOLDER, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError("材质资产创建失败")

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.3333)
    try:
        material.set_editor_property("used_with_skeletal_mesh", True)
    except Exception as usage_error:
        unreal.log_warning(f"[MMDReferenceToon] SkeletalMesh 用法标记由首次使用时自动补齐: {usage_error}")

    base_texture = add_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1260, -180)
    base_texture.set_editor_property("parameter_name", "BaseColorMap")
    base_texture.set_editor_property("texture", white_texture)
    base_texture.set_editor_property("group", "Surface")
    base_texture.set_editor_property("sort_priority", 0)

    base_tint = add_expression(material, unreal.MaterialExpressionVectorParameter, -1260, -30)
    base_tint.set_editor_property("parameter_name", "DiffuseColor")
    base_tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    base_tint.set_editor_property("group", "Surface")
    base_tint.set_editor_property("sort_priority", 1)

    base_multiply = add_expression(material, unreal.MaterialExpressionMultiply, -1020, -120)
    connect(base_texture, base_multiply, "A", "RGB")
    connect(base_tint, base_multiply, "B")

    opacity_strength = add_expression(
        material, unreal.MaterialExpressionScalarParameter, -1020, 20
    )
    opacity_strength.set_editor_property("parameter_name", "opacity_strength")
    opacity_strength.set_editor_property("default_value", 1.0)
    opacity_strength.set_editor_property("group", "Surface")
    opacity_strength.set_editor_property("sort_priority", 2)
    opacity_multiply = add_expression(material, unreal.MaterialExpressionMultiply, -790, 20)
    connect(base_texture, opacity_multiply, "A", "A")
    connect(opacity_strength, opacity_multiply, "B")

    light_data_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1260, 150)
    light_data_input.set_editor_property("texture", light_data)
    shadow_map_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1260, 270)
    shadow_map_input.set_editor_property("texture", shadow_map)

    scalar_specs = [
        ("shadow_threshold", 0.08),
        ("shadow_softness", 0.03),
        ("midtone_width", 0.24),
        ("shadow_strength", 0.68),
        ("ambient_strength", 0.10),
        ("light_saturation", 0.75),
        ("scene_shadow_strength", 0.85),
        ("mmd_shadow_bias", 0.0),
        ("specular_threshold", 0.94),
        ("specular_strength", 0.18),
        ("rim_power", 4.5),
        ("rim_strength", 0.12),
        ("anisotropy_strength", 0.0),
    ]
    scalar_nodes = {}
    for index, (name, value) in enumerate(scalar_specs):
        column = index // 7
        row = index % 7
        scalar_nodes[name] = add_scalar(
            material, name, value, -780 + column * 230, -500 + row * 115, index
        )

    rim_color = add_expression(material, unreal.MaterialExpressionVectorParameter, -550, 360)
    rim_color.set_editor_property("parameter_name", "rim_color")
    rim_color.set_editor_property("default_value", unreal.LinearColor(0.38, 0.82, 1.0, 1.0))
    rim_color.set_editor_property("group", "Reference Toon")
    rim_color.set_editor_property("sort_priority", 13)

    custom = add_expression(material, unreal.MaterialExpressionCustom, -160, -120)
    custom.set_editor_property(
        "code",
        '#include "/Plugin/Ue5MMDTools/TMMDShader/MMDReferenceToonGlamorous.usf"\nreturn 0;',
    )
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "独立华丽二次元参考着色")

    input_names = [
        "base_color",
        "light_data_tex",
        "mmd_shadow_map",
        "shadow_threshold",
        "shadow_softness",
        "midtone_width",
        "shadow_strength",
        "ambient_strength",
        "light_saturation",
        "scene_shadow_strength",
        "mmd_shadow_bias",
        "specular_threshold",
        "specular_strength",
        "rim_power",
        "rim_strength",
        "rim_color",
        "anisotropy_strength",
    ]
    custom_inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    connect(base_multiply, custom, "base_color")
    connect(light_data_input, custom, "light_data_tex")
    connect(shadow_map_input, custom, "mmd_shadow_map")
    for name, node in scalar_nodes.items():
        connect(node, custom, name)
    connect(rim_color, custom, "rim_color", "RGB")

    if not unreal.MaterialEditingLibrary.connect_material_property(
        custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Custom 输出连接 EmissiveColor 失败")
    if not unreal.MaterialEditingLibrary.connect_material_property(
        opacity_multiply, "", unreal.MaterialProperty.MP_OPACITY_MASK
    ):
        raise RuntimeError("BaseColorMap Alpha 连接 OpacityMask 失败")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"[MMDReferenceToon] 新材质创建完成: {ASSET_PATH}")


try:
    create_material()
except Exception as error:
    unreal.log_error(f"[MMDReferenceToon] 创建失败: {error}")
    raise
