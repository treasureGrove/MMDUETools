# CreateMMDStandardMaterials.py
# 创建 MMDStandard 系列父材质：
#   /Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Opaque        （全素材版，忠实 MMD 原生管线）
#   /Ue5MMDTools/Resources/MaterialInstance/M_MMDStandard_Translucent   （同结构 + Opacity）
#   /Ue5MMDTools/Resources/MaterialInstance/M_MMD_LiteToon              （极简版：仅 base_color）
# 运行前先跑 CreateToonRampTextures.py。已存在则跳过（幂等）。

import unreal


ASSET_FOLDER = "/Ue5MMDTools/Resources/MaterialInstance"
WHITE_TEXTURE_PATH = "/Ue5MMDTools/Resources/DefaultTexture/T_White_1x1"
BLACK_TEXTURE_PATH = "/Ue5MMDTools/Resources/DefaultTexture/T_Black_1x1"
DEFAULT_TOON_PATH = "/Ue5MMDTools/Resources/ToonRamps/T_MMDToon01"
LIGHT_DATA_PATH = "/Ue5MMDTools/Rendering/LightDataRT"
SHADOW_MAP_PATH = "/Ue5MMDTools/Rendering/MMDShadowMapRT"

PARAMETER_GROUP = "MMD Standard"


def add_expression(material, expression_class, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(
        material, expression_class, x, y
    )


def connect(source, destination, destination_input, source_output=""):
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
        source, source_output, destination, destination_input
    ):
        raise RuntimeError(f"连接失败: {destination_input}")


def make_base_material(name, blend_mode):
    asset_path = f"{ASSET_FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.log_warning(f"[MMDStandard] 材质已存在，跳过: {asset_path}")
        return None

    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, ASSET_FOLDER, unreal.Material, unreal.MaterialFactoryNew()
    )
    if not material:
        raise RuntimeError(f"材质创建失败: {asset_path}")

    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", blend_mode)
    material.set_editor_property("two_sided", True)
    try:
        material.set_editor_property("used_with_skeletal_mesh", True)
    except Exception as usage_error:
        unreal.log_warning(f"[MMDStandard] SkeletalMesh 用途标记自动补齐: {usage_error}")
    return material


