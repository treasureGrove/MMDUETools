import unreal


ASSET_PATH = "/Ue5MMDTools/Resources/MaterialInstance/M_MMD_UniversalAnimeToon"
ASSET_NAME = "M_MMD_UniversalAnimeToon"
ASSET_FOLDER = "/Ue5MMDTools/Resources/MaterialInstance"
WHITE_TEXTURE_PATH = "/Ue5MMDTools/Resources/DefaultTexture/T_White_1x1"
LIGHT_DATA_PATH = "/Ue5MMDTools/Rendering/LightDataRT"


def add_expression(material, expression_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, expression_class, x, y)


def connect(source, destination, destination_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, destination, destination_input
    ):
        raise RuntimeError(f"连接失败: {destination_input}")


def add_scalar(material, name, value, x, y, priority):
    expression = add_expression(material, unreal.MaterialExpressionScalarParameter, x, y)
    expression.set_editor_property("parameter_name", name)
    expression.set_editor_property("default_value", value)
    expression.set_editor_property("group", "Universal Toon")
    expression.set_editor_property("sort_priority", priority)
    return expression


def create_material():
    if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
        unreal.log_warning(f"[UniversalAnimeToon] 独立材质已存在，未覆盖: {ASSET_PATH}")
        return

    white_texture = unreal.load_asset(WHITE_TEXTURE_PATH)
    light_data = unreal.load_asset(LIGHT_DATA_PATH)
    if not white_texture or not light_data:
        raise RuntimeError("默认白贴图或 LightDataRT 加载失败")

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ASSET_NAME, ASSET_FOLDER, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError("材质资产创建失败")

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("opacity_mask_clip_value", 0.3333)
    try:
        material.set_editor_property("used_with_skeletal_mesh", True)
    except Exception as usage_error:
        unreal.log_warning(f"[UniversalAnimeToon] SkeletalMesh 标记自动补齐: {usage_error}")

    base_texture = add_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1180, -180)
    base_texture.set_editor_property("parameter_name", "BaseColorMap")
    base_texture.set_editor_property("texture", white_texture)
    base_texture.set_editor_property("group", "Surface")
    base_texture.set_editor_property("sort_priority", 0)

    diffuse_color = add_expression(material, unreal.MaterialExpressionVectorParameter, -1180, -20)
    diffuse_color.set_editor_property("parameter_name", "DiffuseColor")
    diffuse_color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    diffuse_color.set_editor_property("group", "Surface")
    diffuse_color.set_editor_property("sort_priority", 1)

    base_color = add_expression(material, unreal.MaterialExpressionMultiply, -930, -120)
    connect(base_texture, base_color, "A", "RGB")
    connect(diffuse_color, base_color, "B", "RGB")

    opacity = add_expression(material, unreal.MaterialExpressionScalarParameter, -1180, 120)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 1.0)
    opacity.set_editor_property("group", "Surface")
    opacity.set_editor_property("sort_priority", 2)
    opacity_product = add_expression(material, unreal.MaterialExpressionMultiply, -930, 80)
    connect(base_texture, opacity_product, "A", "A")
    connect(diffuse_color, opacity_product, "B", "A")
    opacity_product_2 = add_expression(material, unreal.MaterialExpressionMultiply, -710, 80)
    connect(opacity_product, opacity_product_2, "A")
    connect(opacity, opacity_product_2, "B")

    light_data_input = add_expression(material, unreal.MaterialExpressionTextureObject, -930, 210)
    light_data_input.set_editor_property("texture", light_data)

    scalar_specs = [
        ("shadow_step", -0.06),
        ("shadow_softness", 0.07),
        ("shadow_strength", 0.54),
        ("ambient_strength", 0.32),
        ("light_color_influence", 0.10),
        ("highlight_step", 0.92),
        ("highlight_strength", 0.12),
        ("rim_strength", 0.035),
        ("rim_power", 4.5),
    ]
    scalar_nodes = {}
    for index, (name, value) in enumerate(scalar_specs):
        column = index // 5
        row = index % 5
        scalar_nodes[name] = add_scalar(
            material, name, value, -660 + column * 230, -500 + row * 110, index
        )

    custom = add_expression(material, unreal.MaterialExpressionCustom, -160, -100)
    custom.set_editor_property(
        "code",
        '#include "/Plugin/Ue5MMDTools/TMMDShader/MMDUniversalAnimeToonR05.usf"\nreturn 0;',
    )
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "通用二次元三段式 Toon；不依赖模型 UV 阈值图")

    input_names = [
        "base_color",
        "light_data_tex",
        "shadow_step",
        "shadow_softness",
        "shadow_strength",
        "ambient_strength",
        "light_color_influence",
        "highlight_step",
        "highlight_strength",
        "rim_strength",
        "rim_power",
        "base_texture_color",
        "diffuse_color",
    ]
    custom_inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    connect(base_color, custom, "base_color")
    connect(light_data_input, custom, "light_data_tex")
    for name, node in scalar_nodes.items():
        connect(node, custom, name)
    connect(base_texture, custom, "base_texture_color", "RGB")
    connect(diffuse_color, custom, "diffuse_color", "RGB")

    if not unreal.MaterialEditingLibrary.connect_material_property(
        custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("Custom 输出连接 EmissiveColor 失败")
    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"[UniversalAnimeToon] 独立材质创建完成: {ASSET_PATH}")


try:
    create_material()
except Exception as error:
    unreal.log_error(f"[UniversalAnimeToon] 创建失败: {error}")
    raise