def create_standard_material(name, blend_mode):
    material = make_base_material(name, blend_mode)
    if material is None:
        return
    is_translucent = blend_mode == unreal.BlendMode.BLEND_TRANSLUCENT

    white_texture = unreal.load_asset(WHITE_TEXTURE_PATH)
    black_texture = unreal.load_asset(BLACK_TEXTURE_PATH)
    default_toon = unreal.load_asset(DEFAULT_TOON_PATH)
    light_data = unreal.load_asset(LIGHT_DATA_PATH)
    shadow_map = unreal.load_asset(SHADOW_MAP_PATH)
    for required_name, required_asset in (
        ("白贴图", white_texture), ("黑贴图", black_texture), ("默认 toon", default_toon),
        ("LightDataRT", light_data), ("MMDShadowMapRT", shadow_map),
    ):
        if not required_asset:
            raise RuntimeError(f"依赖资产加载失败: {required_name}")

    base_texture = add_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1500, -400)
    base_texture.set_editor_property("parameter_name", "BaseColorMap")
    base_texture.set_editor_property("texture", white_texture)
    base_texture.set_editor_property("group", PARAMETER_GROUP)
    base_texture.set_editor_property("sort_priority", 0)

    toon_texture = add_expression(material, unreal.MaterialExpressionTextureObjectParameter, -1500, -160)
    toon_texture.set_editor_property("parameter_name", "ToonTex")
    toon_texture.set_editor_property("texture", default_toon)
    toon_texture.set_editor_property("group", PARAMETER_GROUP)
    toon_texture.set_editor_property("sort_priority", 1)

    sphere_texture = add_expression(material, unreal.MaterialExpressionTextureObjectParameter, -1500, -60)
    sphere_texture.set_editor_property("parameter_name", "SphereTex")
    sphere_texture.set_editor_property("texture", black_texture)
    sphere_texture.set_editor_property("group", PARAMETER_GROUP)
    sphere_texture.set_editor_property("sort_priority", 2)

    light_data_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1500, 220)
    light_data_input.set_editor_property("texture", light_data)

    shadow_map_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1500, 320)
    shadow_map_input.set_editor_property("texture", shadow_map)

    vector_specs = [
        ("DiffuseColor", unreal.LinearColor(1.0, 1.0, 1.0, 1.0), -1240, -420, 10),
        ("AmbientColor", unreal.LinearColor(0.5, 0.5, 0.5, 0.5), -1240, -300, 11),
        ("SpecularColor", unreal.LinearColor(0.0, 0.0, 0.0, 1.0), -1240, -180, 12),
    ]
    vector_nodes = {}
    for parameter_name, default_value, x, y, priority in vector_specs:
        node = add_expression(material, unreal.MaterialExpressionVectorParameter, x, y)
        node.set_editor_property("parameter_name", parameter_name)
        node.set_editor_property("default_value", default_value)
        node.set_editor_property("group", PARAMETER_GROUP)
        node.set_editor_property("sort_priority", priority)
        vector_nodes[parameter_name] = node

    scalar_specs = [
        ("specular_power", 5.0, 20),
        ("toon_use_texture", 1.0, 21),
        ("toon_strength", 1.0, 22),
        ("sphere_mode", 0.0, 23),
        ("shadow_strength", 0.8, 24),
        ("ambient_strength", 1.0, 25),
        ("other_lights_strength", 0.35, 26),
    ]
    scalar_nodes = {}
    for index, (parameter_name, value, priority) in enumerate(scalar_specs):
        node = add_expression(material, unreal.MaterialExpressionScalarParameter, -1240, -40 + index * 100)
        node.set_editor_property("parameter_name", parameter_name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", PARAMETER_GROUP)
        node.set_editor_property("sort_priority", priority)
        scalar_nodes[parameter_name] = node

    custom = add_expression(material, unreal.MaterialExpressionCustom, -400, -200)
    custom.set_editor_property(
        "code",
        '#include "/Plugin/Ue5MMDTools/MMDStandard/MMDStandard.usf"\nreturn 0;',
    )
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "MMD 原生管线：toon ramp + sphere + Blinn-Phong")

    input_names = [
        "base_texture_color",
        "diffuse_color",
        "ambient_color",
        "specular_color",
        "specular_power",
        "toon_tex",
        "toon_use_texture",
        "toon_strength",
        "sphere_tex",
        "sphere_mode",
        "light_data_tex",
        "mmd_shadow_map",
        "shadow_strength",
        "ambient_strength",
        "other_lights_strength",
    ]
    custom_inputs = []
    for input_name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    connect(base_texture, custom, "base_texture_color", "RGB")
    connect(vector_nodes["DiffuseColor"], custom, "diffuse_color", "RGB")
    connect(vector_nodes["AmbientColor"], custom, "ambient_color", "RGB")
    connect(vector_nodes["SpecularColor"], custom, "specular_color", "RGB")
    connect(scalar_nodes["specular_power"], custom, "specular_power")
    connect(toon_texture, custom, "toon_tex")
    connect(scalar_nodes["toon_use_texture"], custom, "toon_use_texture")
    connect(scalar_nodes["toon_strength"], custom, "toon_strength")
    connect(sphere_texture, custom, "sphere_tex")
    connect(scalar_nodes["sphere_mode"], custom, "sphere_mode")
    connect(light_data_input, custom, "light_data_tex")
    connect(shadow_map_input, custom, "mmd_shadow_map")
    connect(scalar_nodes["shadow_strength"], custom, "shadow_strength")
    connect(scalar_nodes["ambient_strength"], custom, "ambient_strength")
    connect(scalar_nodes["other_lights_strength"], custom, "other_lights_strength")

    if not unreal.MaterialEditingLibrary.connect_material_property(
        custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError(f"{name}: Custom 输出连接 EmissiveColor 失败")

    if is_translucent:
        opacity = add_expression(material, unreal.MaterialExpressionScalarParameter, -1240, 700)
        opacity.set_editor_property("parameter_name", "Opacity")
        opacity.set_editor_property("default_value", 1.0)
        opacity.set_editor_property("group", PARAMETER_GROUP)
        opacity.set_editor_property("sort_priority", 30)
        opacity_product = add_expression(material, unreal.MaterialExpressionMultiply, -980, 640)
        connect(base_texture, opacity_product, "A", "A")
        connect(vector_nodes["DiffuseColor"], opacity_product, "B", "A")
        opacity_product_2 = add_expression(material, unreal.MaterialExpressionMultiply, -780, 640)
        connect(opacity_product, opacity_product_2, "A")
        connect(opacity, opacity_product_2, "B")
        if not unreal.MaterialEditingLibrary.connect_material_property(
            opacity_product_2, "", unreal.MaterialProperty.MP_OPACITY
        ):
            raise RuntimeError(f"{name}: Opacity 连接失败")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(f"{name} HLSL 编译失败: " + " | ".join(str(e) for e in compile_errors))
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"[MMDStandard] 材质创建完成: {ASSET_FOLDER}/{name}")


def create_lite_toon_material():
    name = "M_MMD_LiteToon"
    material = make_base_material(name, unreal.BlendMode.BLEND_OPAQUE)
    if material is None:
        return

    white_texture = unreal.load_asset(WHITE_TEXTURE_PATH)
    light_data = unreal.load_asset(LIGHT_DATA_PATH)
    shadow_map = unreal.load_asset(SHADOW_MAP_PATH)

    base_texture = add_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -300)
    base_texture.set_editor_property("parameter_name", "BaseColorMap")
    base_texture.set_editor_property("texture", white_texture)
    base_texture.set_editor_property("group", "MMD Lite Toon")
    base_texture.set_editor_property("sort_priority", 0)

    light_data_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1100, -80)
    light_data_input.set_editor_property("texture", light_data)

    shadow_map_input = add_expression(material, unreal.MaterialExpressionTextureObject, -1100, 20)
    shadow_map_input.set_editor_property("texture", shadow_map)

    scalar_specs = [
        ("shadow_step", -0.10),
        ("shadow_softness", 0.06),
        ("shadow_strength", 0.8),
        ("rim_strength", 0.15),
    ]
    scalar_nodes = {}
    for index, (parameter_name, value) in enumerate(scalar_specs):
        node = add_expression(material, unreal.MaterialExpressionScalarParameter, -1100, 120 + index * 100)
        node.set_editor_property("parameter_name", parameter_name)
        node.set_editor_property("default_value", value)
        node.set_editor_property("group", "MMD Lite Toon")
        node.set_editor_property("sort_priority", 10 + index)
        scalar_nodes[parameter_name] = node

    custom = add_expression(material, unreal.MaterialExpressionCustom, -400, -120)
    custom.set_editor_property(
        "code",
        '#include "/Plugin/Ue5MMDTools/MMDStandard/MMDLiteToon.usf"\nreturn 0;',
    )
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "极简两段 toon：仅 base_color 即可着色")

    input_names = [
        "base_color",
        "light_data_tex",
        "shadow_step",
        "shadow_softness",
        "shadow_strength",
        "rim_strength",
        "mmd_shadow_map",
    ]
    custom_inputs = []
    for input_name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    connect(base_texture, custom, "base_color", "RGB")
    connect(light_data_input, custom, "light_data_tex")
    connect(shadow_map_input, custom, "mmd_shadow_map")
    for parameter_name, node in scalar_nodes.items():
        connect(node, custom, parameter_name)

    if not unreal.MaterialEditingLibrary.connect_material_property(
        custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR
    ):
        raise RuntimeError("M_MMD_LiteToon: Custom 输出连接 EmissiveColor 失败")

    unreal.MaterialEditingLibrary.layout_material_expressions(material)
    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(f"M_MMD_LiteToon HLSL 编译失败: " + " | ".join(str(e) for e in compile_errors))
    unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    unreal.log(f"[MMDStandard] 材质创建完成: {ASSET_FOLDER}/{name}")


def main():
    create_standard_material("M_MMDStandard_Opaque", unreal.BlendMode.BLEND_OPAQUE)
    create_standard_material("M_MMDStandard_Translucent", unreal.BlendMode.BLEND_TRANSLUCENT)
    create_lite_toon_material()


try:
    main()
except Exception as error:
    unreal.log_error(f"[MMDStandard] 材质创建失败: {error}")
    raise
